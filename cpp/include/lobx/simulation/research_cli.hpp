#pragma once

#include <string>
#include <vector>

#include "lobx/simulation/strategy_ranking.hpp"

namespace lobx::sim {

struct ResearchCliOptions {
  std::string scenario_path;
  std::string sweep_path;
  std::string seeds_path;
  std::string rank_bot;
  std::string metric;
  std::string output_dir;
  int top_n{0};
  bool verbose{false};
  bool help{false};
};

struct ResearchCliResult {
  int exit_code{1};
  std::string stdout_text;
  std::string stderr_text;
};

bool parse_ranking_metric(const std::string& s, RankingMetric* out);

ResearchCliResult parse_research_cli_args(const std::vector<std::string>& args,
                                          ResearchCliOptions* out);

ResearchCliResult run_research_cli(const std::vector<std::string>& args);

std::string research_cli_usage();

} // namespace lobx::sim
