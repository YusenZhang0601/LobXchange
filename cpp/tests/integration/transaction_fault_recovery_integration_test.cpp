#define LOBX_TESTING 1

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

struct EngineSnapshotState {
  lobx::WalletBalance alice_wallet;
  lobx::WalletBalance bob_wallet;
  lobx::WalletBalance carol_wallet;
  lobx::WalletBalance david_wallet;
  lobx::Position alice_position;
  lobx::Position bob_position;
  lobx::Position carol_position;
  lobx::Position david_position;
  lobx::Amount alice_fee;
  lobx::Amount bob_fee;
  lobx::Amount insurance_fund;
  lobx::Amount bad_debt;
  size_t total_events;
};

EngineSnapshotState capture_engine_state(PerpEngineFixture& f, lobx::UserId david = 40) {
  return EngineSnapshotState{
      f.ledger.balance(f.alice, f.margin_asset),
      f.ledger.balance(f.bob, f.margin_asset),
      f.ledger.balance(f.carol, f.margin_asset),
      f.ledger.balance(david, f.margin_asset),
      f.positions.position(f.alice, f.market.id),
      f.positions.position(f.bob, f.market.id),
      f.positions.position(f.carol, f.market.id),
      f.positions.position(david, f.market.id),
      f.engine.account_fee_total(f.alice),
      f.engine.account_fee_total(f.bob),
      f.engine.insurance_fund_balance(),
      f.engine.bad_debt(),
      f.events.records().size()};
}

void expect_engine_state_restored(PerpEngineFixture& f, const EngineSnapshotState& before, lobx::UserId david = 40) {
  expect_wallet_eq(f.ledger.balance(f.alice, f.margin_asset), before.alice_wallet);
  expect_wallet_eq(f.ledger.balance(f.bob, f.margin_asset), before.bob_wallet);
  expect_wallet_eq(f.ledger.balance(f.carol, f.margin_asset), before.carol_wallet);
  expect_wallet_eq(f.ledger.balance(david, f.margin_asset), before.david_wallet);

  expect_position_eq(f.positions.position(f.alice, f.market.id), before.alice_position);
  expect_position_eq(f.positions.position(f.bob, f.market.id), before.bob_position);
  expect_position_eq(f.positions.position(f.carol, f.market.id), before.carol_position);
  expect_position_eq(f.positions.position(david, f.market.id), before.david_position);

  EXPECT_EQ(f.engine.account_fee_total(f.alice), before.alice_fee);
  EXPECT_EQ(f.engine.account_fee_total(f.bob), before.bob_fee);
  EXPECT_EQ(f.engine.insurance_fund_balance(), before.insurance_fund);
  EXPECT_EQ(f.engine.bad_debt(), before.bad_debt);

  // 断言 Events 记录完全零污染零残留
  EXPECT_EQ(f.events.records().size(), before.total_events);
  EXPECT_TRUE(f.ledger.invariant_ok());
}

void set_fault(PerpEngineFixture& f, lobx::MarketEngineFaultPoint point) {
  f.engine.set_fault_point_for_testing(point);
}

void clear_fault(PerpEngineFixture& f) {
  f.engine.clear_fault_point_for_testing();
}

} // namespace

// ============================================================================
// FAULT_REC_001: 强平清算 (liquidate_position) 故障回滚与账本零残留
// ============================================================================
TEST(TransactionFaultRecoveryIntegration, FAULT_REC_001_LiquidationRollsBackOnFailure) {
  PerpEngineFixture f(1000);

  // 1. 设置标记价格模式为 LastTrade
  expect_ok(f.engine.set_mark_price_mode(lobx::MarkPriceMode::LastTrade));

  // 2. Bob 建立 100 价格的空头仓位
  expect_ok(f.submit(f.bob, 950001, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1000));
  expect_ok(f.submit(f.alice, 950002, lob::Side::Bid, 100, 10, lob::IOC, 1001));

  // 3. Alice 与 Carol 在 150 成交使盘口爆涨，使 Bob 处于可被强平状态
  expect_ok(f.submit(f.carol, 950003, lob::Side::Ask, 150, 10, lob::POST_ONLY, 1002));
  expect_ok(f.submit(f.alice, 950004, lob::Side::Bid, 150, 10, lob::IOC, 1003));
  EXPECT_TRUE(f.engine.is_liquidatable(f.bob));

  // 抓取强平尝试前的完整状态快照
  const EngineSnapshotState before_liq = capture_engine_state(f);

  // 4. 注入 ForceLedgerInvariantFailure 故障点
  set_fault(f, lobx::MarketEngineFaultPoint::ForceLedgerInvariantFailure);

  // 尝试强平应当因为故障注入而被拒绝
  auto liq_result = f.engine.liquidate_position(f.bob, f.carol, 1004);
  EXPECT_FALSE(liq_result.ok);

  clear_fault(f);

  // 5. 校验引擎状态、资金账本、仓位与事件完全恢复无损
  expect_engine_state_restored(f, before_liq);
  EXPECT_TRUE(f.engine.is_liquidatable(f.bob));

  // 6. 清除故障后重试强平，验证强平功能顺利恢复通过
  expect_ok(f.engine.liquidate_position(f.bob, f.carol, 1005));
  EXPECT_FALSE(f.engine.is_liquidatable(f.bob));
  EXPECT_TRUE(f.ledger.invariant_ok());
}

