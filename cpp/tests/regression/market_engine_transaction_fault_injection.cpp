#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

#include <string>
#include <vector>

using namespace lobx_test;

namespace {

void expect_ok(const lobx::SubmitResult& result) {
  EXPECT_TRUE_MSG(result.accepted, result.reason);
}

int event_count(const lobx::EventStore& events, const std::string& type) {
  int count = 0;
  for (const auto& record : events.records()) {
    if (record.type == type) ++count;
  }
  return count;
}

lob::Quantity book_qty(PerpEngineFixture& f, lob::Side side, lob::Tick price) {
  lob::Quantity qty = 0;
  for (const auto& level : f.engine.topN(side, 10)) {
    if (level.first == price) qty += level.second;
  }
  return qty;
}

bool has_open_order(PerpEngineFixture& f, lobx::OrderId id) {
  for (const auto& order : f.engine.open_orders()) {
    if (order.id == id) return true;
  }
  return false;
}

void expect_wallet_eq(const lobx::WalletBalance& actual, const lobx::WalletBalance& expected) {
  EXPECT_EQ(actual.total, expected.total);
  EXPECT_EQ(actual.free, expected.free);
  EXPECT_EQ(actual.locked, expected.locked);
}

void expect_position_eq(const lobx::Position& actual, const lobx::Position& expected) {
  EXPECT_EQ(actual.signed_qty, expected.signed_qty);
  EXPECT_EQ(actual.entry_price, expected.entry_price);
  EXPECT_EQ(actual.realized_pnl, expected.realized_pnl);
  EXPECT_EQ(actual.leverage, expected.leverage);
}

struct PerpState {
  lobx::WalletBalance alice_wallet;
  lobx::WalletBalance bob_wallet;
  lobx::Position alice_position;
  lobx::Position bob_position;
  lobx::Amount alice_fee_total{0};
  lobx::Amount bob_fee_total{0};
  int accepted_events{0};
  int rejected_events{0};
  int trade_events{0};
  int fee_events{0};
};

PerpState capture_state(PerpEngineFixture& f) {
  return PerpState{
      f.ledger.balance(f.alice, f.margin_asset),
      f.ledger.balance(f.bob, f.margin_asset),
      f.positions.position(f.alice, f.market.id),
      f.positions.position(f.bob, f.market.id),
      f.engine.account_fee_total(f.alice),
      f.engine.account_fee_total(f.bob),
      event_count(f.events, "order.accepted"),
      event_count(f.events, "order.rejected"),
      event_count(f.events, "trade"),
      event_count(f.events, "perp.fee_charged")};
}

void expect_state_restored(PerpEngineFixture& f, const PerpState& before) {
  expect_wallet_eq(f.ledger.balance(f.alice, f.margin_asset), before.alice_wallet);
  expect_wallet_eq(f.ledger.balance(f.bob, f.margin_asset), before.bob_wallet);
  expect_position_eq(f.positions.position(f.alice, f.market.id), before.alice_position);
  expect_position_eq(f.positions.position(f.bob, f.market.id), before.bob_position);
  EXPECT_EQ(f.engine.account_fee_total(f.alice), before.alice_fee_total);
  EXPECT_EQ(f.engine.account_fee_total(f.bob), before.bob_fee_total);
  EXPECT_EQ(event_count(f.events, "order.accepted"), before.accepted_events);
  EXPECT_EQ(event_count(f.events, "trade"), before.trade_events);
  EXPECT_EQ(event_count(f.events, "perp.fee_charged"), before.fee_events);
  EXPECT_EQ(event_count(f.events, "order.rejected"), before.rejected_events + 1);
}

void expect_internal_reject(const lobx::SubmitResult& result) {
  EXPECT_FALSE_MSG(result.accepted, "forced transaction fault should reject");
  EXPECT_EQ(result.code, lobx::RejectCode::InternalError);
  EXPECT_TRUE_MSG(result.trades.empty(), "failed transaction must not expose committed trades");
}

void clear_fault(PerpEngineFixture& f) {
  f.engine.clear_fault_point_for_testing();
}

void set_fault(PerpEngineFixture& f, lobx::MarketEngineFaultPoint point) {
  f.engine.set_fault_point_for_testing(point);
}

} // namespace

TEST(MarketEngineTransactionFaultInjection, AfterBookSubmitRollsBackBookOpenLedgerPosition) {
  PerpEngineFixture f;
  const PerpState before = capture_state(f);

  set_fault(f, lobx::MarketEngineFaultPoint::AfterBookSubmit);
  auto failed = f.submit(f.alice, 940001, lob::Side::Bid, 100, 5, lob::POST_ONLY, 1);

  expect_internal_reject(failed);
  clear_fault(f);
  expect_state_restored(f, before);
  EXPECT_EQ(book_qty(f, lob::Side::Bid, 100), 0);
  EXPECT_FALSE(has_open_order(f, 940001));

  auto retry = f.submit(f.alice, 940001, lob::Side::Bid, 100, 5, lob::POST_ONLY, 2);
  expect_ok(retry);
  EXPECT_EQ(book_qty(f, lob::Side::Bid, 100), 5);
  EXPECT_TRUE(has_open_order(f, 940001));
}

