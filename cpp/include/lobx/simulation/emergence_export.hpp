#pragma once

#include <string>

#include "lobx/simulation/emergence_metrics.hpp"
#include "lobx/simulation/file_export.hpp"

namespace lobx::sim {

FileWriteResult write_emergence_bundle(const std::string& output_dir,
                                       const EmergenceMetrics& metrics);

} // namespace lobx::sim
