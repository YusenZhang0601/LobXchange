#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(OrderFlagsTest, LegalFlagsAreAccepted) {
  auto f = ExchangeFixture::Spot();
  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 1, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1);
  EXPECT_TRUE_MSG(order.accepted, "user=alice symbol=BTC-USDT order_id=1 side=SELL price=100 qty=1 flags=POST_ONLY reason=" + order.reason);
}

TEST(OrderFlagsTest, UnknownFlagBitsAreRejected) {
  auto f = ExchangeFixture::Spot();
  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 2, lob::Side::Ask, 100, 1, 1u << 30, 1);
  EXPECT_FALSE_MSG(order.accepted, "user=alice symbol=BTC-USDT order_id=2 side=SELL price=100 qty=1 flags=UNKNOWN");
  EXPECT_EQ(order.code, lobx::RejectCode::UnsupportedOrderType);
}

TEST(OrderFlagsTest, UnknownFlagMixedWithLegalFlagIsRejected) {
  auto f = ExchangeFixture::Spot();
  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 3, lob::Side::Ask, 100, 1, lob::POST_ONLY | (1u << 30), 1);
  EXPECT_FALSE(order.accepted);
  EXPECT_EQ(order.code, lobx::RejectCode::UnsupportedOrderType);
}

TEST(OrderFlagsTest, ReduceOnlySpotOrdersAreRejected) {
  auto f = ExchangeFixture::Spot();
  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 4, lob::Side::Ask, 100, 1, lobx::LOBX_REDUCE_ONLY, 1);
  EXPECT_FALSE(order.accepted);
  EXPECT_EQ(order.code, lobx::RejectCode::ReduceOnlyUnsupported);
}

TEST(OrderFlagsTest, PostOnlyIocNonCrossingOrderHasExplicitCancelRemainderSemantics) {
  auto f = ExchangeFixture::Spot();
  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 5, lob::Side::Ask, 100, 1, lob::POST_ONLY | lob::IOC, 1);
  EXPECT_TRUE(order.accepted);
  EXPECT_EQ(order.exec.filled, 0);
  EXPECT_EQ(order.exec.remaining, 1);
  EXPECT_TRUE(f.exchange.topN(f.spot_symbol, lob::Side::Ask, 10).empty());
}

TEST(OrderFlagsTest, PostOnlyFokNonCrossingOrderHasExplicitCancelRemainderSemantics) {
  auto f = ExchangeFixture::Spot();
  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 6, lob::Side::Ask, 100, 1, lob::POST_ONLY | lob::FOK, 1);
  EXPECT_TRUE(order.accepted);
  EXPECT_EQ(order.exec.filled, 0);
  EXPECT_EQ(order.exec.remaining, 1);
  EXPECT_TRUE(f.exchange.topN(f.spot_symbol, lob::Side::Ask, 10).empty());
}
