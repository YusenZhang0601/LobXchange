#pragma once

#include <vector>

#include "lobx/agents/agent_action.hpp"
#include "lobx/agents/agent_context.hpp"
#include "lobx/agents/agent_types.hpp"

namespace lobx::agents {

// Strategy boundary: agents decide from read-only context and emit actions.
// They never receive Exchange or MarketEngine references.
class IAgent {
public:
  virtual ~IAgent() = default;

  virtual AgentId id() const = 0;
  virtual AgentType type() const = 0;

  virtual void initialize(const AgentInitContext& ctx) { (void)ctx; }
  virtual std::vector<AgentAction> decide(const AgentContext& ctx) = 0;
  virtual void on_event(const AgentEvent& event) { (void)event; }
};

} // namespace lobx::agents
