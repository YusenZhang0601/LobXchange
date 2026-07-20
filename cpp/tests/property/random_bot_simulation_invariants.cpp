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

BotAction scripted_action(lobx::UserId user,
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

BotRunResult run_random_bot_simulation(uint64_t seed, int ticks, int bot_count) {
  BotSimulationRunner runner(seed, 0, 10, 100);
  const std::vector<lobx::UserId> users{runner.fixture().alice, runner.fixture().bob, runner.fixture().carol,
                                        40, 50, 60, 70, 80, 90};

  if (bot_count >= 1) {
    runner.add_bot(bot(users[0], "maker", TestLatencyModel{0, 0, 1},
                       std::make_unique<MarketMakerStrategy>(99, 101, 1, 710001)));
  }
  if (bot_count >= 2) {
    runner.add_bot(bot(users[1], "taker", TestLatencyModel{0, 0, 1},
                       std::make_unique<TakerSweepStrategy>(lob::Side::Bid, 1, 101, 101.0L, 711001)));
  }
  if (bot_count >= 3) {
    runner.add_bot(bot(users[2], "scripted", TestLatencyModel{1, 0, 1},
                       std::make_unique<UserStrategyStub>(
                           std::vector<BotAction>{
                               scripted_action(users[2], 712001, lob::Side::Ask, 99, 1, lob::IOC, 20),
                               scripted_action(users[2], 712002, lob::Side::Bid, 101, 1, lob::IOC, 40)})));
  }
  for (int i = 3; i < bot_count && i < static_cast<int>(users.size()); ++i) {
    runner.add_bot(bot(users[static_cast<size_t>(i)], "noise-" + std::to_string(i), TestLatencyModel{i % 3, 0, i % 2},
                       std::make_unique<NoiseTraderStrategy>(seed * 17 + static_cast<uint64_t>(i), 713000 + i * 1000)));
  }

  return runner.run(ticks);
}

const BotBalanceView* find_balance(const BotRunResult& result, lobx::UserId user, lobx::AssetId asset) {
  for (const auto& balance : result.balances) {
    if (balance.user == user && balance.asset == asset) return &balance;
  }
  return nullptr;
}

void expect_metrics_match_final_balances(const BotRunResult& result, lobx::AssetId base_asset, lobx::AssetId quote_asset) {
  for (const auto& [user, metrics] : result.metrics) {
    const BotBalanceView* quote = find_balance(result, user, quote_asset);
    const BotBalanceView* base = find_balance(result, user, base_asset);
    EXPECT_TRUE_MSG(quote != nullptr, "missing quote balance user=" + std::to_string(user));
    EXPECT_TRUE_MSG(base != nullptr, "missing base balance user=" + std::to_string(user));
    if (quote == nullptr || base == nullptr) continue;
    EXPECT_EQ(metrics.ending_quote, quote->total);
    EXPECT_EQ(metrics.ending_base, base->total);
    EXPECT_EQ(metrics.inventory, metrics.ending_base - metrics.starting_base);
  }
}

} // namespace

TEST(RandomBotSimulationInvariants, RandomBotSimulationSameSeedDeterministic) {
  const auto a = run_random_bot_simulation(42, 200, 5);
  const auto b = run_random_bot_simulation(42, 200, 5);

  EXPECT_TRUE(same_bot_run_result(a, b));
}

TEST(RandomBotSimulationInvariants, RandomBotSimulationDifferentSeedDivergesActionTrace) {
  const auto a = run_random_bot_simulation(42, 200, 5);
  const auto b = run_random_bot_simulation(43, 200, 5);

  EXPECT_TRUE_MSG(a.action_trace != b.action_trace, "different seeds must diverge at the generated action trace");
}

TEST(RandomBotSimulationInvariants, RandomBotSimulationMaintainsLedgerInvariant) {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    const auto result = run_random_bot_simulation(seed, 200, 5);
    EXPECT_TRUE_MSG(result.ledger_invariant_ok, "seed=" + std::to_string(seed));
  }
}

TEST(RandomBotSimulationInvariants, RandomBotSimulationMaintainsBookOpenConsistency) {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    const auto result = run_random_bot_simulation(seed, 200, 5);
    EXPECT_TRUE_MSG(result.book_open_consistency_ok, "seed=" + std::to_string(seed));
  }
}

TEST(RandomBotSimulationInvariants, RandomBotSimulationConservesSpotAssetsIncludingFees) {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    const auto result = run_random_bot_simulation(seed, 200, 5);
    EXPECT_EQ_MSG(result.ending_base_total, result.starting_base_total, "base conservation seed=" + std::to_string(seed));
    EXPECT_EQ_MSG(result.ending_quote_total, result.starting_quote_total, "quote conservation seed=" + std::to_string(seed));
  }
}

TEST(RandomBotSimulationInvariants, RandomBotSimulationMetricsMatchFinalBalances) {
  const auto result = run_random_bot_simulation(123, 200, 5);

  expect_metrics_match_final_balances(result, 1, 2);
}

TEST(RandomBotSimulationInvariants, RandomBotSimulationNoNegativeBalances) {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    const auto result = run_random_bot_simulation(seed, 200, 5);
    EXPECT_TRUE_MSG(result.no_negative_balances, "seed=" + std::to_string(seed));
    for (const auto& balance : result.balances) {
      EXPECT_TRUE(balance.free >= 0);
      EXPECT_TRUE(balance.locked >= 0);
      EXPECT_TRUE(balance.total >= 0);
      EXPECT_EQ(balance.total, balance.free + balance.locked);
    }
  }
}

TEST(RandomBotSimulationInvariants, RandomBotSimulationNoRestingIOCOrFOKOrders) {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    const auto result = run_random_bot_simulation(seed, 200, 5);
    EXPECT_TRUE_MSG(result.no_resting_ioc_or_fok, "seed=" + std::to_string(seed));
  }
}

TEST(RandomBotSimulationInvariants, RandomBotSimulationNoFilledOrderRemainsOpen) {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    const auto result = run_random_bot_simulation(seed, 200, 5);
    EXPECT_TRUE_MSG(result.no_filled_order_remains_open, "seed=" + std::to_string(seed));
  }
}

TEST(RandomBotSimulationInvariants, RandomBotSimulationNoBotObservesFuturePublicTrades) {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    const auto result = run_random_bot_simulation(seed, 200, 5);
    EXPECT_TRUE_MSG(result.no_future_public_trades_observed, "seed=" + std::to_string(seed));
  }
}

TEST(RandomBotSimulationInvariants, RandomBotSimulationNoBotReceivesOtherUsersPrivateFills) {
  for (uint64_t seed = 1; seed <= 20; ++seed) {
    const auto result = run_random_bot_simulation(seed, 200, 5);
    EXPECT_TRUE_MSG(result.no_private_data_leak, "seed=" + std::to_string(seed));
  }
}
