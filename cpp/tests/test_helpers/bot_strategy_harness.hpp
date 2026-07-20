#pragma once

#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/market_microstructure_helpers.hpp"
#include "test_helpers/simulation_test_harness.hpp"
#include "test_helpers/strategy_metrics.hpp"
#include "test_helpers/test_market_data_feed.hpp"

#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

namespace lobx_test {

enum class BotActionType {
  SubmitLimit,
  CancelOrder,
  Noop
};

struct BotAction {
  lobx::UserId user{0};
  lobx::OrderId order_id{0};
  lob::Side side{lob::Side::Bid};
  lob::Tick price{0};
  lob::Quantity qty{0};
  uint32_t flags{lob::NONE};
  BotActionType type{BotActionType::SubmitLimit};
  std::string market_symbol{"BTC-USDT"};
  lob::Timestamp decision_ts{0};
};

inline BotAction noop_action(lobx::UserId user = 0) {
  BotAction action{};
  action.user = user;
  action.type = BotActionType::Noop;
  return action;
}

struct BotContext {
  lobx::UserId user{0};
  lob::Timestamp now{0};

  std::vector<PublicTrade> public_trades;
  std::vector<PrivateFill> own_fills;

  lobx::WalletBalance quote_balance;
  lobx::WalletBalance base_balance;

  std::vector<std::pair<lob::Tick, lob::Quantity>> public_bids;
  std::vector<std::pair<lob::Tick, lob::Quantity>> public_asks;
  std::vector<lobx::OpenOrder> own_open_orders;
  std::function<lobx::SimulatedFill(lob::Side, lob::Tick, lob::Quantity, uint32_t)> simulate_fill;
};

class Strategy {
public:
  virtual ~Strategy() = default;
  virtual std::vector<BotAction> on_tick(const BotContext& ctx) = 0;
};

struct BotInstance {
  lobx::UserId user{0};
  std::string name;
  TestLatencyModel latency;
  std::unique_ptr<Strategy> strategy;
};

struct BotBalanceView {
  lobx::UserId user{0};
  lobx::AssetId asset{0};
  lobx::Amount total{0};
  lobx::Amount locked{0};
  lobx::Amount free{0};

  bool operator==(const BotBalanceView&) const = default;
};

struct BotRunResult {
  std::vector<lobx::TradeEvent> trades;
  std::vector<std::string> event_types;
  std::vector<BotBalanceView> balances;
  std::vector<std::pair<lob::Tick, lob::Quantity>> bids;
  std::vector<std::pair<lob::Tick, lob::Quantity>> asks;
  std::map<lobx::UserId, StrategyMetrics> metrics;
  std::vector<std::string> action_trace;
  bool ledger_invariant_ok{true};
  bool book_open_consistency_ok{true};
  bool no_negative_balances{true};
  bool no_resting_ioc_or_fok{true};
  bool no_filled_order_remains_open{true};
  bool no_private_data_leak{true};
  bool no_future_public_trades_observed{true};
  lobx::Amount starting_base_total{0};
  lobx::Amount ending_base_total{0};
  lobx::Amount starting_quote_total{0};
  lobx::Amount ending_quote_total{0};
};

class BotSimulationRunner {
public:
  explicit BotSimulationRunner(uint64_t seed,
                               int maker_fee_bps = 0,
                               int taker_fee_bps = 0,
                               lob::Tick reference_price = 100)
      : seed_(seed),
        fixture_(maker_fee_bps, taker_fee_bps),
        public_feed_(TestLatencyModel{0, 0, 0}),
        private_feed_(0),
        reference_price_(reference_price) {
    (void)seed_;
    seed_extra_users();
    result_.starting_base_total = total_asset(fixture_.ledger, fixture_.base_asset);
    result_.starting_quote_total = total_asset(fixture_.ledger, fixture_.quote_asset);
  }

  SpotEngineFixture& fixture() { return fixture_; }
  const SpotEngineFixture& fixture() const { return fixture_; }

  bool add_bot(BotInstance bot) {
    if (bot.user == dedicated_fee_account()) {
      result_.action_trace.push_back("reject_bot reserved_fee_account user=" + std::to_string(bot.user));
      return false;
    }
    initialize_metric(bot.user);
    bots_.push_back(std::move(bot));
    return true;
  }

