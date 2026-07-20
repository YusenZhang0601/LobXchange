#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(PriceQtyValidationTest, NonPositivePriceIsRejected) {
  auto f = ExchangeFixture::Spot();
  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 10, lob::Side::Bid, 0, 1, lob::NONE, 1);
  EXPECT_FALSE(order.accepted);
  EXPECT_EQ(order.code, lobx::RejectCode::InvalidPrice);
}

TEST(PriceQtyValidationTest, NonPositiveQuantityIsRejected) {
  auto f = ExchangeFixture::Spot();
  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 11, lob::Side::Bid, 100, 0, lob::NONE, 1);
  EXPECT_FALSE(order.accepted);
  EXPECT_EQ(order.code, lobx::RejectCode::InvalidQuantity);
}

TEST(PriceQtyValidationTest, TickSizeMisalignmentIsRejected) {
  lobx::Exchange ex;
  EXPECT_TRUE(ex.issue_asset("USDT", 6, 1000000, 1, 0).ok);
  EXPECT_TRUE(ex.issue_asset("BTC", 8, 1000000, 1, 0).ok);
  EXPECT_TRUE(ex.create_spot_market("BTC-USDT", "BTC", "USDT", 5, 1, 1, 1).ok);
  EXPECT_TRUE(ex.deposit(10, "USDT", 100000).ok);
  auto order = ex.submit_limit("BTC-USDT", 10, 12, lob::Side::Bid, 101, 1, lob::NONE, 1);
  EXPECT_FALSE(order.accepted);
  EXPECT_EQ(order.code, lobx::RejectCode::InvalidPrice);
}

TEST(PriceQtyValidationTest, LotSizeMisalignmentIsRejected) {
  lobx::Exchange ex;
  EXPECT_TRUE(ex.issue_asset("USDT", 6, 1000000, 1, 0).ok);
  EXPECT_TRUE(ex.issue_asset("BTC", 8, 1000000, 1, 0).ok);
  EXPECT_TRUE(ex.create_spot_market("BTC-USDT", "BTC", "USDT", 1, 5, 1, 1).ok);
  EXPECT_TRUE(ex.deposit(10, "USDT", 100000).ok);
  auto order = ex.submit_limit("BTC-USDT", 10, 13, lob::Side::Bid, 100, 3, lob::NONE, 1);
  EXPECT_FALSE(order.accepted);
  EXPECT_EQ(order.code, lobx::RejectCode::InvalidQuantity);
}

TEST(PriceQtyValidationTest, MinNotionalIsEnforced) {
  lobx::Exchange ex;
  EXPECT_TRUE(ex.issue_asset("USDT", 6, 1000000, 1, 0).ok);
  EXPECT_TRUE(ex.issue_asset("BTC", 8, 1000000, 1, 0).ok);
  EXPECT_TRUE(ex.create_spot_market("BTC-USDT", "BTC", "USDT", 1, 1, 1, 1000).ok);
  EXPECT_TRUE(ex.deposit(10, "USDT", 100000).ok);
  auto order = ex.submit_limit("BTC-USDT", 10, 14, lob::Side::Bid, 100, 1, lob::NONE, 1);
  EXPECT_FALSE(order.accepted);
  EXPECT_EQ(order.code, lobx::RejectCode::InvalidNotional);
}

TEST(PriceQtyValidationTest, UnknownSymbolIsRejected) {
  auto f = ExchangeFixture::Spot();
  auto order = f.exchange.submit_limit("UNKNOWN", f.alice, 15, lob::Side::Bid, 100, 1, lob::NONE, 1);
  EXPECT_FALSE(order.accepted);
  EXPECT_EQ(order.code, lobx::RejectCode::UnknownMarket);
}

TEST(PriceQtyValidationTest, HaltedMarketRejectsNewOrder) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.markets().halt(1).ok);
  auto order = f.exchange.submit_limit(f.spot_symbol, f.alice, 16, lob::Side::Bid, 100, 1, lob::NONE, 1);
  EXPECT_FALSE(order.accepted);
  EXPECT_EQ(order.code, lobx::RejectCode::MarketNotActive);
}
