#include "lobx/simulation/file_export.hpp"

#include <filesystem>
#include <fstream>
#include <system_error>

namespace lobx::sim {

FileWriteResult write_text_file(const std::string& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  if (!out.is_open()) return FileWriteResult{false, "failed to open file for writing: " + path};
  out << content;
  if (!out.good()) return FileWriteResult{false, "failed to write file: " + path};
  return FileWriteResult{true, {}};
}

FileWriteResult write_ranked_results_csv(const std::string& path,
                                         const std::vector<RankedStrategyResult>& ranked) {
  return write_text_file(path, export_ranked_results_csv(ranked));
}

FileWriteResult write_aggregated_stats_csv(const std::string& path,
                                           const std::vector<AggregatedStrategyStats>& stats) {
  return write_text_file(path, export_aggregated_stats_csv(stats));
}

FileWriteResult write_run_summary_json(const std::string& path, const ResearchRunResult& result) {
  return write_text_file(path, export_run_summary_json(result));
}

FileWriteResult write_research_bundle(const std::string& output_dir,
                                      const std::vector<RankedStrategyResult>& ranked,
                                      const std::vector<AggregatedStrategyStats>& stats,
                                      const ResearchRunResult& summary) {
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) return FileWriteResult{false, "failed to create output directory: " + ec.message()};
  if (!std::filesystem::is_directory(output_dir, ec)) {
    return FileWriteResult{false, "output path is not a directory: " + output_dir};
  }

  const std::filesystem::path dir(output_dir);
  FileWriteResult ranking = write_ranked_results_csv((dir / "ranking.csv").string(), ranked);
  if (!ranking.ok) return ranking;
  FileWriteResult aggregate = write_aggregated_stats_csv((dir / "aggregate.csv").string(), stats);
  if (!aggregate.ok) return aggregate;
  return write_run_summary_json((dir / "summary.json").string(), summary);
}

} // namespace lobx::sim
