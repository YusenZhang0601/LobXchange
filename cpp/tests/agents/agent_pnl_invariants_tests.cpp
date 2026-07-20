#include "lobx/agents/agent_factory.hpp"
#include "lobx/simulation/agent_runtime.hpp"
#include "lobx/simulation/price_series_recorder.hpp"

#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"
#include "test_utils/accounting_test_utils.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace lobx::agents;
using namespace lobx::simulation;
using namespace lobx_test;

namespace {

constexpr double kMarkPrice = 100.0;
constexpr double kTolerance = 0.0;

double equity_from_runtime_initial(const AgentRuntimeConfig& config, double mark_price) {
  return static_cast<double>(config.initial_quote) + static_cast<double>(config.initial_base) * mark_price;
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

} // namespace

TEST(AgentPnlInvariants, MarkToMarketInventoryPreventsFalseCashLoss) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/0);
  const lobx::UserId seller = f.alice;
  const lobx::UserId buyer = f.bob;
  const AccountSnapshot buyer_before = snapshot_account(f.ledger, buyer, f.quote_asset, f.base_asset);

  EXPECT_TRUE(f.submit(seller, 8001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  const lobx::SubmitResult buy = f.submit(buyer, 8002, lob::Side::Bid, 100, 1, lob::IOC, 2);
  EXPECT_TRUE_MSG(buy.accepted, buy.reason);
  EXPECT_EQ(buy.trades.size(), static_cast<std::size_t>(1));

  const AccountSnapshot buyer_after = snapshot_account(f.ledger, buyer, f.quote_asset, f.base_asset);
  const double cash_pnl = buyer_after.total_cash - buyer_before.total_cash;
  const double equity_pnl = buyer_after.equity(kMarkPrice) - buyer_before.equity(kMarkPrice);
  EXPECT_NEAR_VALUE(cash_pnl, -100.0, kTolerance, "cash-only pnl after buy");
  EXPECT_NEAR_VALUE(equity_pnl, 0.0, kTolerance,
                    "mark-to-market total equity pnl must not treat inventory purchase as loss");
}

TEST(AgentPnlInvariants, AgentRuntimeZeroFeeClosedSystemAggregatePnlIsConserved) {
  AgentRuntimeConfig config{};
  config.steps = 25;
  config.reference_price = 100;
  config.initial_quote = 1000000;
  config.initial_base = 100000;
  config.book_levels = 10;
  AgentRuntime runtime(config);

  AgentFactoryRegistry registry;
  register_builtin_agents(registry);

  for (AgentId i = 1; i <= 5; ++i) {
    AgentConfig agent_config{};
    agent_config.type = "static_market_maker";
    agent_config.group_id = 1;
    agent_config.numeric_params["seed"] = static_cast<double>(1000 + i);
    runtime.add_agent(registry.create(agent_config.type, i, agent_config));
  }
  for (AgentId i = 6; i <= 10; ++i) {
    AgentConfig agent_config{};
    agent_config.type = "noise_trader";
    agent_config.group_id = 2;
    agent_config.numeric_params["seed"] = static_cast<double>(2000 + i);
    runtime.add_agent(registry.create(agent_config.type, i, agent_config));
  }

  const double initial_sum = equity_from_runtime_initial(config, kMarkPrice) * 10.0;
  runtime.run();
  const double final_sum = runtime.total_agent_equity(kMarkPrice);
  const double residual = final_sum - initial_sum;
  EXPECT_NEAR_VALUE(residual, 0.0, kTolerance, "zero-fee AgentRuntime closed system residual");

  const std::vector<AgentEquitySnapshot> snapshots = runtime.agent_equity_snapshots(kMarkPrice);
  EXPECT_EQ(snapshots.size(), static_cast<std::size_t>(10));
  int negative_count = 0;
  for (const AgentEquitySnapshot& snapshot : snapshots) {
    if (snapshot.pnl < 0.0) ++negative_count;
  }
  EXPECT_FALSE_MSG(negative_count == static_cast<int>(snapshots.size()),
                   "all agents have negative total-equity PnL; check cash-only PnL or locked balances");
}

TEST(AgentPnlInvariants, PriceImpactAccountingSummaryResidualIsNearZero) {
  AgentRuntimeConfig config{};
  config.steps = 20;
  config.reference_price = 100;
  config.initial_quote = 1000000;
  config.initial_base = 100000;
  AgentRuntime runtime(config);

  AgentFactoryRegistry registry;
  register_builtin_agents(registry);
  for (AgentId i = 1; i <= 3; ++i) {
    AgentConfig agent_config{};
    agent_config.type = "static_market_maker";
    agent_config.group_id = 1;
    agent_config.numeric_params["seed"] = static_cast<double>(3000 + i);
    runtime.add_agent(registry.create(agent_config.type, i, agent_config));
  }
  for (AgentId i = 4; i <= 6; ++i) {
    AgentConfig agent_config{};
    agent_config.type = "noise_trader";
    agent_config.group_id = 2;
    agent_config.numeric_params["seed"] = static_cast<double>(4000 + i);
    runtime.add_agent(registry.create(agent_config.type, i, agent_config));
  }

  PriceSeriesRecorder recorder;
  recorder.record(0, runtime);
  for (int step = 1; step <= config.steps; ++step) {
    runtime.step();
    recorder.record(step, runtime);
  }

  const PriceImpactSummary summary = recorder.summarize("accounting_smoke", 9001, 6, config.steps);
  const AccountingSummary accounting = runtime.accounting_summary(kMarkPrice);
  EXPECT_TRUE(std::isfinite(accounting.system_pnl_residual));
  EXPECT_NEAR_VALUE(accounting.system_pnl_residual, 0.0, kTolerance, "price impact accounting residual");
  EXPECT_FALSE_MSG(accounting.negative_pnl_agent_count == accounting.agent_count,
                   "all agents negative; fee=" + std::to_string(accounting.exchange_fee_revenue) +
                       " residual=" + std::to_string(accounting.system_pnl_residual) +
                       " mark_price=" + std::to_string(kMarkPrice));

  const std::filesystem::path out_dir = std::filesystem::temp_directory_path() / "lobx_agent_pnl_invariants";
  std::filesystem::create_directories(out_dir);
  EXPECT_TRUE(recorder.write_summary_json((out_dir / "summary.json").string(), summary, accounting));
  EXPECT_TRUE(std::filesystem::exists(out_dir / "accounting_summary.json"));
  const std::string accounting_json = read_file(out_dir / "accounting_summary.json");
  EXPECT_TRUE(accounting_json.find("\"system_pnl_residual\":0") != std::string::npos);
  EXPECT_TRUE(accounting_json.find("\"negative_pnl_agent_count\"") != std::string::npos);
}
