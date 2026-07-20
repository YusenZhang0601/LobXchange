#include "test_helpers/market_microstructure_helpers.hpp"

using namespace lobx_test;

TEST(FeePnlConservationInvariants, SpotTradeConservesBaseAsset) {
  SpotEngineFixture f;
  const auto before = total_asset(f.ledger, f.base_asset);
  EXPECT_TRUE(f.submit(f.alice, 55001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  EXPECT_TRUE(f.submit(f.bob, 55002, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);

  EXPECT_EQ(total_asset(f.ledger, f.base_asset), before);
}

TEST(FeePnlConservationInvariants, SpotTradeConservesQuoteIncludingDedicatedFeeAccount) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  const auto before = total_asset(f.ledger, f.quote_asset);
  EXPECT_TRUE(f.submit(f.alice, 55011, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  EXPECT_TRUE(f.submit(f.bob, 55012, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);

  EXPECT_EQ(total_asset(f.ledger, f.quote_asset), before);
}

TEST(FeePnlConservationInvariants, FeeAlwaysGoesToDedicatedFeeAccount) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  EXPECT_TRUE(f.submit(f.alice, 55021, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  EXPECT_TRUE(f.submit(f.bob, 55022, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);

  EXPECT_EQ(f.ledger.balance(dedicated_fee_account(), f.quote_asset).total, 1);
}

TEST(FeePnlConservationInvariants, UserZeroDoesNotReceiveFees) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  constexpr lobx::UserId user_zero = 0;
  EXPECT_TRUE(f.ledger.deposit(user_zero, f.quote_asset, 10).ok);
  const auto before = f.ledger.balance(user_zero, f.quote_asset).total;
  EXPECT_TRUE(f.submit(f.alice, 55031, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  EXPECT_TRUE(f.submit(f.bob, 55032, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);

  EXPECT_EQ(f.ledger.balance(user_zero, f.quote_asset).total, before);
  EXPECT_EQ(f.ledger.balance(dedicated_fee_account(), f.quote_asset).total, 1);
}

TEST(FeePnlConservationInvariants, FailedOrderDoesNotChargeFee) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);

  EXPECT_FALSE(f.submit(f.bob, 55041, lob::Side::Bid, 100, 1, 1u << 30, 1).accepted);

  EXPECT_EQ(f.ledger.balance(dedicated_fee_account(), f.quote_asset).total, 0);
}

TEST(FeePnlConservationInvariants, CanceledOrderDoesNotChargeFee) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  EXPECT_TRUE(f.submit(f.alice, 55051, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  EXPECT_TRUE(f.engine.cancel(55051, f.alice, 2));

  EXPECT_EQ(f.ledger.balance(dedicated_fee_account(), f.quote_asset).total, 0);
}

TEST(FeePnlConservationInvariants, ExpiredFOKDoesNotChargeFee) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);

  auto expired = f.submit(f.bob, 55061, lob::Side::Bid, 100, 1, lob::FOK, 1);

  EXPECT_TRUE(expired.accepted);
  EXPECT_EQ(expired.exec.filled, 0);
  EXPECT_EQ(f.ledger.balance(dedicated_fee_account(), f.quote_asset).total, 0);
}

TEST(FeePnlConservationInvariants, PartialFillChargesFeeOnlyOnFilledQty) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  EXPECT_TRUE(f.submit(f.alice, 55071, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto bid = f.submit(f.bob, 55072, lob::Side::Bid, 100, 3, lob::IOC, 2);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.exec.filled, 1);
  EXPECT_EQ(f.ledger.balance(dedicated_fee_account(), f.quote_asset).total, 1);
}

TEST(FeePnlConservationInvariants, MultiFillFeeEqualsSumOfFillFees) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  EXPECT_TRUE(f.submit(f.alice, 55081, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 55082, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);

  auto bid = f.submit(f.bob, 55083, lob::Side::Bid, 100, 2, lob::IOC, 3);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ(bid.trades.size(), 2UL);
  EXPECT_EQ(f.ledger.balance(dedicated_fee_account(), f.quote_asset).total, 2);
}

TEST(FeePnlConservationInvariants, FeeRoundingIsDeterministic) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/1);
  EXPECT_TRUE(f.submit(f.alice, 55091, lob::Side::Ask, 9999, 1, lob::POST_ONLY, 1).accepted);

  EXPECT_TRUE(f.submit(f.bob, 55092, lob::Side::Bid, 9999, 1, lob::IOC, 2).accepted);

  EXPECT_EQ(f.ledger.balance(dedicated_fee_account(), f.quote_asset).total, 0);
}

TEST(FeePnlConservationInvariants, RoundTripTradePnlIncludesFees) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  const auto quote_before = f.ledger.balance(f.alice, f.quote_asset).total;
  EXPECT_TRUE(f.submit(f.bob, 55101, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 55102, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.submit(f.carol, 55103, lob::Side::Bid, 110, 1, lob::POST_ONLY, 3).accepted);

  EXPECT_TRUE(f.submit(f.alice, 55104, lob::Side::Ask, 110, 1, lob::IOC, 4).accepted);

  EXPECT_EQ(f.ledger.balance(f.alice, f.quote_asset).total - quote_before, 8);
  EXPECT_EQ(f.ledger.balance(dedicated_fee_account(), f.quote_asset).total, 2);
}

TEST(FeePnlConservationInvariants, BotGrossPnlMinusFeesEqualsNetPnl) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  const auto quote_before = f.ledger.balance(f.alice, f.quote_asset).total;
  EXPECT_TRUE(f.submit(f.bob, 55111, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.alice, 55112, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.submit(f.carol, 55113, lob::Side::Bid, 110, 1, lob::POST_ONLY, 3).accepted);
  EXPECT_TRUE(f.submit(f.alice, 55114, lob::Side::Ask, 110, 1, lob::IOC, 4).accepted);

  const lobx::Amount gross_pnl = 10;
  const lobx::Amount fees = 2;
  const lobx::Amount net_pnl = f.ledger.balance(f.alice, f.quote_asset).total - quote_before;

  EXPECT_EQ(net_pnl, gross_pnl - fees);
}
