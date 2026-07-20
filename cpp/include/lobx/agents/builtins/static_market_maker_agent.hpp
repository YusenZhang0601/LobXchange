#pragma once

#include <random>
#include <string>
#include <vector>

#include "lobx/agents/agent.hpp"
#include "lobx/agents/agent_factory.hpp"

namespace lobx::agents {

class StaticMarketMakerAgent final : public IAgent {
public:
  StaticMarketMakerAgent(AgentId id, const AgentConfig& config);

  AgentId id() const override { return id_; }
  AgentType type() const override { return AgentType::StaticMarketMaker; }
  std::vector<AgentAction> decide(const AgentContext& ctx) override;

private:
  AgentAction make_limit_action(const AgentContext& ctx, SubmitLimitOrder payload, std::string reason_tag);
  AgentAction make_cancel_action(const AgentContext& ctx, CancelOrder payload, std::string reason_tag);

  AgentId id_{0};
  AgentGroupId group_id_{0};
  std::mt19937_64 rng_;
  ClientActionId next_action_id_{1};
  ClientOrderId next_order_id_{1};
  bool bounded_quotes_{false};
  int max_open_orders_per_side_{1};
  Timestamp quote_refresh_interval_steps_{10};
  Timestamp quote_ttl_steps_{20};
  bool cancel_stale_quotes_{true};
  bool replace_on_price_change_{true};
};

} // namespace lobx::agents
