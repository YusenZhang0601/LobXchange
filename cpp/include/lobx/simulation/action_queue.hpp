#pragma once

#include <cstdint>
#include <queue>
#include <vector>

#include "lobx/agents/agent_action.hpp"

namespace lobx::simulation {

struct ScheduledAction {
  lobx::agents::AgentAction action;
  lobx::agents::Timestamp decision_ts{0};
  lobx::agents::Timestamp arrival_ts{0};
  std::uint64_t seq{0};
};

class ActionQueue {
public:
  void push(ScheduledAction action);
  bool has_due(lobx::agents::Timestamp now) const;
  ScheduledAction pop_due();
  bool empty() const;
  std::size_t size() const;

private:
  struct Compare {
    bool operator()(const ScheduledAction& a, const ScheduledAction& b) const {
      if (a.arrival_ts != b.arrival_ts) return a.arrival_ts > b.arrival_ts;
      return a.seq > b.seq;
    }
  };

  std::priority_queue<ScheduledAction, std::vector<ScheduledAction>, Compare> queue_;
};

} // namespace lobx::simulation
