#pragma once

#include "lobx/agents/agent.hpp"
#include "lobx/agents/agent_factory.hpp"

namespace lobx::agents {

class GridBotAgent final : public IAgent {
public:
  GridBotAgent(AgentId id, const AgentConfig& config);

  AgentId id() const override { return id_; }
  AgentType type() const override { return AgentType::GridBot; }
  std::vector<AgentAction> decide(const AgentContext& ctx) override;

private:
  AgentId id_{0};
  AgentGroupId group_id_{0};
  ClientActionId next_action_id_{1};
  ClientOrderId next_order_id_{1};
};

} // namespace lobx::agents
