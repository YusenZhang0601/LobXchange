#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "lobx/account_ledger.hpp"
#include "lobx/simulation/strategy_metrics.hpp"
#include "lobx/types.hpp"

namespace lobx::sim {

struct LatencyConfig {
  lob::Timestamp order_latency{1};
  lob::Timestamp cancel_latency{1};
  lob::Timestamp market_data_latency{1};
  lob::Timestamp private_data_latency{1};
};

struct BotConfig {
  UserId user{0};
  std::string name;
  std::string strategy_type;
  LatencyConfig latency;
  std::map<std::string, double> params;
};

struct ScenarioConfig {
  uint64_t seed{0};
  int ticks{0};
  std::string market_symbol{"BTC-USDT"};
  std::vector<BotConfig> bots;
};

struct ValidationResult {
  bool ok{false};
  std::string reason;
};

struct ResearchRunResult {
  ScenarioConfig config;

  std::vector<std::string> action_trace;
  std::vector<TradeEvent> trades;
  std::vector<std::string> event_types;

  std::vector<WalletBalance> balances;

  std::vector<std::pair<lob::Tick, lob::Quantity>> bids;
  std::vector<std::pair<lob::Tick, lob::Quantity>> asks;

  std::map<UserId, StrategyMetrics> metrics;

  bool ledger_invariant_ok{true};
  bool book_open_consistency_ok{true};
  bool no_private_data_leak{true};
  bool no_future_public_data_leak{true};
};

ValidationResult validate_scenario_config(const ScenarioConfig& config);
bool same_research_result(const ResearchRunResult& a, const ResearchRunResult& b);

} // namespace lobx::sim
