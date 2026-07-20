#include "lobx/simulation/agent_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace lobx::simulation {

namespace {

void require_ok(const lobx::Result& result, const std::string& context) {
  if (!result.ok) throw std::runtime_error(context + ": " + result.reason);
}

std::string escape_json(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char ch : s) {
    switch (ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += ch; break;
    }
  }
  return out;
}

lobx::agents::AgentId agent_id_from_user(lobx::UserId user) {
  return static_cast<lobx::agents::AgentId>(user);
}

constexpr lobx::UserId kFeeAccountUser = std::numeric_limits<lobx::UserId>::max();

double elapsed_ms(std::chrono::steady_clock::time_point start, std::chrono::steady_clock::time_point end) {
  return static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
}

} // namespace

AgentRuntime::AgentRuntime(AgentRuntimeConfig config) : config_(std::move(config)) {
  bootstrap_exchange();
  update_market_view();
}

void AgentRuntime::bootstrap_exchange() {
  require_ok(exchange_.issue_asset(config_.quote_asset, 6, 900000000000000000LL, 1, 0), "issue quote asset");
  require_ok(exchange_.issue_asset(config_.base_asset, 8, 900000000000000000LL, 1, 0), "issue base asset");
  require_ok(exchange_.create_spot_market(config_.symbol, config_.base_asset, config_.quote_asset, 1, 1, 1, 1),
             "create spot market");
  exchange_.events().set_memory_enabled(false);
}

void AgentRuntime::add_agent(std::unique_ptr<lobx::agents::IAgent> agent) {
  if (!agent) throw std::invalid_argument("AgentRuntime::add_agent requires non-null agent");
  const lobx::agents::AgentId id = agent->id();
  require_ok(exchange_.deposit(static_cast<lobx::UserId>(id), config_.quote_asset, config_.initial_quote), "deposit quote");
  require_ok(exchange_.deposit(static_cast<lobx::UserId>(id), config_.base_asset, config_.initial_base), "deposit base");
  state_store_.initialize_agent(id);
  agent->initialize(lobx::agents::AgentInitContext{now_, config_.symbol, 0});
  agents_.push_back(std::move(agent));
  summary_.agent_count = static_cast<int>(agents_.size());
}

void AgentRuntime::run() {
  run_steps(config_.steps);
}

void AgentRuntime::run_steps(int steps) {
  for (int i = 0; i < steps; ++i) step();
}

void AgentRuntime::step() {
  ++now_;
  last_step_stats_ = AgentRuntimeStepStats{};
  last_step_stats_.ts = now_;
  auto phase_start = std::chrono::steady_clock::now();
  update_market_view();
  auto phase_end = std::chrono::steady_clock::now();
  last_step_stats_.book_sample_ms += elapsed_ms(phase_start, phase_end);

  for (const auto& agent : agents_) {
    if (config_.enable_scheduler) {
      const auto next_it = next_decision_step_.find(agent->id());
      const lobx::agents::Timestamp due = next_it == next_decision_step_.end() ? now_ : next_it->second;
      if (now_ < due) {
        ++last_step_stats_.agents_skipped_count;
        continue;
      }
    }
    ++last_step_stats_.agents_due_count;
    const lobx::agents::PrivateAgentState private_state = state_store_.get_private_state(agent->id());
    const lobx::agents::AgentContext ctx{now_, market_view_, private_state, config_.environment, {}};
    phase_start = std::chrono::steady_clock::now();
    std::vector<lobx::agents::AgentAction> actions = agent->decide(ctx);
    phase_end = std::chrono::steady_clock::now();
    last_step_stats_.agent_decide_ms += elapsed_ms(phase_start, phase_end);
    ++last_step_stats_.agent_decisions_count;

    lobx::agents::Timestamp next_due = now_ + decision_interval_for(agent->type());
    for (const auto& action : actions) {
      if (const auto* sleep = std::get_if<lobx::agents::SleepUntil>(&action.payload)) {
        if (sleep->next_decision_ts > now_) next_due = sleep->next_decision_ts;
      }
    }
    if (config_.enable_scheduler) next_decision_step_[agent->id()] = next_due;

    SimulationEvent event{};
    event.ts = now_;
    event.source = EventSource::Agent;
    event.agent_id = agent->id();
    event.agent_type = agent->type();
    event.payload = AgentDecisionEvent{actions.size()};
    record_event(std::move(event));
    phase_start = std::chrono::steady_clock::now();
    schedule_actions(actions);
    phase_end = std::chrono::steady_clock::now();
    last_step_stats_.action_schedule_ms += elapsed_ms(phase_start, phase_end);
  }

  phase_start = std::chrono::steady_clock::now();
  execute_due_actions();
  phase_end = std::chrono::steady_clock::now();
  last_step_stats_.exchange_apply_ms += elapsed_ms(phase_start, phase_end);
  phase_start = std::chrono::steady_clock::now();
  update_market_view();
  phase_end = std::chrono::steady_clock::now();
  last_step_stats_.book_sample_ms += elapsed_ms(phase_start, phase_end);
  record_book_snapshot();
  summary_.steps = static_cast<int>(now_);
}

