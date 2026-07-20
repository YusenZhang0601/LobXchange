#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

#include <string>
#include <vector>

using namespace lobx_test;

namespace {

void expect_ok(const lobx::Result& result) {
  EXPECT_TRUE_MSG(result.ok, result.reason);
}

void expect_ok(const lobx::SubmitResult& result) {
  EXPECT_TRUE_MSG(result.accepted, result.reason);
}

std::vector<lobx::PerpRiskTier> high_maintenance_tiers() {
  return {lobx::PerpRiskTier{0, 0, 1000, 10000, 10}};
}

void set_infinite_insurance(ExchangeFixture& f) {
  lobx::LiquidationOptions options;
  options.mode = lobx::LiquidationMode::InfiniteInsurance;
  f.exchange.set_liquidation_options(options);
}

void prepare_perp(ExchangeFixture& f) {
  expect_ok(f.exchange.set_perp_risk_tiers(f.perp_market_id, high_maintenance_tiers()));
  expect_ok(f.exchange.set_mark_price_mode(f.perp_market_id, lobx::MarkPriceMode::IndexPrice));
  f.exchange.set_leverage(f.alice, f.perp_symbol, 10);
  f.exchange.set_leverage(f.bob, f.perp_symbol, 10);
  f.exchange.set_leverage(f.carol, f.perp_symbol, 10);
}

void open_long(ExchangeFixture& f, lobx::UserId user, lobx::OrderId base_id,
               lob::Quantity qty = 100000) {
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.bob, base_id, lob::Side::Ask,
                                    100, qty, lob::POST_ONLY, base_id));
  expect_ok(f.exchange.submit_limit(f.perp_symbol, user, base_id + 1, lob::Side::Bid,
                                    100, qty, lob::IOC, base_id + 1));
}

void open_short(ExchangeFixture& f, lobx::UserId user, lobx::OrderId base_id,
                lob::Quantity qty = 100000) {
  expect_ok(f.exchange.submit_limit(f.perp_symbol, f.bob, base_id, lob::Side::Bid,
                                    100, qty, lob::POST_ONLY, base_id));
  expect_ok(f.exchange.submit_limit(f.perp_symbol, user, base_id + 1, lob::Side::Ask,
                                    100, qty, lob::IOC, base_id + 1));
}

int event_count(const lobx::EventStore& events, const std::string& type) {
  int count = 0;
  for (const auto& record : events.records()) {
    if (record.type == type) ++count;
  }
  return count;
}

std::string last_payload(const lobx::EventStore& events, const std::string& type) {
  for (auto it = events.records().rbegin(); it != events.records().rend(); ++it) {
    if (it->type == type) return it->payload;
  }
  return {};
}

void expect_payload_contains(const std::string& payload, const std::string& needle) {
  EXPECT_TRUE_MSG(payload.find(needle) != std::string::npos, payload);
}

} // namespace

TEST(LiquidationRealism, NoopWhenPositionIsHealthy) {
  auto f = ExchangeFixture::Perp();
  prepare_perp(f);
  set_infinite_insurance(f);
  open_long(f, f.alice, 950001, 1000);
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 100));
  const auto position_before = f.exchange.position(f.alice, f.perp_symbol);
  const auto balance_before = f.exchange.balance(f.alice, "USDT");

  const auto result = f.exchange.liquidate_if_needed(f.perp_symbol, f.alice, 10);

  expect_ok(result);
  EXPECT_FALSE(f.exchange.is_liquidatable(f.alice, f.perp_symbol));
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, position_before.signed_qty);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).entry_price, position_before.entry_price);
  EXPECT_EQ(f.exchange.balance(f.alice, "USDT").total, balance_before.total);
  EXPECT_EQ(f.exchange.balance(f.alice, "USDT").locked, balance_before.locked);
  EXPECT_EQ(event_count(f.exchange.events(), "liquidation"), 0);
}

TEST(LiquidationRealism, LongPositionLiquidatesWhenMarkPriceCrashes) {
  auto f = ExchangeFixture::Perp();
  prepare_perp(f);
  set_infinite_insurance(f);
  open_long(f, f.alice, 950011);
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 1));
  EXPECT_TRUE(f.exchange.is_liquidatable(f.alice, f.perp_symbol));

  const auto result = f.exchange.liquidate_if_needed(f.perp_symbol, f.alice, 20);

  expect_ok(result);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 0);
  EXPECT_EQ(f.exchange.balance(f.alice, "USDT").locked, 0);
  EXPECT_EQ(f.exchange.insurance_fund_balance(f.perp_symbol), 0);
  EXPECT_EQ(f.exchange.bad_debt(f.perp_symbol), 0);
  EXPECT_EQ(event_count(f.exchange.events(), "insurance_fund.absorbed_loss"), 1);
  EXPECT_EQ(event_count(f.exchange.events(), "liquidation"), 1);
}