  BotContext context_for_user(lobx::UserId user, lob::Timestamp now, const TestLatencyModel& latency) {
    const lob::Timestamp public_cutoff = now - latency.market_data_latency;
    const lob::Timestamp private_cutoff = now - latency.market_data_latency;
    std::vector<lobx::OpenOrder> own_orders;
    for (const auto& order : fixture_.engine.open_orders()) {
      if (order.user == user) own_orders.push_back(order);
    }
    const std::vector<PublicTrade> public_trades =
        public_cutoff >= 0 ? public_feed_.visible_trades(public_cutoff) : std::vector<PublicTrade>{};
    const std::vector<PrivateFill> private_fills =
        private_cutoff >= 0 ? private_feed_.visible_private_fills(user, private_cutoff) : std::vector<PrivateFill>{};
    for (const auto& trade : public_trades) {
      result_.no_future_public_trades_observed =
          result_.no_future_public_trades_observed && trade.ts <= public_cutoff && trade.ts <= now;
    }
    for (const auto& fill : private_fills) {
      result_.no_private_data_leak = result_.no_private_data_leak && fill.user == user;
    }
    return BotContext{user,
                      now,
                      public_trades,
                      private_fills,
                      fixture_.ledger.balance(user, fixture_.quote_asset),
                      fixture_.ledger.balance(user, fixture_.base_asset),
                      fixture_.engine.topN(lob::Side::Bid, 10),
                      fixture_.engine.topN(lob::Side::Ask, 10),
                      own_orders,
                      [this, user](lob::Side side, lob::Tick price, lob::Quantity qty, uint32_t flags) {
                        return fixture_.engine.simulate_fill(user, side, price, qty, flags);
                      }};
  }

  void publish_trade_for_test(const lobx::TradeEvent& trade) {
    publish_trade(trade);
  }

  BotRunResult run(int ticks) {
    for (int tick = 1; tick <= ticks; ++tick) {
      const lob::Timestamp now = static_cast<lob::Timestamp>(tick);
      for (size_t i = 0; i < bots_.size(); ++i) {
        BotInstance& bot = bots_[i];
        const BotContext ctx = context_for_user(bot.user, now, bot.latency);
        std::vector<BotAction> actions = bot.strategy ? bot.strategy->on_tick(ctx) : std::vector<BotAction>{};
        for (BotAction action : actions) {
          if (action.type == BotActionType::Noop) {
            result_.action_trace.push_back("tick=" + std::to_string(tick) + " user=" + std::to_string(bot.user) + " noop");
            continue;
          }
          if (action.decision_ts == 0) action.decision_ts = now;
          const lob::Timestamp arrival = action.type == BotActionType::CancelOrder
                                           ? bot.latency.cancel_arrival(action.decision_ts)
                                           : bot.latency.order_arrival(action.decision_ts);
          // BotAction.user is untrusted. The runner always executes as BotInstance.user.
          scheduled_.push(ScheduledAction{arrival, next_seq_++, bot.user, action});
          result_.action_trace.push_back("tick=" + std::to_string(tick) + " schedule actor=" +
                                         std::to_string(bot.user) + " claimed_user=" +
                                         std::to_string(action.user) + " order=" +
                                         std::to_string(action.order_id) + " arrival=" +
                                         std::to_string(arrival) + " type=" +
                                         std::to_string(static_cast<int>(action.type)) + " side=" +
                                         std::to_string(static_cast<int>(action.side)) + " price=" +
                                         std::to_string(action.price) + " qty=" +
                                         std::to_string(action.qty) + " flags=" +
                                         std::to_string(action.flags));
        }
      }
      process_until(now);
      update_invariant_status();
    }
    process_all();
    update_invariant_status();
    finalize();
    return result_;
  }

private:
  struct ScheduledAction {
    lob::Timestamp arrival_ts{0};
    uint64_t seq{0};
    lobx::UserId effective_user{0};
    BotAction action;
  };

  struct ScheduledActionGreater {
    bool operator()(const ScheduledAction& a, const ScheduledAction& b) const {
      if (a.arrival_ts != b.arrival_ts) return a.arrival_ts > b.arrival_ts;
      return a.seq > b.seq;
    }
  };

  void seed_extra_users() {
    for (lobx::UserId user : {40ULL, 50ULL, 60ULL, 70ULL, 80ULL, 90ULL}) {
      (void)fixture_.ledger.deposit(user, fixture_.base_asset, 1000000LL);
      (void)fixture_.ledger.deposit(user, fixture_.quote_asset, 1000000LL);
    }
  }