void AgentRuntime::update_market_view() {
  bid_cache_.clear();
  ask_cache_.clear();
  for (const auto& [price, qty] : exchange_.topN(config_.symbol, lob::Side::Bid, config_.book_levels)) {
    bid_cache_.push_back(BookLevelView{static_cast<double>(price), static_cast<double>(qty)});
  }
  for (const auto& [price, qty] : exchange_.topN(config_.symbol, lob::Side::Ask, config_.book_levels)) {
    ask_cache_.push_back(BookLevelView{static_cast<double>(price), static_cast<double>(qty)});
  }

  const double best_bid = bid_cache_.empty() ? 0.0 : bid_cache_.front().price;
  const double best_ask = ask_cache_.empty() ? 0.0 : ask_cache_.front().price;
  double mid = static_cast<double>(config_.reference_price);
  if (best_bid > 0.0 && best_ask > 0.0) mid = (best_bid + best_ask) / 2.0;
  else if (!recent_trades_.empty()) mid = recent_trades_.back().price;
  const double spread = best_bid > 0.0 && best_ask > 0.0 && best_ask >= best_bid ? best_ask - best_bid : 0.0;
  const double spread_bps = mid > 0.0 ? (spread / mid) * 10000.0 : 0.0;

  market_view_ = MarketView{config_.symbol,
                            std::span<const BookLevelView>(bid_cache_.data(), bid_cache_.size()),
                            std::span<const BookLevelView>(ask_cache_.data(), ask_cache_.size()),
                            std::span<const TradePrintView>(recent_trades_.data(), recent_trades_.size()),
                            best_bid,
                            best_ask,
                            mid,
                            spread_bps,
                            now_,
                            now_};
  summary_.final_best_bid = static_cast<lobx::agents::Price>(best_bid);
  summary_.final_best_ask = static_cast<lobx::agents::Price>(best_ask);
}

void AgentRuntime::schedule_actions(const std::vector<lobx::agents::AgentAction>& actions) {
  for (lobx::agents::AgentAction action : actions) {
    if (action.decision_ts == 0) action.decision_ts = now_;
    const lobx::agents::Timestamp arrival_ts = action.decision_ts + config_.action_latency;
    count_action(action.agent_type, action.payload);
    if (config_.retain_action_trace) action_trace_.push_back(action);
    ++summary_.action_count;
    action_queue_.push(ScheduledAction{action, action.decision_ts, arrival_ts, next_queue_seq_++});

    SimulationEvent event{};
    event.ts = action.decision_ts;
    event.source = EventSource::Runtime;
    event.agent_id = action.agent_id;
    event.agent_type = action.agent_type;
    event.group_id = action.group_id;
    event.payload = ActionScheduledEvent{action.decision_ts, arrival_ts, action.reason_tag};
    record_event(std::move(event));
  }
}

void AgentRuntime::execute_due_actions() {
  while (action_queue_.has_due(now_)) {
    execute_action(action_queue_.pop_due());
  }
}

