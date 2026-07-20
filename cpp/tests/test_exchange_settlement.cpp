#include "test_helpers/market_microstructure_helpers.hpp"

using namespace lobx_test;

namespace {

constexpr lobx::UserId poor_buyer = 71;
constexpr lobx::UserId poor_seller = 72;
constexpr lobx::UserId tight_buyer = 73;
constexpr lobx::UserId dave = 40;

void expect_balances_non_negative(const lobx::AccountLedger& ledger) {
  for (const auto& balance : ledger.balances()) {
    EXPECT_TRUE_MSG(balance.total >= 0, "total balance must be non-negative");
    EXPECT_TRUE_MSG(balance.free >= 0, "free balance must be non-negative");
    EXPECT_TRUE_MSG(balance.locked >= 0, "locked balance must be non-negative");
  }
}

} // namespace

TEST(ExchangeSettlement, ST001ToST006BuyerSellerWalletsAndTotalsAreConserved) {
  SpotEngineFixture f;
  const auto quote_before = total_asset(f.ledger, f.quote_asset);
  const auto base_before = total_asset(f.ledger, f.base_asset);
  const auto bob_quote = f.ledger.balance(f.bob, f.quote_asset).total;
  const auto bob_base = f.ledger.balance(f.bob, f.base_asset).total;
  const auto alice_quote = f.ledger.balance(f.alice, f.quote_asset).total;
  const auto alice_base = f.ledger.balance(f.alice, f.base_asset).total;

  EXPECT_TRUE(f.submit(f.alice, 62001, lob::Side::Ask, 100, 3, lob::POST_ONLY, 1).accepted);
  auto buy = f.submit(f.bob, 62002, lob::Side::Bid, 100, 3, lob::IOC, 2);

  EXPECT_TRUE_MSG(buy.accepted, buy.reason);
  EXPECT_EQ(f.ledger.balance(f.bob, f.quote_asset).total, bob_quote - 300);
  EXPECT_EQ(f.ledger.balance(f.bob, f.base_asset).total, bob_base + 3);
  EXPECT_EQ(f.ledger.balance(f.alice, f.quote_asset).total, alice_quote + 300);
  EXPECT_EQ(f.ledger.balance(f.alice, f.base_asset).total, alice_base - 3);
  EXPECT_EQ(total_asset(f.ledger, f.quote_asset), quote_before);
  EXPECT_EQ(total_asset(f.ledger, f.base_asset), base_before);
  EXPECT_TRUE(f.ledger.invariant_ok());
  expect_balances_non_negative(f.ledger);
}

TEST(ExchangeSettlement, ST007ST008InsufficientCashOrAssetRejectsWithoutMutation) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.ledger.deposit(poor_buyer, f.base_asset, 10).ok);
  EXPECT_TRUE(f.ledger.deposit(poor_seller, f.quote_asset, 1000).ok);
  const auto bids_before = f.engine.topN(lob::Side::Bid, 10);
  const auto asks_before = f.engine.topN(lob::Side::Ask, 10);
  const auto poor_buyer_quote = f.ledger.balance(poor_buyer, f.quote_asset);
  const auto poor_seller_base = f.ledger.balance(poor_seller, f.base_asset);

  auto buy = f.submit(poor_buyer, 62011, lob::Side::Bid, 100, 1, lob::POST_ONLY, 1);
  auto sell = f.submit(poor_seller, 62012, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2);

  EXPECT_FALSE(buy.accepted);
  EXPECT_EQ(buy.code, lobx::RejectCode::InsufficientBalance);
  EXPECT_FALSE(sell.accepted);
  EXPECT_EQ(sell.code, lobx::RejectCode::InsufficientBalance);
  EXPECT_TRUE(f.engine.topN(lob::Side::Bid, 10) == bids_before);
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 10) == asks_before);
  EXPECT_EQ(f.ledger.balance(poor_buyer, f.quote_asset).total, poor_buyer_quote.total);
  EXPECT_EQ(f.ledger.balance(poor_buyer, f.quote_asset).locked, poor_buyer_quote.locked);
  EXPECT_EQ(f.ledger.balance(poor_seller, f.base_asset).total, poor_seller_base.total);
  EXPECT_EQ(f.ledger.balance(poor_seller, f.base_asset).locked, poor_seller_base.locked);
  EXPECT_TRUE(f.ledger.invariant_ok());
}

TEST(ExchangeSettlement, ST009MultipleTradesLeaveAccountsNonNegative) {
  SpotEngineFixture f;
  deposit_spot_user(f, dave);
  EXPECT_TRUE(f.submit(f.alice, 62021, lob::Side::Ask, 99, 2, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 62022, lob::Side::Ask, 100, 3, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, 62023, lob::Side::Ask, 101, 4, lob::POST_ONLY, 3).accepted);

  auto buy = f.submit(f.bob, 62024, lob::Side::Bid, 101, 8, lob::IOC, 4);

  EXPECT_TRUE_MSG(buy.accepted, buy.reason);
  EXPECT_EQ(buy.exec.filled, 8);
  expect_balances_non_negative(f.ledger);
  EXPECT_TRUE(f.ledger.invariant_ok());
}

