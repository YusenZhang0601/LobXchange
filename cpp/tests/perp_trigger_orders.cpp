#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

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
  for (const auto& record : events.records()) if (record.type == type) ++count;
  return count;
}

void rest_ask(ExchangeFixture& f, lobx::UserId user, lobx::OrderId id, lob::Tick price, lob::Quantity qty) {
  expect_ok(f.exchange.submit_limit(f.perp_symbol, user, id, lob::Side::Ask, price, qty, lob::POST_ONLY, id));
}

void rest_bid(ExchangeFixture& f, lobx::UserId user, lobx::OrderId id, lob::Tick price, lob::Quantity qty) {
  expect_ok(f.exchange.submit_limit(f.perp_symbol, user, id, lob::Side::Bid, price, qty, lob::POST_ONLY, id));
}

void open_long(ExchangeFixture& f, lob::Tick price, lob::Quantity qty, lobx::OrderId base_id) {
  rest_ask(f, f.bob, base_id, price, qty);
  expect_ok(f.exchange.submit_market(f.perp_symbol, f.alice, base_id + 1, lob::Side::Bid, qty, price, lob::NONE, base_id + 1));
}

void open_short(ExchangeFixture& f, lob::Tick price, lob::Quantity qty, lobx::OrderId base_id) {
  rest_bid(f, f.bob, base_id, price, qty);
  expect_ok(f.exchange.submit_market(f.perp_symbol, f.alice, base_id + 1, lob::Side::Ask, qty, price, lob::NONE, base_id + 1));
}

lobx::Result create_trigger(ExchangeFixture& f, lobx::OrderId id, lob::Side side, lob::Quantity qty,
                            lob::Tick trigger_price, lobx::TriggerPriceType price_type,
                            lobx::TriggerCondition condition, lobx::TriggerChildOrderType child_type,
                            lob::Tick child_limit, lob::Tick protection, uint32_t flags = lob::NONE) {
  return f.exchange.create_trigger_order(f.perp_symbol, f.alice, id, side, qty, trigger_price,
                                         price_type, condition, child_type, child_limit, protection, flags, id);
}

lobx::TriggerOrder trigger_by_id(const ExchangeFixture& f, lobx::OrderId id) {
  for (const auto& trigger : f.exchange.trigger_orders(f.perp_symbol)) {
    if (trigger.request.trigger_order_id == id) return trigger;
  }
  return {};
}

void set_index_mark(ExchangeFixture& f, lob::Tick price) {
  expect_ok(f.exchange.set_index_price(f.perp_market_id, price));
  expect_ok(f.exchange.set_mark_price_mode(f.perp_market_id, lobx::MarkPriceMode::IndexPrice));
}

} // namespace

TEST(PerpTriggerOrders, PERP_TRG_001CreateTriggerOrder) {
  auto f = ExchangeFixture::Perp();

  expect_ok(create_trigger(f, 84001, lob::Side::Bid, 1, 100, lobx::TriggerPriceType::Mark,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Market, 0, 101));

  EXPECT_EQ(f.exchange.trigger_orders(f.perp_symbol).size(), 1UL);
  EXPECT_EQ(trigger_by_id(f, 84001).status, lobx::TriggerOrderStatus::Active);
}

TEST(PerpTriggerOrders, PERP_TRG_002TriggerOrderNotInLobBeforeTriggered) {
  auto f = ExchangeFixture::Perp();

  expect_ok(create_trigger(f, 84011, lob::Side::Bid, 1, 100, lobx::TriggerPriceType::Mark,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Limit, 99, 0));

  EXPECT_TRUE(f.exchange.topN(f.perp_symbol, lob::Side::Bid, 10).empty());
  EXPECT_TRUE(f.exchange.topN(f.perp_symbol, lob::Side::Ask, 10).empty());
}

