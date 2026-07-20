#pragma once

#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace lobx_test {

#ifndef LOBX_TEST_COUNT_EVENTS_DEFINED
#define LOBX_TEST_COUNT_EVENTS_DEFINED
inline int count_events(const lobx::EventStore& events, const std::string& type) {
  int count = 0;
  for (const auto& event : events.records()) {
    if (event.type == type) ++count;
  }
  return count;
}
#endif

inline int event_count(const lobx::EventStore& events, const std::string& type) {
  return count_events(events, type);
}

inline std::vector<lobx::EventRecord> events_of_type(const lobx::EventStore& events, const std::string& type) {
  std::vector<lobx::EventRecord> out;
  for (const auto& event : events.records()) {
    if (event.type == type) out.push_back(event);
  }
  return out;
}

inline bool has_open_order(lobx::MarketEngine& engine, lobx::OrderId id) {
  for (const auto& order : engine.open_orders()) {
    if (order.id == id) return true;
  }
  return false;
}

inline bool has_open_order(const SpotEngineFixture& f, lobx::OrderId id) {
  for (const auto& order : f.engine.open_orders()) {
    if (order.id == id) return true;
  }
  return false;
}

inline lobx::OpenOrder require_open_order(lobx::MarketEngine& engine, lobx::OrderId id) {
  for (const auto& order : engine.open_orders()) {
    if (order.id == id) return order;
  }
  FAIL_TEST("missing open order id=" + std::to_string(id));
  return lobx::OpenOrder{};
}

inline lobx::OpenOrder get_open_order_or_fail(const SpotEngineFixture& f, lobx::OrderId id) {
  for (const auto& order : f.engine.open_orders()) {
    if (order.id == id) return order;
  }
  FAIL_TEST("missing open order id=" + std::to_string(id));
  return lobx::OpenOrder{};
}

inline lob::Quantity top_qty(lobx::MarketEngine& engine, lob::Side side, lob::Tick price) {
  for (const auto& level : engine.topN(side, 1000)) {
    if (level.first == price) return level.second;
  }
  return 0;
}

inline lob::Quantity aggregate_open_qty(const SpotEngineFixture& f, lob::Side side, lob::Tick price) {
  lob::Quantity qty = 0;
  for (const auto& order : f.engine.open_orders()) {
    if (order.side == side && order.limit_price == price) qty += order.leaves_qty;
  }
  return qty;
}

inline lob::Quantity aggregate_topN_qty(SpotEngineFixture& f, lob::Side side, lob::Tick price, int levels = 100) {
  for (const auto& level : f.engine.topN(side, levels)) {
    if (level.first == price) return level.second;
  }
  return 0;
}

inline std::map<lob::Tick, lob::Quantity> aggregate_open_orders(lobx::MarketEngine& engine, lob::Side side) {
  std::map<lob::Tick, lob::Quantity> out;
  for (const auto& order : engine.open_orders()) {
    if (order.side == side) out[order.limit_price] += order.leaves_qty;
  }
  return out;
}

inline std::map<lob::Tick, lob::Quantity> aggregate_topN(lobx::MarketEngine& engine, lob::Side side) {
  std::map<lob::Tick, lob::Quantity> out;
  for (const auto& level : engine.topN(side, 1000)) out[level.first] += level.second;
  return out;
}

inline std::string depth_summary(const std::map<lob::Tick, lob::Quantity>& depth) {
  std::ostringstream os;
  bool first = true;
  for (const auto& [price, qty] : depth) {
    if (!first) os << ",";
    first = false;
    os << price << "@" << qty;
  }
  return os.str();
}

inline void expect_depth_matches(const std::map<lob::Tick, lob::Quantity>& actual,
                                 const std::map<lob::Tick, lob::Quantity>& expected,
                                 const std::string& side) {
  EXPECT_TRUE_MSG(actual == expected,
                  side + " topN must match open order aggregate actual=[" + depth_summary(actual) +
                      "] expected=[" + depth_summary(expected) + "]");
}

