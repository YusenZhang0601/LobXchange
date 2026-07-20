#include "lobx/agents/builtins/static_market_maker_agent.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace lobx::agents {

namespace {

double number_or(const AgentConfig& config, const std::string& name, double fallback) {
  const auto it = config.numeric_params.find(name);
  return it == config.numeric_params.end() ? fallback : it->second;
}

} // namespace

StaticMarketMakerAgent::StaticMarketMakerAgent(AgentId id, const AgentConfig& config)
    : id_(id),
      group_id_(config.group_id),
      rng_(static_cast<std::uint64_t>(number_or(config, "seed", 7919.0 + static_cast<double>(id)))),
      bounded_quotes_(number_or(config, "bounded_quotes", 0.0) != 0.0),
      max_open_orders_per_side_(std::max(1, static_cast<int>(number_or(config, "max_open_orders_per_side", 1.0)))),
      quote_refresh_interval_steps_(std::max<Timestamp>(1, static_cast<Timestamp>(number_or(config, "quote_refresh_interval_steps", 10.0)))),
      quote_ttl_steps_(std::max<Timestamp>(1, static_cast<Timestamp>(number_or(config, "quote_ttl_steps", 20.0)))),
      cancel_stale_quotes_(number_or(config, "cancel_stale_quotes", 1.0) != 0.0),
      replace_on_price_change_(number_or(config, "replace_on_price_change", 1.0) != 0.0) {}

AgentAction StaticMarketMakerAgent::make_limit_action(const AgentContext& ctx,
                                                      SubmitLimitOrder payload,
                                                      std::string reason_tag) {
  return AgentAction{id_, type(), group_id_, ctx.now, next_action_id_++, std::move(payload), std::move(reason_tag)};
}

AgentAction StaticMarketMakerAgent::make_cancel_action(const AgentContext& ctx,
                                                       CancelOrder payload,
                                                       std::string reason_tag) {
  return AgentAction{id_, type(), group_id_, ctx.now, next_action_id_++, std::move(payload), std::move(reason_tag)};
}

std::vector<AgentAction> StaticMarketMakerAgent::decide(const AgentContext& ctx) {
  const Price mid = static_cast<Price>(ctx.market_view.mid_price > 0.0 ? ctx.market_view.mid_price : 100.0);
  std::uniform_int_distribution<int> spread_dist(0, 2);
  std::uniform_int_distribution<int> qty_dist(0, 2);
  const Price spread = static_cast<Price>(2 + spread_dist(rng_));
  const Quantity qty = static_cast<Quantity>(2 + qty_dist(rng_));

  SubmitLimitOrder bid{};
  bid.client_order_id = next_order_id_++;
  bid.symbol = ctx.market_view.symbol;
  bid.side = Side::Bid;
  bid.price = std::max<Price>(1, mid - spread);
  bid.quantity = qty;
  bid.post_only = true;

  SubmitLimitOrder ask{};
  ask.client_order_id = next_order_id_++;
  ask.symbol = ctx.market_view.symbol;
  ask.side = Side::Ask;
  ask.price = std::max<Price>(1, mid + spread);
  ask.quantity = qty;
  ask.post_only = true;

  if (!bounded_quotes_) {
    return {make_limit_action(ctx, std::move(bid), "static_quote"),
            make_limit_action(ctx, std::move(ask), "static_quote")};
  }

  std::vector<AgentAction> actions;
  auto handle_side = [&](Side side, SubmitLimitOrder quote) {
    std::vector<AgentOrderView> orders;
    for (const AgentOrderView& order : ctx.private_state.open_orders) {
      if (order.side == side) orders.push_back(order);
    }
    std::sort(orders.begin(), orders.end(), [](const AgentOrderView& a, const AgentOrderView& b) {
      if (a.submitted_ts != b.submitted_ts) return a.submitted_ts < b.submitted_ts;
      return a.client_order_id < b.client_order_id;
    });

    for (std::size_t i = static_cast<std::size_t>(max_open_orders_per_side_); i < orders.size(); ++i) {
      actions.push_back(make_cancel_action(ctx,
                                           CancelOrder{orders[i].client_order_id},
                                           "cancel_stale_quote"));
    }

    const std::size_t kept = std::min<std::size_t>(orders.size(), static_cast<std::size_t>(max_open_orders_per_side_));
    if (kept == 0) {
      actions.push_back(make_limit_action(ctx, std::move(quote), "static_quote"));
      return;
    }

    const AgentOrderView& active = orders.front();
    const bool stale = cancel_stale_quotes_ && (ctx.now - active.submitted_ts >= quote_ttl_steps_);
    const bool refresh_due = ctx.now - active.submitted_ts >= quote_refresh_interval_steps_;
    const bool price_changed = active.price != quote.price;
    if (stale || (replace_on_price_change_ && refresh_due && price_changed)) {
      actions.push_back(make_cancel_action(ctx,
                                           CancelOrder{active.client_order_id},
                                           stale ? "cancel_stale_quote" : "quote_refresh"));
      actions.push_back(make_limit_action(ctx,
                                          std::move(quote),
                                          stale ? "static_quote" : "quote_refresh"));
    }
  };

  handle_side(Side::Bid, std::move(bid));
  handle_side(Side::Ask, std::move(ask));
  return actions;
}

} // namespace lobx::agents