void AgentRuntime::execute_action(const ScheduledAction& scheduled) {
  std::visit([&](const auto& payload) {
    using T = std::decay_t<decltype(payload)>;
    if constexpr (std::is_same_v<T, lobx::agents::SubmitLimitOrder>) {
      execute_submit_limit(scheduled, payload);
    } else if constexpr (std::is_same_v<T, lobx::agents::SubmitMarketOrder>) {
      execute_submit_market(scheduled, payload);
    } else if constexpr (std::is_same_v<T, lobx::agents::CancelOrder>) {
      execute_cancel(scheduled, payload);
    } else if constexpr (std::is_same_v<T, lobx::agents::ReplaceOrder>) {
      execute_replace(scheduled, payload);
    } else if constexpr (std::is_same_v<T, lobx::agents::CancelAllOrders>) {
      execute_cancel_all(scheduled, payload);
    } else if constexpr (std::is_same_v<T, lobx::agents::SleepUntil>) {
      (void)payload;
    }
  }, scheduled.action.payload);
}

std::uint32_t AgentRuntime::flags_for(const lobx::agents::SubmitLimitOrder& order) const {
  std::uint32_t flags = lob::NONE;
  if (order.tif == lobx::agents::TimeInForce::Ioc) flags |= lob::IOC;
  if (order.tif == lobx::agents::TimeInForce::Fok) flags |= lob::FOK;
  if (order.post_only) flags |= lob::POST_ONLY;
  return flags;
}

lobx::OrderId AgentRuntime::next_exchange_order_id() {
  return next_order_id_++;
}

lobx::agents::ClientOrderId AgentRuntime::client_order_id_for(const lobx::agents::AgentAction& action,
                                                              lobx::agents::ClientOrderId explicit_id) const {
  return explicit_id == 0 ? action.client_action_id : explicit_id;
}

void AgentRuntime::execute_submit_limit(const ScheduledAction& scheduled,
                                        const lobx::agents::SubmitLimitOrder& order) {
  const lobx::OrderId exchange_order_id = next_exchange_order_id();
  const lobx::agents::ClientOrderId client_order_id = client_order_id_for(scheduled.action, order.client_order_id);

  SimulationEvent submitted{};
  submitted.ts = scheduled.arrival_ts;
  submitted.source = EventSource::Exchange;
  submitted.agent_id = scheduled.action.agent_id;
  submitted.agent_type = scheduled.action.agent_type;
  submitted.group_id = scheduled.action.group_id;
  submitted.payload = OrderSubmittedEvent{order.symbol,
                                          lobx::agents::side_name(order.side),
                                          static_cast<double>(order.price),
                                          static_cast<double>(order.quantity)};
  record_event(std::move(submitted));

  const lobx::SubmitResult result = exchange_.submit_limit(order.symbol,
                                                           static_cast<lobx::UserId>(scheduled.action.agent_id),
                                                           exchange_order_id,
                                                           order.side,
                                                           order.price,
                                                           order.quantity,
                                                           flags_for(order),
                                                           scheduled.arrival_ts);
  account_submit_result(scheduled, exchange_order_id, client_order_id, order.side, order.price, order.symbol, result);
}

lobx::agents::Price AgentRuntime::protection_price(lobx::agents::Side side) const {
  const double mid = market_view_.mid_price > 0.0 ? market_view_.mid_price : static_cast<double>(config_.reference_price);
  const double offset = std::max(10.0, mid * 0.10);
  return std::max<lobx::agents::Price>(
      1,
      static_cast<lobx::agents::Price>(side == lob::Side::Bid ? mid + offset : mid - offset));
}

void AgentRuntime::execute_submit_market(const ScheduledAction& scheduled,
                                         const lobx::agents::SubmitMarketOrder& order) {
  const lobx::OrderId exchange_order_id = next_exchange_order_id();
  const lobx::agents::ClientOrderId client_order_id = client_order_id_for(scheduled.action, order.client_order_id);
  const lobx::agents::Price price = protection_price(order.side);

  SimulationEvent submitted{};
  submitted.ts = scheduled.arrival_ts;
  submitted.source = EventSource::Exchange;
  submitted.agent_id = scheduled.action.agent_id;
  submitted.agent_type = scheduled.action.agent_type;
  submitted.group_id = scheduled.action.group_id;
  submitted.payload = OrderSubmittedEvent{order.symbol,
                                          lobx::agents::side_name(order.side),
                                          static_cast<double>(price),
                                          static_cast<double>(order.quantity)};
  record_event(std::move(submitted));

  const lobx::SubmitResult result = exchange_.submit_market(order.symbol,
                                                            static_cast<lobx::UserId>(scheduled.action.agent_id),
                                                            exchange_order_id,
                                                            order.side,
                                                            order.quantity,
                                                            price,
                                                            lob::NONE,
                                                            scheduled.arrival_ts);
  account_submit_result(scheduled, exchange_order_id, client_order_id, order.side, price, order.symbol, result);
}

