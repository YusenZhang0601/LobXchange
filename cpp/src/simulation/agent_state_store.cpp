#include "lobx/simulation/agent_state_store.hpp"

#include <algorithm>

namespace lobx::simulation {

void AgentStateStore::initialize_agent(lobx::agents::AgentId agent_id) {
  (void)state_for(agent_id);
}

const AgentStateStore::MutableAgentState* AgentStateStore::find_state(lobx::agents::AgentId agent_id) const {
  const auto it = states_.find(agent_id);
  return it == states_.end() ? nullptr : &it->second;
}

AgentStateStore::MutableAgentState& AgentStateStore::state_for(lobx::agents::AgentId agent_id) {
  return states_[agent_id];
}

lobx::agents::PrivateAgentState AgentStateStore::get_private_state(lobx::agents::AgentId agent_id) const {
  static const std::vector<lobx::agents::AgentOrderView> empty_orders;
  static const std::vector<lobx::agents::AgentFillView> empty_fills;

  const MutableAgentState* state = find_state(agent_id);
  if (state == nullptr) {
    return lobx::agents::PrivateAgentState{agent_id,
                                           0.0,
                                           0.0,
                                           0.0,
                                           0,
                                           0,
                                           0.0,
                                           std::span<const lobx::agents::AgentOrderView>(empty_orders.data(), empty_orders.size()),
                                           std::span<const lobx::agents::AgentFillView>(empty_fills.data(), empty_fills.size()),
                                           0,
                                           0};
  }
  return lobx::agents::PrivateAgentState{agent_id,
                                         state->cash,
                                         state->realized_pnl,
                                         state->unrealized_pnl,
                                         state->inventory,
                                         state->max_inventory,
                                         state->risk_budget_remaining,
                                         std::span<const lobx::agents::AgentOrderView>(state->open_orders.data(), state->open_orders.size()),
                                         std::span<const lobx::agents::AgentFillView>(state->recent_fills.data(), state->recent_fills.size()),
                                         state->last_fill_ts,
                                         state->last_action_ts};
}

std::vector<lobx::agents::AgentOrderView> AgentStateStore::open_orders(lobx::agents::AgentId agent_id) const {
  const MutableAgentState* state = find_state(agent_id);
  return state == nullptr ? std::vector<lobx::agents::AgentOrderView>{} : state->open_orders;
}

std::optional<lobx::agents::AgentOrderView> AgentStateStore::find_order(
    lobx::agents::AgentId agent_id,
    lobx::agents::ClientOrderId client_order_id) const {
  const MutableAgentState* state = find_state(agent_id);
  if (state == nullptr) return std::nullopt;
  for (const auto& order : state->open_orders) {
    if (order.client_order_id == client_order_id) return order;
  }
  return std::nullopt;
}

void AgentStateStore::on_order_accepted(lobx::agents::AgentId agent_id,
                                        lobx::agents::ClientOrderId client_order_id,
                                        const std::string& symbol,
                                        lobx::agents::Side side,
                                        lobx::agents::Price price,
                                        lobx::agents::Quantity remaining_quantity,
                                        lobx::agents::Timestamp ts) {
  MutableAgentState& state = state_for(agent_id);
  state.last_action_ts = ts;
  if (remaining_quantity <= 0) return;
  auto it = std::find_if(state.open_orders.begin(), state.open_orders.end(), [&](const auto& order) {
    return order.client_order_id == client_order_id;
  });
  lobx::agents::AgentOrderView view{client_order_id, symbol, side, price, remaining_quantity, ts};
  if (it == state.open_orders.end()) state.open_orders.push_back(std::move(view));
  else *it = std::move(view);
}

void AgentStateStore::on_order_canceled(lobx::agents::AgentId agent_id,
                                        lobx::agents::ClientOrderId client_order_id,
                                        lobx::agents::Timestamp ts) {
  MutableAgentState& state = state_for(agent_id);
  state.last_action_ts = ts;
  state.open_orders.erase(std::remove_if(state.open_orders.begin(), state.open_orders.end(), [&](const auto& order) {
    return order.client_order_id == client_order_id;
  }), state.open_orders.end());
}

void AgentStateStore::on_trade(lobx::agents::AgentId agent_id,
                               lobx::agents::ClientOrderId client_order_id,
                               const std::string& symbol,
                               lobx::agents::Side side,
                               lobx::agents::Price price,
                               lobx::agents::Quantity quantity,
                               lobx::agents::Timestamp ts) {
  MutableAgentState& state = state_for(agent_id);
  state.last_fill_ts = ts;
  state.last_action_ts = ts;
  state.inventory += side == lob::Side::Bid ? quantity : -quantity;
  state.recent_fills.push_back(lobx::agents::AgentFillView{symbol, side, price, quantity, ts});
  if (state.recent_fills.size() > 64) {
    state.recent_fills.erase(state.recent_fills.begin(),
                             state.recent_fills.begin() + static_cast<std::ptrdiff_t>(state.recent_fills.size() - 64));
  }

  for (auto& order : state.open_orders) {
    if (order.client_order_id != client_order_id) continue;
    order.remaining_quantity = std::max<lobx::agents::Quantity>(0, order.remaining_quantity - quantity);
    break;
  }
  state.open_orders.erase(std::remove_if(state.open_orders.begin(), state.open_orders.end(), [](const auto& order) {
    return order.remaining_quantity <= 0;
  }), state.open_orders.end());
}

void AgentStateStore::on_order_rejected(lobx::agents::AgentId agent_id, lobx::agents::Timestamp ts) {
  state_for(agent_id).last_action_ts = ts;
}

} // namespace lobx::simulation
