#pragma once

#include <unordered_map>
#include <vector>

#include "lobx/types.hpp"

namespace lobx {

struct Position {
  UserId user{0};
  MarketId market_id{0};
  lob::Quantity signed_qty{0};
  lob::Tick entry_price{0};
  int leverage{1};
  Amount realized_pnl{0};
};

class PositionEngine {
public:
  using Snapshot = std::unordered_map<UserId, std::unordered_map<MarketId, Position>>;

  void set_leverage(UserId user, MarketId market_id, int leverage);
  int leverage(UserId user, MarketId market_id) const;
  Position position(UserId user, MarketId market_id) const;
  bool reduce_only_would_increase(UserId user, MarketId market_id, lob::Side side, lob::Quantity qty) const;
  void apply_trade(UserId user, MarketId market_id, lob::Side side, lob::Tick price, lob::Quantity qty);
  bool apply_trade_checked(UserId user, MarketId market_id, lob::Side side, lob::Tick price, lob::Quantity qty);
  std::vector<Position> positions() const;
  Snapshot snapshot() const;
  void restore(const Snapshot& snapshot);

private:
  Position& get_or_create(UserId user, MarketId market_id);
  std::unordered_map<UserId, std::unordered_map<MarketId, Position>> positions_;
};

} // namespace lobx