void AgentRuntime::execute_cancel(const ScheduledAction& scheduled, const lobx::agents::CancelOrder& cancel) {
  auto user_it = client_to_exchange_.find(scheduled.action.agent_id);
  if (user_it == client_to_exchange_.end()) return;
  auto order_it = user_it->second.find(cancel.client_order_id);
  if (order_it == user_it->second.end()) return;
  const lobx::OrderId exchange_order_id = order_it->second;
  const bool canceled = exchange_.cancel(config_.symbol,
                                         static_cast<lobx::UserId>(scheduled.action.agent_id),
                                         exchange_order_id,
                                         scheduled.arrival_ts);
  if (!canceled) return;
  ++summary_.canceled_orders;
  ++last_step_stats_.canceled_orders;
  state_store_.on_order_canceled(scheduled.action.agent_id, cancel.client_order_id, scheduled.arrival_ts);
  user_it->second.erase(order_it);

  SimulationEvent event{};
  event.ts = scheduled.arrival_ts;
  event.source = EventSource::Exchange;
  event.agent_id = scheduled.action.agent_id;
  event.agent_type = scheduled.action.agent_type;
  event.group_id = scheduled.action.group_id;
  event.payload = OrderCanceledEvent{exchange_order_id};
  record_event(std::move(event));
}

void AgentRuntime::execute_replace(const ScheduledAction& scheduled, const lobx::agents::ReplaceOrder& replace) {
  const auto existing = state_store_.find_order(scheduled.action.agent_id, replace.old_client_order_id);
  if (!existing) {
    SimulationEvent rejected{};
    rejected.ts = scheduled.arrival_ts;
    rejected.source = EventSource::Runtime;
    rejected.agent_id = scheduled.action.agent_id;
    rejected.agent_type = scheduled.action.agent_type;
    rejected.group_id = scheduled.action.group_id;
    rejected.payload = OrderRejectedEvent{"replace target not found"};
    record_event(std::move(rejected));
    return;
  }
  execute_cancel(scheduled, lobx::agents::CancelOrder{replace.old_client_order_id});

  ScheduledAction next = scheduled;
  lobx::agents::SubmitLimitOrder order{};
  order.client_order_id = replace.old_client_order_id;
  order.symbol = existing->symbol;
  order.side = existing->side;
  order.price = replace.new_price;
  order.quantity = replace.new_quantity;
  order.post_only = true;
  execute_submit_limit(next, order);
}

void AgentRuntime::execute_cancel_all(const ScheduledAction& scheduled,
                                      const lobx::agents::CancelAllOrders& cancel_all) {
  const std::vector<lobx::agents::AgentOrderView> orders = state_store_.open_orders(scheduled.action.agent_id);
  for (const auto& order : orders) {
    if (!cancel_all.symbol.empty() && order.symbol != cancel_all.symbol) continue;
    if (cancel_all.side && order.side != *cancel_all.side) continue;
    execute_cancel(scheduled, lobx::agents::CancelOrder{order.client_order_id});
  }
}

