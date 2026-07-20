#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

using namespace lobx_test;

namespace {

void expect_ok(const lobx::SubmitResult& result) {
  EXPECT_TRUE_MSG(result.accepted, result.reason);
}

lobx::SubmitResult submit_market(PerpEngineFixture& f, lobx::UserId user, lobx::OrderId id, lob::Side side,
                                 lob::Quantity qty, lob::Tick protection, uint32_t flags = lob::NONE) {
  lobx::OrderRequest req{f.market.id, user, id, f.events.next_seq(), static_cast<lob::Timestamp>(id), side, protection, qty, flags};
  return f.engine.submit_market(req, protection);
}

void rest_ask(PerpEngineFixture& f, lobx::UserId user, lobx::OrderId id, lob::Tick price, lob::Quantity qty) {
  expect_ok(f.submit(user, id, lob::Side::Ask, price, qty, lob::POST_ONLY, static_cast<lob::Timestamp>(id)));
}

void rest_bid(PerpEngineFixture& f, lobx::UserId user, lobx::OrderId id, lob::Tick price, lob::Quantity qty) {
  expect_ok(f.submit(user, id, lob::Side::Bid, price, qty, lob::POST_ONLY, static_cast<lob::Timestamp>(id)));
}

int event_count(const lobx::EventStore& events, const std::string& type) {
  int count = 0;
  for (const auto& record : events.records()) if (record.type == type) ++count;
  return count;
}

} // namespace

TEST(PerpMarketOrder, PERP_MKT_ORD_001MarketBuyConsumesAsks) {
  PerpEngineFixture f;
  rest_ask(f, f.bob, 83001, 100, 2);

  const auto result = submit_market(f, f.alice, 83002, lob::Side::Bid, 2, 100);

  expect_ok(result);
  EXPECT_EQ(result.trades.size(), 1UL);
  EXPECT_EQ(result.trades[0].price, 100);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 2);
}

TEST(PerpMarketOrder, PERP_MKT_ORD_002MarketSellConsumesBids) {
  PerpEngineFixture f;
  rest_bid(f, f.bob, 83011, 100, 3);

  const auto result = submit_market(f, f.alice, 83012, lob::Side::Ask, 3, 100);

  expect_ok(result);
  EXPECT_EQ(result.trades.size(), 1UL);
  EXPECT_EQ(result.trades[0].price, 100);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, -3);
}

TEST(PerpMarketOrder, PERP_MKT_ORD_003MarketOrderNeverRests) {
  PerpEngineFixture f;
  rest_ask(f, f.bob, 83021, 100, 1);

  expect_ok(submit_market(f, f.alice, 83022, lob::Side::Bid, 1, 100));

  for (const auto& order : f.engine.open_orders()) {
    EXPECT_NE(order.id, 83022);
  }
}

TEST(PerpMarketOrder, PERP_MKT_ORD_004MarketBuySlippageProtectionRejects) {
  PerpEngineFixture f;
  rest_ask(f, f.bob, 83031, 101, 1);
  const auto balance_before = f.ledger.balance(f.alice, f.margin_asset);

  const auto result = submit_market(f, f.alice, 83032, lob::Side::Bid, 1, 100);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, balance_before.total);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);
}

TEST(PerpMarketOrder, PERP_MKT_ORD_005MarketSellSlippageProtectionRejects) {
  PerpEngineFixture f;
  rest_bid(f, f.bob, 83041, 99, 1);

  const auto result = submit_market(f, f.alice, 83042, lob::Side::Ask, 1, 100);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);
}

TEST(PerpMarketOrder, PERP_MKT_ORD_006MarketOrderRequiresProtectionPrice) {
  PerpEngineFixture f;

  const auto result = submit_market(f, f.alice, 83052, lob::Side::Bid, 1, 0);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.code, lobx::RejectCode::InvalidPrice);
}

TEST(PerpMarketOrder, PERP_MKT_ORD_007MarketOrderSupportsReduceOnly) {
  PerpEngineFixture f;
  rest_ask(f, f.bob, 83061, 100, 2);
  expect_ok(submit_market(f, f.alice, 83062, lob::Side::Bid, 2, 100));
  rest_bid(f, f.carol, 83063, 105, 1);

  const auto close = submit_market(f, f.alice, 83064, lob::Side::Ask, 1, 105, lobx::LOBX_REDUCE_ONLY);

  expect_ok(close);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 1);
}

TEST(PerpMarketOrder, PERP_MKT_ORD_008MarketReduceOnlyCannotIncreasePosition) {
  PerpEngineFixture f;
  rest_ask(f, f.bob, 83071, 100, 1);

  const auto result = submit_market(f, f.alice, 83072, lob::Side::Bid, 1, 100, lobx::LOBX_REDUCE_ONLY);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(result.code, lobx::RejectCode::ReduceOnlyWouldIncrease);
}

TEST(PerpMarketOrder, PERP_MKT_ORD_009MarketOrderSupportsStp) {
  PerpEngineFixture f;
  rest_ask(f, f.alice, 83081, 100, 1);

  const auto result = submit_market(f, f.alice, 83082, lob::Side::Bid, 1, 100, lob::STP);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);
  EXPECT_EQ(f.engine.open_orders().size(), 1UL);
}

TEST(PerpMarketOrder, PERP_MKT_ORD_010MarketOrderChargesTakerFee) {
  PerpEngineFixture f;
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{0, 100, 0}).ok);
  rest_ask(f, f.bob, 83091, 10000, 1);

  expect_ok(submit_market(f, f.alice, 83092, lob::Side::Bid, 1, 10000));

  EXPECT_EQ(f.engine.account_fee_total(f.alice), 100);
}

TEST(PerpMarketOrder, PERP_MKT_ORD_011FailedMarketOrderRollsBackAllState) {
  PerpEngineFixture f(1000);
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{0, 10000, 0}).ok);
  rest_ask(f, f.bob, 83101, 100, 10);
  const auto alice_before = f.ledger.balance(f.alice, f.margin_asset);
  const auto bob_before = f.ledger.balance(f.bob, f.margin_asset);
  const auto open_before = f.engine.open_orders().size();

  const auto result = submit_market(f, f.alice, 83102, lob::Side::Bid, 10, 100);

  EXPECT_FALSE(result.accepted);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, alice_before.total);
  EXPECT_EQ(f.ledger.balance(f.bob, f.margin_asset).locked, bob_before.locked);
  EXPECT_EQ(f.positions.position(f.alice, f.market.id).signed_qty, 0);
  EXPECT_EQ(f.engine.account_fee_total(f.alice), 0);
  EXPECT_EQ(f.engine.open_orders().size(), open_before);
}

TEST(PerpMarketOrder, PERP_MKT_ORD_012MarketOrderEmitsEvent) {
  PerpEngineFixture f;
  rest_ask(f, f.bob, 83111, 100, 1);

  expect_ok(submit_market(f, f.alice, 83112, lob::Side::Bid, 1, 100));

  EXPECT_EQ(event_count(f.events, "order.market"), 1);
  EXPECT_EQ(event_count(f.events, "trade"), 1);
}
