#pragma once

#include "lobx/agents/agent.hpp"
#include "lobx/agents/agent_factory.hpp"

namespace lobx::agents {

class MeanReverterAgent final : public IAgent {
public:
  MeanReverterAgent(AgentId id, const AgentConfig& config);

  AgentId id() const override { return id_; }
  AgentType type() const override { return AgentType::MeanReverter; }
  std::vector<AgentAction> decide(const AgentContext& ctx) override;

private:
  AgentAction make_action(const AgentContext& ctx, SubmitLimitOrder payload);

  AgentId id_{0};
  AgentGroupId group_id_{0};
  Price reference_price_{100};
  ClientActionId next_action_id_{1};
  ClientOrderId next_order_id_{1};
};

} // namespace lobx::agents
