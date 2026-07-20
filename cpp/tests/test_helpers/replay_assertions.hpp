#pragma once

#include "test_helpers/market_microstructure_helpers.hpp"

#include <string>

namespace lobx_test {

inline int committed_trade_event_count(const lobx::EventStore& events) {
  return event_count(events, "trade");
}

inline bool payload_contains(const lobx::EventRecord& event, const std::string& text) {
  return event.payload.find(text) != std::string::npos;
}

inline void expect_trade_history_matches_trade_events(const lobx::Exchange& exchange) {
  const int trade_events = committed_trade_event_count(const_cast<lobx::Exchange&>(exchange).events());
  EXPECT_EQ_MSG(static_cast<int>(exchange.trades().size()), trade_events,
                "trade history count must match committed trade events");
}

inline void expect_no_later_trade_for_order(const lobx::EventStore& events, lobx::OrderId order_id) {
  const std::string order_text = "order=" + std::to_string(order_id);
  bool terminal_seen = false;
  for (const auto& event : events.records()) {
    if ((event.type == "order.rejected" || event.type == "order.expired") && payload_contains(event, order_text)) {
      terminal_seen = true;
    }
    if (terminal_seen && event.type == "trade" &&
        (payload_contains(event, "buyer_order=" + std::to_string(order_id)) ||
         payload_contains(event, "seller_order=" + std::to_string(order_id)))) {
      FAIL_TEST("terminal order has later trade event order_id=" + std::to_string(order_id));
    }
  }
}

} // namespace lobx_test