void AgentRuntime::account_submit_result(const ScheduledAction& scheduled,
                                         lobx::OrderId exchange_order_id,
                                         lobx::agents::ClientOrderId client_order_id,
                                         lobx::agents::Side side,
                                         lobx::agents::Price price,
                                         const std::string& symbol,
                                         const lobx::SubmitResult& result) {
  if (result.accepted) {
    ++summary_.accepted_orders;
    ++last_step_stats_.accepted_orders;
    by_exchange_order_id_[exchange_order_id] = OrderRef{scheduled.action.agent_id, client_order_id, side, symbol};
    client_to_exchange_[scheduled.action.agent_id][client_order_id] = exchange_order_id;
    state_store_.on_order_accepted(scheduled.action.agent_id,
                                   client_order_id,
                                   symbol,
                                   side,
                                   price,
                                   result.exec.remaining,
                                   scheduled.arrival_ts);

    SimulationEvent event{};
    event.ts = scheduled.arrival_ts;
    event.source = EventSource::Exchange;
    event.agent_id = scheduled.action.agent_id;
    event.agent_type = scheduled.action.agent_type;
    event.group_id = scheduled.action.group_id;
    event.payload = OrderAcceptedEvent{exchange_order_id};
    record_event(std::move(event));
  } else {
    ++summary_.rejected_orders;
    ++last_step_stats_.rejected_orders;
    state_store_.on_order_rejected(scheduled.action.agent_id, scheduled.arrival_ts);
    SimulationEvent event{};
    event.ts = scheduled.arrival_ts;
    event.source = EventSource::Exchange;
    event.agent_id = scheduled.action.agent_id;
    event.agent_type = scheduled.action.agent_type;
    event.group_id = scheduled.action.group_id;
    event.payload = OrderRejectedEvent{result.reason};
    record_event(std::move(event));
  }

  for (const lobx::TradeEvent& trade : result.trades) account_trade(trade);
}

void AgentRuntime::account_trade(const lobx::TradeEvent& trade) {
  ++summary_.trade_count;
  summary_.cum_volume += trade.qty;
  AgentRuntimeTradeStepStats& trade_stats = last_step_stats_.trades;
  ++trade_stats.trade_count;
  trade_stats.volume += static_cast<double>(trade.qty);
  trade_stats.notional += static_cast<double>(trade.price) * static_cast<double>(trade.qty);
  trade_stats.vwap = trade_stats.volume > 0.0 ? trade_stats.notional / trade_stats.volume : 0.0;
  if (trade_stats.min_price <= 0.0) trade_stats.min_price = static_cast<double>(trade.price);
  else trade_stats.min_price = std::min(trade_stats.min_price, static_cast<double>(trade.price));
  trade_stats.max_price = std::max(trade_stats.max_price, static_cast<double>(trade.price));
  trade_stats.last_trade_price = static_cast<double>(trade.price);
  if (trade.liquidity_side == lob::Side::Ask) trade_stats.buy_aggressor_volume += static_cast<double>(trade.qty);
  else trade_stats.sell_aggressor_volume += static_cast<double>(trade.qty);

  recent_trades_.push_back(TradePrintView{static_cast<double>(trade.price),
                                          static_cast<double>(trade.qty),
                                          trade.liquidity_side == lob::Side::Ask ? 1 : -1,
                                          trade.ts});
  if (recent_trades_.size() > static_cast<std::size_t>(std::max(1, config_.recent_trade_limit))) {
    recent_trades_.erase(recent_trades_.begin(),
                         recent_trades_.begin() + static_cast<std::ptrdiff_t>(
                             recent_trades_.size() - static_cast<std::size_t>(config_.recent_trade_limit)));
  }

  const auto buyer_it = by_exchange_order_id_.find(trade.buyer_order_id);
  const auto seller_it = by_exchange_order_id_.find(trade.seller_order_id);
  if (buyer_it != by_exchange_order_id_.end()) {
    state_store_.on_trade(buyer_it->second.agent_id,
                          buyer_it->second.client_order_id,
                          buyer_it->second.symbol,
                          lob::Side::Bid,
                          trade.price,
                          trade.qty,
                          trade.ts);
  }
  if (seller_it != by_exchange_order_id_.end()) {
    state_store_.on_trade(seller_it->second.agent_id,
                          seller_it->second.client_order_id,
                          seller_it->second.symbol,
                          lob::Side::Ask,
                          trade.price,
                          trade.qty,
                          trade.ts);
  }

  const lobx::agents::AgentId buyer = agent_id_from_user(trade.buyer);
  const lobx::agents::AgentId seller = agent_id_from_user(trade.seller);
  CumulativeAgentStats& buyer_stats = cumulative_agent_stats_[buyer];
  ++buyer_stats.trade_count;
  buyer_stats.buy_volume += static_cast<double>(trade.qty);
  CumulativeAgentStats& seller_stats = cumulative_agent_stats_[seller];
  ++seller_stats.trade_count;
  seller_stats.sell_volume += static_cast<double>(trade.qty);

  const lobx::agents::AgentId aggressor =
      trade.liquidity_side == lob::Side::Ask ? buyer : seller;
  const lobx::agents::AgentId passive =
      trade.liquidity_side == lob::Side::Ask ? seller : buyer;

  SimulationEvent event{};
  event.ts = trade.ts;
  event.source = EventSource::Exchange;
  event.agent_id = aggressor;
  event.payload = TradeEvent{config_.symbol,
                             static_cast<double>(trade.price),
                             static_cast<double>(trade.qty),
                             aggressor,
                             passive};
  record_event(std::move(event));
}

