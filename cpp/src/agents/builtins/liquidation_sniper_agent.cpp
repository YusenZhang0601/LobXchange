#include "lobx/agents/builtins/liquidation_sniper_agent.hpp"

namespace lobx::agents {

LiquidationSniperAgent::LiquidationSniperAgent(AgentId id, const AgentConfig& config)
    : id_(id), group_id_(config.group_id) {}

std::vector<AgentAction> LiquidationSniperAgent::decide(const AgentContext& /*ctx*/) {
  // Skeleton placeholder: returns empty actions (NOP)
  return {};
}

} // namespace lobx::agents
