#include "test_helpers/market_microstructure_helpers.hpp"

using namespace lobx_test;

TEST(OrderBookStructure, OB001SinglePriceLevelAggregatesMultipleOrders) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 63001, lob::Side::Bid, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 63002, lob::Side::Bid, 100, 2, lob::POST_ONLY, 2).accepted);

  EXPECT_EQ(top_qty(f.engine, lob::Side::Bid, 100), 3);
  EXPECT_TRUE(has_open_order(f.engine, 63001));
  EXPECT_TRUE(has_open_order(f.engine, 63002));
  EXPECT_EQ(require_open_order(f.engine, 63001).leaves_qty, 1);
  EXPECT_EQ(require_open_order(f.engine, 63002).leaves_qty, 2);
  expect_topN_matches_open_orders(f.engine);
}

TEST(OrderBookStructure, OB002BidSideIsSortedDescending) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 63011, lob::Side::Bid, 99, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 63012, lob::Side::Bid, 101, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(f.carol, 63013, lob::Side::Bid, 100, 1, lob::POST_ONLY, 3).accepted);

  const auto bids = f.engine.topN(lob::Side::Bid, 3);
  EXPECT_EQ(bids.size(), 3UL);
  EXPECT_EQ(bids[0].first, 101);
  EXPECT_EQ(bids[1].first, 100);
  EXPECT_EQ(bids[2].first, 99);
  expect_topN_matches_open_orders(f.engine);
}

TEST(OrderBookStructure, OB003AskSideIsSortedAscending) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 63021, lob::Side::Ask, 103, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 63022, lob::Side::Ask, 101, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(f.carol, 63023, lob::Side::Ask, 102, 1, lob::POST_ONLY, 3).accepted);

  const auto asks = f.engine.topN(lob::Side::Ask, 3);
  EXPECT_EQ(asks.size(), 3UL);
  EXPECT_EQ(asks[0].first, 101);
  EXPECT_EQ(asks[1].first, 102);
  EXPECT_EQ(asks[2].first, 103);
  expect_topN_matches_open_orders(f.engine);
}

TEST(OrderBookStructure, OB004RemovingLastOrderDeletesPriceLevel) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 63031, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 63032, lob::Side::Ask, 101, 1, lob::POST_ONLY, 2).accepted);

  EXPECT_TRUE(f.engine.cancel(63031, f.alice, 3));

  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 0);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 101), 1);
  const auto asks = f.engine.topN(lob::Side::Ask, 2);
  EXPECT_EQ(asks.size(), 1UL);
  EXPECT_EQ(asks.front().first, 101);
  expect_topN_matches_open_orders(f.engine);
}

TEST(OrderBookStructure, OB005OB006BestBidAndAskUpdateAfterCancel) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 63041, lob::Side::Bid, 101, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 63042, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(f.alice, 63043, lob::Side::Ask, 102, 1, lob::POST_ONLY, 3).accepted);
  EXPECT_TRUE(f.submit(f.bob, 63044, lob::Side::Ask, 103, 1, lob::POST_ONLY, 4).accepted);

  EXPECT_TRUE(f.engine.cancel(63041, f.alice, 5));
  EXPECT_TRUE(f.engine.cancel(63043, f.alice, 6));

  const auto bids = f.engine.topN(lob::Side::Bid, 1);
  const auto asks = f.engine.topN(lob::Side::Ask, 1);
  EXPECT_EQ(bids.size(), 1UL);
  EXPECT_EQ(asks.size(), 1UL);
  EXPECT_EQ(bids.front().first, 100);
  EXPECT_EQ(asks.front().first, 103);
  EXPECT_TRUE(bids.front().first <= asks.front().first);
  expect_topN_matches_open_orders(f.engine);
}

TEST(OrderBookStructure, OB007DepthSnapshotAggregatesEachLevel) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 63051, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 63052, lob::Side::Ask, 100, 3, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(f.carol, 63053, lob::Side::Ask, 101, 4, lob::POST_ONLY, 3).accepted);
  EXPECT_TRUE(f.submit(f.alice, 63054, lob::Side::Bid, 99, 5, lob::POST_ONLY, 4).accepted);

  const auto asks = f.engine.topN(lob::Side::Ask, 5);
  const auto bids = f.engine.topN(lob::Side::Bid, 5);

  EXPECT_EQ(asks.size(), 2UL);
  EXPECT_EQ(asks[0].first, 100);
  EXPECT_EQ(asks[0].second, 5);
  EXPECT_EQ(asks[1].first, 101);
  EXPECT_EQ(asks[1].second, 4);
  EXPECT_EQ(bids.size(), 1UL);
  EXPECT_EQ(bids[0].first, 99);
  EXPECT_EQ(bids[0].second, 5);
  expect_topN_matches_open_orders(f.engine);
}

TEST(OrderBookStructure, OB008OB009OrderIdLookupInvalidatesAfterCancel) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 63061, lob::Side::Bid, 99, 2, lob::POST_ONLY, 1).accepted);

  EXPECT_TRUE(has_open_order(f.engine, 63061));
  EXPECT_EQ(require_open_order(f.engine, 63061).limit_price, 99);
  EXPECT_TRUE(f.engine.cancel(63061, f.alice, 2));
  EXPECT_FALSE(has_open_order(f.engine, 63061));
  EXPECT_FALSE(f.engine.cancel(63061, f.alice, 3));
  EXPECT_TRUE(f.engine.topN(lob::Side::Bid, 1).empty());
  expect_topN_matches_open_orders(f.engine);
}

TEST(OrderBookStructure, OB010BulkInsertCancelKeepsBookAndIndexConsistent) {
  SpotEngineFixture f;
  for (int i = 0; i < 200; ++i) {
    const lobx::OrderId id = static_cast<lobx::OrderId>(63070 + i);
    const lobx::UserId user = i % 3 == 0 ? f.alice : (i % 3 == 1 ? f.bob : f.carol);
    if (i % 2 == 0) {
      EXPECT_TRUE(f.submit(user, id, lob::Side::Bid, 80 + (i % 10), 1 + (i % 3), lob::POST_ONLY, i + 1).accepted);
    } else {
      EXPECT_TRUE(f.submit(user, id, lob::Side::Ask, 120 + (i % 10), 1 + (i % 3), lob::POST_ONLY, i + 1).accepted);
    }
  }

  for (int i = 0; i < 200; i += 3) {
    const lobx::OrderId id = static_cast<lobx::OrderId>(63070 + i);
    const lobx::UserId user = i % 3 == 0 ? f.alice : (i % 3 == 1 ? f.bob : f.carol);
    EXPECT_TRUE(f.engine.cancel(id, user, 1000 + i));
  }

  expect_topN_matches_open_orders(f.engine);
  for (const auto& order : f.engine.open_orders()) {
    EXPECT_TRUE(order.leaves_qty > 0);
    EXPECT_TRUE((order.flags & (lob::IOC | lob::FOK)) == 0u);
  }
  EXPECT_TRUE(f.ledger.invariant_ok());
}
