#include "lobx/simulation/multi_seed_evaluation.hpp"

#include "lobx/simulation/research_runner.hpp"

#include <algorithm>
#include <utility>

namespace lobx::sim {

namespace {

const BotConfig* find_bot(const ScenarioConfig& config, const std::string& name) {
  for (const BotConfig& bot : config.bots) {
    if (bot.name == name) return &bot;
  }
  return nullptr;
}

} // namespace

std::vector<SeedEvaluationRun> run_multi_seed_evaluation(ResearchRunner& runner,
                                                         ScenarioConfig base,
                                                         const std::vector<uint64_t>& seeds) {
  std::vector<SeedEvaluationRun> out;
  for (uint64_t seed : seeds) {
    ScenarioConfig config = base;
    config.seed = seed;
    ResearchRunResult result = runner.run_scenario(config);
    if (!result.ledger_invariant_ok) return {};
    out.push_back(SeedEvaluationRun{seed, std::move(result)});
  }
  return out;
}

AggregatedStrategyStats aggregate_strategy_stats(const std::vector<SeedEvaluationRun>& runs,
                                                 const std::string& bot_name) {
  AggregatedStrategyStats stats{};
  stats.bot_name = bot_name;
  std::vector<long double> values;

  for (const SeedEvaluationRun& run : runs) {
    const BotConfig* bot = find_bot(run.result.config, bot_name);
    if (bot == nullptr) return AggregatedStrategyStats{bot_name};
    stats.user = bot->user;
    const auto metrics_it = run.result.metrics.find(bot->user);
    if (metrics_it == run.result.metrics.end()) return AggregatedStrategyStats{bot_name, bot->user};
    values.push_back(metrics_it->second.net_pnl);
  }

  if (values.empty()) return stats;
  std::sort(values.begin(), values.end());
  stats.runs = static_cast<int>(values.size());
  stats.min_net_pnl = values.front();
  stats.max_net_pnl = values.back();
  long double sum = 0.0L;
  int wins = 0;
  for (long double value : values) {
    sum += value;
    if (value > 0.0L) ++wins;
  }
  stats.mean_net_pnl = sum / static_cast<long double>(values.size());
  const size_t mid = values.size() / 2;
  stats.median_net_pnl = (values.size() % 2) == 0
                             ? (values[mid - 1] + values[mid]) / 2.0L
                             : values[mid];
  stats.win_rate = static_cast<long double>(wins) / static_cast<long double>(values.size());
  return stats;
}

} // namespace lobx::sim
