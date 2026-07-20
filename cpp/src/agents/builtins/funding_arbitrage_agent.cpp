#include "lobx/agents/builtins/funding_arbitrage_agent.hpp"

namespace lobx::agents {

FundingArbitrageAgent::FundingArbitrageAgent(AgentId id, const AgentConfig& config)
    : id_(id), group_id_(config.group_id) {}

std::vector<AgentAction> FundingArbitrageAgent::decide(const AgentContext& /*ctx*/) {
  // Skeleton placeholder: returns empty actions (NOP)
  return {};
}

} // namespace lobx::agents
