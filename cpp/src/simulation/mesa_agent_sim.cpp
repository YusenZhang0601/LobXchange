#include "lobx/simulation/mesa_agent_sim.hpp"

#include "lobx/exchange.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace lobx::sim {

namespace {

constexpr const char* kBaseAsset = "BTC";
constexpr const char* kQuoteAsset = "USDT";
constexpr uint32_t kPostOnly = lob::POST_ONLY;
constexpr uint32_t kIoc = lob::IOC;

void require_ok(const Result& result, const std::string& context) {
  if (!result.ok) throw std::runtime_error(context + ": " + result.reason);
}

Exchange bootstrap_exchange(const std::string& market_symbol) {
  Exchange exchange;
  require_ok(exchange.issue_asset(kQuoteAsset, 6, 900000000000000000LL, 1, 0), "issue USDT");
  require_ok(exchange.issue_asset(kBaseAsset, 8, 900000000000000000LL, 1, 0), "issue BTC");
  require_ok(exchange.create_spot_market(market_symbol, kBaseAsset, kQuoteAsset, 1, 1, 1, 1), "create market");
  exchange.events().set_memory_enabled(false);
  return exchange;
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

void validate_config(const MesaAgentSimConfig& config) {
  if (config.reference_price <= 0) throw std::invalid_argument("reference_price must be positive");
  if (config.steps < 0) throw std::invalid_argument("steps must be non-negative");
  if (config.book_levels <= 0) throw std::invalid_argument("book_levels must be positive");
  if (config.initial_base < 0 || config.initial_quote < 0) {
    throw std::invalid_argument("initial balances must be non-negative");
  }
  if (config.agents.market_makers < 0 || config.agents.noise_traders < 0 ||
      config.agents.momentum < 0 || config.agents.mean_reversion < 0 ||
      config.agents.whale_sweepers < 0) {
    throw std::invalid_argument("agent counts must be non-negative");
  }
  for (int interval : config.candle_intervals) {
    if (interval <= 0) throw std::invalid_argument("candle intervals must be positive");
  }
  for (lob::Tick price : config.initial_trade_prices) {
    if (price <= 0) throw std::invalid_argument("initial trade prices must be positive");
  }
}

int total_agent_count(const MesaAgentCounts& counts) {
  return counts.market_makers + counts.noise_traders + counts.momentum +
         counts.mean_reversion + counts.whale_sweepers;
}

std::string format_double(double value) {
  std::ostringstream os;
  os << std::fixed << std::setprecision(6) << value;
  return os.str();
}

const char* side_name(lob::Side side) {
  return side == lob::Side::Bid ? "BID" : "ASK";
}

class StepCandleAggregator {
public:
  explicit StepCandleAggregator(std::vector<int> intervals) : intervals_(std::move(intervals)) {
    std::sort(intervals_.begin(), intervals_.end());
    intervals_.erase(std::unique(intervals_.begin(), intervals_.end()), intervals_.end());
  }

  std::vector<MesaStepCandle> update(int step, const std::vector<TradeEvent>& trades) {
    if (trades.empty()) return {};
    std::vector<MesaStepCandle> changed;
    std::vector<std::pair<int, int>> changed_keys;
    for (int interval : intervals_) {
      const int open_step = ((step - 1) / interval) * interval + 1;
      const std::pair<int, int> key{interval, open_step};
      MesaStepCandle& candle = candles_[key];
      for (const TradeEvent& trade : trades) {
        if (candle.trade_count == 0) {
          candle.interval_steps = interval;
          candle.open_step = open_step;
          candle.close_step = open_step + interval - 1;
          candle.open = trade.price;
          candle.high = trade.price;
          candle.low = trade.price;
          candle.close = trade.price;
        }
        candle.high = std::max(candle.high, trade.price);
        candle.low = std::min(candle.low, trade.price);
        candle.close = trade.price;
        candle.volume += trade.qty;
        candle.quote_volume += static_cast<Amount>(trade.price * trade.qty);
        ++candle.trade_count;
      }
      changed_keys.push_back(key);
    }
    std::sort(changed_keys.begin(), changed_keys.end());
    changed_keys.erase(std::unique(changed_keys.begin(), changed_keys.end()), changed_keys.end());
    changed.reserve(changed_keys.size());
    for (const auto& key : changed_keys) changed.push_back(candles_.at(key));
    return changed;
  }

private:
  std::vector<int> intervals_;
  std::map<std::pair<int, int>, MesaStepCandle> candles_;
};

} // namespace

struct MesaAgentSimulation::Impl {
  struct Agent {
    UserId user{0};
    std::string name;
    lob::Tick reference_price{100};

