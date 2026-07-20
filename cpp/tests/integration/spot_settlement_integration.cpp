#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

TEST(SpotSettlementIntegration, BuyerAndSellerWalletsSettleBaseAndQuote) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 12001, lob::Side::Ask, 100, 3, lob::POST_ONLY, 1).accepted);
  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 12002, lob::Side::Bid, 100, 3, lob::IOC, 2);

  EXPECT_TRUE_MSG(bid.accepted, "user=bob symbol=BTC-USDT order_id=12002 side=BUY price=100 qty=3 flags=IOC reason=" + bid.reason);
  EXPECT_EQ_MSG(f.exchange.balance(f.bob, "USDT").total, 1000000LL - 300LL, f.wallet_summary(f.bob));
  EXPECT_EQ_MSG(f.exchange.balance(f.bob, "BTC").total, 1000000LL + 3LL, f.wallet_summary(f.bob));
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "BTC").total, 1000000LL - 3LL, f.wallet_summary(f.alice));
  EXPECT_EQ_MSG(f.exchange.balance(f.alice, "USDT").total, 1000000LL + 300LL, f.wallet_summary(f.alice));
  require_invariants(f.exchange);
}

TEST(SpotSettlementIntegration, OverLockedQuoteIsReleasedAfterPriceImprovement) {
  auto f = ExchangeFixture::Spot();

  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 12011, lob::Side::Ask, 80, 5, lob::POST_ONLY, 1).accepted);
  auto bid = f.exchange.submit_limit(f.spot_symbol, f.bob, 12012, lob::Side::Bid, 100, 5, lob::IOC, 2);

  EXPECT_TRUE(bid.accepted);
  EXPECT_EQ_MSG(f.exchange.balance(f.bob, "USDT").locked, 0, f.wallet_summary(f.bob));
  EXPECT_EQ_MSG(f.exchange.balance(f.bob, "USDT").total, 1000000LL - 400LL, f.wallet_summary(f.bob));
  require_invariants(f.exchange);
}

TEST(SpotSettlementIntegration, TakerFeeIsDebitedFromQuoteWallet) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);

  EXPECT_TRUE(f.submit(f.alice, 12021, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  auto bid = f.submit(f.bob, 12022, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_TRUE_MSG(bid.accepted, "user=bob symbol=BTC-USDT order_id=12022 side=BUY price=100 qty=1 flags=IOC reason=" + bid.reason);
  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.quote_asset).total, 1000000LL - 101LL, f.ledger_summary(f.bob));
  EXPECT_TRUE_MSG(f.ledger.invariant_ok(), f.ledger_summary(f.bob));
}

TEST(SpotSettlementIntegration, FeeRoundingUsesIntegerFloor) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/1);

  EXPECT_TRUE(f.submit(f.alice, 12031, lob::Side::Ask, 9999, 1, lob::POST_ONLY, 1).accepted);
  auto bid = f.submit(f.bob, 12032, lob::Side::Bid, 9999, 1, lob::IOC, 2);

  EXPECT_TRUE_MSG(bid.accepted, "user=bob symbol=BTC-USDT order_id=12032 side=BUY price=9999 qty=1 flags=IOC reason=" + bid.reason);
  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.quote_asset).total, 1000000LL - 9999LL, f.ledger_summary(f.bob));
  EXPECT_TRUE_MSG(f.ledger.invariant_ok(), f.ledger_summary(f.bob));
}
