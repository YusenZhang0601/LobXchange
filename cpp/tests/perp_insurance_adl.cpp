#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

#include <string>

using namespace lobx_test;

namespace {

void expect_ok(const lobx::Result& result) {
  EXPECT_TRUE_MSG(result.ok, result.reason);
}

void expect_ok(const lobx::SubmitResult& result) {
  EXPECT_TRUE_MSG(result.accepted, result.reason);
}

void rest_ask(PerpEngineFixture& f, lobx::UserId user, lobx::OrderId id, lob::Tick price, lob::Quantity qty) {
  expect_ok(f.submit(user, id, lob::Side::Ask, price, qty, lob::POST_ONLY, static_cast<lob::Timestamp>(id)));
}

void open_lossy_long(PerpEngineFixture& f, lobx::OrderId base_id) {
  rest_ask(f, f.bob, base_id, 100, 10);
  expect_ok(f.submit(f.alice, base_id + 1, lob::Side::Bid, 100, 10, lob::IOC, static_cast<lob::Timestamp>(base_id + 1)));
  expect_ok(f.engine.set_index_price(1));
  expect_ok(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice));
  EXPECT_TRUE(f.engine.is_liquidatable(f.alice));
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

void set_profitable_long(PerpEngineFixture& f, lobx::UserId user, lob::Tick entry, lob::Quantity qty) {
  EXPECT_TRUE(f.positions.apply_trade_checked(user, f.market.id, lob::Side::Bid, entry, qty));
}

void set_losing_short(PerpEngineFixture& f, lobx::UserId user, lob::Tick entry, lob::Quantity qty) {
  EXPECT_TRUE(f.positions.apply_trade_checked(user, f.market.id, lob::Side::Ask, entry, qty));
}

void use_index_mark(PerpEngineFixture& f, lob::Tick mark) {
  expect_ok(f.engine.set_index_price(mark));
  expect_ok(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice));
}

} // namespace

TEST(PerpInsuranceAdl, PERP_INS_001CreditInsuranceFundIncreasesBalance) {
  PerpEngineFixture f;

  expect_ok(f.engine.credit_insurance_fund(500, 1, "test_credit"));

  EXPECT_EQ(f.engine.insurance_fund_balance(), 500);
  EXPECT_EQ(event_count(f.events, "insurance_fund.credited"), 1);
  expect_payload_contains(last_payload(f.events, "insurance_fund.credited"), "balance_after=500");
}

TEST(PerpInsuranceAdl, PERP_INS_002RejectNonPositiveInsuranceFundCredit) {
  PerpEngineFixture f;

  EXPECT_FALSE(f.engine.credit_insurance_fund(0, 1, "zero").ok);
  EXPECT_FALSE(f.engine.credit_insurance_fund(-1, 1, "negative").ok);
  EXPECT_EQ(f.engine.insurance_fund_balance(), 0);
}

TEST(PerpInsuranceAdl, PERP_INS_003InsuranceFundStateRestoredOnRollback) {
  PerpEngineFixture f(300);
  expect_ok(f.engine.credit_insurance_fund(500, 1, "seed"));
  open_lossy_long(f, 82001);
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{0, 0, 10000}).ok);
  const auto events_before = f.events.records().size();

  const auto result = f.engine.liquidate_position(f.alice, f.carol, 2);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(f.engine.insurance_fund_balance(), 500);
  EXPECT_EQ(f.engine.bad_debt(), 0);
  EXPECT_EQ(f.events.records().size(), events_before);
}

TEST(PerpInsuranceAdl, PERP_INS_004LiquidationLossUsesInsuranceFund) {
  PerpEngineFixture f(300);
  expect_ok(f.engine.credit_insurance_fund(1000, 1, "seed"));
  open_lossy_long(f, 82011);

  expect_ok(f.engine.liquidate_position(f.alice, f.carol, 2));

  EXPECT_EQ(f.engine.insurance_fund_balance(), 310);
  EXPECT_EQ(f.engine.bad_debt(), 0);
  EXPECT_EQ(f.ledger.balance(f.alice, f.margin_asset).total, 0);
  EXPECT_EQ(event_count(f.events, "insurance_fund.debited"), 1);
}

