#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <variant>

#include "lobx/agents/agent_types.hpp"

namespace lobx::simulation {

enum class EventSource : std::uint8_t {
  Agent = 0,
  Runtime = 1,
  Exchange = 2,
  Recorder = 3,
  Metrics = 4,
};

struct AgentDecisionEvent {
  std::size_t action_count{0};
};

struct ActionScheduledEvent {
  lobx::agents::Timestamp decision_ts{0};
  lobx::agents::Timestamp arrival_ts{0};
  std::string reason_tag;
};

struct OrderSubmittedEvent {
  std::string symbol;
  std::string side;
  double price{0.0};
  double quantity{0.0};
};

struct OrderAcceptedEvent {
  std::uint64_t order_id{0};
};

struct OrderRejectedEvent {
  std::string reason;
};

struct OrderCanceledEvent {
  std::uint64_t order_id{0};
};

struct TradeEvent {
  std::string symbol;
  double price{0.0};
  double quantity{0.0};
  lobx::agents::AgentId aggressor_agent_id{0};
  std::optional<lobx::agents::AgentId> passive_agent_id;
};

struct BookSnapshotEvent {
  std::string symbol;
  double best_bid{0.0};
  double best_ask{0.0};
  double mid_price{0.0};
  double spread_bps{0.0};
};

using SimulationEventPayload = std::variant<
    AgentDecisionEvent,
    ActionScheduledEvent,
    OrderSubmittedEvent,
    OrderAcceptedEvent,
    OrderRejectedEvent,
    OrderCanceledEvent,
    TradeEvent,
    BookSnapshotEvent>;

struct SimulationEvent {
  lobx::agents::Timestamp ts{0};
  std::uint64_t seq{0};
  EventSource source{EventSource::Runtime};
  std::optional<lobx::agents::AgentId> agent_id;
  std::optional<lobx::agents::AgentType> agent_type;
  std::optional<lobx::agents::AgentGroupId> group_id;
  SimulationEventPayload payload{AgentDecisionEvent{}};
};

const char* event_source_name(EventSource source);
const char* simulation_event_payload_name(const SimulationEventPayload& payload);

} // namespace lobx::simulation
