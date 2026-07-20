#include "test_helpers/bot_test_agents.hpp"
#include "test_helpers/market_microstructure_helpers.hpp"
#include "test_helpers/test_framework.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace lobx_test;

namespace {

BotInstance bot(lobx::UserId user, std::string name, TestLatencyModel latency, std::unique_ptr<Strategy> strategy) {
  return BotInstance{user, std::move(name), latency, std::move(strategy)};
}

BotAction action(lobx::UserId user,
                 lobx::OrderId order_id,
                 lob::Side side,
                 lob::Tick price,
                 lob::Quantity qty,
                 uint32_t flags,
                 lob::Timestamp decision_ts) {
  BotAction out{user, order_id, side, price, qty, flags};
  out.decision_ts = decision_ts;
  return out;
}

BotRunResult run_round_trip_with_fee() {
  BotSimulationRunner runner(11, 0, 100, 10000);
  const lobx::UserId seller = runner.fixture().alice;
  const lobx::UserId taker = runner.fixture().bob;
  const lobx::UserId buyer = runner.fixture().carol;

  runner.add_bot(bot(seller, "seller", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{action(seller, 620001, lob::Side::Ask, 10000, 1, lob::POST_ONLY, 1)})));
  runner.add_bot(bot(taker, "roundtrip", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{
                             action(taker, 620002, lob::Side::Bid, 10000, 1, lob::IOC, 2),
                             action(taker, 620004, lob::Side::Ask, 10000, 1, lob::IOC, 4)})));
  runner.add_bot(bot(buyer, "buyer", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{action(buyer, 620003, lob::Side::Bid, 10000, 1, lob::POST_ONLY, 3)})));
  return runner.run(10);
}

} // namespace

TEST(BotStrategyMetrics, RoundTripPnLIncludesFees) {
  const auto result = run_round_trip_with_fee();
  const auto bob = result.metrics.at(20);

  EXPECT_EQ(result.trades.size(), 2UL);
  EXPECT_EQ(bob.fees_paid, 200);
  EXPECT_EQ(static_cast<int>(bob.gross_pnl), 0);
  EXPECT_TRUE_MSG(bob.net_pnl < 0.0L, "same-price taker round trip must lose fees");
}

TEST(BotStrategyMetrics, TakerSweepPnLIncludesSlippage) {
  BotSimulationRunner runner(12, 0, 0, 100);
  EXPECT_TRUE(runner.fixture().submit(runner.fixture().alice, 620101, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(runner.fixture().submit(runner.fixture().carol, 620102, lob::Side::Ask, 110, 1, lob::POST_ONLY, 2).accepted);
  runner.add_bot(bot(runner.fixture().bob, "taker", TestLatencyModel{0, 0, 0},
                     std::make_unique<TakerSweepStrategy>(lob::Side::Bid, 2, 110, 110.0L, 620103)));

  const auto result = runner.run(2);
  const auto bob = result.metrics.at(runner.fixture().bob);

  EXPECT_EQ(result.trades.size(), 2UL);
  EXPECT_EQ(static_cast<int>(bob.gross_pnl), -10);
  EXPECT_EQ(static_cast<int>(bob.net_pnl), -10);
}

TEST(BotStrategyMetrics, MakerSpreadCaptureMatchesExpectedPnL) {
  BotSimulationRunner runner(13, 0, 0, 100);
  const lobx::UserId maker = runner.fixture().alice;
  const lobx::UserId buy_taker = runner.fixture().bob;
  const lobx::UserId sell_taker = runner.fixture().carol;

  runner.add_bot(bot(maker, "maker", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{
                             action(maker, 620201, lob::Side::Ask, 101, 1, lob::POST_ONLY, 1),
                             action(maker, 620203, lob::Side::Bid, 99, 1, lob::POST_ONLY, 3)})));
  runner.add_bot(bot(buy_taker, "buy-taker", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{action(buy_taker, 620202, lob::Side::Bid, 101, 1, lob::IOC, 2)})));
  runner.add_bot(bot(sell_taker, "sell-taker", TestLatencyModel{0, 0, 0},
                     std::make_unique<UserStrategyStub>(
                         std::vector<BotAction>{action(sell_taker, 620204, lob::Side::Ask, 99, 1, lob::IOC, 4)})));

  const auto result = runner.run(10);
  const auto maker_metrics = result.metrics.at(maker);

  EXPECT_EQ(result.trades.size(), 2UL);
  EXPECT_EQ(static_cast<int>(maker_metrics.gross_pnl), 2);
  EXPECT_EQ(static_cast<int>(maker_metrics.net_pnl), 2);
}

TEST(BotStrategyMetrics, MetricsSameSeedReplayDeterministic) {
  auto run = [](uint64_t seed) {
    BotSimulationRunner runner(seed, 0, 100, 10000);
    runner.add_bot(bot(runner.fixture().alice, "maker", TestLatencyModel{0, 0, 0},
                       std::make_unique<MarketMakerStrategy>(9999, 10000, 1, 620301)));
    runner.add_bot(bot(runner.fixture().bob, "taker", TestLatencyModel{0, 0, 0},
                       std::make_unique<TakerSweepStrategy>(lob::Side::Bid, 1, 10000, 10000.0L, 620401)));
    runner.add_bot(bot(40, "noise", TestLatencyModel{0, 0, 0},
                       std::make_unique<NoiseTraderStrategy>(seed + 9, 620501)));
    return runner.run(50);
  };

  EXPECT_TRUE(same_bot_run_result(run(444), run(444)));
}

TEST(BotStrategyMetrics, FeesPaidMatchesDedicatedFeeAccountDelta) {
  const auto result = run_round_trip_with_fee();

  lobx::Amount metric_fees = 0;
  for (const auto& [_, metrics] : result.metrics) metric_fees += metrics.fees_paid;

  lobx::Amount fee_account_quote = 0;
  for (const auto& balance : result.balances) {
    if (balance.user == dedicated_fee_account() && balance.asset == 2) fee_account_quote = balance.total;
  }

  EXPECT_EQ(metric_fees, fee_account_quote);
  EXPECT_EQ(fee_account_quote, 200);
}
