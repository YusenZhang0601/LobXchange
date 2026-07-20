#include "test_helpers/market_microstructure_helpers.hpp"

using namespace lobx_test;

namespace {

constexpr lobx::UserId dave = 40;

void deposit_dave(SpotEngineFixture& f) {
  deposit_spot_user(f, dave);
}

void rest_three_same_price_asks(SpotEngineFixture& f,
                                lobx::OrderId alice_order,
                                lobx::OrderId carol_order,
                                lobx::OrderId dave_order) {
  deposit_dave(f);
  EXPECT_TRUE(f.submit(f.alice, alice_order, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, carol_order, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, dave_order, lob::Side::Ask, 100, 1, lob::POST_ONLY, 3).accepted);
}

} // namespace

TEST(MarketMicrostructureRegression, MakerPriceUsedForTradeExecution) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 50001, lob::Side::Ask, 90, 1, lob::POST_ONLY, 1).accepted);

  auto bid = f.submit(f.bob, 50002, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(bid.trades.size(), 1UL);
  expect_trade(bid.trades.front(), f.bob, f.alice, 50002, 50001, 90, 1);
  EXPECT_TRUE(f.engine.open_orders().empty());
  EXPECT_EQ(count_events(f.events, "trade"), 1);
}

TEST(MarketMicrostructureRegression, BidTakerAgainstAskMakerExecutesAtAskPrice) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 50011, lob::Side::Ask, 90, 1, lob::POST_ONLY, 1).accepted);

  auto bid = f.submit(f.bob, 50012, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.trades.size(), 1UL);
  expect_trade(bid.trades.front(), f.bob, f.alice, 50012, 50011, 90, 1);
  EXPECT_TRUE(f.engine.open_orders().empty());
  EXPECT_EQ(count_events(f.events, "trade"), 1);
}

TEST(MarketMicrostructureRegression, AskTakerAgainstBidMakerExecutesAtBidPrice) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 50021, lob::Side::Bid, 100, 1, lob::POST_ONLY, 1).accepted);

  auto ask = f.submit(f.bob, 50022, lob::Side::Ask, 90, 1, lob::IOC, 2);

  EXPECT_TRUE(ask.accepted);
  EXPECT_EQ(ask.trades.size(), 1UL);
  expect_trade(ask.trades.front(), f.alice, f.bob, 50021, 50022, 100, 1);
  EXPECT_TRUE(f.engine.open_orders().empty());
  EXPECT_EQ(count_events(f.events, "trade"), 1);
}

TEST(MarketMicrostructureRegression, SamePriceFIFOPreservedAcrossFills) {
  SpotEngineFixture f;
  rest_three_same_price_asks(f, 57001, 57002, 57003);

  auto bid = f.submit(f.bob, 57004, lob::Side::Bid, 100, 2, lob::IOC, 4);

  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(bid.trades.size(), 2UL);
  EXPECT_EQ(bid.trades[0].seller_order_id, 57001ULL);
  EXPECT_EQ(bid.trades[1].seller_order_id, 57002ULL);
  EXPECT_TRUE(has_open_order(f, 57003));
  expect_top_level(f, lob::Side::Ask, 100, 1);
}

TEST(MarketMicrostructureRegression, SamePriceFIFOPreservedAfterCancel) {
  SpotEngineFixture f;
  rest_three_same_price_asks(f, 57011, 57012, 57013);
  EXPECT_TRUE(f.engine.cancel(57012, f.carol, 4));

  auto bid = f.submit(f.bob, 57014, lob::Side::Bid, 100, 2, lob::IOC, 5);

  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(bid.trades.size(), 2UL);
  EXPECT_EQ(bid.trades[0].seller_order_id, 57011ULL);
  EXPECT_EQ(bid.trades[1].seller_order_id, 57013ULL);
  EXPECT_FALSE(has_open_order(f, 57012));
  EXPECT_TRUE(f.engine.open_orders().empty());
}

