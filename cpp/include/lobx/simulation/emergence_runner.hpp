#pragma once

#include <string>

#include "lobx/simulation/agent_population.hpp"
#include "lobx/simulation/emergence_metrics.hpp"
#include "lobx/simulation/market_environment.hpp"

namespace lobx::sim {

struct EmergenceConfig {
  MarketEnvironmentConfig market_environment;
  AgentPopulationConfig agent_population;
};

struct EmergenceRunResult {
  EmergenceConfig config;
  ResearchRunResult research_result;
  EmergenceMetrics metrics;

  bool ok{false};
  std::string reason;

  bool ledger_invariant_ok{true};
  bool book_open_consistency_ok{true};
  bool no_private_data_leak{true};
  bool no_future_public_data_leak{true};
};

class EmergenceRunner {
public:
  EmergenceRunner();

  EmergenceRunResult run(const EmergenceConfig& config);
};

} // namespace lobx::sim