void AgentRuntime::record_event(SimulationEvent event) {
  event.seq = next_event_seq_++;
  if (config_.retain_events) events_.push_back(std::move(event));
  summary_.event_count = static_cast<int>(next_event_seq_ - 1);
}

void AgentRuntime::record_book_snapshot() {
  SimulationEvent event{};
  event.ts = now_;
  event.source = EventSource::Runtime;
  event.payload = BookSnapshotEvent{config_.symbol,
                                    market_view_.best_bid,
                                    market_view_.best_ask,
                                    market_view_.mid_price,
                                    market_view_.spread_bps};
  record_event(std::move(event));
}

void AgentRuntime::count_action(lobx::agents::AgentType agent_type, const lobx::agents::AgentActionPayload& payload) {
  auto increment = [&](AgentRuntimeActionCounts& counts) {
    ++counts.total_actions;
    std::visit([&](const auto& value) {
      using T = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<T, lobx::agents::SubmitLimitOrder>) ++counts.submit_limit_order;
      else if constexpr (std::is_same_v<T, lobx::agents::SubmitMarketOrder>) ++counts.submit_market_order;
      else if constexpr (std::is_same_v<T, lobx::agents::CancelOrder>) ++counts.cancel_order;
      else if constexpr (std::is_same_v<T, lobx::agents::ReplaceOrder>) ++counts.replace_order;
      else if constexpr (std::is_same_v<T, lobx::agents::CancelAllOrders>) ++counts.cancel_all_orders;
      else if constexpr (std::is_same_v<T, lobx::agents::SleepUntil>) ++counts.sleep_until;
    }, payload);
  };
  increment(last_step_stats_.actions);
  increment(last_step_stats_.actions_by_type[agent_type]);
}

int AgentRuntime::open_order_count() const {
  int count = 0;
  for (const auto& agent : agents_) {
    count += static_cast<int>(state_store_.open_orders(agent->id()).size());
  }
  return count;
}

AgentRuntimeOpenOrderSummary AgentRuntime::open_order_summary(int stale_after_steps) const {
  AgentRuntimeOpenOrderSummary out{};
  for (const auto& agent : agents_) {
    int per_agent = 0;
    const std::vector<lobx::agents::AgentOrderView> orders = state_store_.open_orders(agent->id());
    for (const auto& order : orders) {
      ++out.total_open_orders;
      ++per_agent;
      if (order.side == lob::Side::Bid) ++out.open_bid_orders;
      else ++out.open_ask_orders;
      if (stale_after_steps > 0 && now_ - order.submitted_ts > stale_after_steps) ++out.stale_order_count;
    }
    out.max_open_orders_per_agent = std::max(out.max_open_orders_per_agent, per_agent);
  }
  if (!agents_.empty()) {
    out.mean_open_orders_per_agent = static_cast<double>(out.total_open_orders) / static_cast<double>(agents_.size());
  }
  return out;
}

int AgentRuntime::decision_interval_for(lobx::agents::AgentType type) const {
  const auto it = config_.decision_interval_by_type.find(type);
  if (it != config_.decision_interval_by_type.end()) return std::max(1, it->second);
  return std::max(1, config_.default_decision_interval_steps);
}

