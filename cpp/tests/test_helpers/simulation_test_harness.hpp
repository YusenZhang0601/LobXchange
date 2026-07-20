#pragma once

#include "lobx/event_store.hpp"
#include "lobx/types.hpp"

#include <queue>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace lobx_test {

struct TestSimulationClock {
  lob::Timestamp now{0};

  bool advance_to(lob::Timestamp ts) {
    if (ts < now) return false;
    now = ts;
    return true;
  }
};

struct TestOrderAction {
  lob::Timestamp arrival_ts{0};
  std::string type;
  lobx::UserId user{0};
  lobx::OrderId order_id{0};
  lob::Side side{lob::Side::Bid};
  lob::Tick price{0};
  lob::Quantity qty{0};
  uint32_t flags{lob::NONE};
};

struct TestQueuedEvent {
  lob::Timestamp ts{0};
  uint64_t seq{0};
  std::string type;
  lobx::UserId user{0};
  TestOrderAction order;
};

struct TestQueuedEventGreater {
  bool operator()(const TestQueuedEvent& a, const TestQueuedEvent& b) const {
    if (a.ts != b.ts) return a.ts > b.ts;
    return a.seq > b.seq;
  }
};

class TestEventQueue {
public:
  void push(lob::Timestamp ts, std::string type, lobx::UserId user = 0) {
    queue_.push(TestQueuedEvent{ts, next_seq_++, std::move(type), user, {}});
  }

  uint64_t push_order(const TestOrderAction& action) {
    const uint64_t seq = next_seq_++;
    queue_.push(TestQueuedEvent{action.arrival_ts, seq, action.type, action.user, action});
    return seq;
  }

  TestQueuedEvent pop() {
    TestQueuedEvent event = queue_.top();
    queue_.pop();
    return event;
  }

  TestOrderAction pop_order() { return pop().order; }

  bool empty() const { return queue_.empty(); }

private:
  uint64_t next_seq_{1};
  std::priority_queue<TestQueuedEvent, std::vector<TestQueuedEvent>, TestQueuedEventGreater> queue_;
};

struct TestLatencyModel {
  lob::Timestamp order_latency{0};
  lob::Timestamp cancel_latency{0};
  lob::Timestamp market_data_latency{0};

  lob::Timestamp order_arrival(lob::Timestamp decision_ts) const { return decision_ts + order_latency; }
  lob::Timestamp cancel_arrival(lob::Timestamp decision_ts) const { return decision_ts + cancel_latency; }
  lob::Timestamp market_data_arrival(lob::Timestamp exchange_ts) const { return exchange_ts + market_data_latency; }
};

#ifndef LOBX_TEST_COUNT_EVENTS_DEFINED
#define LOBX_TEST_COUNT_EVENTS_DEFINED
inline int count_events(const lobx::EventStore& events, const std::string& type) {
  int count = 0;
  for (const auto& event : events.records()) {
    if (event.type == type) ++count;
  }
  return count;
}
#endif

inline std::vector<int> deterministic_seed_trace(uint32_t seed, int count) {
  std::mt19937 rng(seed);
  std::vector<int> out;
  out.reserve(static_cast<size_t>(count));
  for (int i = 0; i < count; ++i) out.push_back(static_cast<int>(rng() % 1000));
  return out;
}

} // namespace lobx_test
