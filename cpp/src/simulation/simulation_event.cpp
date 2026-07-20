#include "lobx/simulation/simulation_event.hpp"

#include <type_traits>

namespace lobx::simulation {

const char* event_source_name(EventSource source) {
  switch (source) {
    case EventSource::Agent: return "agent";
    case EventSource::Runtime: return "runtime";
    case EventSource::Exchange: return "exchange";
    case EventSource::Recorder: return "recorder";
    case EventSource::Metrics: return "metrics";
  }
  return "unknown";
}

const char* simulation_event_payload_name(const SimulationEventPayload& payload) {
  return std::visit([](const auto& value) -> const char* {
    using T = std::decay_t<decltype(value)>;
    if constexpr (std::is_same_v<T, AgentDecisionEvent>) return "agent_decision";
    if constexpr (std::is_same_v<T, ActionScheduledEvent>) return "action_scheduled";
    if constexpr (std::is_same_v<T, OrderSubmittedEvent>) return "order_submitted";
    if constexpr (std::is_same_v<T, OrderAcceptedEvent>) return "order_accepted";
    if constexpr (std::is_same_v<T, OrderRejectedEvent>) return "order_rejected";
    if constexpr (std::is_same_v<T, OrderCanceledEvent>) return "order_canceled";
    if constexpr (std::is_same_v<T, TradeEvent>) return "trade";
    if constexpr (std::is_same_v<T, BookSnapshotEvent>) return "book_snapshot";
    return "unknown";
  }, payload);
}

} // namespace lobx::simulation
