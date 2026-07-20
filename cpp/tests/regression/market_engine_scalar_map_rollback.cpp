#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>
#include <string>

using namespace lobx_test;

namespace {

void expect_ok(const lobx::Result& result) {
  EXPECT_TRUE_MSG(result.ok, result.reason);
}

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

void rest_perp_ask(ExchangeFixture& f, lobx::UserId user, lobx::OrderId id,
                   lob::Tick price, lob::Quantity qty) {
  expect_ok(f.exchange.submit_limit(f.perp_symbol, user, id, lob::Side::Ask,
                                    price, qty, lob::POST_ONLY, id));
}

lobx::Result create_index_trigger(ExchangeFixture& f, lobx::OrderId id, lob::Side side,
                                  lob::Quantity qty, lob::Tick trigger_price,
                                  lobx::TriggerChildOrderType child_type,
                                  lob::Tick child_limit, lob::Tick protection,
                                  uint32_t flags = lob::NONE) {
  return f.exchange.create_trigger_order(f.perp_symbol, f.alice, id, side, qty,
                                         trigger_price, lobx::TriggerPriceType::Index,
                                         lobx::TriggerCondition::AboveOrEqual,
                                         child_type, child_limit, protection, flags, id);
}

lobx::TriggerOrder trigger_by_id(const ExchangeFixture& f, lobx::OrderId id) {
  for (const auto& trigger : f.exchange.trigger_orders(f.perp_symbol)) {
    if (trigger.request.trigger_order_id == id) return trigger;
  }
  return {};
}

void open_long_short(PerpEngineFixture& f, lob::Quantity qty = 100) {
  expect_ok(f.submit(f.bob, 920301, lob::Side::Ask, 100, qty, lob::POST_ONLY, 1));
  expect_ok(f.submit(f.alice, 920302, lob::Side::Bid, 100, qty, lob::IOC, 2));
  expect_ok(f.engine.set_index_price(100));
  expect_ok(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice));
}

void force_perp_fee_account_credit_failure(PerpEngineFixture& f) {
  const auto deposit = f.ledger.deposit(std::numeric_limits<lobx::UserId>::max(),
                                        f.margin_asset,
                                        std::numeric_limits<lobx::Amount>::max());
  EXPECT_TRUE_MSG(deposit.ok, "setup fee account overflow guard reason=" + deposit.reason);
}

} // namespace

TEST(MarketEngineScalarMapRollback, SuccessfulPerpTradePersistsMarginAndFeeTotals) {
  PerpEngineFixture f;
  expect_ok(f.engine.set_fee_config(lobx::PerpFeeConfig{10, 20, 0}));
  const auto alice_before = f.ledger.balance(f.alice, f.margin_asset);
  const auto bob_before = f.ledger.balance(f.bob, f.margin_asset);

  expect_ok(f.submit(f.bob, 920001, lob::Side::Ask, 100, 100, lob::POST_ONLY, 1));
  auto fill = f.submit(f.alice, 920002, lob::Side::Bid, 100, 100, lob::IOC, 2);

  expect_ok(fill);
  EXPECT_EQ(fill.exec.filled, 100);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 100);
  EXPECT_EQ(f.positions.position(f.bob, f.market.id).signed_qty, -100);
  EXPECT_TRUE(f.ledger.balance(f.alice, f.margin_asset).locked > alice_before.locked);
  EXPECT_TRUE(f.ledger.balance(f.bob, f.margin_asset).locked > bob_before.locked);
  EXPECT_EQ(f.engine.account_fee_total(f.alice), 20);
  EXPECT_EQ(f.engine.account_fee_total(f.bob), 10);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, alice_before.total - 20);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).total, bob_before.total - 10);
}

TEST(MarketEngineScalarMapRollback, CloseTradeReleasesMarginAndKeepsFeeTotals) {
  PerpEngineFixture f;
  expect_ok(f.engine.set_fee_config(lobx::PerpFeeConfig{10, 20, 0}));

  expect_ok(f.submit(f.bob, 920011, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1));
  expect_ok(f.submit(f.alice, 920012, lob::Side::Bid, 100, 10, lob::IOC, 2));
  EXPECT_TRUE(f.ledger.balance(f.alice, f.margin_asset).locked > 0);
  EXPECT_EQ(f.engine.account_fee_total(f.alice), 2);
  expect_ok(f.submit(f.carol, 920013, lob::Side::Bid, 110, 10, lob::POST_ONLY, 3));

  auto close = f.submit(f.alice, 920014, lob::Side::Ask, 110, 10, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);

  expect_ok(close);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).realized_pnl, 100);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).locked, 0);
  EXPECT_EQ(f.engine.account_fee_total(f.alice), 4);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, 1000096);
}

TEST(MarketEngineScalarMapRollback, FundingSuccessPersistsFundingTotals) {
  PerpEngineFixture f;
  open_long_short(f);
  expect_ok(f.engine.set_funding_rate(100));

  auto result = f.engine.settle_funding(10);

  expect_ok(result);
  EXPECT_EQ(f.engine.account_funding_total(f.alice), -100);
  EXPECT_EQ(f.engine.account_funding_total(f.bob), 100);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, 999900);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).total, 1000100);
}

