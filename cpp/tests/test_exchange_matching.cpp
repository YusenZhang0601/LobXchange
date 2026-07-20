#include "test_helpers/market_microstructure_helpers.hpp"

using namespace lobx_test;

namespace {

constexpr lobx::UserId dave = 40;

void deposit_dave(SpotEngineFixture& f) {
  deposit_spot_user(f, dave);
}

void expect_legal_book(SpotEngineFixture& f) {
  const auto bids = f.engine.topN(lob::Side::Bid, 1);
  const auto asks = f.engine.topN(lob::Side::Ask, 1);
  if (!bids.empty() && !asks.empty()) {
    EXPECT_TRUE_MSG(bids.front().first <= asks.front().first,
                    "best_bid must not exceed best_ask");
  }
  expect_book_matches_open_orders(f);
}

} // namespace

TEST(ExchangeMatching, EX001EmptyBookHasNoBestBidOrAsk) {
  SpotEngineFixture f;

  EXPECT_TRUE(f.engine.topN(lob::Side::Bid, 1).empty());
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 1).empty());
  EXPECT_TRUE(f.engine.open_orders().empty());
}

TEST(ExchangeMatching, EX002EX003PostOnlyBuyAndSellRestInBook) {
  SpotEngineFixture f;

  auto buy = f.submit(f.bob, 61001, lob::Side::Bid, 99, 3, lob::POST_ONLY, 1);
  auto sell = f.submit(f.alice, 61002, lob::Side::Ask, 101, 4, lob::POST_ONLY, 2);

  EXPECT_TRUE_MSG(buy.accepted, buy.reason);
  EXPECT_TRUE_MSG(sell.accepted, sell.reason);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Bid, 99), 3);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 101), 4);
  EXPECT_TRUE(has_open_order(f.engine, 61001));
  EXPECT_TRUE(has_open_order(f.engine, 61002));
  EXPECT_EQ(count_events(f.events, "trade"), 0);
  expect_legal_book(f);
}

TEST(ExchangeMatching, EX004BuyIOCTradesAtRestingAskPrice) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 61011, lob::Side::Ask, 100, 5, lob::POST_ONLY, 1).accepted);

  auto buy = f.submit(f.bob, 61012, lob::Side::Bid, 105, 2, lob::IOC, 2);

  EXPECT_TRUE_MSG(buy.accepted, buy.reason);
  EXPECT_EQ(buy.exec.filled, 2);
  EXPECT_EQ(buy.exec.remaining, 0);
  EXPECT_EQ(buy.trades.size(), 1UL);
  expect_trade(buy.trades.front(), f.bob, f.alice, 61012, 61011, 100, 2);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 3);
  EXPECT_FALSE(has_open_order(f.engine, 61012));
  expect_legal_book(f);
}

TEST(ExchangeMatching, EX005SellIOCTradesAtRestingBidPrice) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 61021, lob::Side::Bid, 100, 5, lob::POST_ONLY, 1).accepted);

  auto sell = f.submit(f.bob, 61022, lob::Side::Ask, 95, 2, lob::IOC, 2);

  EXPECT_TRUE_MSG(sell.accepted, sell.reason);
  EXPECT_EQ(sell.exec.filled, 2);
  EXPECT_EQ(sell.trades.size(), 1UL);
  expect_trade(sell.trades.front(), f.alice, f.bob, 61021, 61022, 100, 2);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Bid, 100), 3);
  EXPECT_FALSE(has_open_order(f.engine, 61022));
  expect_legal_book(f);
}

TEST(ExchangeMatching, EX006EX007IOCNoFillAndPartialFillNeverRest) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 61031, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);

  auto no_fill = f.submit(f.bob, 61032, lob::Side::Bid, 99, 5, lob::IOC, 2);
  EXPECT_TRUE_MSG(no_fill.accepted, no_fill.reason);
  EXPECT_EQ(no_fill.exec.filled, 0);
  EXPECT_EQ(no_fill.exec.remaining, 5);
  EXPECT_TRUE(no_fill.trades.empty());
  EXPECT_FALSE(has_open_order(f.engine, 61032));
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 2);

  auto partial = f.submit(f.bob, 61033, lob::Side::Bid, 100, 5, lob::IOC, 3);
  EXPECT_TRUE_MSG(partial.accepted, partial.reason);
  EXPECT_EQ(partial.exec.filled, 2);
  EXPECT_EQ(partial.exec.remaining, 3);
  EXPECT_EQ(partial.trades.size(), 1UL);
  EXPECT_FALSE(has_open_order(f.engine, 61033));
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 1).empty());
  expect_legal_book(f);
}