// ============================================================================
// FAULT_REC_002: 资金费率批量划转 (settle_funding) 中途失败事务全盘回滚
// ============================================================================
TEST(TransactionFaultRecoveryIntegration, FAULT_REC_002_FundingSettlementRollsBackOnFailure) {
  PerpEngineFixture f(10000);

  // 1. 设置资金费率为正数 (100 bps = 1%)，即多头向空头支付资金费
  expect_ok(f.engine.set_funding_rate(100));

  // Alice 建多头 100 张，Bob 建空头 100 张 (价格 100)
  expect_ok(f.submit(f.bob, 950010, lob::Side::Ask, 100, 100, lob::POST_ONLY, 2000));
  expect_ok(f.submit(f.alice, 950011, lob::Side::Bid, 100, 100, lob::IOC, 2001));

  // 抓取资金费结算前的状态
  const EngineSnapshotState before_funding = capture_engine_state(f);

  // 正常结算一次资金费，校验能够正常通过
  expect_ok(f.engine.settle_funding(2002));
  EXPECT_NE(f.events.records().size(), before_funding.total_events);

  // 重新捕获最新状态
  const EngineSnapshotState before_failed_funding = capture_engine_state(f);

  // 人为将标记价格调为非法值 0，使 settle_funding 中途内部校验 fail
  // settle_funding 内部会触发 restore_snapshot
  expect_ok(f.engine.set_index_price(0));
  expect_ok(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice));

  auto failed_funding = f.engine.settle_funding(2003);
  EXPECT_FALSE(failed_funding.ok);

  // 恢复指数价格模式为 LastTrade
  expect_ok(f.engine.set_mark_price_mode(lobx::MarkPriceMode::LastTrade));

  // 校验资金费结算失败后，所有已变动的账户资金与事件已被完整撤销
  expect_engine_state_restored(f, before_failed_funding);
}

// ============================================================================
// FAULT_REC_003: 止盈止损触发单 (evaluate_triggers) 链式评估故障回滚与二次触发
// ============================================================================
TEST(TransactionFaultRecoveryIntegration, FAULT_REC_003_TriggerOrderRollsBackAndCanReEvaluate) {
  PerpEngineFixture f(2000);

  // 1. Alice 建立 10 张多头仓位
  expect_ok(f.submit(f.bob, 950020, lob::Side::Ask, 100, 10, lob::POST_ONLY, 3000));
  expect_ok(f.submit(f.alice, 950021, lob::Side::Bid, 100, 10, lob::IOC, 3001));

  // 2. Alice 创建一个止盈触发卖单：当价格达到 120 时，下发市价卖单 10 张
  constexpr lobx::OrderId trigger_id = 880001;
  expect_ok(f.engine.create_trigger_order(lobx::TriggerOrderRequest{
      f.market.id, f.alice, trigger_id, lob::Side::Ask, 10, 120,
      lobx::TriggerPriceType::Last, lobx::TriggerCondition::AboveOrEqual,
      lobx::TriggerChildOrderType::Market, 0, 100}));

  EXPECT_EQ(f.engine.trigger_orders().size(), 1);

  // 3. Carol 与 Bob 在 125 价格成交推升最新价至 125，满足触发条件
  expect_ok(f.submit(f.bob, 950022, lob::Side::Ask, 125, 5, lob::POST_ONLY, 3002));
  expect_ok(f.submit(f.carol, 950023, lob::Side::Bid, 125, 5, lob::IOC, 3003));

  const EngineSnapshotState before_eval = capture_engine_state(f);

  // 4. 在子订单挂簿/撮合中途注入 FaultPoint
  set_fault(f, lobx::MarketEngineFaultPoint::AfterBookSubmit);

  // 评估触发单，因为下发子订单时遭遇故障点，evaluate_triggers 内部下发失败
  int triggered_count = f.engine.evaluate_triggers(lobx::TriggerPriceType::Last, 3004);
  EXPECT_EQ(triggered_count, 0); // 没有成功评估发出的子单

  clear_fault(f);

  // 5. 校验引擎状态无破坏，触发单仍然完好留在列表（未损坏未丢弃）
  expect_engine_state_restored(f, before_eval);
  EXPECT_EQ(f.engine.trigger_orders().size(), 1);

  // 6. 清除故障后二次调用 evaluate_triggers，验证顺利触发并吃单平仓
  triggered_count = f.engine.evaluate_triggers(lobx::TriggerPriceType::Last, 3005);
  EXPECT_EQ(triggered_count, 1);
  EXPECT_EQ(f.engine.trigger_orders().size(), 0); // 成功触发并移除
}

