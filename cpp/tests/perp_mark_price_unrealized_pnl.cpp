#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(PerpMarkPrice, PERP_MARK_001SetGetIndexPrice) {
  auto f = ExchangeFixture::Perp();

  EXPECT_TRUE(f.exchange.set_index_price(f.perp_market_id, 101).ok);
  EXPECT_EQ(f.exchange.get_index_price(f.perp_market_id), 101);
}

TEST(PerpMarkPrice, PERP_MARK_002IndexPriceMustBePositive) {
  auto f = ExchangeFixture::Perp();

  auto result = f.exchange.set_index_price(f.perp_market_id, 0);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(result.code, lobx::RejectCode::InvalidPrice);
}

TEST(PerpMarkPrice, PERP_MARK_003MarkPriceUsesLastTrade) {
  auto f = ExchangeFixture::Perp();
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 70001, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 70002, lob::Side::Bid, 100, 2, lob::IOC, 2).accepted);

  EXPECT_EQ(f.exchange.mark_price(f.perp_market_id), 100);
}

TEST(PerpMarkPrice, PERP_MARK_004MarkPriceUsesMidPrice) {
  auto f = ExchangeFixture::Perp();
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 70011, lob::Side::Bid, 90, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.carol, 70012, lob::Side::Ask, 110, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.exchange.set_mark_price_mode(f.perp_market_id, lobx::MarkPriceMode::MidPrice).ok);

  EXPECT_EQ(f.exchange.mark_price(f.perp_market_id), 100);
}

TEST(PerpMarkPrice, PERP_MARK_005MarkPriceUsesIndexPrice) {
  auto f = ExchangeFixture::Perp();
  EXPECT_TRUE(f.exchange.set_index_price(f.perp_market_id, 123).ok);
  EXPECT_TRUE(f.exchange.set_mark_price_mode(f.perp_market_id, lobx::MarkPriceMode::IndexPrice).ok);

  EXPECT_EQ(f.exchange.mark_price(f.perp_market_id), 123);
}

TEST(PerpMarkPrice, PERP_MARK_006LongUnrealizedPnl) {
  auto f = ExchangeFixture::Perp();
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 70021, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 70022, lob::Side::Bid, 100, 4, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.exchange.set_index_price(f.perp_market_id, 110).ok);
  EXPECT_TRUE(f.exchange.set_mark_price_mode(f.perp_market_id, lobx::MarkPriceMode::IndexPrice).ok);

  EXPECT_EQ(f.exchange.unrealized_pnl(f.alice, f.perp_symbol), 40);
}

TEST(PerpMarkPrice, PERP_MARK_007ShortUnrealizedPnl) {
  auto f = ExchangeFixture::Perp();
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 70031, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 70032, lob::Side::Bid, 100, 4, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.exchange.set_index_price(f.perp_market_id, 90).ok);
  EXPECT_TRUE(f.exchange.set_mark_price_mode(f.perp_market_id, lobx::MarkPriceMode::IndexPrice).ok);

  EXPECT_EQ(f.exchange.unrealized_pnl(f.bob, f.perp_symbol), 40);
}

TEST(PerpMarkPrice, PERP_MARK_008FullCloseUnrealizedPnlIsZero) {
  auto f = ExchangeFixture::Perp();
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 70041, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 70042, lob::Side::Bid, 100, 4, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.carol, 70043, lob::Side::Bid, 110, 4, lob::POST_ONLY, 3).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 70044, lob::Side::Ask, 110, 4, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4).accepted);
  EXPECT_TRUE(f.exchange.set_index_price(f.perp_market_id, 120).ok);
  EXPECT_TRUE(f.exchange.set_mark_price_mode(f.perp_market_id, lobx::MarkPriceMode::IndexPrice).ok);

  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 0);
  EXPECT_EQ(f.exchange.unrealized_pnl(f.alice, f.perp_symbol), 0);
}
