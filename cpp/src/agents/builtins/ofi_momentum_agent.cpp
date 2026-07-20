#include "lobx/agents/builtins/ofi_momentum_agent.hpp"

namespace lobx::agents {

OfiMomentumAgent::OfiMomentumAgent(AgentId id, const AgentConfig& config)
    : id_(id), group_id_(config.group_id) {}

std::vector<AgentAction> OfiMomentumAgent::decide(const AgentContext& /*ctx*/) {
  // Skeleton placeholder: returns empty actions (NOP)
  return {};
}

} // namespace lobx::agents
