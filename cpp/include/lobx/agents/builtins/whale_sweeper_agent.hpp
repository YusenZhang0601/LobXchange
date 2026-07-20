#pragma once

#include <random>

#include "lobx/agents/agent.hpp"
#include "lobx/agents/agent_factory.hpp"

namespace lobx::agents {

class WhaleSweeperAgent final : public IAgent {
public:
  WhaleSweeperAgent(AgentId id, const AgentConfig& config);

  AgentId id() const override { return id_; }
  AgentType type() const override { return AgentType::WhaleSweeper; }
  std::vector<AgentAction> decide(const AgentContext& ctx) override;

private:
  AgentAction make_action(const AgentContext& ctx, SubmitMarketOrder payload);

  AgentId id_{0};
  AgentGroupId group_id_{0};
  std::mt19937_64 rng_;
  int interval_{12};
  Quantity quantity_{20};
  ClientActionId next_action_id_{1};
  ClientOrderId next_order_id_{1};
};

} // namespace lobx::agents
