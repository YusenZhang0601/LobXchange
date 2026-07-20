#pragma once

#include <random>

#include "lobx/agents/agent.hpp"
#include "lobx/agents/agent_factory.hpp"

namespace lobx::agents {

class NoiseTraderAgent final : public IAgent {
public:
  NoiseTraderAgent(AgentId id, const AgentConfig& config);

  AgentId id() const override { return id_; }
  AgentType type() const override { return AgentType::NoiseTrader; }
  std::vector<AgentAction> decide(const AgentContext& ctx) override;

private:
  AgentAction make_action(const AgentContext& ctx, SubmitLimitOrder payload);

  AgentId id_{0};
  AgentGroupId group_id_{0};
  std::mt19937_64 rng_;
  ClientActionId next_action_id_{1};
  ClientOrderId next_order_id_{1};
};

} // namespace lobx::agents
