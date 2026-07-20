#include "lobx/agents/builtins/whale_sweeper_agent.hpp"

#include <algorithm>

namespace lobx::agents {

namespace {

double number_or(const AgentConfig& config, const std::string& name, double fallback) {
  const auto it = config.numeric_params.find(name);
  return it == config.numeric_params.end() ? fallback : it->second;
}

} // namespace

WhaleSweeperAgent::WhaleSweeperAgent(AgentId id, const AgentConfig& config)
    : id_(id),
      group_id_(config.group_id),
      rng_(static_cast<std::uint64_t>(number_or(config, "seed", 524287.0 + static_cast<double>(id)))),
      interval_(std::max(1, static_cast<int>(number_or(config, "interval", 12.0)))),
      quantity_(std::max<Quantity>(1, static_cast<Quantity>(number_or(config, "quantity", 20.0)))) {}

AgentAction WhaleSweeperAgent::make_action(const AgentContext& ctx, SubmitMarketOrder payload) {
  return AgentAction{id_, type(), group_id_, ctx.now, next_action_id_++, std::move(payload), "whale_sweep"};
}

std::vector<AgentAction> WhaleSweeperAgent::decide(const AgentContext& ctx) {
  if (ctx.now <= 0 || (ctx.now % interval_) != 0) return {};
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  const bool buy = unit(rng_) < 0.5;
  SubmitMarketOrder order{};
  order.client_order_id = next_order_id_++;
  order.symbol = ctx.market_view.symbol;
  order.side = buy ? Side::Bid : Side::Ask;
  order.quantity = quantity_;
  return {make_action(ctx, std::move(order))};
}

} // namespace lobx::agents
