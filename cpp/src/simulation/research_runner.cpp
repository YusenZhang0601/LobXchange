#include "lobx/simulation/research_runner.hpp"

#include "lobx/market_engine.hpp"
#include "lobx/position_engine.hpp"

#include <algorithm>
#include <iomanip>
#include <limits>
#include <map>
#include <queue>
#include <random>
#include <sstream>
#include <utility>

namespace lobx::sim {

namespace {

constexpr AssetId kBaseAsset = 1;
constexpr AssetId kQuoteAsset = 2;
constexpr MarketId kSpotMarketId = 1;
constexpr Amount kInitialBalance = 1000000LL;
constexpr lob::Tick kReferencePrice = 100;

double param_or(const BotConfig& bot, const std::string& name, double fallback) {
  const auto it = bot.params.find(name);
  return it == bot.params.end() ? fallback : it->second;
}

Amount fee_for_notional(Amount notional, int fee_bps) {
  if (notional <= 0 || fee_bps <= 0) return 0;
  return (notional * fee_bps) / 10000;
}

long double mark_to_reference(Amount quote_delta, Amount base_delta) {
  return static_cast<long double>(quote_delta) +
         static_cast<long double>(base_delta) * static_cast<long double>(kReferencePrice);
}

std::string escape_json(const std::string& input) {
  std::ostringstream os;
  for (char c : input) {
    switch (c) {
      case '\\': os << "\\\\"; break;
      case '"': os << "\\\""; break;
      case '\n': os << "\\n"; break;
      case '\r': os << "\\r"; break;
      case '\t': os << "\\t"; break;
      default: os << c; break;
    }
  }
  return os.str();
}

std::string format_decimal(long double value) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(6) << static_cast<double>(value);
  return os.str();
}

struct PublicTrade {
  MarketId market_id{0};
  lob::Timestamp ts{0};
  lob::Tick price{0};
  lob::Quantity qty{0};
  lob::Side liquidity_side{lob::Side::Ask};
  uint64_t public_trade_id{0};
};

struct PrivateFill {
  MarketId market_id{0};
  lob::Timestamp ts{0};
  lob::Tick price{0};
  lob::Quantity qty{0};
  UserId user{0};
  OrderId own_order_id{0};
  bool is_buyer{false};
};

struct VisiblePublicTrade {
  PublicTrade trade;
};

struct VisiblePrivateFill {
  UserId user{0};
  PrivateFill fill;
};

struct BotContext {
  UserId user{0};
  lob::Timestamp now{0};
  std::vector<PublicTrade> public_trades;
  std::vector<PrivateFill> own_fills;
  WalletBalance quote_balance;
  WalletBalance base_balance;
  std::vector<std::pair<lob::Tick, lob::Quantity>> public_bids;
  std::vector<std::pair<lob::Tick, lob::Quantity>> public_asks;
  std::vector<OpenOrder> own_open_orders;
};

enum class ActionType {
  SubmitLimit,
  CancelOrder,
  Noop
};

struct BotAction {
  ActionType type{ActionType::Noop};
  std::string market_symbol{"BTC-USDT"};
  OrderId order_id{0};
  lob::Side side{lob::Side::Bid};
  lob::Tick price{0};
  lob::Quantity qty{0};
  uint32_t flags{lob::NONE};
  lob::Timestamp decision_ts{0};
};

struct BotState {
  BotConfig config;
  std::mt19937 rng;
  OrderId next_order_id{0};
  bool taker_done{false};
  bool script_done{false};
};

struct ScheduledAction {
  lob::Timestamp arrival_ts{0};
  uint64_t seq{0};
  UserId user{0};
  BotAction action;
};

struct ScheduledActionGreater {
  bool operator()(const ScheduledAction& a, const ScheduledAction& b) const {
    if (a.arrival_ts != b.arrival_ts) return a.arrival_ts > b.arrival_ts;
    return a.seq > b.seq;
  }
};

class SpotResearchSession {
public:
  explicit SpotResearchSession(ScenarioConfig config)
      : config_(std::move(config)),
        market_{kSpotMarketId,
                "BTC-USDT",
                kBaseAsset,
                kQuoteAsset,
                kQuoteAsset,
                MarketType::Spot,
                MarketStatus::Active,
                1,
                1,
                1,
                1,
                0,
                0,
                1},
        engine_(market_, ledger_, risk_, &positions_, &events_) {
    result_.config = config_;
    setup_bots();
  }

