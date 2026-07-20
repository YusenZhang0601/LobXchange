#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/snapshot_state.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>

using namespace lobx_test;

namespace {

void open_alice_long(PerpEngineFixture& f, lob::Quantity qty = 4) {
  EXPECT_TRUE(f.submit(f.bob, 45001, lob::Side::Ask, 100, qty, lob::POST_ONLY, 1).accepted);
  auto open = f.submit(f.alice, 45002, lob::Side::Bid, 100, qty, lob::IOC, 2);
  EXPECT_TRUE_MSG(open.accepted, "setup open long reason=" + open.reason + " " + f.margin_summary(f.alice));
}

void rest_alice_loss_close(PerpEngineFixture& f, lob::Quantity qty = 4) {
  auto close = f.submit(f.alice, 45003, lob::Side::Ask, 50, qty, lobx::LOBX_REDUCE_ONLY | lob::POST_ONLY, 3);
  EXPECT_TRUE_MSG(close.accepted, "setup resting loss close reason=" + close.reason + " " + f.margin_summary(f.alice));
}

} // namespace

TEST(MarketEnginePerpAtomicityEdges, PerpBuyerCommitSellerCommitFailureRollsBackBuyer) {
  PerpEngineFixture f;
  open_alice_long(f);
  rest_alice_loss_close(f);
  EXPECT_TRUE(f.ledger.debit_locked(f.alice, f.margin_asset, 1).ok);
  const auto before = EngineSnapshot::capture(f);

  auto fill = f.submit(f.carol, 45004, lob::Side::Bid, 50, 4, lob::IOC, 4);

  EXPECT_FALSE(fill.accepted);
  const auto after = EngineSnapshot::capture(f);
  EXPECT_TRUE_MSG(same_positions(before, after), "seller release failure must roll back buyer position");
  EXPECT_TRUE_MSG(same_balances(before, after), "seller release failure must roll back buyer margin");
}

TEST(MarketEnginePerpAtomicityEdges, PerpSellerCommitFailureDoesNotLeaveBuyerPositionChanged) {
  PerpEngineFixture f;
  open_alice_long(f);
  rest_alice_loss_close(f);
  EXPECT_TRUE(f.ledger.debit_locked(f.alice, f.margin_asset, 1).ok);
  const auto carol_before = f.positions.position(f.carol, f.market.id);

  auto fill = f.submit(f.carol, 45014, lob::Side::Bid, 50, 4, lob::IOC, 4);

  EXPECT_FALSE(fill.accepted);
  EXPECT_EQ_MSG(f.positions.position(f.carol, f.market.id).signed_qty, carol_before.signed_qty,
                "buyer position must not change when seller commit fails");
}

TEST(MarketEnginePerpAtomicityEdges, PerpReleaseMarginFailureDoesNotChangePositionMargin) {
  PerpEngineFixture f;
  open_alice_long(f);
  rest_alice_loss_close(f);
  const auto locked_before = f.ledger.locked(f.alice, f.margin_asset);
  EXPECT_TRUE(f.ledger.debit_locked(f.alice, f.margin_asset, 1).ok);

  auto fill = f.submit(f.carol, 45024, lob::Side::Bid, 50, 4, lob::IOC, 4);

  EXPECT_FALSE(fill.accepted);
  EXPECT_EQ_MSG(f.ledger.locked(f.alice, f.margin_asset), locked_before - 1,
                "failed margin release must not apply additional margin changes");
}

TEST(MarketEnginePerpAtomicityEdges, PerpRealizedPnlCreditFailureDoesNotChangeOrderOrMargin) {
  PerpEngineFixture f;
  open_alice_long(f);
  auto deposit = f.ledger.deposit(f.alice, f.margin_asset,
                                  std::numeric_limits<lobx::Amount>::max() -
                                      f.ledger.balance(f.alice, f.margin_asset).total);
  EXPECT_TRUE_MSG(deposit.ok, "setup alice max margin total reason=" + deposit.reason);
  EXPECT_TRUE(f.submit(f.carol, 45031, lob::Side::Bid, 110, 4, lob::POST_ONLY, 3).accepted);
  const auto before = EngineSnapshot::capture(f);

  auto close = f.submit(f.alice, 45032, lob::Side::Ask, 110, 4, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);

  EXPECT_FALSE(close.accepted);
  const auto after = EngineSnapshot::capture(f);
  EXPECT_TRUE_MSG(same_book_and_open(before, after), "PnL credit failure must not mutate book/open");
  EXPECT_TRUE_MSG(same_balances(before, after), "PnL credit failure must not mutate margin ledger");
}

TEST(MarketEnginePerpAtomicityEdges, PerpRestingShortUsesMakerPriceAndDoesNotNeedExtraMargin) {
  PerpEngineFixture f;
  auto ask = f.submit(f.alice, 45041, lob::Side::Ask, 90, 1, lob::POST_ONLY, 1);
  EXPECT_TRUE(ask.accepted);
  const auto free = f.ledger.balance(f.alice, f.margin_asset).free;
  EXPECT_TRUE(f.ledger.withdraw(f.alice, f.margin_asset, free).ok);

  auto cross = f.submit(f.carol, 45043, lob::Side::Bid, 100, 1, lob::IOC, 3);

  EXPECT_TRUE_MSG(cross.accepted, "resting short should fill at maker price without extra margin: " + cross.reason);
  EXPECT_EQ_MSG(cross.trades.front().price, 90, "perp resting order must fill at maker price");
  EXPECT_EQ_MSG(f.positions.position(f.alice, f.market.id).signed_qty, -1, "seller should open one short");
  EXPECT_EQ_MSG(f.ledger.locked(f.alice, f.margin_asset), 18, "short margin should match maker price not taker bid");
}

TEST(MarketEnginePerpAtomicityEdges, PerpSettlementFailureDoesNotAppendTradeEvent) {
  PerpEngineFixture f;
  open_alice_long(f);
  rest_alice_loss_close(f);
  EXPECT_TRUE(f.ledger.debit_locked(f.alice, f.margin_asset, 1).ok);
  const size_t events_before = f.events.records().size();

  auto fill = f.submit(f.carol, 45054, lob::Side::Bid, 50, 4, lob::IOC, 4);

  EXPECT_FALSE(fill.accepted);
  EXPECT_EQ_MSG(f.events.records().size(), events_before + 1, "failed perp settlement should only append rejection event");
  EXPECT_EQ_MSG(f.events.records().back().type, std::string("order.rejected"), "failed perp settlement must not append trade");
}
