#pragma once

#include <string>
#include <vector>

#include "lobx/simulation/multi_seed_evaluation.hpp"
#include "lobx/simulation/parameter_sweep.hpp"
#include "lobx/simulation/scenario_config.hpp"
#include "lobx/simulation/emergence_runner.hpp"

namespace lobx::sim {

struct ConfigLoadResult {
  bool ok{false};
  std::string reason;
};

struct ScenarioConfigLoadResult {
  bool ok{false};
  std::string reason;
  ScenarioConfig config;
};

struct SweepConfigLoadResult {
  bool ok{false};
  std::string reason;
  std::vector<SweepParam> params;
};

struct MultiSeedConfigLoadResult {
  bool ok{false};
  std::string reason;
  std::vector<uint64_t> seeds;
};

struct MarketEnvironmentConfigLoadResult {
  bool ok{false};
  std::string reason;
  MarketEnvironmentConfig config;
};

struct AgentPopulationConfigLoadResult {
  bool ok{false};
  std::string reason;
  AgentPopulationConfig config;
};

struct EmergenceConfigLoadResult {
  bool ok{false};
  std::string reason;
  EmergenceConfig config;
};

ScenarioConfigLoadResult load_scenario_config_from_json_string(const std::string& json);
ScenarioConfigLoadResult load_scenario_config_from_json_file(const std::string& path);

SweepConfigLoadResult load_sweep_config_from_json_string(const std::string& json);
SweepConfigLoadResult load_sweep_config_from_json_file(const std::string& path);

MultiSeedConfigLoadResult load_seed_config_from_json_string(const std::string& json);
MultiSeedConfigLoadResult load_seed_config_from_json_file(const std::string& path);

MarketEnvironmentConfigLoadResult load_market_environment_from_json_string(const std::string& json);
MarketEnvironmentConfigLoadResult load_market_environment_from_json_file(const std::string& path);

AgentPopulationConfigLoadResult load_agent_population_from_json_string(const std::string& json);
AgentPopulationConfigLoadResult load_agent_population_from_json_file(const std::string& path);

EmergenceConfigLoadResult load_emergence_config_from_json_string(const std::string& json);
EmergenceConfigLoadResult load_emergence_config_from_json_file(const std::string& path);

std::string scenario_config_to_json(const ScenarioConfig& config);
std::string sweep_config_to_json(const std::vector<SweepParam>& params);
std::string seed_config_to_json(const std::vector<uint64_t>& seeds);
std::string market_environment_to_json(const MarketEnvironmentConfig& config);
std::string agent_population_to_json(const AgentPopulationConfig& config);
std::string emergence_config_to_json(const EmergenceConfig& config);

} // namespace lobx::sim