TEST(MarketEngineTransactionFaultInjection, AfterFeeChargeRollsBackFeeTotalsAndWallets) {
  PerpEngineFixture f;
  EXPECT_TRUE_MSG(f.engine.set_fee_config(lobx::PerpFeeConfig{10, 20, 0}).ok, "fee config setup");
  expect_ok(f.submit(f.bob, 940010, lob::Side::Ask, 100, 100, lob::POST_ONLY, 1));
  const PerpState before = capture_state(f);

  set_fault(f, lobx::MarketEngineFaultPoint::AfterFeeCharge);
  auto failed = f.submit(f.alice, 940011, lob::Side::Bid, 100, 100, lob::IOC, 2);

  expect_internal_reject(failed);
  clear_fault(f);
  expect_state_restored(f, before);
  EXPECT_EQ(book_qty(f, lob::Side::Ask, 100), 100);
  EXPECT_TRUE(has_open_order(f, 940010));
  EXPECT_FALSE(has_open_order(f, 940011));
}

TEST(MarketEngineTransactionFaultInjection, AfterPositionApplyRollsBackPositionsAndMargin) {
  PerpEngineFixture f;
  expect_ok(f.submit(f.bob, 940020, lob::Side::Ask, 100, 20, lob::POST_ONLY, 1));
  const PerpState before = capture_state(f);

  set_fault(f, lobx::MarketEngineFaultPoint::AfterPositionApply);
  auto failed = f.submit(f.alice, 940021, lob::Side::Bid, 100, 20, lob::IOC, 2);

  expect_internal_reject(failed);
  clear_fault(f);
  expect_state_restored(f, before);
  EXPECT_EQ(book_qty(f, lob::Side::Ask, 100), 20);
  EXPECT_TRUE(has_open_order(f, 940020));
  EXPECT_FALSE(has_open_order(f, 940021));
}

TEST(MarketEngineTransactionFaultInjection, AfterReleaseAndEraseRollsBackRestingOrderAndLocks) {
  PerpEngineFixture f;
  expect_ok(f.submit(f.bob, 940030, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1));
  const PerpState before = capture_state(f);

  set_fault(f, lobx::MarketEngineFaultPoint::AfterReleaseAndErase);
  auto failed = f.submit(f.alice, 940031, lob::Side::Bid, 100, 4, lob::IOC, 2);

  expect_internal_reject(failed);
  clear_fault(f);
  expect_state_restored(f, before);
  EXPECT_EQ(book_qty(f, lob::Side::Ask, 100), 4);
  EXPECT_TRUE(has_open_order(f, 940030));
  EXPECT_FALSE(has_open_order(f, 940031));
}

TEST(MarketEngineTransactionFaultInjection, AfterAdjustRestingLockRollsBackPartialFillRemainder) {
  PerpEngineFixture f;
  expect_ok(f.submit(f.bob, 940040, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1));
  const PerpState before = capture_state(f);

  set_fault(f, lobx::MarketEngineFaultPoint::AfterAdjustRestingLock);
  auto failed = f.submit(f.alice, 940041, lob::Side::Bid, 100, 5, lob::NONE, 2);

  expect_internal_reject(failed);
  clear_fault(f);
  expect_state_restored(f, before);
  EXPECT_EQ(book_qty(f, lob::Side::Ask, 100), 2);
  EXPECT_EQ(book_qty(f, lob::Side::Bid, 100), 0);
  EXPECT_TRUE(has_open_order(f, 940040));
  EXPECT_FALSE(has_open_order(f, 940041));
}

TEST(MarketEngineTransactionFaultInjection, ForcedInvariantFailureRollsBackEverything) {
  PerpEngineFixture f;
  expect_ok(f.submit(f.bob, 940050, lob::Side::Ask, 100, 3, lob::POST_ONLY, 1));
  const PerpState before = capture_state(f);

  set_fault(f, lobx::MarketEngineFaultPoint::ForceLedgerInvariantFailure);
  auto failed = f.submit(f.alice, 940051, lob::Side::Bid, 100, 3, lob::IOC, 2);

  expect_internal_reject(failed);
  clear_fault(f);
  expect_state_restored(f, before);
  EXPECT_EQ(book_qty(f, lob::Side::Ask, 100), 3);
  EXPECT_TRUE(has_open_order(f, 940050));
  EXPECT_FALSE(has_open_order(f, 940051));
}

TEST(MarketEngineTransactionFaultInjection, FaultPointDoesNotConsumeOrderIdOnRejectedTransaction) {
  PerpEngineFixture f;
  constexpr lobx::OrderId order_id = 940060;

  set_fault(f, lobx::MarketEngineFaultPoint::AfterBookSubmit);
  auto failed = f.submit(f.alice, order_id, lob::Side::Bid, 100, 5, lob::POST_ONLY, 1);

  expect_internal_reject(failed);
  clear_fault(f);

  auto retry = f.submit(f.alice, order_id, lob::Side::Bid, 100, 5, lob::POST_ONLY, 2);
  expect_ok(retry);
  EXPECT_EQ(book_qty(f, lob::Side::Bid, 100), 5);
}
