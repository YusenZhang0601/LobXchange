#include "test_helpers/market_microstructure_helpers.hpp"

#include <limits>

using namespace lobx_test;

namespace {

constexpr lobx::UserId dave = 40;
constexpr lobx::UserId erin = 50;

void add_priority_users(SpotEngineFixture& f) {
  deposit_spot_user(f, dave);
  deposit_spot_user(f, erin);
}

void rest_three_same_price_asks(SpotEngineFixture& f) {
  add_priority_users(f);
  EXPECT_TRUE(f.submit(f.alice, 51001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 51002, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, 51003, lob::Side::Ask, 100, 1, lob::POST_ONLY, 3).accepted);
}

void force_fee_failure(SpotEngineFixture& f) {
  EXPECT_TRUE(f.ledger.deposit(dedicated_fee_account(), f.quote_asset, std::numeric_limits<lobx::Amount>::max()).ok);
}

void clear_fee_failure(SpotEngineFixture& f) {
  EXPECT_TRUE(f.ledger.withdraw(dedicated_fee_account(), f.quote_asset, std::numeric_limits<lobx::Amount>::max()).ok);
}

void expect_alice_then_carol(const std::vector<lobx::TradeEvent>& trades) {
  EXPECT_EQ_MSG(trades.size(), 2UL, trade_sequence(trades));
  EXPECT_EQ_MSG(trades[0].seller_order_id, 51001ULL, trade_sequence(trades));
  EXPECT_EQ_MSG(trades[1].seller_order_id, 51002ULL, trade_sequence(trades));
}

void expect_seller_order_ids(const std::vector<lobx::TradeEvent>& trades,
                             const std::vector<lobx::OrderId>& expected) {
  EXPECT_EQ_MSG(trades.size(), expected.size(), trade_sequence(trades));
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ_MSG(trades[i].seller_order_id, expected[i], trade_sequence(trades));
  }
}

} // namespace

TEST(OrderBookPriorityInvariants, SamePriceFIFOPreservedAcrossFills) {
  SpotEngineFixture f;
  rest_three_same_price_asks(f);

  auto bid = f.submit(f.bob, 51004, lob::Side::Bid, 100, 2, lob::IOC, 4);

  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  expect_alice_then_carol(bid.trades);
  EXPECT_TRUE(has_open_order(f.engine, 51003));
}

TEST(OrderBookPriorityInvariants, SamePriceFIFOPreservedAfterCancel) {
  SpotEngineFixture f;
  rest_three_same_price_asks(f);
  EXPECT_TRUE(f.engine.cancel(51001, f.alice, 4));

  auto bid = f.submit(f.bob, 51005, lob::Side::Bid, 100, 2, lob::IOC, 5);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.trades.size(), 2UL);
  EXPECT_EQ(bid.trades[0].seller_order_id, 51002ULL);
  EXPECT_EQ(bid.trades[1].seller_order_id, 51003ULL);
}

TEST(OrderBookPriorityInvariants, SamePriceFIFOPreservedAfterFailedSubmitRollback) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  rest_three_same_price_asks(f);
  force_fee_failure(f);
  EXPECT_FALSE(f.submit(f.bob, 51006, lob::Side::Bid, 100, 1, lob::IOC, 4).accepted);
  clear_fee_failure(f);

  auto bid = f.submit(erin, 51007, lob::Side::Bid, 100, 2, lob::IOC, 5);

  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  expect_alice_then_carol(bid.trades);
}

TEST(OrderBookPriorityInvariants, SamePriceFIFOPreservedAfterSnapshotRebuild) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  rest_three_same_price_asks(f);
  force_fee_failure(f);
  EXPECT_FALSE(f.submit(f.bob, 51008, lob::Side::Bid, 100, 2, lob::IOC, 4).accepted);
  clear_fee_failure(f);

  auto bid = f.submit(erin, 51009, lob::Side::Bid, 100, 2, lob::IOC, 5);

  EXPECT_TRUE(bid.accepted);
  expect_alice_then_carol(bid.trades);
}

TEST(OrderBookPriorityInvariants, RetryAfterRollbackDoesNotReorderExistingMakers) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  rest_three_same_price_asks(f);
  force_fee_failure(f);
  EXPECT_FALSE(f.submit(f.bob, 51010, lob::Side::Bid, 100, 1, lob::IOC, 4).accepted);
  EXPECT_FALSE(f.submit(f.bob, 51010, lob::Side::Bid, 100, 1, lob::IOC, 5).accepted);
  clear_fee_failure(f);

  auto bid = f.submit(erin, 51011, lob::Side::Bid, 100, 2, lob::IOC, 6);

  EXPECT_TRUE(bid.accepted);
  expect_alice_then_carol(bid.trades);
}

TEST(OrderBookPriorityInvariants, CancelAfterRollbackCancelsOriginalPriorityOrder) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  rest_three_same_price_asks(f);
  force_fee_failure(f);
  EXPECT_FALSE(f.submit(f.bob, 51012, lob::Side::Bid, 100, 1, lob::IOC, 4).accepted);
  clear_fee_failure(f);
  EXPECT_TRUE(f.engine.cancel(51001, f.alice, 5));

  auto bid = f.submit(erin, 51013, lob::Side::Bid, 100, 1, lob::IOC, 6);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.trades.size(), 1UL);
  EXPECT_EQ(bid.trades.front().seller_order_id, 51002ULL);
}

TEST(OrderBookPriorityInvariants, RollbackPreservesSamePriceFIFO) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  add_priority_users(f);
  EXPECT_TRUE(f.submit(f.alice, 51101, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 51102, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, 51103, lob::Side::Ask, 100, 1, lob::POST_ONLY, 3).accepted);
  force_fee_failure(f);
  EXPECT_FALSE(f.submit(f.bob, 51104, lob::Side::Bid, 100, 1, lob::IOC, 4).accepted);
  clear_fee_failure(f);

  auto bid = f.submit(erin, 51105, lob::Side::Bid, 100, 3, lob::IOC, 5);

  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  expect_seller_order_ids(bid.trades, {51101, 51102, 51103});
  EXPECT_TRUE(f.engine.open_orders().empty());
}

TEST(OrderBookPriorityInvariants, CancelMiddleOrderPreservesRemainingFIFO) {
  SpotEngineFixture f;
  add_priority_users(f);
  EXPECT_TRUE(f.submit(f.alice, 51111, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 51112, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, 51113, lob::Side::Ask, 100, 1, lob::POST_ONLY, 3).accepted);
  EXPECT_TRUE(f.engine.cancel(51112, f.carol, 4));

  auto bid = f.submit(f.bob, 51114, lob::Side::Bid, 100, 2, lob::IOC, 5);

  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  expect_seller_order_ids(bid.trades, {51111, 51113});
}

TEST(OrderBookPriorityInvariants, SameTimestampUsesOrderSeqAsTieBreaker) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 51121, lob::Side::Ask, 100, 1, lob::POST_ONLY, 10).accepted);
  EXPECT_TRUE(f.submit(f.carol, 51122, lob::Side::Ask, 100, 1, lob::POST_ONLY, 10).accepted);

  auto bid = f.submit(f.bob, 51123, lob::Side::Bid, 100, 1, lob::IOC, 11);

  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(bid.trades.size(), 1UL);
  EXPECT_EQ(bid.trades.front().seller_order_id, 51121ULL);
  EXPECT_TRUE(has_open_order(f.engine, 51122));
}