  ResearchRunResult run() {
    for (int tick = 1; tick <= config_.ticks; ++tick) {
      const lob::Timestamp now = static_cast<lob::Timestamp>(tick);
      for (BotState& bot : bots_) {
        const BotContext ctx = context_for(bot, now);
        std::vector<BotAction> actions = actions_for(bot, ctx);
        for (BotAction action : actions) {
          if (action.type == ActionType::Noop) {
            result_.action_trace.push_back("tick=" + std::to_string(tick) +
                                           " user=" + std::to_string(bot.config.user) + " noop");
            continue;
          }
          if (action.decision_ts == 0) action.decision_ts = now;
          const lob::Timestamp arrival = action.type == ActionType::CancelOrder
                                             ? bot.config.latency.cancel_latency + action.decision_ts
                                             : bot.config.latency.order_latency + action.decision_ts;
          queue_.push(ScheduledAction{arrival, next_seq_++, bot.config.user, action});
          result_.action_trace.push_back("tick=" + std::to_string(tick) +
                                         " schedule user=" + std::to_string(bot.config.user) +
                                         " order=" + std::to_string(action.order_id) +
                                         " arrival=" + std::to_string(arrival) +
                                         " type=" + std::to_string(static_cast<int>(action.type)) +
                                         " side=" + std::to_string(static_cast<int>(action.side)) +
                                         " price=" + std::to_string(action.price) +
                                         " qty=" + std::to_string(action.qty) +
                                         " flags=" + std::to_string(action.flags));
        }
      }
      process_until(now);
      update_invariants();
    }
    process_all();
    update_invariants();
    finalize();
    return result_;
  }

private:
  void setup_bots() {
    for (size_t i = 0; i < config_.bots.size(); ++i) {
      const BotConfig& cfg = config_.bots[i];
      (void)ledger_.deposit(cfg.user, kBaseAsset, kInitialBalance);
      (void)ledger_.deposit(cfg.user, kQuoteAsset, kInitialBalance);
      StrategyMetrics metrics{};
      metrics.user = cfg.user;
      metrics.bot_name = cfg.name;
      metrics.starting_base = ledger_.balance(cfg.user, kBaseAsset).total;
      metrics.starting_quote = ledger_.balance(cfg.user, kQuoteAsset).total;
      result_.metrics[cfg.user] = metrics;

      const uint64_t rng_seed = cfg.params.find("seed") == cfg.params.end()
                                    ? config_.seed * 131 + cfg.user * 17 + static_cast<uint64_t>(i)
                                    : static_cast<uint64_t>(param_or(cfg, "seed", 0));
      bots_.push_back(BotState{cfg,
                               std::mt19937(static_cast<uint32_t>(rng_seed)),
                               static_cast<OrderId>(1000000 + i * 100000),
                               false,
                               false});
    }
  }

  BotContext context_for(const BotState& bot, lob::Timestamp now) {
    std::vector<PublicTrade> public_trades;
    for (const VisiblePublicTrade& visible : public_trades_) {
      if (visible.trade.ts + bot.config.latency.market_data_latency <= now) {
        public_trades.push_back(visible.trade);
        result_.no_future_public_data_leak =
            result_.no_future_public_data_leak && visible.trade.ts <= now;
      }
    }

    std::vector<PrivateFill> own_fills;
    for (const VisiblePrivateFill& visible : private_fills_) {
      if (visible.user == bot.config.user &&
          visible.fill.ts + bot.config.latency.private_data_latency <= now) {
        own_fills.push_back(visible.fill);
        result_.no_private_data_leak =
            result_.no_private_data_leak && visible.fill.user == bot.config.user;
      }
    }

    std::vector<OpenOrder> own_orders;
    for (const OpenOrder& order : engine_.open_orders()) {
      if (order.user == bot.config.user) own_orders.push_back(order);
    }

    return BotContext{bot.config.user,
                      now,
                      public_trades,
                      own_fills,
                      ledger_.balance(bot.config.user, kQuoteAsset),
                      ledger_.balance(bot.config.user, kBaseAsset),
                      engine_.topN(lob::Side::Bid, 10),
                      engine_.topN(lob::Side::Ask, 10),
                      own_orders};
  }