    Agent(UserId user_id, std::string agent_name, lob::Tick reference)
        : user(user_id), name(std::move(agent_name)), reference_price(reference) {}
    virtual ~Agent() = default;

    virtual const char* kind() const = 0;
    virtual void step(Impl& model) = 0;

    SubmitResult submit(Impl& model, lob::Side side, lob::Tick price, lob::Quantity qty, uint32_t flags) const {
      return model.submit_action(*this, side, std::max<lob::Tick>(1, price), std::max<lob::Quantity>(1, qty), flags);
    }
  };

  struct MarketMakerAgent final : Agent {
    using Agent::Agent;

    const char* kind() const override { return "market_maker"; }

    void step(Impl& model) override {
      const lob::Tick mid = model.last_price();
      const lob::Tick spread = static_cast<lob::Tick>(2 + model.rand_int(0, 2));
      const lob::Quantity qty = static_cast<lob::Quantity>(2 + model.rand_int(0, 2));
      submit(model, lob::Side::Bid, mid - spread, qty, kPostOnly);
      submit(model, lob::Side::Ask, mid + spread, qty, kPostOnly);
    }
  };

  struct NoiseTraderAgent final : Agent {
    using Agent::Agent;

    const char* kind() const override { return "noise_trader"; }

    void step(Impl& model) override {
      const lob::Tick mid = model.last_price();
      const bool buy = model.rand_unit() < 0.5;
      const int price_offset = model.rand_int(-4, 4);
      const bool aggressive = model.rand_unit() < 0.35;
      lob::Tick price = mid;
      if (buy) {
        price = mid + static_cast<lob::Tick>(aggressive ? 8 : price_offset);
      } else {
        price = mid - static_cast<lob::Tick>(aggressive ? 8 : price_offset);
      }
      submit(model, buy ? lob::Side::Bid : lob::Side::Ask, price, 1, aggressive ? kIoc : kPostOnly);
    }
  };

  struct MomentumAgent final : Agent {
    using Agent::Agent;

    const char* kind() const override { return "momentum"; }

    void step(Impl& model) override {
      if (model.trade_prices.size() < 2) return;
      const lob::Tick previous = model.trade_prices[model.trade_prices.size() - 2];
      const lob::Tick last = model.trade_prices.back();
      if (last == previous) return;
      const bool buy = last > previous;
      submit(model, buy ? lob::Side::Bid : lob::Side::Ask, buy ? last + 12 : last - 12, 2, kIoc);
    }
  };

  struct MeanReversionAgent final : Agent {
    using Agent::Agent;

    const char* kind() const override { return "mean_reversion"; }

    void step(Impl& model) override {
      const lob::Tick last = model.last_price();
      const lob::Tick deviation = last - reference_price;
      if (std::llabs(static_cast<long long>(deviation)) < 3) return;
      const bool sell = deviation > 0;
      submit(model, sell ? lob::Side::Ask : lob::Side::Bid, sell ? last - 4 : last + 4, 2, kIoc);
    }
  };

  struct WhaleSweeperAgent final : Agent {
    using Agent::Agent;

    const char* kind() const override { return "whale_sweeper"; }

    void step(Impl& model) override {
      if (model.now % 12 != 0) return;
      const bool buy = model.rand_unit() < 0.5;
      const lob::Tick last = model.last_price();
      submit(model, buy ? lob::Side::Bid : lob::Side::Ask, buy ? last + 25 : last - 25, 20, kIoc);
    }
  };