TEST(MarketMicrostructureRegression, SamePriceFIFOPreservedAfterFailedSubmitRollback) {
  SpotEngineFixture f;
  rest_three_same_price_asks(f, 57021, 57022, 57023);
  auto rejected = f.submit(f.bob, 57024, lob::Side::Bid, 100, 1, lob::POST_ONLY, 4);
  EXPECT_FALSE(rejected.accepted);

  auto bid = f.submit(f.bob, 57025, lob::Side::Bid, 100, 2, lob::IOC, 5);

  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(bid.trades.size(), 2UL);
  EXPECT_EQ(bid.trades[0].seller_order_id, 57021ULL);
  EXPECT_EQ(bid.trades[1].seller_order_id, 57022ULL);
  EXPECT_TRUE(has_open_order(f, 57023));
  expect_top_level(f, lob::Side::Ask, 100, 1);
}

TEST(MarketMicrostructureRegression, IOCConsumesExpectedDepthAcrossLevels) {
  SpotEngineFixture f;
  deposit_dave(f);
  EXPECT_TRUE(f.submit(f.alice, 50031, lob::Side::Ask, 90, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 50032, lob::Side::Ask, 95, 2, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, 50033, lob::Side::Ask, 100, 3, lob::POST_ONLY, 3).accepted);

  auto bid = f.submit(f.bob, 50034, lob::Side::Bid, 100, 4, lob::IOC, 4);

  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(bid.exec.filled, 4);
  EXPECT_EQ(bid.trades.size(), 3UL);
  EXPECT_EQ(bid.trades[0].price, 90);
  EXPECT_EQ(bid.trades[0].qty, 1);
  EXPECT_EQ(bid.trades[1].price, 95);
  EXPECT_EQ(bid.trades[1].qty, 2);
  EXPECT_EQ(bid.trades[2].price, 100);
  EXPECT_EQ(bid.trades[2].qty, 1);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 2);
  EXPECT_FALSE(has_open_order(f.engine, 50034));
}

TEST(MarketMicrostructureRegression, IOCBidConsumesExpectedAskDepthAcrossLevels) {
  SpotEngineFixture f;
  deposit_dave(f);
  const auto bob_base_before = f.ledger.balance(f.bob, f.base_asset).total;
  const auto alice_quote_before = f.ledger.balance(f.alice, f.quote_asset).total;
  const auto carol_quote_before = f.ledger.balance(f.carol, f.quote_asset).total;
  const auto dave_quote_before = f.ledger.balance(dave, f.quote_asset).total;
  EXPECT_TRUE(f.submit(f.alice, 57101, lob::Side::Ask, 90, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 57102, lob::Side::Ask, 95, 2, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, 57103, lob::Side::Ask, 100, 3, lob::POST_ONLY, 3).accepted);

  auto bid = f.submit(f.bob, 57104, lob::Side::Bid, 100, 4, lob::IOC, 4);

  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(bid.trades.size(), 3UL);
  expect_trade(bid.trades[0], f.bob, f.alice, 57104, 57101, 90, 1);
  expect_trade(bid.trades[1], f.bob, f.carol, 57104, 57102, 95, 2);
  expect_trade(bid.trades[2], f.bob, dave, 57104, 57103, 100, 1);
  expect_top_level(f, lob::Side::Ask, 100, 2);
  EXPECT_FALSE(has_open_order(f, 57104));
  EXPECT_EQ(f.ledger.balance(f.bob, f.base_asset).total, bob_base_before + 4);
  EXPECT_EQ(f.ledger.balance(f.alice, f.quote_asset).total, alice_quote_before + 90);
  EXPECT_EQ(f.ledger.balance(f.carol, f.quote_asset).total, carol_quote_before + 190);
  EXPECT_EQ(f.ledger.balance(dave, f.quote_asset).total, dave_quote_before + 100);
  EXPECT_EQ(count_events(f.events, "trade"), 3);
}

TEST(MarketMicrostructureRegression, IOCAskConsumesExpectedBidDepthAcrossLevels) {
  SpotEngineFixture f;
  deposit_dave(f);
  EXPECT_TRUE(f.submit(f.alice, 57111, lob::Side::Bid, 110, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 57112, lob::Side::Bid, 105, 2, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, 57113, lob::Side::Bid, 100, 3, lob::POST_ONLY, 3).accepted);

  auto ask = f.submit(f.bob, 57114, lob::Side::Ask, 100, 4, lob::IOC, 4);

  EXPECT_TRUE_MSG(ask.accepted, ask.reason);
  EXPECT_EQ(ask.trades.size(), 3UL);
  expect_trade(ask.trades[0], f.alice, f.bob, 57111, 57114, 110, 1);
  expect_trade(ask.trades[1], f.carol, f.bob, 57112, 57114, 105, 2);
  expect_trade(ask.trades[2], dave, f.bob, 57113, 57114, 100, 1);
  expect_top_level(f, lob::Side::Bid, 100, 2);
  EXPECT_FALSE(has_open_order(f, 57114));
  EXPECT_EQ(count_events(f.events, "trade"), 3);
}