TEST(PerpTriggerOrders, PERP_TRG_003CancelTriggerOrder) {
  auto f = ExchangeFixture::Perp();
  expect_ok(create_trigger(f, 84021, lob::Side::Bid, 1, 100, lobx::TriggerPriceType::Mark,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Market, 0, 101));

  EXPECT_TRUE(f.exchange.cancel_trigger_order(f.perp_symbol, f.alice, 84021, 2));

  EXPECT_EQ(trigger_by_id(f, 84021).status, lobx::TriggerOrderStatus::Cancelled);
}

TEST(PerpTriggerOrders, PERP_TRG_004MarkAboveTriggers) {
  auto f = ExchangeFixture::Perp();
  rest_ask(f, f.bob, 84031, 101, 1);
  expect_ok(create_trigger(f, 84032, lob::Side::Bid, 1, 100, lobx::TriggerPriceType::Mark,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Market, 0, 101));
  set_index_mark(f, 100);

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Mark, 3), 1);

  EXPECT_EQ(trigger_by_id(f, 84032).status, lobx::TriggerOrderStatus::Triggered);
}

TEST(PerpTriggerOrders, PERP_TRG_005MarkBelowTriggers) {
  auto f = ExchangeFixture::Perp();
  rest_bid(f, f.bob, 84041, 99, 1);
  expect_ok(create_trigger(f, 84042, lob::Side::Ask, 1, 100, lobx::TriggerPriceType::Mark,
                           lobx::TriggerCondition::BelowOrEqual, lobx::TriggerChildOrderType::Market, 0, 99));
  set_index_mark(f, 100);

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Mark, 3), 1);
}

TEST(PerpTriggerOrders, PERP_TRG_006LastAboveTriggers) {
  auto f = ExchangeFixture::Perp();
  expect_ok(create_trigger(f, 84051, lob::Side::Bid, 1, 100, lobx::TriggerPriceType::Last,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Limit, 90, 0));
  rest_ask(f, f.bob, 84052, 100, 1);
  expect_ok(f.exchange.submit_market(f.perp_symbol, f.carol, 84053, lob::Side::Bid, 1, 100, lob::NONE, 3));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Last, 4), 1);
}

TEST(PerpTriggerOrders, PERP_TRG_007LastBelowTriggers) {
  auto f = ExchangeFixture::Perp();
  expect_ok(create_trigger(f, 84061, lob::Side::Ask, 1, 100, lobx::TriggerPriceType::Last,
                           lobx::TriggerCondition::BelowOrEqual, lobx::TriggerChildOrderType::Limit, 110, 0));
  rest_bid(f, f.bob, 84062, 100, 1);
  expect_ok(f.exchange.submit_market(f.perp_symbol, f.carol, 84063, lob::Side::Ask, 1, 100, lob::NONE, 3));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Last, 4), 1);
}

TEST(PerpTriggerOrders, PERP_TRG_008IndexAboveTriggers) {
  auto f = ExchangeFixture::Perp();
  expect_ok(create_trigger(f, 84071, lob::Side::Bid, 1, 100, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Limit, 90, 0));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 2), 1);
}

TEST(PerpTriggerOrders, PERP_TRG_009IndexBelowTriggers) {
  auto f = ExchangeFixture::Perp();
  expect_ok(create_trigger(f, 84081, lob::Side::Ask, 1, 100, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::BelowOrEqual, lobx::TriggerChildOrderType::Limit, 110, 0));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 2), 1);
}

TEST(PerpTriggerOrders, PERP_TRG_010TriggerFiresOnceOnly) {
  auto f = ExchangeFixture::Perp();
  expect_ok(create_trigger(f, 84091, lob::Side::Bid, 1, 100, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Limit, 90, 0));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 2), 1);
  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 3), 0);
}

TEST(PerpTriggerOrders, PERP_TRG_011TriggerCreatesChildMarketOrder) {
  auto f = ExchangeFixture::Perp();
  rest_ask(f, f.bob, 84101, 100, 1);
  expect_ok(create_trigger(f, 84102, lob::Side::Bid, 1, 100, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Market, 0, 100));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 3), 1);

  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 1);
}

