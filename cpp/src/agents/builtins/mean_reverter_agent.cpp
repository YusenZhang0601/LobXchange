#include "lobx/agents/builtins/mean_reverter_agent.hpp"

#include <algorithm>
#include <cmath>

namespace lobx::agents {

namespace {

double number_or(const AgentConfig& config, const std::string& name, double fallback) {
  const auto it = config.numeric_params.find(name);
  return it == config.numeric_params.end() ? fallback : it->second;
}

} // namespace

MeanReverterAgent::MeanReverterAgent(AgentId id, const AgentConfig& config)
    : id_(id),
      group_id_(config.group_id),
      reference_price_(static_cast<Price>(number_or(config, "reference_price", 100.0))) {}

AgentAction MeanReverterAgent::make_action(const AgentContext& ctx, SubmitLimitOrder payload) {
  return AgentAction{id_, type(), group_id_, ctx.now, next_action_id_++, std::move(payload), "mean_reversion"};
}

std::vector<AgentAction> MeanReverterAgent::decide(const AgentContext& ctx) {
  const Price last = ctx.market_view.recent_trades.empty()
                         ? static_cast<Price>(ctx.market_view.mid_price)
                         : static_cast<Price>(ctx.market_view.recent_trades.back().price);
  if (last <= 0) return {};
  const Price deviation = last - reference_price_;
  if (std::llabs(static_cast<long long>(deviation)) < 3) return {};
  const bool sell = deviation > 0;

  SubmitLimitOrder order{};
  order.client_order_id = next_order_id_++;
  order.symbol = ctx.market_view.symbol;
  order.side = sell ? Side::Ask : Side::Bid;
  order.price = std::max<Price>(1, last + static_cast<Price>(sell ? -4 : 4));
  order.quantity = 2;
  order.tif = TimeInForce::Ioc;
  return {make_action(ctx, std::move(order))};
}

} // namespace lobx::agents