TEST(LiquidationRealism, ShortPositionLiquidatesWhenMarkPriceSpikes) {
  auto f = ExchangeFixture::Perp();
  prepare_perp(f);
  set_infinite_insurance(f);
  open_short(f, f.alice, 950021);
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 200));
  EXPECT_TRUE(f.exchange.is_liquidatable(f.alice, f.perp_symbol));

  const auto result = f.exchange.liquidate_if_needed(f.perp_symbol, f.alice, 30);

  expect_ok(result);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 0);
  EXPECT_EQ(f.exchange.balance(f.alice, "USDT").locked, 0);
  EXPECT_EQ(f.exchange.bad_debt(f.perp_symbol), 0);
  EXPECT_EQ(event_count(f.exchange.events(), "insurance_fund.absorbed_loss"), 1);
}

TEST(LiquidationRealism, LiquidationDoesNotRequireOrderBookLiquidity) {
  auto f = ExchangeFixture::Perp();
  prepare_perp(f);
  set_infinite_insurance(f);
  open_long(f, f.alice, 950031);
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 1));
  EXPECT_TRUE(f.exchange.topN(f.perp_symbol, lob::Side::Bid, 5).empty());
  EXPECT_TRUE(f.exchange.topN(f.perp_symbol, lob::Side::Ask, 5).empty());

  const auto result = f.exchange.liquidate_if_needed(f.perp_symbol, f.alice, 40);

  expect_ok(result);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, 0);
  EXPECT_TRUE(f.exchange.topN(f.perp_symbol, lob::Side::Bid, 5).empty());
  EXPECT_TRUE(f.exchange.topN(f.perp_symbol, lob::Side::Ask, 5).empty());
}

TEST(LiquidationRealism, InfiniteInsuranceNeverFailsForInsufficientFund) {
  auto f = ExchangeFixture::Perp();
  prepare_perp(f);
  set_infinite_insurance(f);
  open_long(f, f.alice, 950041);
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 1));

  const auto result = f.exchange.liquidate_if_needed(f.perp_symbol, f.alice, 50);

  expect_ok(result);
  EXPECT_EQ(f.exchange.insurance_fund_balance(f.perp_symbol), 0);
  EXPECT_EQ(f.exchange.bad_debt(f.perp_symbol), 0);
  const std::string payload = last_payload(f.exchange.events(), "insurance_fund.absorbed_loss");
  expect_payload_contains(payload, "amount=8900000");
  expect_payload_contains(payload, "mode=infinite_insurance");
}

TEST(LiquidationRealism, LiquidationIsDisabledByDefault) {
  auto f = ExchangeFixture::Perp();
  prepare_perp(f);
  open_long(f, f.alice, 950051);
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 1));
  EXPECT_TRUE(f.exchange.is_liquidatable(f.alice, f.perp_symbol));
  const auto position_before = f.exchange.position(f.alice, f.perp_symbol);

  const auto result = f.exchange.liquidate_if_needed(f.perp_symbol, f.alice, 60);

  expect_ok(result);
  EXPECT_EQ(f.exchange.position(f.alice, f.perp_symbol).signed_qty, position_before.signed_qty);
  EXPECT_EQ(event_count(f.exchange.events(), "liquidation"), 0);
}

TEST(LiquidationRealism, LiquidationEventContainsUsefulDiagnostics) {
  auto f = ExchangeFixture::Perp();
  prepare_perp(f);
  set_infinite_insurance(f);
  open_long(f, f.alice, 950061);
  expect_ok(f.exchange.set_index_price(f.perp_market_id, 1));

  expect_ok(f.exchange.liquidate_if_needed(f.perp_symbol, f.alice, 70));

  const std::string payload = last_payload(f.exchange.events(), "liquidation");
  expect_payload_contains(payload, "account_id=10");
  expect_payload_contains(payload, "market_id=");
  expect_payload_contains(payload, "position_qty=100000");
  expect_payload_contains(payload, "mark_price=1");
  expect_payload_contains(payload, "qty=100000");
  expect_payload_contains(payload, "loss=9900000");
  expect_payload_contains(payload, "account_loss_paid=1000000");
  expect_payload_contains(payload, "insurance_paid=8900000");
  expect_payload_contains(payload, "bad_debt=0");
  expect_payload_contains(payload, "mode=infinite_insurance");
}
