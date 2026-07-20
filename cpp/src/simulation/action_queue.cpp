#include "lobx/simulation/action_queue.hpp"

#include <stdexcept>

namespace lobx::simulation {

void ActionQueue::push(ScheduledAction action) {
  queue_.push(std::move(action));
}

bool ActionQueue::has_due(lobx::agents::Timestamp now) const {
  return !queue_.empty() && queue_.top().arrival_ts <= now;
}

ScheduledAction ActionQueue::pop_due() {
  if (queue_.empty()) throw std::runtime_error("ActionQueue::pop_due on empty queue");
  ScheduledAction action = queue_.top();
  queue_.pop();
  return action;
}

bool ActionQueue::empty() const {
  return queue_.empty();
}

std::size_t ActionQueue::size() const {
  return queue_.size();
}

} // namespace lobx::simulation
