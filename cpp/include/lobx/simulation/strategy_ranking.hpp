#pragma once

#include <map>
#include <string>
#include <vector>

#include "lobx/simulation/parameter_sweep.hpp"
#include "lobx/simulation/strategy_metrics.hpp"

namespace lobx::sim {

enum class RankingMetric {
  NetPnl,
  GrossPnl,
  FeesPaidInverse
};

struct RankedStrategyResult {
  int rank{0};
  std::string bot_name;
  UserId user{0};
  std::map<std::string, double> params;
  long double score{0.0L};
  StrategyMetrics metrics;
};

std::vector<RankedStrategyResult> rank_sweep_results(const std::vector<SweepRun>& runs,
                                                     const std::string& bot_name,
                                                     RankingMetric metric);

} // namespace lobx::sim