std::vector<AgentEquitySnapshot> AgentRuntime::agent_equity_snapshots(double mark_price) const {
  std::vector<AgentEquitySnapshot> snapshots;
  snapshots.reserve(agents_.size());
  const double initial_equity =
      static_cast<double>(config_.initial_quote) + static_cast<double>(config_.initial_base) * mark_price;
  for (const auto& agent : agents_) {
    const lobx::UserId user = static_cast<lobx::UserId>(agent->id());
    const lobx::WalletBalance quote = exchange_.balance(user, config_.quote_asset);
    const lobx::WalletBalance base = exchange_.balance(user, config_.base_asset);
    AgentEquitySnapshot snapshot{};
    snapshot.agent_id = agent->id();
    snapshot.agent_type = agent->type();
    snapshot.initial_cash = static_cast<double>(config_.initial_quote);
    snapshot.initial_inventory = static_cast<double>(config_.initial_base);
    snapshot.initial_equity = initial_equity;
    snapshot.total_cash = static_cast<double>(quote.total);
    snapshot.locked_cash = static_cast<double>(quote.locked);
    snapshot.free_cash = static_cast<double>(quote.free);
    snapshot.total_inventory = static_cast<double>(base.total);
    snapshot.locked_inventory = static_cast<double>(base.locked);
    snapshot.free_inventory = static_cast<double>(base.free);
    snapshot.final_equity = snapshot.total_cash + snapshot.total_inventory * mark_price;
    snapshot.pnl = snapshot.final_equity - snapshot.initial_equity;
    snapshot.open_order_count = static_cast<int>(state_store_.open_orders(agent->id()).size());
    if (const auto stats_it = cumulative_agent_stats_.find(agent->id()); stats_it != cumulative_agent_stats_.end()) {
      snapshot.trade_count = stats_it->second.trade_count;
      snapshot.buy_volume = stats_it->second.buy_volume;
      snapshot.sell_volume = stats_it->second.sell_volume;
      snapshot.fees_paid = stats_it->second.fees_paid;
    }
    snapshots.push_back(snapshot);
  }
  return snapshots;
}

double AgentRuntime::total_agent_equity(double mark_price) const {
  double total = 0.0;
  for (const AgentEquitySnapshot& snapshot : agent_equity_snapshots(mark_price)) {
    total += snapshot.final_equity;
  }
  return total;
}

AccountingSummary AgentRuntime::accounting_summary(double mark_price) const {
  AccountingSummary out{};
  out.mark_price = mark_price;
  out.mark_price_policy = "mid_price";
  const std::vector<AgentEquitySnapshot> snapshots = agent_equity_snapshots(mark_price);
  out.agent_count = static_cast<int>(snapshots.size());
  for (const AgentEquitySnapshot& snapshot : snapshots) {
    out.sum_initial_equity += snapshot.initial_equity;
    out.sum_final_equity += snapshot.final_equity;
    out.sum_agent_pnl += snapshot.pnl;
    out.sum_cash_free += snapshot.free_cash;
    out.sum_cash_locked += snapshot.locked_cash;
    out.sum_cash_total += snapshot.total_cash;
    out.sum_inventory_free += snapshot.free_inventory;
    out.sum_inventory_locked += snapshot.locked_inventory;
    out.sum_inventory_total += snapshot.total_inventory;
    if (snapshot.pnl < 0.0) ++out.negative_pnl_agent_count;
    else if (snapshot.pnl > 0.0) ++out.positive_pnl_agent_count;
    else ++out.zero_pnl_agent_count;
  }
  const lobx::WalletBalance fee_quote = exchange_.balance(kFeeAccountUser, config_.quote_asset);
  const lobx::WalletBalance fee_base = exchange_.balance(kFeeAccountUser, config_.base_asset);
  out.exchange_fee_revenue = static_cast<double>(fee_quote.total) + static_cast<double>(fee_base.total) * mark_price;
  out.system_pnl_residual = out.sum_agent_pnl + out.exchange_fee_revenue + out.house_account_pnl + out.insurance_fund_pnl;
  return out;
}

