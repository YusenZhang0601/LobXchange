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
// 测试 1：LTP 暴涨 50% 插针时，IndexPrice 标记价格模式拦截恶意爆仓
// ============================================================================
TEST(OracleMarkPriceProtection, ORACLE_MARK_001_LtpSpikeProtectedByOracleIndexPrice) {
  PerpEngineFixture f(1000);

  // 1. 初始化设置：指数价格设为 100，标记价格模式设为 IndexPrice
  expect_ok(f.engine.set_index_price(100));
  expect_ok(f.engine.set_mark_price_mode(lobx::MarkPriceMode::IndexPrice));

  // 2. 构造 Bob 的空头头寸 (与 Alice 在 100 挂单撮合成交 10 张空单)
  expect_ok(f.submit(f.bob, 80001, lob::Side::Ask, 100, 10, lob::POST_ONLY, 1000));
  expect_ok(f.submit(f.alice, 80002, lob::Side::Bid, 100, 10, lob::IOC, 1001));

  // 3. 模拟对倒/拉盘插针：Alice 与 Carol 在盘口 150 撮合成交，使最新成交价 (LTP) 暴涨 50%
  expect_ok(f.submit(f.carol, 80003, lob::Side::Ask, 150, 10, lob::POST_ONLY, 1002));
  expect_ok(f.submit(f.alice, 80004, lob::Side::Bid, 150, 10, lob::IOC, 1003));
  EXPECT_EQ(f.engine.last_trade_price(), 150);

  // 4. 核心断言：
  // 外部 IndexPrice 依旧为 100，Bob 的持仓重估使用 100，不被 LTP (150) 恶意插针爆仓
  EXPECT_FALSE(f.engine.is_liquidatable(f.bob));

  // 尝试强平 Bob 应当被风控引擎拦截拒绝
  const auto liq_result = f.engine.liquidate_position(f.bob, f.carol, 2);
  EXPECT_FALSE(liq_result.ok);

  // 全局资金账本 100% 守恒
  EXPECT_TRUE(f.ledger.invariant_ok());
}

// ============================================================================
// 测试 2：标记价格模式切换为 LastPrice 后对 LTP 暴涨响应并触发强平
// ============================================================================
TEST(OracleMarkPriceProtection, ORACLE_MARK_002_ModeSwitchToLastPriceTriggersLiquidation) {
  PerpEngineFixture f(1000);

  // 1. 初始化并拉高 LTP 到 150
  expect_ok(f.engine.set_index_price(100));
  expect_ok(f.submit(f.bob, 81001, lob::Side::Ask, 100, 10, lob::POST_ONLY, 2000));
  expect_ok(f.submit(f.alice, 81002, lob::Side::Bid, 100, 10, lob::IOC, 2001));

  expect_ok(f.submit(f.carol, 81003, lob::Side::Ask, 150, 10, lob::POST_ONLY, 2002));
  expect_ok(f.submit(f.alice, 81004, lob::Side::Bid, 150, 10, lob::IOC, 2003));
  EXPECT_EQ(f.engine.last_trade_price(), 150);

  // 2. 将标记价格模式切换为 LastPrice
  expect_ok(f.engine.set_mark_price_mode(lobx::MarkPriceMode::LastPrice));

  // 3. 断言风控重新评估后 Bob 可被强平，且强平顺利完成
  EXPECT_TRUE(f.engine.is_liquidatable(f.bob));
  expect_ok(f.engine.liquidate_position(f.bob, f.carol, 2));

  // 全局资金账本 100% 守恒
  EXPECT_TRUE(f.ledger.invariant_ok());
}
