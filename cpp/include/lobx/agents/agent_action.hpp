#pragma once

#include <optional>
#include <string>
#include <variant>

#include "lobx/agents/agent_types.hpp"

namespace lobx::agents {

struct SubmitLimitOrder {
  ClientOrderId client_order_id{0};
  std::string symbol;
  Side side{Side::Bid};
  Price price{0};
  Quantity quantity{0};
  TimeInForce tif{TimeInForce::Gtc};
  bool post_only{false};
};

struct SubmitMarketOrder {
  ClientOrderId client_order_id{0};
  std::string symbol;
  Side side{Side::Bid};
  Quantity quantity{0};
};

struct CancelOrder {
  ClientOrderId client_order_id{0};
};

struct ReplaceOrder {
  ClientOrderId old_client_order_id{0};
  Price new_price{0};
  Quantity new_quantity{0};
};

struct CancelAllOrders {
  std::string symbol;
  std::optional<Side> side;
};

struct SleepUntil {
  Timestamp next_decision_ts{0};
};

using AgentActionPayload = std::variant<
    SubmitLimitOrder,
    SubmitMarketOrder,
    CancelOrder,
    ReplaceOrder,
    CancelAllOrders,
    SleepUntil>;

struct AgentAction {
  AgentId agent_id{0};
  AgentType agent_type{AgentType::Unknown};
  AgentGroupId group_id{0};
  Timestamp decision_ts{0};
  ClientActionId client_action_id{0};
  AgentActionPayload payload{SleepUntil{}};
  std::string reason_tag;
};

const char* action_payload_name(const AgentActionPayload& payload);

} // namespace lobx::agents
