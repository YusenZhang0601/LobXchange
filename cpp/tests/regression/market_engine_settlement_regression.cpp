#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

void set_buyer_quote(SpotEngineFixture& f, lobx::Amount buyer_quote) {
  const auto current = f.ledger.balance(f.bob, f.quote_asset).free;
  if (current > buyer_quote) {
    const auto withdraw = f.ledger.withdraw(f.bob, f.quote_asset, current - buyer_quote);
    EXPECT_TRUE_MSG(withdraw.ok, "setup exact buyer quote reason=" + withdraw.reason);
  }
}

int count_events(const lobx::EventStore& events, const std::string& type) {
  int count = 0;
  for (const auto& event : events.records()) {
    if (event.type == type) ++count;
  }
  return count;
}

} // namespace

TEST(MarketEngineSettlementRegression, SettlementFailureDoesNotMutateOrderBook) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  set_buyer_quote(f, /*buyer_quote=*/100);
  EXPECT_TRUE(f.submit(f.alice, 30001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  auto buy = f.submit(f.bob, 30002, lob::Side::Bid, 100, 1, lob::IOC, 2);
  EXPECT_FALSE_MSG(buy.accepted, "fee shortfall should be rejected before matching mutates the book");
  EXPECT_EQ(buy.code, lobx::RejectCode::InsufficientBalance);

  auto asks = f.engine.topN(lob::Side::Ask, 10);
  EXPECT_FALSE_MSG(asks.empty(), "fee rejection must preserve passive ask on book");
  EXPECT_EQ_MSG(asks[0].second, 1, "fee rejection must preserve passive ask qty");
}

TEST(MarketEngineSettlementRegression, SettlementFailureDoesNotCreditBuyerBase) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  set_buyer_quote(f, /*buyer_quote=*/100);
  EXPECT_TRUE(f.submit(f.alice, 30011, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  const auto buyer_base_before = f.ledger.balance(f.bob, f.base_asset).total;
  auto buy = f.submit(f.bob, 30012, lob::Side::Bid, 100, 1, lob::IOC, 2);
  EXPECT_FALSE(buy.accepted);

  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.base_asset).total, buyer_base_before,
                "buyer base must not change when fee precheck rejects");
}

TEST(MarketEngineSettlementRegression, SettlementFailureDoesNotCreditSellerQuote) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  set_buyer_quote(f, /*buyer_quote=*/100);
  EXPECT_TRUE(f.submit(f.alice, 30021, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  const auto seller_quote_before = f.ledger.balance(f.alice, f.quote_asset).total;
  auto buy = f.submit(f.bob, 30022, lob::Side::Bid, 100, 1, lob::IOC, 2);
  EXPECT_FALSE(buy.accepted);

  EXPECT_EQ_MSG(f.ledger.balance(f.alice, f.quote_asset).total, seller_quote_before,
                "seller quote must not change when fee precheck rejects");
}

TEST(MarketEngineSettlementRegression, SettlementFailureDoesNotChangeLockedBalances) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  set_buyer_quote(f, /*buyer_quote=*/100);
  EXPECT_TRUE(f.submit(f.alice, 30031, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto seller_base_locked_before = f.ledger.locked(f.alice, f.base_asset);

  auto buy = f.submit(f.bob, 30032, lob::Side::Bid, 100, 1, lob::IOC, 2);
  EXPECT_FALSE(buy.accepted);

  EXPECT_EQ_MSG(f.ledger.locked(f.alice, f.base_asset), seller_base_locked_before,
                "passive seller lock must not change when fee precheck rejects");
  EXPECT_EQ_MSG(f.ledger.locked(f.bob, f.quote_asset), 0,
                "rejected IOC taker must not leave quote locked");
}

TEST(MarketEngineSettlementRegression, SettlementFailureDoesNotLeaveAcceptedOrderEventAsFinalState) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  set_buyer_quote(f, /*buyer_quote=*/100);
  EXPECT_TRUE(f.submit(f.alice, 30041, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const int accepted_before = count_events(f.events, "order.accepted");

  auto buy = f.submit(f.bob, 30042, lob::Side::Bid, 100, 1, lob::IOC, 2);
  EXPECT_FALSE(buy.accepted);

  EXPECT_EQ_MSG(count_events(f.events, "order.accepted"), accepted_before,
                "rejected taker order must not append order.accepted");
}

TEST(MarketEngineSettlementRegression, SecondFillFailureRollsBackFirstFill) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  set_buyer_quote(f, /*buyer_quote=*/201);
  EXPECT_TRUE(f.submit(f.alice, 30051, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 30052, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2).accepted);

  const auto buyer_base_before = f.ledger.balance(f.bob, f.base_asset).total;
  auto buy = f.submit(f.bob, 30053, lob::Side::Bid, 100, 2, lob::IOC, 3);
  EXPECT_FALSE_MSG(buy.accepted, "multi-fill fee shortfall should be rejected before matching");

  EXPECT_EQ_MSG(f.ledger.balance(f.bob, f.base_asset).total, buyer_base_before,
                "first fill must not commit when multi-fill fee precheck rejects");
  auto asks = f.engine.topN(lob::Side::Ask, 10);
  EXPECT_FALSE_MSG(asks.empty(), "both passive orders should remain after fee precheck rejection");
  EXPECT_EQ_MSG(asks[0].second, 2, "aggregate ask qty should remain after fee precheck rejection");
}

TEST(MarketEngineSettlementRegression, CancelReleaseFailureDoesNotEraseOpenOrder) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 30061, lob::Side::Ask, 100, 2, lob::NONE, 1).accepted);
  const auto corrupt = f.ledger.debit_locked(f.alice, f.base_asset, 1);
  EXPECT_TRUE_MSG(corrupt.ok, "setup removes one locked unit to force release failure reason=" + corrupt.reason);

  const bool canceled = f.engine.cancel(30061, f.alice, 2);
  EXPECT_FALSE_MSG(canceled, "cancel should surface ledger release failure and keep open order");

  auto asks = f.engine.topN(lob::Side::Ask, 10);
  EXPECT_FALSE_MSG(asks.empty(), "cancel release failure should leave the order on book");
  EXPECT_EQ_MSG(asks[0].second, 2, "cancel release failure should preserve resting quantity");
  bool still_open = false;
  for (const auto& order : f.engine.open_orders()) {
    if (order.id == 30061) still_open = true;
  }
  EXPECT_TRUE_MSG(still_open, "cancel release failure should not erase open order metadata");
}