  std::vector<BotAction> actions_for(BotState& bot, const BotContext& ctx) {
    if (bot.config.strategy_type == "market_maker") return market_maker_actions(bot, ctx);
    if (bot.config.strategy_type == "taker_sweep") return taker_actions(bot);
    if (bot.config.strategy_type == "noise_trader") return noise_actions(bot);
    if (bot.config.strategy_type == "user_script") return script_actions(bot, ctx.now);
    return {};
  }

  std::vector<BotAction> market_maker_actions(BotState& bot, const BotContext& ctx) {
    bool has_bid = false;
    bool has_ask = false;
    for (const OpenOrder& order : ctx.own_open_orders) {
      if (order.side == lob::Side::Bid) has_bid = true;
      if (order.side == lob::Side::Ask) has_ask = true;
    }

    const lob::Tick bid_px = static_cast<lob::Tick>(param_or(bot.config, "bid_px", 99));
    const lob::Tick ask_px = static_cast<lob::Tick>(param_or(bot.config, "ask_px", 101));
    const lob::Quantity qty = static_cast<lob::Quantity>(param_or(bot.config, "qty", 1));
    const bool bid_would_cross = !ctx.public_asks.empty() && ctx.public_asks.front().first <= bid_px;
    const bool ask_would_cross = !ctx.public_bids.empty() && ctx.public_bids.front().first >= ask_px;

    std::vector<BotAction> out;
    if (!has_bid && !bid_would_cross) {
      out.push_back(BotAction{ActionType::SubmitLimit, config_.market_symbol, bot.next_order_id++,
                              lob::Side::Bid, bid_px, qty, lob::POST_ONLY, 0});
    }
    if (!has_ask && !ask_would_cross) {
      out.push_back(BotAction{ActionType::SubmitLimit, config_.market_symbol, bot.next_order_id++,
                              lob::Side::Ask, ask_px, qty, lob::POST_ONLY, 0});
    }
    return out;
  }

  std::vector<BotAction> taker_actions(BotState& bot) {
    if (bot.taker_done) return {};
    const int side_value = static_cast<int>(param_or(bot.config, "side", 0));
    const lob::Side side = side_value == 1 ? lob::Side::Ask : lob::Side::Bid;
    const lob::Quantity qty = static_cast<lob::Quantity>(param_or(bot.config, "target_qty", 1));
    const lob::Tick limit_price = static_cast<lob::Tick>(param_or(bot.config, "limit_price", 101));
    const long double max_avg = static_cast<long double>(param_or(bot.config, "max_avg_price", 101));
    const SimulatedFill sim = engine_.simulate_fill(bot.config.user, side, limit_price, qty, lob::IOC);
    if (sim.code != RejectCode::None || sim.fillable_qty < qty || sim.avg_price > max_avg) return {};
    bot.taker_done = true;
    return {BotAction{ActionType::SubmitLimit, config_.market_symbol, bot.next_order_id++,
                      side, limit_price, qty, lob::IOC, 0}};
  }

  std::vector<BotAction> noise_actions(BotState& bot) {
    const bool bid = (bot.rng() % 2) == 0;
    const uint32_t flags = (bot.rng() % 4) == 0 ? lob::IOC : lob::POST_ONLY;
    return {BotAction{ActionType::SubmitLimit,
                      config_.market_symbol,
                      bot.next_order_id++,
                      bid ? lob::Side::Bid : lob::Side::Ask,
                      static_cast<lob::Tick>(98 + (bot.rng() % 5)),
                      1,
                      flags,
                      0}};
  }

