#include "lobx/simulation/emergence_export.hpp"

#include <filesystem>
#include <system_error>

namespace lobx::sim {

FileWriteResult write_emergence_bundle(const std::string& output_dir,
                                       const EmergenceMetrics& metrics) {
  std::error_code ec;
  std::filesystem::create_directories(output_dir, ec);
  if (ec) return FileWriteResult{false, "failed to create output directory: " + ec.message()};
  if (!std::filesystem::is_directory(output_dir, ec)) {
    return FileWriteResult{false, "output path is not a directory: " + output_dir};
  }

  const std::filesystem::path dir(output_dir);
  FileWriteResult summary =
      write_text_file((dir / "emergence_summary.json").string(), export_emergence_summary_json(metrics));
  if (!summary.ok) return summary;
  FileWriteResult price =
      write_text_file((dir / "price_series.csv").string(), export_price_series_csv(metrics));
  if (!price.ok) return price;
  FileWriteResult spread =
      write_text_file((dir / "spread_series.csv").string(), export_spread_series_csv(metrics));
  if (!spread.ok) return spread;
  FileWriteResult depth =
      write_text_file((dir / "depth_series.csv").string(), export_depth_series_csv(metrics));
  if (!depth.ok) return depth;
  return write_text_file((dir / "agent_metrics.csv").string(), export_agent_metrics_csv(metrics));
}

} // namespace lobx::sim
