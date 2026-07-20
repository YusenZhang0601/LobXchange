#pragma once

#include <string>
#include <vector>

#include "lobx/simulation/multi_seed_evaluation.hpp"
#include "lobx/simulation/research_runner.hpp"
#include "lobx/simulation/strategy_ranking.hpp"

namespace lobx::sim {

struct FileWriteResult {
  bool ok{false};
  std::string reason;
};

FileWriteResult write_text_file(const std::string& path, const std::string& content);

FileWriteResult write_ranked_results_csv(const std::string& path,
                                         const std::vector<RankedStrategyResult>& ranked);

FileWriteResult write_aggregated_stats_csv(const std::string& path,
                                           const std::vector<AggregatedStrategyStats>& stats);

FileWriteResult write_run_summary_json(const std::string& path, const ResearchRunResult& result);

FileWriteResult write_research_bundle(const std::string& output_dir,
                                      const std::vector<RankedStrategyResult>& ranked,
                                      const std::vector<AggregatedStrategyStats>& stats,
                                      const ResearchRunResult& summary);

} // namespace lobx::sim
