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

void expect_duplicate(const lobx::SubmitResult& result) {
  EXPECT_FALSE_MSG(result.accepted, "duplicate order id should be rejected");
  EXPECT_EQ(result.code, lobx::RejectCode::DuplicateOrderId);
}

void expect_not_duplicate(const lobx::SubmitResult& result) {
  EXPECT_NE(result.code, lobx::RejectCode::DuplicateOrderId);
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

void force_fee_account_credit_failure(SpotEngineFixture& f) {
  const auto deposit = f.ledger.deposit(std::numeric_limits<lobx::UserId>::max(),
                                        f.quote_asset,
                                        std::numeric_limits<lobx::Amount>::max());
  EXPECT_TRUE_MSG(deposit.ok, "setup fee account overflow guard reason=" + deposit.reason);
}

} // namespace

TEST(MarketEngineSeenOrderIdRollback, PreCheckRejectDoesNotConsumeOrderId) {
  auto f = ExchangeFixture::Perp();
  constexpr lobx::OrderId id = 910001;

  auto first = f.exchange.submit_limit(f.perp_symbol, f.alice, id, lob::Side::Bid,
                                       100, 0, lob::POST_ONLY, 1);
  EXPECT_FALSE(first.accepted);
  expect_not_duplicate(first);

  auto second = f.exchange.submit_limit(f.perp_symbol, f.alice, id, lob::Side::Ask,
                                        100, 1, lob::POST_ONLY, 2);
  expect_ok(second);
}

TEST(MarketEngineSeenOrderIdRollback, AcceptedRestingOrderConsumesOrderId) {
  auto f = ExchangeFixture::Perp();
  constexpr lobx::OrderId id = 910010;

  auto first = f.exchange.submit_limit(f.perp_symbol, f.bob, id, lob::Side::Ask,
                                       100, 1, lob::POST_ONLY, 1);
  expect_ok(first);

  auto duplicate = f.exchange.submit_limit(f.perp_symbol, f.bob, id, lob::Side::Bid,
                                           99, 1, lob::POST_ONLY, 2);
  expect_duplicate(duplicate);
}

TEST(MarketEngineSeenOrderIdRollback, AcceptedFilledOrderConsumesOrderId) {
  auto f = ExchangeFixture::Perp();
  rest_perp_ask(f, f.bob, 910019, 100, 1);

  auto filled = f.exchange.submit_limit(f.perp_symbol, f.alice, 910020, lob::Side::Bid,
                                        100, 1, lob::IOC, 2);
  expect_ok(filled);
  EXPECT_EQ(filled.exec.filled, 1);

  auto duplicate = f.exchange.submit_limit(f.perp_symbol, f.alice, 910020, lob::Side::Bid,
                                           99, 1, lob::POST_ONLY, 3);
  expect_duplicate(duplicate);
}

TEST(MarketEngineSeenOrderIdRollback, CancelledOrderStillConsumesOrderId) {
  auto f = ExchangeFixture::Perp();
  constexpr lobx::OrderId id = 910030;

  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.bob, id, lob::Side::Ask,
                                    100, 1, lob::POST_ONLY, 1));
  EXPECT_TRUE(f.exchange.cancel(f.perp_symbol, f.bob, id, 2));

  auto duplicate = f.exchange.submit_limit(f.perp_symbol, f.bob, id, lob::Side::Ask,
                                           101, 1, lob::POST_ONLY, 3);
  expect_duplicate(duplicate);
}

TEST(MarketEngineSeenOrderIdRollback, AcceptedFokExpiredConsumesOrderId) {
  auto f = ExchangeFixture::Perp();
  constexpr lobx::OrderId id = 910040;

  auto expired = f.exchange.submit_limit(f.perp_symbol, f.alice, id, lob::Side::Bid,
                                         100, 2, lob::FOK, 1);
  expect_ok(expired);
  EXPECT_EQ(expired.exec.filled, 0);
  EXPECT_EQ(expired.exec.remaining, 2);
  EXPECT_EQ(event_count(f.exchange.events(), "order.expired"), 1);

  auto duplicate = f.exchange.submit_limit(f.perp_symbol, f.alice, id, lob::Side::Ask,
                                           101, 1, lob::POST_ONLY, 2);
  expect_duplicate(duplicate);
}

TEST(MarketEngineSeenOrderIdRollback, PostOnlyCrossingRejectDoesNotConsumeOrderId) {
  auto f = ExchangeFixture::Perp();
  rest_perp_ask(f, f.bob, 910049, 100, 1);

  auto rejected = f.exchange.submit_limit(f.perp_symbol, f.alice, 910050, lob::Side::Bid,
                                          100, 1, lob::POST_ONLY, 2);
  EXPECT_FALSE(rejected.accepted);
  EXPECT_EQ(rejected.code, lobx::RejectCode::PostOnlyWouldCross);

  auto retry = f.exchange.submit_limit(f.perp_symbol, f.alice, 910050, lob::Side::Bid,
                                       99, 1, lob::POST_ONLY, 3);
  expect_ok(retry);
}

