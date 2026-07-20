#pragma once

#include <string>
#include <vector>

#include "lobx/simulation/scenario_config.hpp"

namespace lobx::sim {

class ResearchRunner;

struct SweepParam {
  std::string bot_name;
  std::string param_name;
  std::vector<double> values;
};

struct SweepRun {
  ScenarioConfig config;
  ResearchRunResult result;
};

std::vector<ScenarioConfig> expand_parameter_sweep(const ScenarioConfig& base,
                                                   const std::vector<SweepParam>& params);

std::vector<SweepRun> run_parameter_sweep(ResearchRunner& runner,
                                          const ScenarioConfig& base,
                                          const std::vector<SweepParam>& params);

} // namespace lobx::sim