TEST(PerpInsuranceAdl, PERP_INS_005InsuranceFundInsufficientRecordsBadDebt) {
  PerpEngineFixture f(300);
  expect_ok(f.engine.credit_insurance_fund(500, 1, "seed"));
  open_lossy_long(f, 82021);

  expect_ok(f.engine.liquidate_position(f.alice, f.carol, 2));

  EXPECT_EQ(f.engine.insurance_fund_balance(), 0);
  EXPECT_EQ(f.engine.bad_debt(), 190);
  EXPECT_EQ(500 + f.engine.bad_debt(), 690);
}

TEST(PerpInsuranceAdl, PERP_INS_006BadDebtQueryReturnsAccumulatedAmount) {
  PerpEngineFixture f(300);
  expect_ok(f.engine.credit_insurance_fund(500, 1, "seed"));
  open_lossy_long(f, 82031);

  expect_ok(f.engine.liquidate_position(f.alice, f.carol, 2));

  EXPECT_EQ(f.engine.bad_debt(), 190);
}

TEST(PerpInsuranceAdl, PERP_INS_007LiquidationEventIncludesInsurancePaidAndBadDebt) {
  PerpEngineFixture f(300);
  expect_ok(f.engine.credit_insurance_fund(500, 1, "seed"));
  open_lossy_long(f, 82041);

  expect_ok(f.engine.liquidate_position(f.alice, f.carol, 2));

  const std::string payload = last_payload(f.events, "liquidation");
  expect_payload_contains(payload, "account_id=10");
  expect_payload_contains(payload, "market_id=1");
  expect_payload_contains(payload, "position_qty=10");
  expect_payload_contains(payload, "mark_price=1");
  expect_payload_contains(payload, "loss=990");
  expect_payload_contains(payload, "insurance_paid=500");
  expect_payload_contains(payload, "bad_debt=190");
}

TEST(PerpInsuranceAdl, PERP_INS_008RollbackRestoresInsuranceFundAndBadDebt) {
  PerpEngineFixture f(300);
  expect_ok(f.engine.credit_insurance_fund(500, 1, "seed"));
  open_lossy_long(f, 82051);
  EXPECT_TRUE(f.engine.set_fee_config(lobx::PerpFeeConfig{0, 0, 10000}).ok);
  const lobx::Amount fund_before = f.engine.insurance_fund_balance();
  const lobx::Amount debt_before = f.engine.bad_debt();

  const auto result = f.engine.liquidate_position(f.alice, f.carol, 2);

  EXPECT_FALSE(result.ok);
  EXPECT_EQ(f.engine.insurance_fund_balance(), fund_before);
  EXPECT_EQ(f.engine.bad_debt(), debt_before);
  EXPECT_EQ(event_count(f.events, "insurance_fund.debited"), 0);
  EXPECT_EQ(event_count(f.events, "perp.bad_debt_recorded"), 0);
  EXPECT_EQ(event_count(f.events, "ADL_REQUIRED"), 0);
}

TEST(PerpInsuranceAdl, PERP_INS_009InsuranceFundDebitEventRecorded) {
  PerpEngineFixture f(300);
  expect_ok(f.engine.credit_insurance_fund(1000, 1, "seed"));
  open_lossy_long(f, 82061);

  expect_ok(f.engine.liquidate_position(f.alice, f.carol, 2));

  const std::string payload = last_payload(f.events, "insurance_fund.debited");
  expect_payload_contains(payload, "amount=690");
  expect_payload_contains(payload, "balance_after=310");
  expect_payload_contains(payload, "reason=liquidation_loss");
}