TEST(MarketMicrostructureRegression, IOCPartialFillDoesNotRest) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 50041, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto bid = f.submit(f.bob, 50042, lob::Side::Bid, 100, 3, lob::IOC, 2);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.exec.filled, 1);
  EXPECT_EQ(bid.exec.remaining, 2);
  EXPECT_FALSE(has_open_order(f.engine, 50042));
  EXPECT_EQ(f.ledger.locked(f.bob, f.quote_asset), 0);
  expect_no_locked_balance(f, f.bob, f.quote_asset);
  EXPECT_FALSE(has_open_order(f, 50042));
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 0);
}

TEST(MarketMicrostructureRegression, IOCNoFillReleasesAllLock) {
  SpotEngineFixture f;

  auto bid = f.submit(f.bob, 50051, lob::Side::Bid, 100, 2, lob::IOC, 1);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.exec.filled, 0);
  EXPECT_FALSE(has_open_order(f.engine, 50051));
  expect_no_locked_balance(f, f.bob, f.quote_asset);
  EXPECT_EQ(count_events(f.events, "trade"), 0);
}

TEST(MarketMicrostructureRegression, FOKWithoutSTPInsufficientLiquidityExpiresWithoutPartialFill) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 50061, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const int trades_before = event_count(f.events, "trade");

  auto bid = f.submit(f.bob, 50062, lob::Side::Bid, 100, 2, lob::FOK, 2);

  EXPECT_TRUE_MSG(bid.accepted, bid.reason);
  EXPECT_EQ(bid.exec.filled, 0);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 1);
  expect_no_locked_balance(f, f.bob, f.quote_asset);
  EXPECT_EQ(event_count(f.events, "trade"), trades_before);
  EXPECT_EQ(event_count(f.events, "order.expired"), 1);
  EXPECT_FALSE(has_open_order(f, 50062));
}

TEST(MarketMicrostructureRegression, FOKWithSTPIgnoresSelfLiquidity) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 50071, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 50072, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);

  auto bid = f.submit(f.alice, 50073, lob::Side::Bid, 100, 2, lob::FOK | lob::STP, 3);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.exec.filled, 0);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 2);
  EXPECT_TRUE(has_open_order(f, 50071));
  EXPECT_TRUE(has_open_order(f, 50072));
  EXPECT_FALSE(has_open_order(f, 50073));
  EXPECT_EQ(count_events(f.events, "trade"), 0);
}

TEST(MarketMicrostructureRegression, FOKFullLiquiditySucceeds) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 50081, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 50082, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);

  auto bid = f.submit(f.bob, 50083, lob::Side::Bid, 100, 2, lob::FOK, 3);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.exec.filled, 2);
  EXPECT_EQ(bid.trades.size(), 2UL);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 0);
  EXPECT_FALSE(has_open_order(f, 50083));
  EXPECT_EQ(count_events(f.events, "trade"), 2);
}

TEST(MarketMicrostructureRegression, PostOnlyCrossingExternalLiquidityRejected) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 50091, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto bid = f.submit(f.bob, 50092, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2);

  EXPECT_FALSE(bid.accepted);
  EXPECT_EQ(bid.code, lobx::RejectCode::PostOnlyWouldCross);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 1);
  EXPECT_FALSE(has_open_order(f, 50092));
  expect_no_locked_balance(f, f.bob, f.quote_asset);
  EXPECT_EQ(count_events(f.events, "order.rejected"), 1);
}