// ============================================================================
// FAULT_REC_004: 多对手方吃单 (Multi-Fill Sweep) 中途故障回滚
// ============================================================================
TEST(TransactionFaultRecoveryIntegration, FAULT_REC_004_MultiFillSweepRollsBackOnFault) {
  PerpEngineFixture f(5000);

  // 初始化第 4 个用户 David
  constexpr lobx::UserId david = 40;
  expect_ok(f.ledger.deposit(david, f.margin_asset, 5000));
  f.positions.set_leverage(david, f.market.id, 5);

  // 1. 3 位 Maker (Bob, Carol, David) 分别在 100 价格挂卖单 2 张
  expect_ok(f.submit(f.bob, 950030, lob::Side::Ask, 100, 2, lob::POST_ONLY, 4000));
  expect_ok(f.submit(f.carol, 950031, lob::Side::Ask, 100, 2, lob::POST_ONLY, 4001));
  expect_ok(f.submit(david, 950032, lob::Side::Ask, 100, 2, lob::POST_ONLY, 4002));
  EXPECT_EQ(book_qty(f, lob::Side::Ask, 100), 6);

  const EngineSnapshotState before_sweep = capture_engine_state(f, david);

  // 2. 注入 AfterAdjustRestingLock 故障点（在调整休息挂单锁定时触发）
  set_fault(f, lobx::MarketEngineFaultPoint::AfterAdjustRestingLock);

  // 3. Alice 提交一笔吃掉全部 6 张盘口的大市价买单
  auto failed_sweep = f.submit(f.alice, 950033, lob::Side::Bid, 6, lob::IOC, 4003);
  EXPECT_FALSE(failed_sweep.accepted);
  EXPECT_EQ(failed_sweep.code, lobx::RejectCode::InternalError);

  clear_fault(f);

  // 4. 核心断言：3 位 Maker 的订单簿挂单、资金与仓位完全还原，Taker 无损
  expect_engine_state_restored(f, before_sweep, david);
  EXPECT_EQ(book_qty(f, lob::Side::Ask, 100), 6);
  EXPECT_TRUE(has_open_order(f, 950030));
  EXPECT_TRUE(has_open_order(f, 950031));
  EXPECT_TRUE(has_open_order(f, 950032));
  EXPECT_FALSE(has_open_order(f, 950033));

  // 5. 故障清除后重试吃单，验证顺利吃掉所有盘口
  expect_ok(f.submit(f.alice, 950034, lob::Side::Bid, 6, lob::IOC, 4004));
  EXPECT_EQ(book_qty(f, lob::Side::Ask, 100), 0);
}

// ============================================================================
// FAULT_REC_005: ReduceOnly 减仓单与撤单 (cancel) 故障恢复与保证金无损测试
// ============================================================================
TEST(TransactionFaultRecoveryIntegration, FAULT_REC_005_ReduceOnlyAndCancelFaultRecovery) {
  PerpEngineFixture f(1000);

  // 1. Bob 建立 5 张多头仓位
  expect_ok(f.submit(f.carol, 950040, lob::Side::Ask, 100, 5, lob::POST_ONLY, 5000));
  expect_ok(f.submit(f.bob, 950041, lob::Side::Bid, 100, 5, lob::IOC, 5001));

  // 2. 在盘口 90 挂一笔常规挂单买单
  constexpr lobx::OrderId resting_order = 950042;
  expect_ok(f.submit(f.bob, resting_order, lob::Side::Bid, 90, 5, lob::POST_ONLY, 5002));
  EXPECT_TRUE(has_open_order(f, resting_order));

  const EngineSnapshotState before_faults = capture_engine_state(f);

  // 3. 测试 ReduceOnly 减仓单挂簿时遭遇 FaultPoint 注入
  set_fault(f, lobx::MarketEngineFaultPoint::AfterBookSubmit);
  constexpr lobx::OrderId reduce_order = 950043;
  auto failed_reduce = f.submit(f.bob, reduce_order, lob::Side::Ask, 110, 5, lob::POST_ONLY | lobx::LOBX_REDUCE_ONLY, 5003);
  EXPECT_FALSE(failed_reduce.accepted);

  clear_fault(f);

  // 校验减仓单提交失败后引擎状态与锁定的保证金无变化
  expect_engine_state_restored(f, before_faults);
  EXPECT_FALSE(has_open_order(f, reduce_order));

  // 4. 重试相同的 ReduceOnly 减仓单，验证依然可以正常挂单成功
  expect_ok(f.submit(f.bob, reduce_order, lob::Side::Ask, 110, 5, lob::POST_ONLY | lobx::LOBX_REDUCE_ONLY, 5004));
  EXPECT_TRUE(has_open_order(f, reduce_order));

  // 5. 成功撤单还原
  EXPECT_TRUE(f.engine.cancel(reduce_order));
  EXPECT_TRUE(f.engine.cancel(resting_order));
  EXPECT_TRUE(f.ledger.invariant_ok());
}