  explicit Impl(MesaAgentSimConfig sim_config)
      : config(std::move(sim_config)),
        exchange(bootstrap_exchange(config.market_symbol)),
        rng(config.seed),
        candles(config.candle_intervals) {
    validate_config(config);
    const int agent_count = total_agent_count(config.agents);
    agents.reserve(static_cast<size_t>(agent_count));
    order.reserve(static_cast<size_t>(agent_count));

    UserId next_user = config.first_user_id;
    add_agents<MarketMakerAgent>(config.agents.market_makers, "maker", next_user);
    add_agents<NoiseTraderAgent>(config.agents.noise_traders, "noise", next_user);
    add_agents<MomentumAgent>(config.agents.momentum, "momentum", next_user);
    add_agents<MeanReversionAgent>(config.agents.mean_reversion, "mean_reversion", next_user);
    add_agents<WhaleSweeperAgent>(config.agents.whale_sweepers, "whale", next_user);

    for (const auto& agent : agents) {
      require_ok(exchange.deposit(agent->user, kQuoteAsset, config.initial_quote), "deposit USDT");
      require_ok(exchange.deposit(agent->user, kBaseAsset, config.initial_base), "deposit BTC");
      ++agent_type_counts[agent->kind()];
    }
    trade_prices = config.initial_trade_prices;
    refresh_book();
  }

  template <typename AgentT>
  void add_agents(int count, const std::string& prefix, UserId& next_user) {
    for (int i = 0; i < count; ++i) {
      agents.push_back(std::make_unique<AgentT>(next_user++, prefix + "_" + std::to_string(i), config.reference_price));
      order.push_back(order.size());
    }
  }

  MesaStepEvents step() {
    ++now;
    refresh_book();
    current_orders.clear();
    std::shuffle(order.begin(), order.end(), rng);
    const size_t before_trades = trades.size();
    for (size_t index : order) agents[index]->step(*this);
    refresh_book();
    if (const auto spread = current_spread(); spread >= 0) spreads.push_back(spread);

    std::vector<TradeEvent> new_trades(trades.begin() + static_cast<std::ptrdiff_t>(before_trades), trades.end());
    MesaStepEvents events{};
    events.step = now;
    events.orders = current_orders;
    events.trades = new_trades;
    events.candles = candles.update(now, new_trades);
    events.stats = stats();
    return events;
  }

  MesaAgentSimSummary run() {
    for (int i = 0; i < config.steps; ++i) step();
    return summary();
  }

  SubmitResult submit_action(const Agent& agent, lob::Side side, lob::Tick price, lob::Quantity qty, uint32_t flags) {
    const OrderId order_id = next_order_id++;
    SubmitResult result = exchange.submit_limit(config.market_symbol, agent.user, order_id, side, price, qty, flags, now);
    if (result.accepted) {
      ++accepted_orders;
    } else {
      ++rejected_orders;
    }
    current_orders.push_back(MesaOrderEvent{now,
                                            agent.user,
                                            agent.kind(),
                                            side,
                                            price,
                                            qty,
                                            flags,
                                            result.accepted,
                                            result.code,
                                            static_cast<int>(result.trades.size()),
                                            result.exec.filled,
                                            result.exec.remaining});
    trades.insert(trades.end(), result.trades.begin(), result.trades.end());
    for (const TradeEvent& trade : result.trades) trade_prices.push_back(trade.price);
    return result;
  }

  void refresh_book() {
    book_bids = exchange.topN(config.market_symbol, lob::Side::Bid, config.book_levels);
    book_asks = exchange.topN(config.market_symbol, lob::Side::Ask, config.book_levels);
  }

  lob::Tick best_bid() const {
    return book_bids.empty() ? 0 : book_bids.front().first;
  }

  lob::Tick best_ask() const {
    return book_asks.empty() ? 0 : book_asks.front().first;
  }

  lob::Tick last_price() const {
    if (!trade_prices.empty()) return trade_prices.back();
    const lob::Tick bid = best_bid();
    const lob::Tick ask = best_ask();
    if (bid > 0 && ask > 0) return (bid + ask) / 2;
    return config.reference_price;
  }