TEST(ExchangeSettlement, ST010CancelReleasesResourcesForLaterOrders) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.ledger.deposit(tight_buyer, f.quote_asset, 100).ok);

  auto first = f.submit(tight_buyer, 62031, lob::Side::Bid, 100, 1, lob::POST_ONLY, 1);
  auto blocked = f.submit(tight_buyer, 62032, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2);
  EXPECT_TRUE_MSG(first.accepted, first.reason);
  EXPECT_FALSE(blocked.accepted);
  EXPECT_EQ(f.ledger.balance(tight_buyer, f.quote_asset).locked, 100);
  EXPECT_TRUE(f.engine.cancel(62031, tight_buyer, 3));

  auto after_cancel = f.submit(tight_buyer, 62033, lob::Side::Bid, 100, 1, lob::POST_ONLY, 4);

  EXPECT_TRUE_MSG(after_cancel.accepted, after_cancel.reason);
  EXPECT_EQ(f.ledger.balance(tight_buyer, f.quote_asset).locked, 100);
  EXPECT_TRUE(has_open_order(f.engine, 62033));
  EXPECT_TRUE(f.ledger.invariant_ok());
}

TEST(ExchangeSettlement, ST011SweepSettlesEachMakerIndependently) {
  SpotEngineFixture f;
  deposit_spot_user(f, dave);
  const auto alice_quote = f.ledger.balance(f.alice, f.quote_asset).total;
  const auto carol_quote = f.ledger.balance(f.carol, f.quote_asset).total;
  const auto dave_quote = f.ledger.balance(dave, f.quote_asset).total;
  const auto alice_base = f.ledger.balance(f.alice, f.base_asset).total;
  const auto carol_base = f.ledger.balance(f.carol, f.base_asset).total;
  const auto dave_base = f.ledger.balance(dave, f.base_asset).total;

  EXPECT_TRUE(f.submit(f.alice, 62041, lob::Side::Ask, 90, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.submit(f.carol, 62042, lob::Side::Ask, 95, 2, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.submit(dave, 62043, lob::Side::Ask, 100, 3, lob::POST_ONLY, 3).accepted);
  auto buy = f.submit(f.bob, 62044, lob::Side::Bid, 100, 4, lob::IOC, 4);

  EXPECT_TRUE_MSG(buy.accepted, buy.reason);
  EXPECT_EQ(f.ledger.balance(f.alice, f.quote_asset).total, alice_quote + 90);
  EXPECT_EQ(f.ledger.balance(f.carol, f.quote_asset).total, carol_quote + 190);
  EXPECT_EQ(f.ledger.balance(dave, f.quote_asset).total, dave_quote + 100);
  EXPECT_EQ(f.ledger.balance(f.alice, f.base_asset).total, alice_base - 1);
  EXPECT_EQ(f.ledger.balance(f.carol, f.base_asset).total, carol_base - 2);
  EXPECT_EQ(f.ledger.balance(dave, f.base_asset).total, dave_base - 1);
  EXPECT_EQ(top_qty(f.engine, lob::Side::Ask, 100), 2);
}

TEST(ExchangeSettlement, ST012RejectedOrderDoesNotChangeWalletBookOrTrades) {
  SpotEngineFixture f;
  EXPECT_TRUE(f.submit(f.alice, 62051, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto bids_before = f.engine.topN(lob::Side::Bid, 10);
  const auto asks_before = f.engine.topN(lob::Side::Ask, 10);
  const auto bob_quote_before = f.ledger.balance(f.bob, f.quote_asset);
  const int trades_before = count_events(f.events, "trade");

  auto rejected = f.submit(f.bob, 62052, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2);

  EXPECT_FALSE(rejected.accepted);
  EXPECT_EQ(rejected.code, lobx::RejectCode::PostOnlyWouldCross);
  EXPECT_TRUE(f.engine.topN(lob::Side::Bid, 10) == bids_before);
  EXPECT_TRUE(f.engine.topN(lob::Side::Ask, 10) == asks_before);
  EXPECT_EQ(f.ledger.balance(f.bob, f.quote_asset).total, bob_quote_before.total);
  EXPECT_EQ(f.ledger.balance(f.bob, f.quote_asset).free, bob_quote_before.free);
  EXPECT_EQ(f.ledger.balance(f.bob, f.quote_asset).locked, bob_quote_before.locked);
  EXPECT_EQ(count_events(f.events, "trade"), trades_before);
  EXPECT_TRUE(f.ledger.invariant_ok());
}
