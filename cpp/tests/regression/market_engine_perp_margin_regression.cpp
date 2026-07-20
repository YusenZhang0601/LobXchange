#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/invariant_checker.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

void open_alice_long(PerpEngineFixture& f, lob::Tick price, lob::Quantity qty, lobx::OrderId base_id) {
  EXPECT_TRUE_MSG(f.submit(f.bob, base_id, lob::Side::Ask, price, qty, lob::POST_ONLY, 1).accepted, f.margin_summary(f.bob));
  auto buy = f.submit(f.alice, base_id + 1, lob::Side::Bid, price, qty, lob::IOC, 2);
  EXPECT_TRUE_MSG(buy.accepted, "open_alice_long reason=" + buy.reason + " " + f.margin_summary(f.alice));
  EXPECT_EQ(buy.exec.filled, qty);
}

} // namespace

TEST(MarketEnginePerpMarginRegression, PerpShortOpenFilledAboveLimitUsesExecutionPriceMargin) {
  PerpEngineFixture f;

  EXPECT_TRUE(f.submit(f.bob, 32001, lob::Side::Bid, 100, 1, lob::POST_ONLY, 1).accepted);
  auto sell = f.submit(f.alice, 32002, lob::Side::Ask, 90, 1, lob::IOC, 2);

  EXPECT_TRUE_MSG(sell.accepted, "user=alice symbol=BTC-USDT-PERP order_id=32002 side=SELL price=90 qty=1 flags=IOC reason=" + sell.reason);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, -1);
  EXPECT_TRUE_MSG(f.ledger.locked(f.alice, f.margin_asset) >= 20,
                  "short taker filled at maker price 100 must retain execution-price margin: " +
                      f.margin_summary(f.alice));
}

TEST(MarketEnginePerpMarginRegression, PerpLongOpenFilledBelowLimitReleasesExcessMargin) {
  PerpEngineFixture f;

  EXPECT_TRUE(f.submit(f.bob, 32011, lob::Side::Ask, 90, 1, lob::POST_ONLY, 1).accepted);
  auto buy = f.submit(f.alice, 32012, lob::Side::Bid, 100, 1, lob::IOC, 2);

  EXPECT_TRUE_MSG(buy.accepted, "user=alice symbol=BTC-USDT-PERP order_id=32012 side=BUY price=100 qty=1 flags=IOC reason=" + buy.reason);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 1);
  EXPECT_EQ_MSG(f.ledger.locked(f.alice, f.margin_asset), 18, f.margin_summary(f.alice));
  auto report = check_order_book_invariants(f.engine, f.ledger);
  EXPECT_TRUE_MSG(report.ok, report_to_string(report));
}

TEST(MarketEnginePerpMarginRegression, PerpFullCloseReleasesAllPositionMargin) {
  PerpEngineFixture f;
  open_alice_long(f, 100, 4, 32021);

  EXPECT_TRUE(f.submit(f.carol, 32023, lob::Side::Bid, 110, 4, lob::POST_ONLY, 3).accepted);
  auto close = f.submit(f.alice, 32024, lob::Side::Ask, 110, 4, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);

  EXPECT_TRUE_MSG(close.accepted, "close reason=" + close.reason + " " + f.margin_summary(f.alice));
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);
  EXPECT_EQ_MSG(f.ledger.locked(f.alice, f.margin_asset), 0, f.margin_summary(f.alice));
}

TEST(MarketEnginePerpMarginRegression, PerpPartialCloseReleasesProportionalMargin) {
  PerpEngineFixture f;
  open_alice_long(f, 100, 4, 32031);

  EXPECT_TRUE(f.submit(f.carol, 32033, lob::Side::Bid, 110, 2, lob::POST_ONLY, 3).accepted);
  auto close = f.submit(f.alice, 32034, lob::Side::Ask, 110, 2, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);

  EXPECT_TRUE_MSG(close.accepted, "partial close reason=" + close.reason + " " + f.margin_summary(f.alice));
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 2);
  EXPECT_EQ_MSG(f.ledger.locked(f.alice, f.margin_asset), 40, f.margin_summary(f.alice));
}

TEST(MarketEnginePerpMarginRegression, PerpBuyerSuccessSellerFailureIsAtomic) {
  PerpEngineFixture f;
  open_alice_long(f, 100, 4, 32041);

  auto reduce = f.submit(f.alice, 32043, lob::Side::Ask, 50, 4, lobx::LOBX_REDUCE_ONLY | lob::POST_ONLY, 3);
  EXPECT_TRUE_MSG(reduce.accepted, "setup resting loss close reason=" + reduce.reason + " " + f.margin_summary(f.alice));

  const auto free_before_withdraw = f.ledger.balance(f.alice, f.margin_asset).free;
  EXPECT_TRUE_MSG(f.ledger.withdraw(f.alice, f.margin_asset, free_before_withdraw).ok,
                  "setup removes alice free margin to force passive seller realized loss failure");
  const auto carol_before = f.positions.position(f.carol, f.market.id);
  const auto alice_before = f.positions.position(f.alice, f.market.id);

  auto fill = f.submit(f.carol, 32044, lob::Side::Bid, 50, 4, lob::IOC, 4);
  EXPECT_FALSE_MSG(fill.accepted, "setup should fail during passive seller loss settlement");
  EXPECT_EQ(fill.code, lobx::RejectCode::InternalError);

  EXPECT_EQ_MSG(f.positions.position(f.carol, f.market.id).signed_qty, carol_before.signed_qty,
                "buyer leg must not commit when seller leg fails");
  EXPECT_EQ_MSG(f.positions.position(f.alice, f.market.id).signed_qty, alice_before.signed_qty,
                "seller position must not change when settlement fails");
}

TEST(MarketEnginePerpMarginRegression, PerpSettlementFailureDoesNotAppendTradeEvent) {
  PerpEngineFixture f;
  open_alice_long(f, 100, 4, 32051);
  EXPECT_TRUE(f.submit(f.alice, 32053, lob::Side::Ask, 50, 4, lobx::LOBX_REDUCE_ONLY | lob::POST_ONLY, 3).accepted);
  const auto free_before_withdraw = f.ledger.balance(f.alice, f.margin_asset).free;
  EXPECT_TRUE(f.ledger.withdraw(f.alice, f.margin_asset, free_before_withdraw).ok);
  const auto event_count_before = f.events.records().size();

  auto fill = f.submit(f.carol, 32054, lob::Side::Bid, 50, 4, lob::IOC, 4);
  EXPECT_FALSE(fill.accepted);

  EXPECT_EQ_MSG(f.events.records().size(), event_count_before + 1,
                "failed settlement should append exactly one rejection event for taker order");
  EXPECT_EQ_MSG(f.events.records().back().type, std::string("order.rejected"),
                "failed settlement must not append order.accepted or trade event");
}