TEST(PerpInsuranceAdl, PERP_INS_010BadDebtEventRecorded) {
  PerpEngineFixture f(300);
  expect_ok(f.engine.credit_insurance_fund(500, 1, "seed"));
  open_lossy_long(f, 82071);

  expect_ok(f.engine.liquidate_position(f.alice, f.carol, 2));

  const std::string payload = last_payload(f.events, "perp.bad_debt_recorded");
  expect_payload_contains(payload, "account_id=10");
  expect_payload_contains(payload, "amount=190");
  expect_payload_contains(payload, "reason=liquidation_loss");
}

TEST(PerpInsuranceAdl, PERP_ADL_001InsuranceFundInsufficientEmitsAdlRequired) {
  PerpEngineFixture f(300);
  expect_ok(f.engine.credit_insurance_fund(500, 1, "seed"));
  open_lossy_long(f, 82081);

  expect_ok(f.engine.liquidate_position(f.alice, f.carol, 2));

  EXPECT_EQ(event_count(f.events, "ADL_REQUIRED"), 1);
  expect_payload_contains(last_payload(f.events, "ADL_REQUIRED"), "bad_debt=190");
}

TEST(PerpInsuranceAdl, PERP_ADL_002AdlCandidatesRankedDeterministically) {
  PerpEngineFixture f;
  use_index_mark(f, 120);
  set_profitable_long(f, f.alice, 100, 10);
  set_profitable_long(f, f.carol, 110, 10);

  const auto candidates = f.engine.rank_adl_candidates();

  EXPECT_EQ(candidates.size(), 2UL);
  EXPECT_EQ(candidates[0].rank, 1);
  EXPECT_EQ(candidates[1].rank, 2);
  EXPECT_EQ(candidates[0].account_id, f.alice);
  EXPECT_EQ(candidates[1].account_id, f.carol);
}

TEST(PerpInsuranceAdl, PERP_ADL_003HighPnlRatioRanksBeforeLowPnlRatio) {
  PerpEngineFixture f;
  use_index_mark(f, 120);
  set_profitable_long(f, f.alice, 100, 10);
  set_profitable_long(f, f.carol, 110, 10);

  const auto candidates = f.engine.rank_adl_candidates();

  EXPECT_EQ(candidates[0].account_id, f.alice);
  EXPECT_TRUE(candidates[0].pnl_ratio > candidates[1].pnl_ratio);
}

TEST(PerpInsuranceAdl, PERP_ADL_004HighLeverageTieBreaksPnlRatio) {
  PerpEngineFixture f;
  use_index_mark(f, 120);
  set_profitable_long(f, f.alice, 100, 20);
  set_profitable_long(f, f.carol, 100, 10);

  const auto candidates = f.engine.rank_adl_candidates();

  EXPECT_EQ(candidates[0].account_id, f.alice);
  EXPECT_TRUE(candidates[0].effective_leverage > candidates[1].effective_leverage);
}

TEST(PerpInsuranceAdl, PERP_ADL_005AccountIdDeterministicTieBreak) {
  PerpEngineFixture f;
  use_index_mark(f, 120);
  set_profitable_long(f, f.bob, 100, 10);
  set_profitable_long(f, f.alice, 100, 10);

  const auto candidates = f.engine.rank_adl_candidates();

  EXPECT_EQ(candidates.size(), 2UL);
  EXPECT_EQ(candidates[0].account_id, f.alice);
  EXPECT_EQ(candidates[1].account_id, f.bob);
}

TEST(PerpInsuranceAdl, PERP_ADL_006NoCandidatesWhenNoProfitableOpposingPositions) {
  PerpEngineFixture f;
  use_index_mark(f, 120);
  set_profitable_long(f, f.alice, 130, 10);
  set_losing_short(f, f.bob, 100, 10);

  const auto candidates = f.engine.rank_adl_candidates();

  EXPECT_TRUE(candidates.empty());
}
