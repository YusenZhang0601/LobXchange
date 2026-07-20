#pragma once

#include <string>
#include <vector>

#include "lobx/simulation/multi_seed_evaluation.hpp"
#include "lobx/simulation/parameter_sweep.hpp"
#include "lobx/simulation/scenario_config.hpp"
#include "lobx/simulation/strategy_ranking.hpp"

namespace lobx::sim {

class ResearchRunner {
public:
  ResearchRunner();

  ResearchRunResult run_scenario(const ScenarioConfig& config);

  std::vector<ResearchRunResult> run_parameter_sweep(const ScenarioConfig& base,
                                                     const std::vector<SweepParam>& params);

  std::vector<SeedEvaluationRun> run_multi_seed(ScenarioConfig base,
                                                const std::vector<uint64_t>& seeds);
};

std::string export_ranked_results_csv(const std::vector<RankedStrategyResult>& ranked);
std::string export_aggregated_stats_csv(const std::vector<AggregatedStrategyStats>& stats);
std::string export_run_summary_json(const ResearchRunResult& result);

} // namespace lobx::sim
