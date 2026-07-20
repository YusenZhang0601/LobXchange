#include "lobx/agents/builtins/hawkes_panic_agent.hpp"

namespace lobx::agents {

HawkesPanicAgent::HawkesPanicAgent(AgentId id, const AgentConfig& config)
    : id_(id), group_id_(config.group_id) {}

std::vector<AgentAction> HawkesPanicAgent::decide(const AgentContext& /*ctx*/) {
  // Skeleton placeholder: returns empty actions (NOP)
  return {};
}

} // namespace lobx::agents