TEST(PerpTriggerOrders, PERP_TRG_012TriggerCreatesChildLimitOrder) {
  auto f = ExchangeFixture::Perp();
  expect_ok(create_trigger(f, 84111, lob::Side::Bid, 1, 100, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Limit, 90, 0));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 2), 1);

  EXPECT_EQ(f.exchange.topN(f.perp_symbol, lob::Side::Bid, 10).size(), 1UL);
}

TEST(PerpTriggerOrders, PERP_TRG_013ChildOrderInheritsReduceOnly) {
  auto f = ExchangeFixture::Perp();
  open_long(f, 100, 2, 84120);
  rest_bid(f, f.carol, 84122, 110, 1);
  expect_ok(create_trigger(f, 84123, lob::Side::Ask, 1, 100, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Market, 0, 110, lobx::LOBX_REDUCE_ONLY));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 4), 1);

  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 1);
}

TEST(PerpTriggerOrders, PERP_TRG_014ChildOrderInheritsStp) {
  auto f = ExchangeFixture::Perp();
  rest_ask(f, f.alice, 84131, 100, 1);
  expect_ok(create_trigger(f, 84132, lob::Side::Bid, 1, 100, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Market, 0, 100, lob::STP));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 3), 0);

  EXPECT_EQ(trigger_by_id(f, 84132).status, lobx::TriggerOrderStatus::Failed);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 0);
}

TEST(PerpTriggerOrders, PERP_TRG_015ChildOrderFailureRecordsTriggerFailed) {
  auto f = ExchangeFixture::Perp();
  rest_ask(f, f.bob, 84141, 100, 1);
  expect_ok(create_trigger(f, 84142, lob::Side::Bid, 1, 100, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Market, 0, 100, lobx::LOBX_REDUCE_ONLY));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 3), 0);

  EXPECT_EQ(event_count(f.exchange.events(), "trigger.failed"), 1);
}

TEST(PerpTriggerOrders, PERP_TRG_016CancelTriggeredOrderFails) {
  auto f = ExchangeFixture::Perp();
  expect_ok(create_trigger(f, 84151, lob::Side::Bid, 1, 100, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Limit, 90, 0));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));
  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 2), 1);

  EXPECT_FALSE(f.exchange.cancel_trigger_order(f.perp_symbol, f.alice, 84151, 3));
}

TEST(PerpTriggerOrders, PERP_TRG_017TriggerRegistryRollbackSafe) {
  auto f = ExchangeFixture::Perp();
  expect_ok(f.exchange.set_perp_fee_config(f.perp_symbol, lobx::PerpFeeConfig{0, 10000, 0}));
  rest_ask(f, f.bob, 84161, 100, 10000);
  const auto balance_before = f.exchange.balance(f.alice, "USDT");
  expect_ok(create_trigger(f, 84162, lob::Side::Bid, 10000, 100, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Market, 0, 100));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 3), 0);

  EXPECT_EQ(trigger_by_id(f, 84162).status, lobx::TriggerOrderStatus::Failed);
  EXPECT_EQ(f.exchange.balance(f.alice, "USDT").total, balance_before.total);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 0);
}

TEST(PerpTriggerOrders, PERP_TRG_018TriggerEventsEmitted) {
  auto f = ExchangeFixture::Perp();
  expect_ok(create_trigger(f, 84171, lob::Side::Bid, 1, 100, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Limit, 90, 0));
  EXPECT_TRUE(f.exchange.cancel_trigger_order(f.perp_symbol, f.alice, 84171, 2));

  EXPECT_EQ(event_count(f.exchange.events(), "trigger.created"), 1);
  EXPECT_EQ(event_count(f.exchange.events(), "trigger.cancelled"), 1);
}

TEST(PerpTpSl, PERP_TP_001LongTakeProfitSellReduceOnly) {
  auto f = ExchangeFixture::Perp();
  open_long(f, 100, 2, 84200);
  rest_bid(f, f.carol, 84202, 120, 1);
  expect_ok(create_trigger(f, 84203, lob::Side::Ask, 1, 120, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Market, 0, 120, lobx::LOBX_REDUCE_ONLY));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 120));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 4), 1);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 1);
}