TEST(MarketEngineSeenOrderIdRollback, ReduceOnlyRejectDoesNotConsumeOrderId) {
  auto f = ExchangeFixture::Perp();
  constexpr lobx::OrderId id = 910060;

  auto rejected = f.exchange.submit_limit(f.perp_symbol, f.alice, id, lob::Side::Bid,
                                          100, 1, lobx::LOBX_REDUCE_ONLY | lob::POST_ONLY, 1);
  EXPECT_FALSE(rejected.accepted);
  EXPECT_EQ(rejected.code, lobx::RejectCode::ReduceOnlyWouldIncrease);

  auto retry = f.exchange.submit_limit(f.perp_symbol, f.alice, id, lob::Side::Ask,
                                       100, 1, lob::POST_ONLY, 2);
  expect_ok(retry);
}

TEST(MarketEngineSeenOrderIdRollback, FailedTriggerChildDoesNotConsumeOrderId) {
  auto f = ExchangeFixture::Perp();
  constexpr lobx::OrderId child_id = 910080;

  expect_ok(f.exchange.set_perp_fee_config(f.perp_symbol, lobx::PerpFeeConfig{0, 10000, 0}));
  rest_perp_ask(f, f.bob, 910079, 100, 10000);
  const auto alice_before = f.exchange.balance(f.alice, "USDT");
  expect_ok(create_index_trigger(f, child_id, lob::Side::Bid, 10000, 100,
                                 lobx::TriggerChildOrderType::Market, 0, 100));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 3), 0);
  EXPECT_EQ(event_count(f.exchange.events(), "trigger.failed"), 1);
  EXPECT_EQ(trigger_by_id(f, child_id).status, lobx::TriggerOrderStatus::Failed);
  EXPECT_EQ(trigger_by_id(f, child_id).child_order_id, child_id);
  EXPECT_EQ(f.exchange.balance(f.alice, "USDT").total, alice_before.total);
  EXPECT_EQ(f.exchange.balance(f.alice, "USDT").locked, alice_before.locked);

  auto retry = f.exchange.submit_limit(f.perp_symbol, f.alice, child_id, lob::Side::Bid,
                                       90, 1, lob::POST_ONLY, 4);
  expect_ok(retry);
}

TEST(MarketEngineSeenOrderIdRollback, SuccessfulTriggerChildConsumesOrderId) {
  auto f = ExchangeFixture::Perp();
  constexpr lobx::OrderId child_id = 910090;

  rest_perp_ask(f, f.bob, 910089, 100, 1);
  expect_ok(create_index_trigger(f, child_id, lob::Side::Bid, 1, 100,
                                 lobx::TriggerChildOrderType::Market, 0, 100));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 3), 1);
  EXPECT_EQ(event_count(f.exchange.events(), "trigger.child_order"), 1);
  EXPECT_EQ(trigger_by_id(f, child_id).status, lobx::TriggerOrderStatus::Triggered);
  EXPECT_EQ(trigger_by_id(f, child_id).child_order_id, child_id);

  auto duplicate = f.exchange.submit_limit(f.perp_symbol, f.alice, child_id, lob::Side::Bid,
                                           99, 1, lob::POST_ONLY, 4);
  expect_duplicate(duplicate);
}

TEST(MarketEngineSeenOrderIdRollback, MutationFailureRollbackDoesNotConsumeOrderId) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/100);
  force_fee_account_credit_failure(f);
  constexpr lobx::OrderId id = 910100;

  expect_ok(f.submit(f.alice, 910099, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1));
  const auto bob_base_before = f.ledger.balance(f.bob, f.base_asset);

  auto failed = f.submit(f.bob, id, lob::Side::Bid, 100, 1, lob::IOC, 2);
  EXPECT_FALSE(failed.accepted);
  EXPECT_EQ(failed.code, lobx::RejectCode::InternalError);
  EXPECT_EQ(f.ledger.balance(f.bob, f.base_asset).total, bob_base_before.total);
  EXPECT_EQ(f.engine.topN(lob::Side::Ask, 10)[0].second, 1);
  EXPECT_EQ(event_count(f.events, "trade"), 0);

  auto retry = f.submit(f.bob, id, lob::Side::Bid, 99, 1, lob::POST_ONLY, 3);
  expect_ok(retry);
  EXPECT_EQ(f.engine.topN(lob::Side::Bid, 10)[0].first, 99);
  EXPECT_EQ(f.engine.topN(lob::Side::Bid, 10)[0].second, 1);
}

TEST(MarketEngineSeenOrderIdRollback, SpotAcceptedRestingOrderConsumesOrderId) {
  SpotEngineFixture f;
  constexpr lobx::OrderId id = 910200;

  auto first = f.submit(f.alice, id, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1);
  expect_ok(first);

  auto duplicate = f.submit(f.alice, id, lob::Side::Ask, 101, 1, lob::POST_ONLY, 2);
  expect_duplicate(duplicate);
}

TEST(MarketEngineSeenOrderIdRollback, SpotRejectedOrderDoesNotConsumeOrderId) {
  SpotEngineFixture f;
  constexpr lobx::OrderId id = 910210;

  auto rejected = f.submit(f.alice, id, lob::Side::Ask, 100, 0, lob::POST_ONLY, 1);
  EXPECT_FALSE(rejected.accepted);
  expect_not_duplicate(rejected);

  auto retry = f.submit(f.alice, id, lob::Side::Ask, 100, 1, lob::POST_ONLY, 2);
  expect_ok(retry);
}