TEST(MarketEngineScalarMapRollback, FundingFailureRollsBackFundingTotalsAndWallets) {
  PerpEngineFixture f(2100);
  open_long_short(f);
  expect_ok(f.engine.set_funding_rate(200));
  const auto alice_before = f.ledger.balance(f.alice, f.margin_asset);
  const auto bob_before = f.ledger.balance(f.bob, f.margin_asset);
  const auto alice_position_before = f.positions.position(f.alice, f.market.id);
  const auto bob_position_before = f.positions.position(f.bob, f.market.id);
  const int events_before = event_count(f.events, "funding.settled");

  auto result = f.engine.settle_funding(10);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(f.engine.account_funding_total(f.alice), 0);
  EXPECT_EQ(f.engine.account_funding_total(f.bob), 0);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, alice_before.total);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).free, alice_before.free);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).total, bob_before.total);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).free, bob_before.free);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, alice_position_before.signed_qty);
  EXPECT_EQ(f.positions.position(f.bob, f.market.id).signed_qty, bob_position_before.signed_qty);
  EXPECT_EQ(event_count(f.events, "funding.settled"), events_before);
  EXPECT_EQ(event_count(f.events, "funding.payment"), 0);
}

TEST(MarketEngineScalarMapRollback, TriggerChildFailureLeavesNoScalarMapResidue) {
  auto f = ExchangeFixture::Perp();
  constexpr lobx::OrderId child_id = 920080;

  expect_ok(f.exchange.set_perp_fee_config(f.perp_symbol, lobx::PerpFeeConfig{0, 10000, 0}));
  rest_perp_ask(f, f.bob, 920079, 100, 10000);
  expect_ok(create_index_trigger(f, child_id, lob::Side::Bid, 10000, 100,
                                 lobx::TriggerChildOrderType::Market, 0, 100));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));
  const auto alice_before = f.exchange.balance(f.alice, "USDT");
  const auto bob_before = f.exchange.balance(f.bob, "USDT");
  const auto alice_position_before = f.exchange.position(f.alice, f.perp_symbol);
  const auto bob_position_before = f.exchange.position(f.bob, f.perp_symbol);

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 3), 0);

  EXPECT_EQ(event_count(f.exchange.events(), "trigger.failed"), 1);
  EXPECT_EQ(trigger_by_id(f, child_id).status, lobx::TriggerOrderStatus::Failed);
  EXPECT_EQ(f.exchange.account_fee_total(f.alice, f.perp_symbol), 0);
  EXPECT_EQ(f.exchange.account_fee_total(f.bob, f.perp_symbol), 0);
  EXPECT_EQ(f.exchange.account_funding_total(f.alice, f.perp_symbol), 0);
  EXPECT_EQ(f.exchange.account_funding_total(f.bob, f.perp_symbol), 0);
  EXPECT_EQ(f.exchange.balance(f.alice, "USDT").total, alice_before.total);
  EXPECT_EQ(f.exchange.balance(f.alice, "USDT").locked, alice_before.locked);
  EXPECT_EQ(f.exchange.balance(f.bob, "USDT").total, bob_before.total);
  EXPECT_EQ(f.exchange.balance(f.bob, "USDT").locked, bob_before.locked);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, alice_position_before.signed_qty);
  EXPECT_EQ(f.exchange.position(f.bob, f.perp_symbol).signed_qty, bob_position_before.signed_qty);
}

TEST(MarketEngineScalarMapRollback, SubmitMutationFailureDoesNotLeaveMarginOrFeeResidue) {
  PerpEngineFixture f;
  expect_ok(f.engine.set_fee_config(lobx::PerpFeeConfig{0, 20, 0}));
  force_perp_fee_account_credit_failure(f);
  expect_ok(f.submit(f.bob, 920101, lob::Side::Ask, 100, 100, lob::POST_ONLY, 1));
  const auto alice_before = f.ledger.balance(f.alice, f.margin_asset);
  const auto bob_before = f.ledger.balance(f.bob, f.margin_asset);
  const int trades_before = event_count(f.events, "trade");
  const int fee_events_before = event_count(f.events, "perp.fee_charged");

  auto rejected = f.submit(f.alice, 920102, lob::Side::Bid, 100, 100, lob::IOC, 2);

  EXPECT_FALSE(rejected.accepted);
  EXPECT_EQ(rejected.code, lobx::RejectCode::InternalError);
  EXPECT_EQ(f.engine.account_fee_total(f.alice), 0);
  EXPECT_EQ(f.engine.account_fee_total(f.bob), 0);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, alice_before.total);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).free, alice_before.free);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).locked, alice_before.locked);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).total, bob_before.total);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).free, bob_before.free);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).locked, bob_before.locked);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);
  EXPECT_EQ(f.positions.position(f.bob, f.market.id).signed_qty, 0);
  EXPECT_EQ(f.engine.account_funding_total(f.alice), 0);
  EXPECT_EQ(f.engine.account_funding_total(f.bob), 0);
  EXPECT_EQ(event_count(f.events, "trade"), trades_before);
  EXPECT_EQ(event_count(f.events, "perp.fee_charged"), fee_events_before);
  EXPECT_EQ(f.engine.topN(lob::Side::Ask, 10)[0].second, 100);
}
