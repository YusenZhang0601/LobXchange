#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>

#include "lobx/agents/agent.hpp"
#include "lobx/agents/agent_types.hpp"

namespace lobx::agents {

struct AgentConfig {
  std::string type;
  AgentGroupId group_id{0};
  std::unordered_map<std::string, double> numeric_params;
  std::unordered_map<std::string, std::string> string_params;
};

class AgentFactoryRegistry {
public:
  using FactoryFn = std::function<std::unique_ptr<IAgent>(AgentId, const AgentConfig&)>;

  void register_factory(const std::string& type, FactoryFn fn);
  std::unique_ptr<IAgent> create(const std::string& type, AgentId id, const AgentConfig& config) const;
  bool contains(const std::string& type) const;

private:
  std::unordered_map<std::string, FactoryFn> factories_;
};

void register_builtin_agents(AgentFactoryRegistry& registry);

} // namespace lobx::agents
