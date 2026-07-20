#include "lobx/agents/builtins/noise_trader_agent.hpp"

#include <algorithm>

namespace lobx::agents {

namespace {

double number_or(const AgentConfig& config, const std::string& name, double fallback) {
  const auto it = config.numeric_params.find(name);
  return it == config.numeric_params.end() ? fallback : it->second;
}

} // namespace

NoiseTraderAgent::NoiseTraderAgent(AgentId id, const AgentConfig& config)
    : id_(id),
      group_id_(config.group_id),
      rng_(static_cast<std::uint64_t>(number_or(config, "seed", 104729.0 + static_cast<double>(id)))) {}

AgentAction NoiseTraderAgent::make_action(const AgentContext& ctx, SubmitLimitOrder payload) {
  return AgentAction{id_, type(), group_id_, ctx.now, next_action_id_++, std::move(payload), "noise_trade"};
}

std::vector<AgentAction> NoiseTraderAgent::decide(const AgentContext& ctx) {
  const Price mid = static_cast<Price>(ctx.market_view.mid_price > 0.0 ? ctx.market_view.mid_price : 100.0);
  std::uniform_real_distribution<double> unit(0.0, 1.0);
  std::uniform_int_distribution<int> offset_dist(-4, 4);
  const bool buy = unit(rng_) < 0.5;
  const int price_offset = offset_dist(rng_);
  const bool aggressive = unit(rng_) < 0.35;
  Price price = mid;
  if (buy) {
    price = mid + static_cast<Price>(aggressive ? 8 : price_offset);
  } else {
    price = mid - static_cast<Price>(aggressive ? 8 : price_offset);
  }

  SubmitLimitOrder order{};
  order.client_order_id = next_order_id_++;
  order.symbol = ctx.market_view.symbol;
  order.side = buy ? Side::Bid : Side::Ask;
  order.price = std::max<Price>(1, price);
  order.quantity = 1;
  order.tif = aggressive ? TimeInForce::Ioc : TimeInForce::Gtc;
  order.post_only = !aggressive;
  return {make_action(ctx, std::move(order))};
}

} // namespace lobx::agents
