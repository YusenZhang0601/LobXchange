#include "lobx/agents/builtins/grid_bot_agent.hpp"

namespace lobx::agents {

GridBotAgent::GridBotAgent(AgentId id, const AgentConfig& config)
    : id_(id), group_id_(config.group_id) {}

std::vector<AgentAction> GridBotAgent::decide(const AgentContext& /*ctx*/) {
  // Skeleton placeholder: returns empty actions (NOP)
  return {};
}

} // namespace lobx::agents