  std::vector<BotAction> script_actions(BotState& bot, lob::Timestamp now) {
    if (bot.script_done) return {};
    const lob::Timestamp decision_ts = static_cast<lob::Timestamp>(param_or(bot.config, "decision_ts", 1));
    if (decision_ts > now) return {};
    const int side_value = static_cast<int>(param_or(bot.config, "side", 0));
    const lob::Side side = side_value == 1 ? lob::Side::Ask : lob::Side::Bid;
    const lob::Tick price = static_cast<lob::Tick>(param_or(bot.config, "price", side == lob::Side::Bid ? 101 : 99));
    const lob::Quantity qty = static_cast<lob::Quantity>(param_or(bot.config, "qty", 1));
    const uint32_t flags = static_cast<uint32_t>(param_or(bot.config, "flags", lob::IOC));
    const OrderId order_id = static_cast<OrderId>(param_or(bot.config, "order_id", bot.next_order_id++));
    bot.script_done = true;
    return {BotAction{ActionType::SubmitLimit, config_.market_symbol, order_id, side, price, qty, flags, decision_ts}};
  }

  void process_until(lob::Timestamp now) {
    while (!queue_.empty() && queue_.top().arrival_ts <= now) process_next();
  }

  void process_all() {
    while (!queue_.empty()) process_next();
  }

  void process_next() {
    const ScheduledAction scheduled = queue_.top();
    queue_.pop();
    StrategyMetrics& metrics = result_.metrics[scheduled.user];

    if (scheduled.action.type == ActionType::SubmitLimit) {
      ++metrics.submitted_orders;
      if (scheduled.action.market_symbol != market_.symbol) {
        ++metrics.rejected_orders;
        result_.action_trace.push_back("arrival=" + std::to_string(scheduled.arrival_ts) +
                                       " submit user=" + std::to_string(scheduled.user) +
                                       " order=" + std::to_string(scheduled.action.order_id) +
                                       " rejected_unknown_market");
        return;
      }
      OrderRequest req{market_.id,
                       scheduled.user,
                       scheduled.action.order_id,
                       events_.next_seq(),
                       scheduled.arrival_ts,
                       scheduled.action.side,
                       scheduled.action.price,
                       scheduled.action.qty,
                       scheduled.action.flags};
      SubmitResult submit = engine_.submit_limit(req);
      result_.action_trace.push_back("arrival=" + std::to_string(scheduled.arrival_ts) +
                                     " submit user=" + std::to_string(scheduled.user) +
                                     " order=" + std::to_string(scheduled.action.order_id) +
                                     " side=" + std::to_string(static_cast<int>(scheduled.action.side)) +
                                     " price=" + std::to_string(scheduled.action.price) +
                                     " qty=" + std::to_string(scheduled.action.qty) +
                                     " flags=" + std::to_string(scheduled.action.flags) +
                                     " accepted=" + std::to_string(submit.accepted));
      if (submit.accepted) {
        ++metrics.accepted_orders;
      } else {
        ++metrics.rejected_orders;
      }
      for (const TradeEvent& trade : submit.trades) {
        publish_trade(trade);
        account_trade_metrics(scheduled.user, trade);
      }
    } else if (scheduled.action.type == ActionType::CancelOrder) {
      const bool canceled = engine_.cancel(scheduled.action.order_id, scheduled.user, scheduled.arrival_ts);
      result_.action_trace.push_back("arrival=" + std::to_string(scheduled.arrival_ts) +
                                     " cancel user=" + std::to_string(scheduled.user) +
                                     " order=" + std::to_string(scheduled.action.order_id) +
                                     " canceled=" + std::to_string(canceled));
      if (canceled) {
        ++metrics.canceled_orders;
      } else {
        ++metrics.rejected_orders;
      }
    }
  }

  void publish_trade(const TradeEvent& trade) {
    result_.trades.push_back(trade);
    public_trades_.push_back(VisiblePublicTrade{
        PublicTrade{trade.market_id, trade.ts, trade.price, trade.qty, trade.liquidity_side, next_public_trade_id_++}});
    private_fills_.push_back(VisiblePrivateFill{trade.buyer,
                                                PrivateFill{trade.market_id, trade.ts, trade.price, trade.qty,
                                                            trade.buyer, trade.buyer_order_id, true}});
    private_fills_.push_back(VisiblePrivateFill{trade.seller,
                                                PrivateFill{trade.market_id, trade.ts, trade.price, trade.qty,
                                                            trade.seller, trade.seller_order_id, false}});
  }