inline void expect_topN_matches_open_orders(lobx::MarketEngine& engine) {
  expect_depth_matches(aggregate_topN(engine, lob::Side::Bid), aggregate_open_orders(engine, lob::Side::Bid), "bid");
  expect_depth_matches(aggregate_topN(engine, lob::Side::Ask), aggregate_open_orders(engine, lob::Side::Ask), "ask");
}

inline void expect_no_locked_balance(const SpotEngineFixture& f, lobx::UserId user, lobx::AssetId asset) {
  EXPECT_EQ_MSG(f.ledger.locked(user, asset), 0, "user=" + std::to_string(user) + " asset=" + std::to_string(asset));
}

inline void expect_top_level(SpotEngineFixture& f, lob::Side side, lob::Tick price, lob::Quantity qty) {
  EXPECT_EQ_MSG(aggregate_topN_qty(f, side, price), qty,
                std::string(side == lob::Side::Bid ? "bid" : "ask") + " price=" + std::to_string(price));
}

inline void expect_trade(const lobx::TradeEvent& trade,
                         lobx::UserId buyer,
                         lobx::UserId seller,
                         lobx::OrderId buyer_order,
                         lobx::OrderId seller_order,
                         lob::Tick price,
                         lob::Quantity qty) {
  EXPECT_EQ(trade.buyer, buyer);
  EXPECT_EQ(trade.seller, seller);
  EXPECT_EQ(trade.buyer_order_id, buyer_order);
  EXPECT_EQ(trade.seller_order_id, seller_order);
  EXPECT_EQ(trade.price, price);
  EXPECT_EQ(trade.qty, qty);
}

inline std::vector<lobx::OrderId> seller_order_sequence(const std::vector<lobx::TradeEvent>& trades) {
  std::vector<lobx::OrderId> out;
  out.reserve(trades.size());
  for (const auto& trade : trades) out.push_back(trade.seller_order_id);
  return out;
}

inline std::vector<lobx::OrderId> buyer_order_sequence(const std::vector<lobx::TradeEvent>& trades) {
  std::vector<lobx::OrderId> out;
  out.reserve(trades.size());
  for (const auto& trade : trades) out.push_back(trade.buyer_order_id);
  return out;
}

inline void expect_book_matches_open_orders(SpotEngineFixture& f) {
  expect_topN_matches_open_orders(f.engine);
  for (const auto& order : f.engine.open_orders()) {
    EXPECT_TRUE_MSG(order.leaves_qty > 0, "open order must have positive leaves order_id=" + std::to_string(order.id));
    EXPECT_TRUE_MSG(aggregate_topN_qty(f, order.side, order.limit_price) >= order.leaves_qty,
                    "open order quantity must appear in book aggregate order_id=" + std::to_string(order.id));
    EXPECT_TRUE_MSG((order.flags & (lob::IOC | lob::FOK)) == 0u,
                    "IOC/FOK order must not remain open order_id=" + std::to_string(order.id));
  }
}

inline void deposit_spot_user(SpotEngineFixture& f, lobx::UserId user,
                              lobx::Amount base = 1000000LL, lobx::Amount quote = 1000000LL) {
  EXPECT_TRUE(f.ledger.deposit(user, f.base_asset, base).ok);
  EXPECT_TRUE(f.ledger.deposit(user, f.quote_asset, quote).ok);
}

inline std::string trade_sequence(const std::vector<lobx::TradeEvent>& trades) {
  std::ostringstream os;
  for (const auto& trade : trades) {
    os << "[px=" << trade.price << ",qty=" << trade.qty
       << ",buyer_order=" << trade.buyer_order_id
       << ",seller_order=" << trade.seller_order_id << "]";
  }
  return os.str();
}

inline lobx::Amount total_asset(const lobx::AccountLedger& ledger, lobx::AssetId asset) {
  lobx::Amount total = 0;
  for (const auto& balance : ledger.balances()) {
    if (balance.asset == asset) total += balance.total;
  }
  return total;
}

inline constexpr lobx::UserId dedicated_fee_account() {
  return std::numeric_limits<lobx::UserId>::max();
}

} // namespace lobx_test
