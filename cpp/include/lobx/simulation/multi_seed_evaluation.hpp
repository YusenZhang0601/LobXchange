#pragma once

#include <string>
#include <vector>

#include "lobx/simulation/scenario_config.hpp"

namespace lobx::sim {

class ResearchRunner;

struct SeedEvaluationRun {
  uint64_t seed{0};
  ResearchRunResult result;
};

struct AggregatedStrategyStats {
  std::string bot_name;
  UserId user{0};

  int runs{0};
  long double mean_net_pnl{0.0L};
  long double median_net_pnl{0.0L};
  long double min_net_pnl{0.0L};
  long double max_net_pnl{0.0L};
  long double win_rate{0.0L};
};

std::vector<SeedEvaluationRun> run_multi_seed_evaluation(ResearchRunner& runner,
                                                         ScenarioConfig base,
                                                         const std::vector<uint64_t>& seeds);

AggregatedStrategyStats aggregate_strategy_stats(const std::vector<SeedEvaluationRun>& runs,
                                                 const std::string& bot_name);

} // namespace lobx::sim
