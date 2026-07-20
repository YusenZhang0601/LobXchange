#pragma once

#include "test_helpers/exchange_fixture.hpp"

#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace lobx_test {

struct EngineSnapshot {
  std::vector<std::pair<lob::Tick, lob::Quantity>> bids;
  std::vector<std::pair<lob::Tick, lob::Quantity>> asks;
  std::vector<lobx::OpenOrder> open_orders;
  std::vector<lobx::WalletBalance> balances;
  std::vector<lobx::Position> positions;
  size_t event_count{0};

  static EngineSnapshot capture(SpotEngineFixture& fixture) {
    return EngineSnapshot{fixture.engine.topN(lob::Side::Bid, 1000),
                          fixture.engine.topN(lob::Side::Ask, 1000),
                          fixture.engine.open_orders(),
                          fixture.ledger.balances(),
                          fixture.positions.positions(),
                          fixture.events.records().size()};
  }

  static EngineSnapshot capture(PerpEngineFixture& fixture) {
    return EngineSnapshot{fixture.engine.topN(lob::Side::Bid, 1000),
                          fixture.engine.topN(lob::Side::Ask, 1000),
                          fixture.engine.open_orders(),
                          fixture.ledger.balances(),
                          fixture.positions.positions(),
                          fixture.events.records().size()};
  }
};

inline std::map<std::pair<lobx::UserId, lobx::AssetId>, lobx::WalletBalance> balance_map(const std::vector<lobx::WalletBalance>& balances) {
  std::map<std::pair<lobx::UserId, lobx::AssetId>, lobx::WalletBalance> out;
  for (const auto& balance : balances) out[{balance.user, balance.asset}] = balance;
  return out;
}

inline std::map<std::pair<lobx::UserId, lobx::MarketId>, lobx::Position> position_map(const std::vector<lobx::Position>& positions) {
  std::map<std::pair<lobx::UserId, lobx::MarketId>, lobx::Position> out;
  for (const auto& position : positions) out[{position.user, position.market_id}] = position;
  return out;
}

inline std::string snapshot_summary(const EngineSnapshot& snapshot) {
  std::ostringstream os;
  os << "bids=" << snapshot.bids.size()
     << " asks=" << snapshot.asks.size()
     << " open_orders=" << snapshot.open_orders.size()
     << " balances=" << snapshot.balances.size()
     << " positions=" << snapshot.positions.size()
     << " events=" << snapshot.event_count;
  return os.str();
}

inline bool same_book_and_open(const EngineSnapshot& a, const EngineSnapshot& b) {
  if (a.bids != b.bids || a.asks != b.asks || a.open_orders.size() != b.open_orders.size()) return false;
  for (size_t i = 0; i < a.open_orders.size(); ++i) {
    const auto& lhs = a.open_orders[i];
    const auto& rhs = b.open_orders[i];
    if (lhs.id != rhs.id || lhs.user != rhs.user || lhs.side != rhs.side ||
        lhs.limit_price != rhs.limit_price || lhs.leaves_qty != rhs.leaves_qty ||
        lhs.locked_asset != rhs.locked_asset || lhs.locked_remaining != rhs.locked_remaining ||
        lhs.flags != rhs.flags) {
      return false;
    }
  }
  return true;
}

inline bool same_balances(const EngineSnapshot& a, const EngineSnapshot& b) {
  const auto lhs = balance_map(a.balances);
  const auto rhs = balance_map(b.balances);
  if (lhs.size() != rhs.size()) return false;
  for (const auto& item : lhs) {
    auto it = rhs.find(item.first);
    if (it == rhs.end()) return false;
    if (item.second.total != it->second.total ||
        item.second.free != it->second.free ||
        item.second.locked != it->second.locked) {
      return false;
    }
  }
  return true;
}

inline bool same_positions(const EngineSnapshot& a, const EngineSnapshot& b) {
  const auto lhs = position_map(a.positions);
  const auto rhs = position_map(b.positions);
  if (lhs.size() != rhs.size()) return false;
  for (const auto& item : lhs) {
    auto it = rhs.find(item.first);
    if (it == rhs.end()) return false;
    if (item.second.signed_qty != it->second.signed_qty ||
        item.second.entry_price != it->second.entry_price ||
        item.second.realized_pnl != it->second.realized_pnl ||
        item.second.leverage != it->second.leverage) {
      return false;
    }
  }
  return true;
}

} // namespace lobx_test
