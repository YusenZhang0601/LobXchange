#include "lobx/agents/builtins/momentum_follower_agent.hpp"

#include <algorithm>

namespace lobx::agents {

MomentumFollowerAgent::MomentumFollowerAgent(AgentId id, const AgentConfig& config)
    : id_(id), group_id_(config.group_id) {}

AgentAction MomentumFollowerAgent::make_action(const AgentContext& ctx, SubmitLimitOrder payload) {
  return AgentAction{id_, type(), group_id_, ctx.now, next_action_id_++, std::move(payload), "momentum_follow"};
}

std::vector<AgentAction> MomentumFollowerAgent::decide(const AgentContext& ctx) {
  if (ctx.market_view.recent_trades.size() < 2) return {};
  const auto& previous = ctx.market_view.recent_trades[ctx.market_view.recent_trades.size() - 2];
  const auto& last = ctx.market_view.recent_trades[ctx.market_view.recent_trades.size() - 1];
  if (last.price == previous.price) return {};
  const bool buy = last.price > previous.price;

  SubmitLimitOrder order{};
  order.client_order_id = next_order_id_++;
  order.symbol = ctx.market_view.symbol;
  order.side = buy ? Side::Bid : Side::Ask;
  order.price = std::max<Price>(1, static_cast<Price>(last.price) + static_cast<Price>(buy ? 12 : -12));
  order.quantity = 2;
  order.tif = TimeInForce::Ioc;
  return {make_action(ctx, std::move(order))};
}

} // namespace lobx::agents