  void initialize_metric(lobx::UserId user) {
    if (result_.metrics.find(user) != result_.metrics.end()) return;
    StrategyMetrics metrics{};
    metrics.user = user;
    metrics.starting_quote = fixture_.ledger.balance(user, fixture_.quote_asset).total;
    metrics.starting_base = fixture_.ledger.balance(user, fixture_.base_asset).total;
    result_.metrics[user] = metrics;
  }

  void process_until(lob::Timestamp now) {
    while (!scheduled_.empty() && scheduled_.top().arrival_ts <= now) {
      process_next();
    }
  }

  void process_all() {
    while (!scheduled_.empty()) process_next();
  }

  void process_next() {
    const ScheduledAction scheduled = scheduled_.top();
    scheduled_.pop();
    const BotAction& action = scheduled.action;
    const lobx::UserId effective_user = scheduled.effective_user;
    initialize_metric(effective_user);
    StrategyMetrics& metrics = result_.metrics[effective_user];

    if (action.type == BotActionType::SubmitLimit) {
      ++metrics.submitted_orders;
      if (action.market_symbol != fixture_.market.symbol) {
        ++metrics.rejected_orders;
        result_.action_trace.push_back("arrival=" + std::to_string(scheduled.arrival_ts) +
                                       " submit actor=" + std::to_string(effective_user) +
                                       " claimed_user=" + std::to_string(action.user) +
                                       " order=" + std::to_string(action.order_id) +
                                       " rejected_unknown_market=" + action.market_symbol);
        return;
      }
      auto submit = fixture_.submit(effective_user, action.order_id, action.side, action.price,
                                    action.qty, action.flags, scheduled.arrival_ts);
      result_.action_trace.push_back("arrival=" + std::to_string(scheduled.arrival_ts) +
                                     " submit actor=" + std::to_string(effective_user) +
                                     " claimed_user=" + std::to_string(action.user) +
                                     " order=" + std::to_string(action.order_id) +
                                     " side=" + std::to_string(static_cast<int>(action.side)) +
                                     " price=" + std::to_string(action.price) +
                                     " qty=" + std::to_string(action.qty) +
                                     " flags=" + std::to_string(action.flags) +
                                     " accepted=" + std::to_string(submit.accepted));
      if (submit.accepted) {
        ++metrics.accepted_orders;
      } else {
        ++metrics.rejected_orders;
      }
      for (const auto& trade : submit.trades) {
        publish_trade(trade);
        account_trade_metrics(effective_user, trade);
      }
    } else if (action.type == BotActionType::CancelOrder) {
      const bool canceled = fixture_.engine.cancel(action.order_id, effective_user, scheduled.arrival_ts);
      result_.action_trace.push_back("arrival=" + std::to_string(scheduled.arrival_ts) +
                                     " cancel actor=" + std::to_string(effective_user) +
                                     " claimed_user=" + std::to_string(action.user) +
                                     " order=" + std::to_string(action.order_id) +
                                     " canceled=" + std::to_string(canceled));
      if (canceled) {
        ++metrics.canceled_orders;
      } else {
        ++metrics.rejected_orders;
      }
    }
  }

  void publish_trade(const lobx::TradeEvent& trade) {
    result_.trades.push_back(trade);
    public_feed_.publish_trade(trade);
    private_feed_.publish_fill(trade.buyer, trade);
    private_feed_.publish_fill(trade.seller, trade);
  }

  void account_trade_metrics(lobx::UserId taker, const lobx::TradeEvent& trade) {
    for (lobx::UserId user : {trade.buyer, trade.seller}) {
      initialize_metric(user);
      ++result_.metrics[user].fills;
    }
    lobx::Amount notional = 0;
    if (lobx::mul_amount(trade.price, trade.qty, notional)) {
      result_.metrics[taker].fees_paid += fee_for_notional(notional, fixture_.market.taker_fee_bps);
    }
  }

  bool book_open_consistent() {
    std::map<lob::Tick, lob::Quantity> bid_open;
    std::map<lob::Tick, lob::Quantity> ask_open;
    for (const auto& order : fixture_.engine.open_orders()) {
      if (order.leaves_qty <= 0) return false;
      if ((order.flags & (lob::IOC | lob::FOK)) != 0u) return false;
      auto& depth = order.side == lob::Side::Bid ? bid_open : ask_open;
      depth[order.limit_price] += order.leaves_qty;
    }
    return bid_open == aggregate_topN(fixture_.engine, lob::Side::Bid) &&
           ask_open == aggregate_topN(fixture_.engine, lob::Side::Ask);
  }