TEST(ExchangeMatching, EX008EX009PostOnlyCrossingOrdersAreRejected) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 61041, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  auto crossing_buy = f.submit(f.bob, 61042, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2);
  EXPECT_FALSE(crossing_buy.accepted);
  EXPECT_EQ(crossing_buy.code, lobx::RejectCode::PostOnlyWouldCross);
  EXPECT_TRUE(crossing_buy.trades.empty());
  EXPECT_FALSE(has_open_order(f.engine, 61042));
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 1);

  SpotEngineFixture g;
  EXPECT_TRUE(g.submit(g.alice, 61043, lob::Side::Bid, 100, 1, lob::POST_ONLY, 1).accepted);
  auto crossing_sell = g.submit(g.bob, 61044, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2);
  EXPECT_FALSE(crossing_sell.accepted);
  EXPECT_EQ(crossing_sell.code, lobx::RejectCode::PostOnlyWouldCross);
  EXPECT_TRUE(crossing_sell.trades.empty());
  EXPECT_FALSE(has_open_order(g.engine, 61044));
  EXPECT_EQ(top_qty(g.engine, lob::Side::Bid, 100), 1);
  expect_legal_book(f);
  expect_legal_book(g);
}

TEST(ExchangeMatching, EX010EX012BuySweepsLowestAsksInPriceThenFIFOOrder) {
  SpotEngineFixture f;
  deposit_dave(f);
  EXPECT_TRUE(f.submit(f.alice, 61051, lob::Side::Ask, 101, 2, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 61052, lob::Side::Ask, 99, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, 61053, lob::Side::Ask, 101, 3, lob::POST_ONLY, 3).accepted);

  auto buy = f.submit(f.bob, 61054, lob::Side::Bid, 101, 5, lob::IOC, 4);

  EXPECT_TRUE_MSG(buy.accepted, buy.reason);
  EXPECT_EQ(buy.exec.filled, 5);
  EXPECT_EQ(buy.trades.size(), 3UL);
  expect_trade(buy.trades[0], f.bob, f.carol, 61054, 61052, 99, 1);
  expect_trade(buy.trades[1], f.bob, f.alice, 61054, 61051, 101, 2);
  expect_trade(buy.trades[2], f.bob, dave, 61054, 61053, 101, 2);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 101), 1);
  expect_legal_book(f);
}

TEST(ExchangeMatching, EX010SellSweepsHighestBidsInPriceOrder) {
  SpotEngineFixture f;
  deposit_dave(f);
  EXPECT_TRUE(f.submit(f.alice, 61061, lob::Side::Bid, 100, 2, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 61062, lob::Side::Bid, 102, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, 61063, lob::Side::Bid, 99, 3, lob::POST_ONLY, 3).accepted);

  auto sell = f.submit(f.bob, 61064, lob::Side::Ask, 99, 4, lob::IOC, 4);

  EXPECT_TRUE_MSG(sell.accepted, sell.reason);
  EXPECT_EQ(sell.exec.filled, 4);
  EXPECT_EQ(sell.trades.size(), 3UL);
  expect_trade(sell.trades[0], f.carol, f.bob, 61062, 61064, 102, 1);
  expect_trade(sell.trades[1], f.alice, f.bob, 61061, 61064, 100, 2);
  expect_trade(sell.trades[2], dave, f.bob, 61063, 61064, 99, 1);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Bid, 99), 2);
  expect_legal_book(f);
}

TEST(ExchangeMatching, EX011SamePriceFIFOUsesEarlierRestingOrderFirst) {
  SpotEngineFixture f;
  deposit_dave(f);
  EXPECT_TRUE(f.submit(f.alice, 61071, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 61072, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, 61073, lob::Side::Ask, 100, 1, lob::POST_ONLY, 3).accepted);

  auto buy = f.submit(f.bob, 61074, lob::Side::Bid, 100, 2, lob::IOC, 4);

  EXPECT_TRUE_MSG(buy.accepted, buy.reason);
  EXPECT_EQ(buy.trades.size(), 2UL);
  EXPECT_EQ(buy.trades[0].seller_order_id, 61071ULL);
  EXPECT_EQ(buy.trades[1].seller_order_id, 61072ULL);
  EXPECT_TRUE(has_open_order(f.engine, 61073));
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 1);
  expect_legal_book(f);
}

TEST(ExchangeMatching, EX013EX014CancelSuccessAndMissingCancelIsNoop) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 61081, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);
  const auto before_missing = f.engine.topN(lob::Side::Ask, 10);

  EXPECT_FALSE(f.engine.cancel(999999, f.alice, 2));
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 10) == before_missing);
  EXPECT_TRUE(f.engine.cancel(61081, f.alice, 3));
  EXPECT_FALSE(has_open_order(f.engine, 61081));
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 1).empty());
  EXPECT_FALSE(f.engine.cancel(61081, f.alice, 4));
  expect_legal_book(f);
}

TEST(ExchangeMatching, EX015FreshEngineResetStartsWithEmptyBookAndStats) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 61091, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_FALSE(f.engine.topN(lob::Side::Ask, 1).empty());

  SpotEngineFixture reset;
  EXPECT_TRUE(reset.engine.topN(lob::Side::Bid, 1).empty());
  EXPECT_TRUE(reset.engine.topN(lob::Side::Ask, 1).empty());
  EXPECT_TRUE(reset.engine.open_orders().empty());
  EXPECT_EQ(count_events(reset.events, "trade"), 0);
}
