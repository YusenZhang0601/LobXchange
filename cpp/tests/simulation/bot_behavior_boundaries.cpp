#include "test_helpers/bot_test_agents.hpp"
#include "test_helpers/market_microstructure_helpers.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace lobx_test;

namespace {

BotInstance bot(lobx::UserId user, std::string name, TestLatencyModel latency, std::unique_ptr<Strategy> strategy) {
  return BotInstance{user, std::move(name), latency, std::move(strategy)};
}

BotAction action(lobx::UserId claimed_user,
                 lobx::OrderId order_id,
                 lob::Side side,
                 lob::Tick price,
                 lob::Quantity qty,
                 uint32_t flags,
                 lob::Timestamp decision_ts,
                 BotActionType type = BotActionType::SubmitLimit) {
  BotAction out{claimed_user, order_id, side, price, qty, flags};
  out.type = type;
  out.decision_ts = decision_ts;
  return out;
}

lobx::UserId owner_of_open_order(SpotEngineFixture& f, lobx::OrderId order_id) {
  for (const auto& order : f.engine.open_orders()) {
    if (order.id == order_id) return order.user;
  }
  return 0;
}

} // namespace

TEST(BotBehaviorBoundaries, RunnerOverridesForgedActionUser) {
  BotSimulationRunner runner(1001);
  const lobx::UserId alice = runner.fixture().alice;
  const lobx::UserId bob = runner.fixture().bob;
  const auto bob_base_before = runner.fixture().ledger.balance(bob, runner.fixture().base_asset);

  runner.add_bot(bot(alice, "forger", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{action(bob, 700001, lob::Side::Ask, 101, 1, lob::POST_ONLY, 1)})));

  const auto result = runner.run(1);

  EXPECT_EQ(owner_of_open_order(runner.fixture(), 700001), alice);
  EXPECT_EQ(runner.fixture().ledger.balance(alice, runner.fixture().base_asset).locked, 1);
  EXPECT_EQ(runner.fixture().ledger.balance(bob, runner.fixture().base_asset).total, bob_base_before.total);
  EXPECT_EQ(runner.fixture().ledger.balance(bob, runner.fixture().base_asset).locked, bob_base_before.locked);
  EXPECT_EQ(result.metrics.at(alice).accepted_orders, 1);
  EXPECT_TRUE(result.metrics.find(bob) == result.metrics.end());
}