TEST(PerpTpSl, PERP_TP_002ShortTakeProfitBuyReduceOnly) {
  auto f = ExchangeFixture::Perp();
  open_short(f, 100, 2, 84210);
  rest_ask(f, f.carol, 84212, 80, 1);
  expect_ok(create_trigger(f, 84213, lob::Side::Bid, 1, 80, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::BelowOrEqual, lobx::TriggerChildOrderType::Market, 0, 80, lobx::LOBX_REDUCE_ONLY));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 80));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 4), 1);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, -1);
}

TEST(PerpTpSl, PERP_SL_001LongStopLossSellReduceOnly) {
  auto f = ExchangeFixture::Perp();
  open_long(f, 100, 2, 84220);
  rest_bid(f, f.carol, 84222, 90, 1);
  expect_ok(create_trigger(f, 84223, lob::Side::Ask, 1, 90, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::BelowOrEqual, lobx::TriggerChildOrderType::Market, 0, 90, lobx::LOBX_REDUCE_ONLY));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 90));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 4), 1);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 1);
}

TEST(PerpTpSl, PERP_SL_002ShortStopLossBuyReduceOnly) {
  auto f = ExchangeFixture::Perp();
  open_short(f, 100, 2, 84230);
  rest_ask(f, f.carol, 84232, 110, 1);
  expect_ok(create_trigger(f, 84233, lob::Side::Bid, 1, 110, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Market, 0, 110, lobx::LOBX_REDUCE_ONLY));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 110));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 4), 1);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, -1);
}

TEST(PerpTpSl, PERP_TP_003TpCannotIncreasePosition) {
  auto f = ExchangeFixture::Perp();
  rest_bid(f, f.carol, 84241, 120, 1);
  expect_ok(create_trigger(f, 84242, lob::Side::Ask, 1, 120, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Market, 0, 120, lobx::LOBX_REDUCE_ONLY));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 120));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 3), 0);
  EXPECT_EQ(trigger_by_id(f, 84242).status, lobx::TriggerOrderStatus::Failed);
}

TEST(PerpTpSl, PERP_SL_003SlCannotFlipPosition) {
  auto f = ExchangeFixture::Perp();
  open_long(f, 100, 1, 84250);
  rest_bid(f, f.carol, 84252, 90, 2);
  expect_ok(create_trigger(f, 84253, lob::Side::Ask, 2, 90, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::BelowOrEqual, lobx::TriggerChildOrderType::Market, 0, 90, lobx::LOBX_REDUCE_ONLY));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 90));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 4), 0);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 1);
}

TEST(PerpTpSl, PERP_TP_004TpChildOrderClosesOrReducesPosition) {
  auto f = ExchangeFixture::Perp();
  open_long(f, 100, 1, 84260);
  rest_bid(f, f.carol, 84262, 120, 1);
  expect_ok(create_trigger(f, 84263, lob::Side::Ask, 1, 120, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::AboveOrEqual, lobx::TriggerChildOrderType::Market, 0, 120, lobx::LOBX_REDUCE_ONLY));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 120));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 4), 1);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 0);
}

TEST(PerpTpSl, PERP_SL_004SlChildOrderClosesOrReducesPosition) {
  auto f = ExchangeFixture::Perp();
  open_long(f, 100, 1, 84270);
  rest_bid(f, f.carol, 84272, 90, 1);
  expect_ok(create_trigger(f, 84273, lob::Side::Ask, 1, 90, lobx::TriggerPriceType::Index,
                           lobx::TriggerCondition::BelowOrEqual, lobx::TriggerChildOrderType::Market, 0, 90, lobx::LOBX_REDUCE_ONLY));
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 90));

  EXPECT_EQ(f.exchange.evaluate_triggers(f.perp_symbol, lobx::TriggerPriceType::Index, 4), 1);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 0);
}
