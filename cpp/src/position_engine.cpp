#include "lobx/position_engine.hpp"

#include <algorithm>
#include <cstdlib>
#include <limits>

namespace lobx {

namespace {

bool checked_quantity_add(lob::Quantity a, lob::Quantity b, lob::Quantity& out) {
  return !__builtin_add_overflow(a, b, &out);
}

bool checked_abs_quantity(lob::Quantity qty, lob::Quantity& out) {
  if (qty == std::numeric_limits<lob::Quantity>::min()) return false;
  out = qty < 0 ? -qty : qty;
  return true;
}

bool checked_realized_delta(lob::Tick price, lob::Tick entry_price, lob::Quantity close_qty, int direction, Amount& out) {
  out = 0;
  Amount delta = 0;
  Amount value = 0;
  if (__builtin_sub_overflow(price, entry_price, &delta)) return false;
  if (__builtin_mul_overflow(delta, close_qty, &value)) return false;
  if (__builtin_mul_overflow(value, static_cast<Amount>(direction), &out)) return false;
  return true;
}

bool checked_amount_add(Amount a, Amount b, Amount& out) {
  return !__builtin_add_overflow(a, b, &out);
}

} // namespace

Position& PositionEngine::get_or_create(UserId user, MarketId market_id) {
  auto& by_market = positions_[user];
  auto it = by_market.find(market_id);
  if (it == by_market.end()) it = by_market.emplace(market_id, Position{user, market_id, 0, 0, 1, 0}).first;
  return it->second;
}

void PositionEngine::set_leverage(UserId user, MarketId market_id, int leverage) {
  Position& p = get_or_create(user, market_id);
  p.leverage = std::max(1, leverage);
}

int PositionEngine::leverage(UserId user, MarketId market_id) const {
  return position(user, market_id).leverage;
}

Position PositionEngine::position(UserId user, MarketId market_id) const {
  auto uit = positions_.find(user);
  if (uit == positions_.end()) return Position{user, market_id, 0, 0, 1, 0};
  auto mit = uit->second.find(market_id);
  if (mit == uit->second.end()) return Position{user, market_id, 0, 0, 1, 0};
  return mit->second;
}

bool PositionEngine::reduce_only_would_increase(UserId user, MarketId market_id, lob::Side side, lob::Quantity qty) const {
  const Position p = position(user, market_id);
  const lob::Quantity delta = side == lob::Side::Bid ? qty : -qty;
  if (p.signed_qty == 0) return true;
  if ((p.signed_qty > 0 && delta > 0) || (p.signed_qty < 0 && delta < 0)) return true;
  return std::llabs(delta) > std::llabs(p.signed_qty);
}

void PositionEngine::apply_trade(UserId user, MarketId market_id, lob::Side side, lob::Tick price, lob::Quantity qty) {
  (void)apply_trade_checked(user, market_id, side, price, qty);
}

bool PositionEngine::apply_trade_checked(UserId user, MarketId market_id, lob::Side side, lob::Tick price, lob::Quantity qty) {
  if (qty <= 0) return true;
  Position& p = get_or_create(user, market_id);
  const lob::Quantity delta = side == lob::Side::Bid ? qty : -qty;
  lob::Quantity updated_qty = 0;
  if (!checked_quantity_add(p.signed_qty, delta, updated_qty)) return false;
  if (p.signed_qty == 0 || ((p.signed_qty > 0) == (delta > 0))) {
    lob::Quantity old_abs = 0;
    lob::Quantity add_abs = 0;
    if (!checked_abs_quantity(p.signed_qty, old_abs) || !checked_abs_quantity(delta, add_abs)) return false;
    lob::Quantity new_abs = 0;
    if (!checked_quantity_add(old_abs, add_abs, new_abs)) return false;
    if (new_abs > 0) {
      const long double weighted = static_cast<long double>(p.entry_price) * old_abs + static_cast<long double>(price) * add_abs;
      p.entry_price = static_cast<lob::Tick>(weighted / new_abs);
    }
    p.signed_qty = updated_qty;
    return true;
  }

  const lob::Quantity old_qty = p.signed_qty;
  lob::Quantity old_abs = 0;
  lob::Quantity delta_abs = 0;
  if (!checked_abs_quantity(old_qty, old_abs) || !checked_abs_quantity(delta, delta_abs)) return false;
  const lob::Quantity close_qty = std::min<lob::Quantity>(old_abs, delta_abs);
  const int direction = old_qty > 0 ? 1 : -1;
  Amount realized_delta = 0;
  if (!checked_realized_delta(price, p.entry_price, close_qty, direction, realized_delta)) return false;
  Amount updated_realized = 0;
  if (!checked_amount_add(p.realized_pnl, realized_delta, updated_realized)) return false;
  p.realized_pnl = updated_realized;
  p.signed_qty = updated_qty;
  if (p.signed_qty == 0) {
    p.entry_price = 0;
  } else if ((old_qty > 0) != (p.signed_qty > 0)) {
    p.entry_price = price;
  }
  return true;
}

std::vector<Position> PositionEngine::positions() const {
  std::vector<Position> out;
  for (const auto& ukv : positions_) for (const auto& mkv : ukv.second) out.push_back(mkv.second);
  std::sort(out.begin(), out.end(), [](const Position& a, const Position& b) {
    if (a.user != b.user) return a.user < b.user;
    return a.market_id < b.market_id;
  });
  return out;
}

PositionEngine::Snapshot PositionEngine::snapshot() const {
  return positions_;
}

void PositionEngine::restore(const Snapshot& snapshot) {
  positions_ = snapshot;
}

} // namespace lobx
