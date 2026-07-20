#pragma once

#include <span>
#include <string>

#include "lobx/agents/agent_types.hpp"
#include "lobx/simulation/market_view.hpp"

namespace lobx::agents {

struct AgentOrderView {
  ClientOrderId client_order_id{0};
  std::string symbol;
  Side side{Side::Bid};
  Price price{0};
  Quantity remaining_quantity{0};
  Timestamp submitted_ts{0};
};

struct AgentFillView {
  std::string symbol;
  Side side{Side::Bid};
  Price price{0};
  Quantity quantity{0};
  Timestamp ts{0};
};

struct PrivateAgentState {
  AgentId agent_id{0};
  double cash{0.0};
  double realized_pnl{0.0};
  double unrealized_pnl{0.0};
  Quantity inventory{0};
  Quantity max_inventory{0};
  double risk_budget_remaining{0.0};
  std::span<const AgentOrderView> open_orders;
  std::span<const AgentFillView> recent_fills;
  Timestamp last_fill_ts{0};
  Timestamp last_action_ts{0};
};

struct EnvironmentView {
  double noise_intensity{0.0};
  double liquidity_scale{1.0};
  double volatility_regime{1.0};
  double spread_regime{1.0};
};

struct AgentConfigView {};

struct AgentInitContext {
  Timestamp start_ts{0};
  std::string symbol;
  AgentGroupId group_id{0};
};

struct AgentEvent {
  Timestamp ts{0};
  AgentId agent_id{0};
  std::string event_type;
};

struct AgentContext {
  Timestamp now{0};
  const lobx::simulation::MarketView& market_view;
  const PrivateAgentState& private_state;
  const EnvironmentView& environment;
  AgentConfigView config;
};

} // namespace lobx::agents