  void account_trade_metrics(UserId taker, const TradeEvent& trade) {
    for (UserId user : {trade.buyer, trade.seller}) {
      ++result_.metrics[user].fills;
    }
    Amount notional = 0;
    if (mul_amount(trade.price, trade.qty, notional)) {
      result_.metrics[taker].fees_paid += fee_for_notional(notional, market_.taker_fee_bps);
    }
  }

  bool book_open_consistent() {
    std::map<lob::Tick, lob::Quantity> bid_open;
    std::map<lob::Tick, lob::Quantity> ask_open;
    for (const OpenOrder& order : engine_.open_orders()) {
      if (order.leaves_qty <= 0) return false;
      if ((order.flags & (lob::IOC | lob::FOK)) != 0u) return false;
      auto& depth = order.side == lob::Side::Bid ? bid_open : ask_open;
      depth[order.limit_price] += order.leaves_qty;
    }
    return bid_open == aggregate_top(lob::Side::Bid) && ask_open == aggregate_top(lob::Side::Ask);
  }

  std::map<lob::Tick, lob::Quantity> aggregate_top(lob::Side side) {
    std::map<lob::Tick, lob::Quantity> out;
    for (const auto& [price, qty] : engine_.topN(side, 1000)) out[price] += qty;
    return out;
  }

  void update_invariants() {
    result_.ledger_invariant_ok = result_.ledger_invariant_ok && ledger_.invariant_ok();
    result_.book_open_consistency_ok = result_.book_open_consistency_ok && book_open_consistent();
  }

  void finalize() {
    result_.bids = engine_.topN(lob::Side::Bid, 100);
    result_.asks = engine_.topN(lob::Side::Ask, 100);
    result_.event_types.clear();
    for (const EventRecord& record : events_.records()) result_.event_types.push_back(record.type);
    result_.balances = ledger_.balances();
    std::sort(result_.balances.begin(), result_.balances.end(), [](const WalletBalance& a, const WalletBalance& b) {
      if (a.user != b.user) return a.user < b.user;
      return a.asset < b.asset;
    });
    for (auto& [user, metrics] : result_.metrics) {
      metrics.ending_quote = ledger_.balance(user, kQuoteAsset).total;
      metrics.ending_base = ledger_.balance(user, kBaseAsset).total;
      metrics.inventory = metrics.ending_base - metrics.starting_base;
      metrics.net_pnl = mark_to_reference(metrics.ending_quote - metrics.starting_quote,
                                          metrics.ending_base - metrics.starting_base);
      metrics.gross_pnl = metrics.net_pnl + static_cast<long double>(metrics.fees_paid);
    }
  }

  ScenarioConfig config_;
  AccountLedger ledger_;
  RiskEngine risk_;
  PositionEngine positions_;
  EventStore events_;
  Market market_;
  MarketEngine engine_;
  ResearchRunResult result_;
  std::vector<BotState> bots_;
  std::priority_queue<ScheduledAction, std::vector<ScheduledAction>, ScheduledActionGreater> queue_;
  uint64_t next_seq_{1};
  uint64_t next_public_trade_id_{1};
  std::vector<VisiblePublicTrade> public_trades_;
  std::vector<VisiblePrivateFill> private_fills_;
};

} // namespace

ResearchRunner::ResearchRunner() = default;

ResearchRunResult ResearchRunner::run_scenario(const ScenarioConfig& config) {
  ResearchRunResult invalid{};
  invalid.config = config;
  const ValidationResult validation = validate_scenario_config(config);
  if (!validation.ok) {
    invalid.ledger_invariant_ok = false;
    invalid.book_open_consistency_ok = false;
    invalid.no_private_data_leak = true;
    invalid.no_future_public_data_leak = true;
    invalid.action_trace.push_back("validation_failed: " + validation.reason);
    return invalid;
  }
  return SpotResearchSession(config).run();
}

std::vector<ResearchRunResult> ResearchRunner::run_parameter_sweep(const ScenarioConfig& base,
                                                                   const std::vector<SweepParam>& params) {
  std::vector<ResearchRunResult> out;
  for (const SweepRun& run : ::lobx::sim::run_parameter_sweep(*this, base, params)) out.push_back(run.result);
  return out;
}