TEST(MarketMicrostructureRegression, PostOnlyNonCrossingRestsInBook) {
  SpotEngineFixture f;

  EXPECT_TRUE(f.submit(f.alice, 50100, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto bid = f.submit(f.bob, 50101, lob::Side::Bid, 99, 2, lob::POST_ONLY, 2);

  EXPECT_TRUE(bid.accepted);
  EXPECT_TRUE(has_open_order(f.engine, 50101));
  EXPECT_EQ(top_qty(f.engine, lob::Side::Bid, 99), 2);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 1);
  EXPECT_EQ(count_events(f.events, "trade"), 0);
}

TEST(MarketMicrostructureRegression, STPSelfLiquidityCannotFillOwnOrder) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 50111, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto bid = f.submit(f.alice, 50112, lob::Side::Bid, 100, 1, lob::IOC | lob::STP, 2);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.exec.filled, 0);
  EXPECT_TRUE(bid.trades.empty());
  EXPECT_FALSE(has_open_order(f, 50112));
  // Current STP mode cancels the resting self order to prevent self-trade.
  EXPECT_FALSE(has_open_order(f.engine, 50111));
  EXPECT_EQ(count_events(f.events, "trade"), 0);
}

TEST(MarketMicrostructureRegression, STPMixedSelfAndExternalLiquidityOnlyUsesExternal) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 50121, lob::Side::Ask, 90, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 50122, lob::Side::Ask, 95, 1, lob::POST_ONLY, 2).accepted);

  auto bid = f.submit(f.alice, 50123, lob::Side::Bid, 100, 2, lob::IOC | lob::STP, 3);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.exec.filled, 1);
  EXPECT_EQ(bid.trades.size(), 1UL);
  EXPECT_EQ(bid.trades.front().seller, f.carol);
  expect_trade(bid.trades.front(), f.alice, f.carol, 50123, 50122, 95, 1);
  // Current STP mode cancels the lower-priced resting self order before using external liquidity.
  EXPECT_FALSE(has_open_order(f.engine, 50121));
  EXPECT_FALSE(has_open_order(f, 50123));
  EXPECT_EQ(count_events(f.events, "trade"), 1);
}

TEST(MarketMicrostructureRegression, BookTopNReflectsAggregatedPriceLevels) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 50131, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 50132, lob::Side::Ask, 100, 2, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(f.bob, 50133, lob::Side::Bid, 90, 3, lob::POST_ONLY, 3).accepted);

  expect_topN_matches_open_orders(f.engine);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 3);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Bid, 90), 3);
}

TEST(MarketMicrostructureRegression, BookTopNMatchesOpenOrdersAfterMixedOperations) {
  SpotEngineFixture f;
  deposit_dave(f);
  EXPECT_TRUE(f.submit(f.alice, 57201, lob::Side::Ask, 100, 3, lob::POST_ONLY, 1).accepted);
  expect_book_matches_open_orders(f);
  EXPECT_TRUE(f.submit(f.carol, 57202, lob::Side::Ask, 101, 2, lob::POST_ONLY, 2).accepted);
  expect_book_matches_open_orders(f);
  EXPECT_TRUE(f.submit(dave, 57203, lob::Side::Bid, 95, 4, lob::POST_ONLY, 3).accepted);
  expect_book_matches_open_orders(f);

  auto partial = f.submit(f.bob, 57204, lob::Side::Bid, 100, 2, lob::IOC, 4);
  EXPECT_TRUE_MSG(partial.accepted, partial.reason);
  EXPECT_EQ(partial.exec.filled, 2);
  expect_book_matches_open_orders(f);

  EXPECT_TRUE(f.engine.cancel(57202, f.carol, 5));
  expect_book_matches_open_orders(f);

  auto no_fill = f.submit(f.bob, 57205, lob::Side::Ask, 110, 1, lob::IOC, 6);
  EXPECT_TRUE(no_fill.accepted);
  EXPECT_TRUE(no_fill.trades.empty());
  expect_book_matches_open_orders(f);

  auto fok_expired = f.submit(f.bob, 57206, lob::Side::Bid, 100, 10, lob::FOK, 7);
  EXPECT_TRUE(fok_expired.accepted);
  EXPECT_EQ(fok_expired.exec.filled, 0);
  expect_book_matches_open_orders(f);

  auto post_only_reject = f.submit(f.bob, 57207, lob::Side::Bid, 100, 1, lob::POST_ONLY, 8);
  EXPECT_FALSE(post_only_reject.accepted);
  expect_book_matches_open_orders(f);
}