  lob::Tick current_spread() const {
    const lob::Tick bid = best_bid();
    const lob::Tick ask = best_ask();
    if (bid <= 0 || ask <= 0 || ask < bid) return -1;
    return ask - bid;
  }

  double mean_spread() const {
    if (spreads.empty()) return 0.0;
    const long long total = std::accumulate(spreads.begin(), spreads.end(), 0LL);
    return static_cast<double>(total) / static_cast<double>(spreads.size());
  }

  MesaStepStats stats() const {
    const lob::Tick spread = current_spread();
    return MesaStepStats{now,
                         last_price(),
                         best_bid(),
                         best_ask(),
                         spread >= 0 ? spread : 0,
                         accepted_orders,
                         rejected_orders,
                         static_cast<int>(trades.size()),
                         static_cast<int>(agents.size()),
                         mean_spread()};
  }

  MesaAgentSimSummary summary() const {
    const lob::Tick bid = best_bid();
    const lob::Tick ask = best_ask();
    const double mid = bid > 0 && ask > 0 ? (static_cast<double>(bid) + static_cast<double>(ask)) / 2.0
                                          : static_cast<double>(last_price());
    return MesaAgentSimSummary{now,
                               static_cast<int>(agents.size()),
                               accepted_orders,
                               rejected_orders,
                               static_cast<int>(trades.size()),
                               bid,
                               ask,
                               mid,
                               mean_spread(),
                               agent_type_counts};
  }

  int rand_int(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(rng);
  }

  double rand_unit() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    return dist(rng);
  }

  MesaAgentSimConfig config;
  Exchange exchange;
  std::mt19937_64 rng;
  StepCandleAggregator candles;
  int now{0};
  OrderId next_order_id{1};
  int accepted_orders{0};
  int rejected_orders{0};
  std::vector<std::unique_ptr<Agent>> agents;
  std::vector<size_t> order;
  std::vector<std::pair<lob::Tick, lob::Quantity>> book_bids;
  std::vector<std::pair<lob::Tick, lob::Quantity>> book_asks;
  std::vector<TradeEvent> trades;
  std::vector<lob::Tick> trade_prices;
  std::vector<MesaOrderEvent> current_orders;
  std::vector<lob::Tick> spreads;
  std::map<std::string, int> agent_type_counts;
};

MesaAgentSimulation::MesaAgentSimulation(MesaAgentSimConfig config) : impl_(std::make_unique<Impl>(std::move(config))) {}
MesaAgentSimulation::~MesaAgentSimulation() = default;
MesaAgentSimulation::MesaAgentSimulation(MesaAgentSimulation&&) noexcept = default;
MesaAgentSimulation& MesaAgentSimulation::operator=(MesaAgentSimulation&&) noexcept = default;

MesaStepEvents MesaAgentSimulation::step() {
  return impl_->step();
}

MesaAgentSimSummary MesaAgentSimulation::run() {
  return impl_->run();
}

MesaAgentSimSummary MesaAgentSimulation::summary() const {
  return impl_->summary();
}

MesaStepStats MesaAgentSimulation::stats() const {
  return impl_->stats();
}

const std::vector<TradeEvent>& MesaAgentSimulation::trades() const {
  return impl_->trades;
}

bool MesaAgentSimulation::accounting_invariant_ok() const {
  return impl_->exchange.ledger().invariant_ok();
}

std::vector<std::pair<lob::Tick, lob::Quantity>> MesaAgentSimulation::bids(int levels) const {
  return impl_->exchange.topN(impl_->config.market_symbol, lob::Side::Bid, levels);
}

std::vector<std::pair<lob::Tick, lob::Quantity>> MesaAgentSimulation::asks(int levels) const {
  return impl_->exchange.topN(impl_->config.market_symbol, lob::Side::Ask, levels);
}