TEST(BotBehaviorBoundaries, StrategyCannotSubmitOrderAsDifferentUser) {
  BotSimulationRunner runner(1002);
  const lobx::UserId alice = runner.fixture().alice;
  const lobx::UserId bob = runner.fixture().bob;
  const lobx::UserId carol = runner.fixture().carol;
  EXPECT_TRUE(runner.fixture().submit(carol, 700101, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const auto bob_quote_before = runner.fixture().ledger.balance(bob, runner.fixture().quote_asset);
  const auto bob_base_before = runner.fixture().ledger.balance(bob, runner.fixture().base_asset);

  runner.add_bot(bot(alice, "forged-taker", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{action(bob, 700102, lob::Side::Bid, 100, 1, lob::IOC, 2)})));

  const auto result = runner.run(3);

  EXPECT_EQ(result.trades.size(), 1UL);
  if (result.trades.empty()) return;
  EXPECT_EQ(result.trades.front().buyer, alice);
  EXPECT_NE(result.trades.front().buyer, bob);
  EXPECT_EQ(runner.fixture().ledger.balance(bob, runner.fixture().quote_asset).total, bob_quote_before.total);
  EXPECT_EQ(runner.fixture().ledger.balance(bob, runner.fixture().base_asset).total, bob_base_before.total);

  const auto alice_ctx = runner.context_for_user(alice, 10, TestLatencyModel{0, 0, 0});
  const auto bob_ctx = runner.context_for_user(bob, 10, TestLatencyModel{0, 0, 0});
  EXPECT_EQ(alice_ctx.own_fills.size(), 1UL);
  EXPECT_TRUE(bob_ctx.own_fills.empty());
}

TEST(BotBehaviorBoundaries, StrategyCannotCancelAnotherUsersOrder) {
  BotSimulationRunner runner(1003);
  const lobx::UserId alice = runner.fixture().alice;
  const lobx::UserId bob = runner.fixture().bob;
  EXPECT_TRUE(runner.fixture().submit(bob, 700201, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);

  BotAction cancel = action(bob, 700201, lob::Side::Ask, 0, 0, lob::NONE, 2, BotActionType::CancelOrder);
  runner.add_bot(bot(alice, "bad-cancel", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(std::vector<BotAction>{cancel})));

  const auto result = runner.run(3);

  EXPECT_EQ(owner_of_open_order(runner.fixture(), 700201), bob);
  EXPECT_EQ(count_events(runner.fixture().events, "order.canceled"), 0);
  EXPECT_EQ(result.metrics.at(alice).rejected_orders, 1);
}

TEST(BotBehaviorBoundaries, BotCannotUseReservedFeeAccount) {
  BotSimulationRunner runner(1004);
  const lobx::UserId fee_account = dedicated_fee_account();
  const bool added = runner.add_bot(bot(fee_account, "fee-account-bot", TestLatencyModel{0, 0, 0},
                                        std::make_unique<UserStrategyStub>(
                                            std::vector<BotAction>{action(fee_account, 700301, lob::Side::Bid, 99, 1, lob::POST_ONLY, 1)})));

  const auto result = runner.run(2);

  EXPECT_FALSE(added);
  EXPECT_TRUE(result.trades.empty());
  EXPECT_TRUE(runner.fixture().engine.open_orders().empty());
  EXPECT_TRUE(result.metrics.find(fee_account) == result.metrics.end());
}

TEST(BotBehaviorBoundaries, BotCannotUseUnknownMarket) {
  BotSimulationRunner runner(1005);
  BotAction bad_market = action(runner.fixture().alice, 700401, lob::Side::Bid, 99, 1, lob::POST_ONLY, 1);
  bad_market.market_symbol = "DOGE-FOO";
  runner.add_bot(bot(runner.fixture().alice, "unknown-market", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(std::vector<BotAction>{bad_market})));

  const auto result = runner.run(2);

  EXPECT_TRUE(runner.fixture().engine.open_orders().empty());
  EXPECT_EQ(result.metrics.at(runner.fixture().alice).submitted_orders, 1);
  EXPECT_EQ(result.metrics.at(runner.fixture().alice).rejected_orders, 1);
}

TEST(BotBehaviorBoundaries, BotInvalidOrderIsRejectedAndCounted) {
  BotSimulationRunner runner(1006);
  runner.add_bot(bot(runner.fixture().alice, "invalid", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{action(runner.fixture().alice, 700501, lob::Side::Bid, 0, 1, lob::POST_ONLY, 1)})));

  const auto result = runner.run(2);

  EXPECT_TRUE(runner.fixture().engine.open_orders().empty());
  EXPECT_EQ(runner.fixture().ledger.balance(runner.fixture().alice, runner.fixture().quote_asset).locked, 0);
  EXPECT_EQ(result.metrics.at(runner.fixture().alice).rejected_orders, 1);
}

TEST(BotBehaviorBoundaries, BotDuplicateOrderIdRejectedAndCounted) {
  BotSimulationRunner runner(1007);
  const lobx::UserId alice = runner.fixture().alice;
  runner.add_bot(bot(alice, "duplicate", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{
                             action(alice, 700601, lob::Side::Bid, 99, 1, lob::POST_ONLY, 1),
                             action(alice, 700601, lob::Side::Bid, 98, 1, lob::POST_ONLY, 1)})));

  const auto result = runner.run(3);

  EXPECT_EQ(owner_of_open_order(runner.fixture(), 700601), alice);
  EXPECT_EQ(result.metrics.at(alice).accepted_orders, 1);
  EXPECT_EQ(result.metrics.at(alice).rejected_orders, 1);
}

TEST(BotBehaviorBoundaries, BotPostOnlyCrossingRejectedAndCounted) {
  BotSimulationRunner runner(1008);
  EXPECT_TRUE(runner.fixture().submit(runner.fixture().carol, 700701, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  runner.add_bot(bot(runner.fixture().alice, "post-only-cross", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{action(runner.fixture().alice, 700702, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2)})));

  const auto result = runner.run(3);

  EXPECT_TRUE(result.trades.empty());
  EXPECT_EQ(owner_of_open_order(runner.fixture(), 700701), runner.fixture().carol);
  EXPECT_EQ(result.metrics.at(runner.fixture().alice).rejected_orders, 1);
}

TEST(BotBehaviorBoundaries, BotInsufficientBalanceRejectedAndCounted) {
  BotSimulationRunner runner(1009);
  const lobx::UserId alice = runner.fixture().alice;
  runner.add_bot(bot(alice, "insufficient", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{action(alice, 700801, lob::Side::Bid, 2000000, 1, lob::POST_ONLY, 1)})));

  const auto result = runner.run(2);

  EXPECT_TRUE(runner.fixture().engine.open_orders().empty());
  EXPECT_EQ(runner.fixture().ledger.balance(alice, runner.fixture().quote_asset).locked, 0);
  EXPECT_EQ(result.metrics.at(alice).rejected_orders, 1);
}