std::vector<SeedEvaluationRun> ResearchRunner::run_multi_seed(ScenarioConfig base,
                                                              const std::vector<uint64_t>& seeds) {
  return ::lobx::sim::run_multi_seed_evaluation(*this, std::move(base), seeds);
}

std::string export_ranked_results_csv(const std::vector<RankedStrategyResult>& ranked) {
  std::ostringstream os;
  os << "rank,bot_name,user,score,net_pnl,gross_pnl,fees_paid,fills,accepted_orders,rejected_orders\n";
  for (const RankedStrategyResult& row : ranked) {
    os << row.rank << ','
       << row.bot_name << ','
       << row.user << ','
       << format_decimal(row.score) << ','
       << format_decimal(row.metrics.net_pnl) << ','
       << format_decimal(row.metrics.gross_pnl) << ','
       << row.metrics.fees_paid << ','
       << row.metrics.fills << ','
       << row.metrics.accepted_orders << ','
       << row.metrics.rejected_orders << '\n';
  }
  return os.str();
}

std::string export_aggregated_stats_csv(const std::vector<AggregatedStrategyStats>& stats) {
  std::ostringstream os;
  os << "bot_name,user,runs,mean_net_pnl,median_net_pnl,min_net_pnl,max_net_pnl,win_rate\n";
  for (const AggregatedStrategyStats& row : stats) {
    os << row.bot_name << ','
       << row.user << ','
       << row.runs << ','
       << format_decimal(row.mean_net_pnl) << ','
       << format_decimal(row.median_net_pnl) << ','
       << format_decimal(row.min_net_pnl) << ','
       << format_decimal(row.max_net_pnl) << ','
       << format_decimal(row.win_rate) << '\n';
  }
  return os.str();
}

std::string export_run_summary_json(const ResearchRunResult& result) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(6);
  os << '{';
  os << "\"seed\":" << result.config.seed << ',';
  os << "\"ticks\":" << result.config.ticks << ',';
  os << "\"market_symbol\":\"" << escape_json(result.config.market_symbol) << "\",";
  os << "\"trade_count\":" << result.trades.size() << ',';
  os << "\"event_count\":" << result.event_types.size() << ',';

  os << "\"metrics\":[";
  bool first = true;
  for (const auto& [_, metrics] : result.metrics) {
    if (!first) os << ',';
    first = false;
    os << '{'
       << "\"user\":" << metrics.user << ','
       << "\"bot_name\":\"" << escape_json(metrics.bot_name) << "\","
       << "\"net_pnl\":" << static_cast<double>(metrics.net_pnl) << ','
       << "\"gross_pnl\":" << static_cast<double>(metrics.gross_pnl) << ','
       << "\"fees_paid\":" << metrics.fees_paid << ','
       << "\"fills\":" << metrics.fills << ','
       << "\"accepted_orders\":" << metrics.accepted_orders << ','
       << "\"rejected_orders\":" << metrics.rejected_orders
       << '}';
  }
  os << "],";

  auto write_depth = [&](const char* name, const std::vector<std::pair<lob::Tick, lob::Quantity>>& depth) {
    os << '"' << name << "\":[";
    for (size_t i = 0; i < depth.size(); ++i) {
      if (i > 0) os << ',';
      os << "{\"price\":" << depth[i].first << ",\"qty\":" << depth[i].second << '}';
    }
    os << ']';
  };
  os << "\"final_book\":{";
  write_depth("bids", result.bids);
  os << ',';
  write_depth("asks", result.asks);
  os << "},";

  os << "\"invariants\":{"
     << "\"ledger\":" << (result.ledger_invariant_ok ? "true" : "false") << ','
     << "\"book_open\":" << (result.book_open_consistency_ok ? "true" : "false") << ','
     << "\"private_data\":" << (result.no_private_data_leak ? "true" : "false") << ','
     << "\"future_public_data\":" << (result.no_future_public_data_leak ? "true" : "false")
     << "}";
  os << '}';
  return os.str();
}

} // namespace lobx::sim
