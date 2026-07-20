#pragma once

#include "test_helpers/scenario_config.hpp"
#include "test_helpers/strategy_metrics.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace lobx_test {

struct SeedEvaluationRun {
  uint64_t seed{0};
  ScenarioConfig config;
  BotRunResult result;
};

struct AggregatedStrategyStats {
  std::string bot_name;
  lobx::UserId user{0};

  int runs{0};
  long double mean_net_pnl{0.0L};
  long double min_net_pnl{0.0L};
  long double max_net_pnl{0.0L};
  long double median_net_pnl{0.0L};
  long double win_rate{0.0L};
};

inline std::vector<SeedEvaluationRun> run_multi_seed_evaluation(ScenarioConfig base,
                                                                const std::vector<uint64_t>& seeds) {
  std::vector<SeedEvaluationRun> out;
  for (uint64_t seed : seeds) {
    ScenarioConfig config = base;
    config.seed = seed;
    ScenarioBuildResult run = run_scenario_config(config);
    if (!run.ok) return {};
    out.push_back(SeedEvaluationRun{seed, config, run.result});
  }
  return out;
}

inline AggregatedStrategyStats aggregate_strategy_stats(const std::vector<SeedEvaluationRun>& runs,
                                                        const std::string& bot_name) {
  AggregatedStrategyStats stats{};
  stats.bot_name = bot_name;
  std::vector<long double> values;

  for (const SeedEvaluationRun& run : runs) {
    const BotConfig* bot = find_bot_config_by_name(run.config, bot_name);
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
  if ((values.size() % 2) == 0) {
    stats.median_net_pnl = (values[mid - 1] + values[mid]) / 2.0L;
  } else {
    stats.median_net_pnl = values[mid];
  }
  stats.win_rate = static_cast<long double>(wins) / static_cast<long double>(values.size());
  return stats;
}

} // namespace lobx_test