std::string AgentRuntime::action_trace_jsonl() const {
  std::ostringstream os;
  for (const auto& action : action_trace_) {
    os << "{\"ts\":" << action.decision_ts
       << ",\"agent_id\":" << action.agent_id
       << ",\"agent_type\":\"" << lobx::agents::agent_type_name(action.agent_type) << "\""
       << ",\"group_id\":" << action.group_id
       << ",\"action_type\":\"" << lobx::agents::action_payload_name(action.payload) << "\""
       << ",\"decision_ts\":" << action.decision_ts
       << ",\"arrival_ts\":" << (action.decision_ts + config_.action_latency)
       << ",\"reason_tag\":\"" << escape_json(action.reason_tag) << "\"";
    std::visit([&](const auto& payload) {
      using T = std::decay_t<decltype(payload)>;
      if constexpr (std::is_same_v<T, lobx::agents::SubmitLimitOrder>) {
        os << ",\"side\":\"" << lobx::agents::side_name(payload.side) << "\""
           << ",\"price\":" << payload.price
           << ",\"quantity\":" << payload.quantity;
      } else if constexpr (std::is_same_v<T, lobx::agents::SubmitMarketOrder>) {
        os << ",\"side\":\"" << lobx::agents::side_name(payload.side) << "\""
           << ",\"quantity\":" << payload.quantity;
      } else if constexpr (std::is_same_v<T, lobx::agents::CancelOrder>) {
        os << ",\"client_order_id\":" << payload.client_order_id;
      }
    }, action.payload);
    os << "}\n";
  }
  return os.str();
}

std::string AgentRuntime::event_trace_jsonl() const {
  std::ostringstream os;
  for (const auto& event : events_) {
    os << "{\"ts\":" << event.ts
       << ",\"seq\":" << event.seq
       << ",\"source\":\"" << event_source_name(event.source) << "\""
       << ",\"event_type\":\"" << simulation_event_payload_name(event.payload) << "\"";
    if (event.agent_id) os << ",\"agent_id\":" << *event.agent_id;
    if (event.agent_type) os << ",\"agent_type\":\"" << lobx::agents::agent_type_name(*event.agent_type) << "\"";
    if (event.group_id) os << ",\"group_id\":" << *event.group_id;
    std::visit([&](const auto& payload) {
      using T = std::decay_t<decltype(payload)>;
      if constexpr (std::is_same_v<T, OrderRejectedEvent>) {
        os << ",\"reason\":\"" << escape_json(payload.reason) << "\"";
      } else if constexpr (std::is_same_v<T, OrderAcceptedEvent>) {
        os << ",\"order_id\":" << payload.order_id;
      } else if constexpr (std::is_same_v<T, OrderSubmittedEvent>) {
        os << ",\"symbol\":\"" << escape_json(payload.symbol) << "\""
           << ",\"side\":\"" << escape_json(payload.side) << "\""
           << ",\"price\":" << payload.price
           << ",\"quantity\":" << payload.quantity;
      } else if constexpr (std::is_same_v<T, TradeEvent>) {
        os << ",\"symbol\":\"" << escape_json(payload.symbol) << "\""
           << ",\"price\":" << payload.price
           << ",\"quantity\":" << payload.quantity
           << ",\"aggressor_agent_id\":" << payload.aggressor_agent_id;
        if (payload.passive_agent_id) os << ",\"passive_agent_id\":" << *payload.passive_agent_id;
      } else if constexpr (std::is_same_v<T, BookSnapshotEvent>) {
        os << ",\"symbol\":\"" << escape_json(payload.symbol) << "\""
           << ",\"best_bid\":" << payload.best_bid
           << ",\"best_ask\":" << payload.best_ask
           << ",\"mid_price\":" << payload.mid_price
           << ",\"spread_bps\":" << payload.spread_bps;
      } else if constexpr (std::is_same_v<T, ActionScheduledEvent>) {
        os << ",\"decision_ts\":" << payload.decision_ts
           << ",\"arrival_ts\":" << payload.arrival_ts
           << ",\"reason_tag\":\"" << escape_json(payload.reason_tag) << "\"";
      } else if constexpr (std::is_same_v<T, AgentDecisionEvent>) {
        os << ",\"action_count\":" << payload.action_count;
      }
    }, event.payload);
    os << "}\n";
  }
  return os.str();
}

bool AgentRuntime::write_action_trace_jsonl(const std::string& path) const {
  std::ofstream out(path);
  if (!out.is_open()) return false;
  out << action_trace_jsonl();
  return out.good();
}

bool AgentRuntime::write_event_trace_jsonl(const std::string& path) const {
  std::ofstream out(path);
  if (!out.is_open()) return false;
  out << event_trace_jsonl();
  return out.good();
}

} // namespace lobx::simulation
