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

} // namespace

// ============================================================================
// 测试 1：买单盘口深度为 0 + 保险基金为 0 场景下的强平与坏账记录及资金守恒
// ============================================================================
TEST(PerpLiquidationStress, PERP_LIQ_STRESS_001_EmptyBookLiquidationTriggersAdlAndPreservesInvariants) {
  PerpEngineFixture f(300); // 初始保证金 300 USDT，以便暴跌后产生穿仓

  // 1. 设置初始环境：保险基金初始余额为 0
  EXPECT_EQ(f.engine.insurance_fund_balance(), 0);

  // 2. 构造 Alice 的多头头寸 (与 Bob 撮合，开仓价 100, 数量 10)
  expect_ok(f.submit(f.bob, 90001, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1000));
  expect_ok(f.submit(f.alice, 90002, lob::Side::Bid, 100, 10, lob::IOC, 1001));

  // 3. 构造流动性空洞：确保买单盘口无任何深度 (best_bid <= 0，为空盘口)
  EXPECT_TRUE(f.engine.best_bid() <= 0);

  // 4. 暴跌标记价格至 1，使 Alice 严重穿仓处于可强平状态
  expect_ok(f.engine.set_index_price(1));
  expect_ok(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice));
  EXPECT_TRUE(f.engine.is_liquidatable(f.alice));

  // 5. 执行强平
  const auto result = f.engine.liquidate_position(f.alice, f.carol, 2);
  expect_ok(result);

  // 6. 核心断言：
  // a) 保险基金余额为 0，累计坏账 Bad Debt > 0 (1000 开仓亏损 - 300 保证金 = 700，扣除微小维护后坏账)
  EXPECT_EQ(f.engine.insurance_fund_balance(), 0);
  EXPECT_TRUE(f.engine.bad_debt() > 0);

  // b) 校验 EventStore 是否成功记录了 ADL_REQUIRED 事件
  bool found_adl_event = false;
  for (const auto& rec : f.events.records()) {
    if (rec.type == "ADL_REQUIRED") {
      found_adl_event = true;
      break;
    }
  }
  EXPECT_TRUE(found_adl_event);

  // c) 全局资金账本守恒检查（必须 100% 守恒）
  EXPECT_TRUE(f.ledger.invariant_ok());
}

// ============================================================================
// 测试 2：流动性空洞下 ADL 减仓候选者确定性排序校验
// ============================================================================
TEST(PerpLiquidationStress, PERP_LIQ_STRESS_002_AdlCandidatesSortedForEmptyBookDeleveraging) {
  PerpEngineFixture f(300);

  // 1. 设置标记价格
  expect_ok(f.engine.set_index_price(120));
  expect_ok(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice));

  // 2. 构造两个对手方持仓 (Alice 与 Carol 挂单撮合开多仓)
  EXPECT_TRUE(f.positions.apply_trade_checked(f.alice, f.market.id, lob::Side::Bid, 100, 10)); // 盈利更高 (均价 100)
  EXPECT_TRUE(f.positions.apply_trade_checked(f.carol, f.market.id, lob::Side::Bid, 110, 10)); // 盈利较低 (均价 110)

  // 3. 在盘口买单挂单深度为 0 情况下，查询 ADL 自动减仓候选者
  const auto candidates = f.engine.rank_adl_candidates();

  // 4. 断言 ADL 列表排序：Alice 盈利比例更高 (均价 100 vs 110)，排名第 1 优先于 Carol
  EXPECT_TRUE(candidates.size() >= 2UL);
  EXPECT_EQ(candidates[0].account_id, f.alice);
  EXPECT_EQ(candidates[1].account_id, f.carol);

  // 5. 断言资金与账本无损不变量
  EXPECT_TRUE(f.ledger.invariant_ok());
}