std::string mesa_agent_summary_json(const MesaAgentSimSummary& summary, bool pretty) {
  const std::string nl = pretty ? "\n" : "";
  const std::string ind1 = pretty ? "  " : "";
  const std::string ind2 = pretty ? "    " : "";
  const std::string sep = pretty ? ": " : ":";
  const std::string comma_nl = pretty ? ",\n" : ",";

  std::ostringstream os;
  os << "{" << nl
     << ind1 << "\"accepted_orders\"" << sep << summary.accepted_orders << comma_nl
     << ind1 << "\"agent_count\"" << sep << summary.agent_count << comma_nl
     << ind1 << "\"agent_types\"" << sep << "{" << nl;
  size_t i = 0;
  for (const auto& [type, count] : summary.agent_types) {
    os << ind2 << "\"" << escape_json(type) << "\"" << sep << count;
    if (++i < summary.agent_types.size()) os << comma_nl;
    else os << nl;
  }
  os << ind1 << "}" << comma_nl
     << ind1 << "\"final_best_ask\"" << sep << summary.final_best_ask << comma_nl
     << ind1 << "\"final_best_bid\"" << sep << summary.final_best_bid << comma_nl
     << ind1 << "\"final_mid_price\"" << sep << format_double(summary.final_mid_price) << comma_nl
     << ind1 << "\"mean_spread\"" << sep << format_double(summary.mean_spread) << comma_nl
     << ind1 << "\"rejected_orders\"" << sep << summary.rejected_orders << comma_nl
     << ind1 << "\"steps\"" << sep << summary.steps << comma_nl
     << ind1 << "\"trade_count\"" << sep << summary.trade_count << nl
     << "}";
  return os.str();
}

std::string mesa_step_stats_json(const MesaStepStats& stats) {
  std::ostringstream os;
  os << "{\"type\":\"stats\",\"step\":" << stats.step
     << ",\"last_price\":" << stats.last_price
     << ",\"best_bid\":" << stats.best_bid
     << ",\"best_ask\":" << stats.best_ask
     << ",\"spread\":" << stats.spread
     << ",\"accepted_orders\":" << stats.accepted_orders
     << ",\"rejected_orders\":" << stats.rejected_orders
     << ",\"trade_count\":" << stats.trade_count
     << ",\"agent_count\":" << stats.agent_count
     << ",\"mean_spread\":" << format_double(stats.mean_spread)
     << "}";
  return os.str();
}

std::string mesa_agent_mix_json(const MesaAgentSimSummary& summary) {
  std::ostringstream os;
  os << "{\"type\":\"agent_mix\",\"agents\":" << summary.agent_count << ",\"agent_types\":{";
  size_t i = 0;
  for (const auto& [type, count] : summary.agent_types) {
    if (i++ > 0) os << ',';
    os << "\"" << escape_json(type) << "\":" << count;
  }
  os << "}}";
  return os.str();
}

std::string mesa_trade_json(const TradeEvent& trade, int step) {
  std::ostringstream os;
  os << "{\"type\":\"trade\",\"step\":" << step
     << ",\"market_id\":" << trade.market_id
     << ",\"ts\":" << trade.ts
     << ",\"price\":" << trade.price
     << ",\"qty\":" << trade.qty
     << ",\"buyer\":" << trade.buyer
     << ",\"seller\":" << trade.seller
     << ",\"buyer_order_id\":" << trade.buyer_order_id
     << ",\"seller_order_id\":" << trade.seller_order_id
     << ",\"liquidity_side\":\"" << side_name(trade.liquidity_side) << "\"}";
  return os.str();
}

std::string mesa_step_candle_json(const MesaStepCandle& candle) {
  std::ostringstream os;
  os << "{\"type\":\"candle\",\"interval_steps\":" << candle.interval_steps
     << ",\"open_step\":" << candle.open_step
     << ",\"close_step\":" << candle.close_step
     << ",\"open\":" << candle.open
     << ",\"high\":" << candle.high
     << ",\"low\":" << candle.low
     << ",\"close\":" << candle.close
     << ",\"volume\":" << candle.volume
     << ",\"quote_volume\":" << candle.quote_volume
     << ",\"trade_count\":" << candle.trade_count
     << "}";
  return os.str();
}

} // namespace lobx::sim
