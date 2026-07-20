#pragma once

#include <map>
#include <string>
#include <vector>

#include "lobx/simulation/market_environment.hpp"
#include "lobx/simulation/scenario_config.hpp"

namespace lobx::sim {

struct DoubleRange {
  double min{0.0};
  double max{0.0};
};

struct IntegerRange {
  int min{0};
  int max{0};
};

struct LatencyRangeConfig {
  int order_min{1};
  int order_max{1};
  int cancel_min{1};
  int cancel_max{1};
  int market_data_min{1};
  int market_data_max{1};
  int private_data_min{1};
  int private_data_max{1};
};

struct AgentGroupConfig {
  std::string strategy_type;
  int count{0};
  std::string name_prefix;

  std::map<std::string, DoubleRange> param_ranges;
  LatencyRangeConfig latency_range;
};

struct AgentPopulationConfig {
  uint64_t seed{0};
  UserId first_user_id{1};
  std::vector<AgentGroupConfig> groups;
};

struct AgentPopulationValidation {
  bool ok{false};
  std::string reason;
};

AgentPopulationValidation validate_agent_population(const AgentPopulationConfig& config);

std::vector<BotConfig> generate_agent_population(const AgentPopulationConfig& config);

} // namespace lobx::sim
