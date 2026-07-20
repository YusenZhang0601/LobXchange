#include "lobx/simulation/emergence_runner.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace lobx::sim {

namespace {

constexpr UserId kReservedFeeAccount = std::numeric_limits<UserId>::max();

BotConfig initial_book_bot(UserId user, OrderId order_id, const InitialBookLevel& level) {
  const int side = level.side == lob::Side::Ask ? 1 : 0;
  return BotConfig{user,
                   "initial_book_" + std::to_string(order_id),
                   "user_script",
                   LatencyConfig{0, 0, 0, 0},
                   {{"side", static_cast<double>(side)},
                    {"price", static_cast<double>(level.price)},
                    {"qty", static_cast<double>(level.qty)},
                    {"flags", static_cast<double>(lob::POST_ONLY)},
                    {"decision_ts", 1.0},
                    {"order_id", static_cast<double>(order_id)}}};
}

std::vector<StrategyMetrics> sorted_metrics(const std::map<UserId, StrategyMetrics>& metrics) {
  std::vector<StrategyMetrics> out;
  for (const auto& [_, value] : metrics) out.push_back(value);
  std::sort(out.begin(), out.end(), [](const StrategyMetrics& a, const StrategyMetrics& b) {
    if (a.user != b.user) return a.user < b.user;
    return a.bot_name < b.bot_name;
  });
  return out;
}

lob::Tick fallback_mid(const MarketEnvironmentConfig& env, const ResearchRunResult& result, int tick) {
  lob::Tick last = 0;
  for (const TradeEvent& trade : result.trades) {
    if (trade.ts <= tick) last = trade.price;
  }
  return last > 0 ? last : env.reference_price;
}

MarketTickSample sample_for_tick(const MarketEnvironmentConfig& env, const ResearchRunResult& result, int tick) {
  const lob::Tick best_bid = result.bids.empty() ? 0 : result.bids.front().first;
  const lob::Tick best_ask = result.asks.empty() ? 0 : result.asks.front().first;
  lob::Tick mid = 0;
  if (best_bid > 0 && best_ask > 0) {
    mid = static_cast<lob::Tick>((best_bid + best_ask) / 2);
  } else {
    mid = fallback_mid(env, result, tick);
  }

  lob::Quantity trade_volume = 0;
  Amount quote_volume = 0;
  for (const TradeEvent& trade : result.trades) {
    if (trade.ts != tick) continue;
    trade_volume += trade.qty;
    Amount notional = 0;
    if (mul_amount(trade.price, trade.qty, notional)) quote_volume += notional;
  }

  auto sum_depth = [](const std::vector<std::pair<lob::Tick, lob::Quantity>>& depth, size_t n) {
    lob::Quantity out = 0;
    for (size_t i = 0; i < depth.size() && i < n; ++i) out += depth[i].second;
    return out;
  };

  return MarketTickSample{tick,
                          mid,
                          best_bid,
                          best_ask,
                          result.bids.empty() ? 0 : result.bids.front().second,
                          result.asks.empty() ? 0 : result.asks.front().second,
                          sum_depth(result.bids, 5),
                          sum_depth(result.asks, 5),
                          trade_volume,
                          quote_volume};
}

} // namespace

EmergenceRunner::EmergenceRunner() = default;

EmergenceRunResult EmergenceRunner::run(const EmergenceConfig& config) {
  EmergenceRunResult out{};
  out.config = config;

  const MarketEnvironmentValidation market_validation =
      validate_market_environment(config.market_environment);
  if (!market_validation.ok) {
    out.reason = market_validation.reason;
    return out;
  }
  const AgentPopulationValidation population_validation =
      validate_agent_population(config.agent_population);
  if (!population_validation.ok) {
    out.reason = population_validation.reason;
    return out;
  }

  std::vector<BotConfig> bots = generate_agent_population(config.agent_population);
  UserId seed_user = config.agent_population.first_user_id + static_cast<UserId>(bots.size());
  OrderId seed_order_id = 900000000;
  for (const InitialBookLevel& level : config.market_environment.initial_book) {
    if (seed_user == kReservedFeeAccount) {
      out.reason = "initial book user would overlap reserved fee account";
      return out;
    }
    bots.push_back(initial_book_bot(seed_user++, seed_order_id++, level));
  }

  ScenarioConfig scenario{};
  scenario.seed = config.agent_population.seed;
  scenario.ticks = config.market_environment.ticks;
  scenario.market_symbol = config.market_environment.market_symbol;
  scenario.bots = std::move(bots);

  ResearchRunner runner;
  out.research_result = runner.run_scenario(scenario);
  out.ledger_invariant_ok = out.research_result.ledger_invariant_ok;
  out.book_open_consistency_ok = out.research_result.book_open_consistency_ok;
  out.no_private_data_leak = out.research_result.no_private_data_leak;
  out.no_future_public_data_leak = out.research_result.no_future_public_data_leak;
  if (!out.research_result.action_trace.empty() &&
      out.research_result.action_trace.front().find("validation_failed:") == 0) {
    out.reason = out.research_result.action_trace.front();
    return out;
  }

  std::vector<MarketTickSample> samples;
  samples.reserve(static_cast<size_t>(config.market_environment.ticks));
  for (int tick = 1; tick <= config.market_environment.ticks; ++tick) {
    samples.push_back(sample_for_tick(config.market_environment, out.research_result, tick));
  }
  out.metrics = summarize_market_samples(config.market_environment.warmup_ticks,
                                         samples,
                                         sorted_metrics(out.research_result.metrics));

  out.ok = out.ledger_invariant_ok &&
           out.book_open_consistency_ok &&
           out.no_private_data_leak &&
           out.no_future_public_data_leak;
  if (!out.ok) out.reason = "emergence run violated research invariants";
  return out;
}

} // namespace lobx::sim
