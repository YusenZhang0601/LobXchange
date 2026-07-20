#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(MarketEngineOrderFlagsRegression, FOKWithoutSTPInsufficientLiquidityDoesNotPartiallyFill) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 44001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto buyer_quote_before = f.ledger.balance(f.bob, f.quote_asset);

  auto fok = f.submit(f.bob, 44002, lob::Side::Bid, 100, 2, lob::FOK, 2);

  EXPECT_TRUE(fok.accepted);
  EXPECT_EQ(fok.exec.filled, 0);
  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.quote_asset).total, buyer_quote_before.total, f.ledger_summary(f.bob));
  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.quote_asset).locked, buyer_quote_before.locked, f.ledger_summary(f.bob));
  EXPECT_EQ(f.engine.topN(lob::Side::Ask, 10)[0].second, 1);
}

TEST(MarketEngineOrderFlagsRegression, FOKWithSTPIgnoresSelfLiquidity) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 44011, lob::Side::Ask, 100, 5, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 44012, lob::Side::Ask, 100, 3, lob::POST_ONLY, 2).accepted);

  auto fok = f.submit(f.alice, 44013, lob::Side::Bid, 100, 4, lob::FOK | lob::STP, 3);

  EXPECT_TRUE(fok.accepted);
  EXPECT_EQ(fok.exec.filled, 0);
  EXPECT_TRUE(f.engine.topN(lob::Side::Bid, 10).empty());
  EXPECT_EQ(f.engine.topN(lob::Side::Ask, 10)[0].second, 8);
}

TEST(MarketEngineOrderFlagsRegression, FOKWithSTPAvailableExternalLiquiditySucceeds) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 44021, lob::Side::Ask, 100, 5, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 44022, lob::Side::Ask, 100, 5, lob::POST_ONLY, 2).accepted);

  auto fok = f.submit(f.alice, 44023, lob::Side::Bid, 100, 5, lob::FOK | lob::STP, 3);

  EXPECT_TRUE_MSG(fok.accepted, "FOK+STP should fill when external liquidity is sufficient reason=" + fok.reason);
  EXPECT_EQ(fok.exec.filled, 5);
  EXPECT_EQ(f.ledger.balance(f.alice, f.base_asset).total, 1000005LL);
  EXPECT_TRUE(f.engine.topN(lob::Side::Bid, 10).empty());
}

TEST(MarketEngineOrderFlagsRegression, IOCNoFillReleasesAllLock) {
  SpotEngineFixture f;
  const auto quote_before = f.ledger.balance(f.bob, f.quote_asset);

  auto ioc = f.submit(f.bob, 44031, lob::Side::Bid, 100, 1, lob::IOC, 1);

  EXPECT_TRUE(ioc.accepted);
  EXPECT_EQ(ioc.exec.filled, 0);
  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.quote_asset).total, quote_before.total, f.ledger_summary(f.bob));
  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.quote_asset).locked, quote_before.locked, f.ledger_summary(f.bob));
  EXPECT_TRUE(f.engine.topN(lob::Side::Bid, 10).empty());
}

TEST(MarketEngineOrderFlagsRegression, IOCPostOnlyCrossingOrderRejectedOrDoesNotTakeLiquidity) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 44041, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto cross = f.submit(f.bob, 44042, lob::Side::Bid, 100, 1, lob::IOC | lob::POST_ONLY, 2);

  EXPECT_FALSE(cross.accepted);
  EXPECT_EQ(cross.code, lobx::RejectCode::PostOnlyWouldCross);
  EXPECT_EQ(f.engine.topN(lob::Side::Ask, 10)[0].second, 1);
}

TEST(MarketEngineOrderFlagsRegression, STPMixedSelfAndExternalLiquidityOnlyUsesExternal) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 44051, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.bob, 44052, lob::Side::Ask, 100, 3, lob::POST_ONLY, 2).accepted);

  auto ioc = f.submit(f.alice, 44053, lob::Side::Bid, 100, 5, lob::IOC | lob::STP, 3);

  EXPECT_TRUE(ioc.accepted);
  EXPECT_EQ(ioc.exec.filled, 3);
  EXPECT_EQ(f.ledger.balance(f.alice, f.base_asset).total, 1000003LL);
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 10).empty());
}
