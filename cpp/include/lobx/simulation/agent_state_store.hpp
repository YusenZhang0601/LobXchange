#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "lobx/agents/agent_context.hpp"
#include "lobx/agents/agent_types.hpp"

namespace lobx::simulation {

class AgentStateStore {
public:
  void initialize_agent(lobx::agents::AgentId agent_id);

  lobx::agents::PrivateAgentState get_private_state(lobx::agents::AgentId agent_id) const;
  std::vector<lobx::agents::AgentOrderView> open_orders(lobx::agents::AgentId agent_id) const;
  std::optional<lobx::agents::AgentOrderView> find_order(lobx::agents::AgentId agent_id,
                                                          lobx::agents::ClientOrderId client_order_id) const;

  void on_order_accepted(lobx::agents::AgentId agent_id,
                         lobx::agents::ClientOrderId client_order_id,
                         const std::string& symbol,
                         lobx::agents::Side side,
                         lobx::agents::Price price,
                         lobx::agents::Quantity remaining_quantity,
                         lobx::agents::Timestamp ts);
  void on_order_canceled(lobx::agents::AgentId agent_id,
                         lobx::agents::ClientOrderId client_order_id,
                         lobx::agents::Timestamp ts);
  void on_trade(lobx::agents::AgentId agent_id,
                lobx::agents::ClientOrderId client_order_id,
                const std::string& symbol,
                lobx::agents::Side side,
                lobx::agents::Price price,
                lobx::agents::Quantity quantity,
                lobx::agents::Timestamp ts);
  void on_order_rejected(lobx::agents::AgentId agent_id, lobx::agents::Timestamp ts);

private:
  struct MutableAgentState {
    double cash{0.0};
    double realized_pnl{0.0};
    double unrealized_pnl{0.0};
    lobx::agents::Quantity inventory{0};
    lobx::agents::Quantity max_inventory{0};
    double risk_budget_remaining{0.0};
    std::vector<lobx::agents::AgentOrderView> open_orders;
    std::vector<lobx::agents::AgentFillView> recent_fills;
    lobx::agents::Timestamp last_fill_ts{0};
    lobx::agents::Timestamp last_action_ts{0};
  };

  const MutableAgentState* find_state(lobx::agents::AgentId agent_id) const;
  MutableAgentState& state_for(lobx::agents::AgentId agent_id);

  std::unordered_map<lobx::agents::AgentId, MutableAgentState> states_;
};

} // namespace lobx::simulation