  void update_invariant_status() {
    result_.ledger_invariant_ok = result_.ledger_invariant_ok && fixture_.ledger.invariant_ok();
    result_.book_open_consistency_ok = result_.book_open_consistency_ok && book_open_consistent();
    for (const auto& balance : fixture_.ledger.balances()) {
      result_.no_negative_balances = result_.no_negative_balances &&
                                     balance.free >= 0 && balance.locked >= 0 &&
                                     balance.total >= 0 && balance.total == balance.free + balance.locked;
    }
    for (const auto& order : fixture_.engine.open_orders()) {
      result_.no_resting_ioc_or_fok = result_.no_resting_ioc_or_fok &&
                                      (order.flags & (lob::IOC | lob::FOK)) == 0u;
      result_.no_filled_order_remains_open = result_.no_filled_order_remains_open &&
                                             order.leaves_qty > 0;
    }
  }

  void finalize() {
    result_.bids = fixture_.engine.topN(lob::Side::Bid, 100);
    result_.asks = fixture_.engine.topN(lob::Side::Ask, 100);
    result_.ending_base_total = total_asset(fixture_.ledger, fixture_.base_asset);
    result_.ending_quote_total = total_asset(fixture_.ledger, fixture_.quote_asset);
    result_.event_types.clear();
    for (const auto& event : fixture_.events.records()) result_.event_types.push_back(event.type);
    result_.balances.clear();
    for (const auto& balance : fixture_.ledger.balances()) {
      result_.balances.push_back(BotBalanceView{balance.user, balance.asset, balance.total, balance.locked, balance.free});
    }
    std::sort(result_.balances.begin(), result_.balances.end(), [](const auto& a, const auto& b) {
      if (a.user != b.user) return a.user < b.user;
      return a.asset < b.asset;
    });
    for (auto& [user, metrics] : result_.metrics) {
      finalize_strategy_metrics(metrics,
                                fixture_.ledger.balance(user, fixture_.quote_asset),
                                fixture_.ledger.balance(user, fixture_.base_asset),
                                reference_price_);
    }
  }

  uint64_t seed_{0};
  SpotEngineFixture fixture_;
  TestMarketDataFeed public_feed_;
  TestPrivateFeed private_feed_;
  lob::Tick reference_price_{100};
  std::vector<BotInstance> bots_;
  BotRunResult result_;
  uint64_t next_seq_{1};
  std::priority_queue<ScheduledAction, std::vector<ScheduledAction>, ScheduledActionGreater> scheduled_;
};

inline bool same_trade_events(const std::vector<lobx::TradeEvent>& a, const std::vector<lobx::TradeEvent>& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); ++i) {
    if (a[i].market_id != b[i].market_id || a[i].ts != b[i].ts ||
        a[i].price != b[i].price || a[i].qty != b[i].qty ||
        a[i].buyer != b[i].buyer || a[i].seller != b[i].seller ||
        a[i].buyer_order_id != b[i].buyer_order_id ||
        a[i].seller_order_id != b[i].seller_order_id ||
        a[i].liquidity_side != b[i].liquidity_side) {
      return false;
    }
  }
  return true;
}

inline bool same_bot_run_result(const BotRunResult& a, const BotRunResult& b) {
  return same_trade_events(a.trades, b.trades) && a.event_types == b.event_types &&
         a.balances == b.balances && a.bids == b.bids && a.asks == b.asks &&
         a.metrics == b.metrics && a.action_trace == b.action_trace &&
         a.ledger_invariant_ok == b.ledger_invariant_ok &&
         a.book_open_consistency_ok == b.book_open_consistency_ok &&
         a.no_negative_balances == b.no_negative_balances &&
         a.no_resting_ioc_or_fok == b.no_resting_ioc_or_fok &&
         a.no_filled_order_remains_open == b.no_filled_order_remains_open &&
         a.no_private_data_leak == b.no_private_data_leak &&
         a.no_future_public_trades_observed == b.no_future_public_trades_observed &&
         a.starting_base_total == b.starting_base_total &&
         a.ending_base_total == b.ending_base_total &&
         a.starting_quote_total == b.starting_quote_total &&
         a.ending_quote_total == b.ending_quote_total;
}

} // namespace lobx_test
