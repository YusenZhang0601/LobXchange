#include "lobx/simulation/agent_population.hpp"

#include "test_helpers/test_framework.hpp"

#include <limits>
#include <set>

using namespace lobx::sim;

namespace {

AgentPopulationConfig population(uint64_t seed = 123) {
  AgentGroupConfig makers{};
  makers.strategy_type = "market_maker";
  makers.count = 3;
  makers.name_prefix = "mm";
  makers.latency_range = LatencyRangeConfig{1, 3, 1, 3, 2, 4, 2, 4};
  makers.param_ranges = {{"bid_px", DoubleRange{98, 99}}, {"ask_px", DoubleRange{101, 102}}, {"qty", DoubleRange{1, 5}}};

  AgentGroupConfig noise{};
  noise.strategy_type = "noise_trader";
  noise.count = 2;
  noise.name_prefix = "noise";
  noise.latency_range = LatencyRangeConfig{1, 2, 1, 2, 1, 2, 1, 2};
  noise.param_ranges = {{"seed", DoubleRange{1, 1000}}};

  return AgentPopulationConfig{seed, 10, {makers, noise}};
}

} // namespace

TEST(AgentPopulationTests, AgentPopulationSameSeedDeterministic) {
  const std::vector<BotConfig> a = generate_agent_population(population(7));
  const std::vector<BotConfig> b = generate_agent_population(population(7));

  EXPECT_EQ(a.size(), b.size());
  for (size_t i = 0; i < a.size(); ++i) {
    EXPECT_EQ(a[i].user, b[i].user);
    EXPECT_EQ(a[i].name, b[i].name);
    EXPECT_TRUE(a[i].params == b[i].params);
  }
}

TEST(AgentPopulationTests, AgentPopulationDifferentSeedChangesGeneratedParams) {
  const std::vector<BotConfig> a = generate_agent_population(population(7));
  const std::vector<BotConfig> b = generate_agent_population(population(8));

  EXPECT_TRUE(a.front().params != b.front().params);
}

TEST(AgentPopulationTests, AgentPopulationGeneratesUniqueUsers) {
  const std::vector<BotConfig> bots = generate_agent_population(population());
  std::set<lobx::UserId> users;
  for (const BotConfig& bot : bots) users.insert(bot.user);

  EXPECT_EQ(users.size(), bots.size());
}

TEST(AgentPopulationTests, AgentPopulationRejectsReservedFeeAccountRange) {
  AgentPopulationConfig config = population();
  config.first_user_id = std::numeric_limits<lobx::UserId>::max() - 1;

  EXPECT_FALSE(validate_agent_population(config).ok);
}

TEST(AgentPopulationTests, AgentPopulationRejectsUnknownStrategy) {
  AgentPopulationConfig config = population();
  config.groups.front().strategy_type = "unknown";

  EXPECT_FALSE(validate_agent_population(config).ok);
}

TEST(AgentPopulationTests, AgentPopulationRejectsInvalidLatencyRange) {
  AgentPopulationConfig config = population();
  config.groups.front().latency_range.order_min = 5;
  config.groups.front().latency_range.order_max = 1;

  EXPECT_FALSE(validate_agent_population(config).ok);
}

TEST(AgentPopulationTests, AgentPopulationParameterRangesApplied) {
  const std::vector<BotConfig> bots = generate_agent_population(population());

  for (const BotConfig& bot : bots) {
    if (bot.strategy_type == "market_maker") {
      EXPECT_TRUE(bot.params.at("bid_px") >= 98.0 && bot.params.at("bid_px") <= 99.0);
      EXPECT_TRUE(bot.params.at("ask_px") >= 101.0 && bot.params.at("ask_px") <= 102.0);
      EXPECT_TRUE(bot.params.at("qty") >= 1.0 && bot.params.at("qty") <= 5.0);
    }
  }
}

TEST(AgentPopulationTests, AgentPopulationNamesAreDeterministic) {
  const std::vector<BotConfig> bots = generate_agent_population(population());

  EXPECT_EQ(bots[0].name, std::string("mm0"));
  EXPECT_EQ(bots[3].name, std::string("noise0"));
}

TEST(AgentPopulationTests, ComplexStrategyTypeIsPreservedInsteadOfFallback) {
  AgentPopulationConfig config = population();
  config.groups.clear();
  AgentGroupConfig momentum{};
  momentum.strategy_type = "momentum";
  momentum.count = 1;
  momentum.name_prefix = "mom";
  momentum.latency_range = LatencyRangeConfig{1, 1, 1, 1, 1, 1, 1, 1};
  momentum.param_ranges = {{"side", DoubleRange{0, 0}},
                           {"target_qty", DoubleRange{1, 1}},
                           {"limit_price", DoubleRange{101, 101}},
                           {"max_avg_price", DoubleRange{101, 101}}};
  config.groups.push_back(momentum);

  const std::vector<BotConfig> bots = generate_agent_population(config);

  EXPECT_EQ(bots.size(), static_cast<size_t>(1));
  EXPECT_EQ(bots.front().strategy_type, std::string("momentum"));
}
