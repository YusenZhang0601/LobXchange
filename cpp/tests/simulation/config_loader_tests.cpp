#include "lobx/simulation/config_loader.hpp"

#include "test_helpers/test_framework.hpp"

#include <fstream>
#include <limits>
#include <string>

using namespace lobx::sim;

namespace {

std::string valid_scenario_json() {
  return R"JSON({
    "seed": 42,
    "ticks": 100,
    "market_symbol": "BTC-USDT",
    "bots": [
      {
        "user": 10,
        "name": "mm",
        "strategy_type": "market_maker",
        "latency": {"order": 1, "cancel": 1, "market_data": 5, "private_data": 3},
        "params": {"bid_px": 99, "ask_px": 101, "qty": 1}
      },
      {
        "user": 20,
        "name": "taker",
        "strategy_type": "taker_sweep",
        "latency": {"order": 1, "cancel": 1, "market_data": 5, "private_data": 3},
        "params": {"side": 0, "target_qty": 1, "limit_price": 101, "max_avg_price": 101}
      }
    ]
  })JSON";
}

std::string valid_sweep_json() {
  return R"JSON({
    "params": [
      {"bot_name": "mm", "param_name": "bid_px", "values": [98, 99]},
      {"bot_name": "mm", "param_name": "ask_px", "values": [101, 102]}
    ]
  })JSON";
}

std::string valid_seed_json() {
  return R"JSON({"seeds": [1, 2, 3, 4, 5]})JSON";
}

std::string temp_path(const std::string& name) {
  return "/tmp/lobx_exchange_config_loader_" + name;
}

void write_file(const std::string& path, const std::string& content) {
  std::ofstream out(path);
  out << content;
}

bool same_scenario_shape(const ScenarioConfig& a, const ScenarioConfig& b) {
  if (a.seed != b.seed || a.ticks != b.ticks || a.market_symbol != b.market_symbol ||
      a.bots.size() != b.bots.size()) {
    return false;
  }
  for (size_t i = 0; i < a.bots.size(); ++i) {
    const BotConfig& x = a.bots[i];
    const BotConfig& y = b.bots[i];
    if (x.user != y.user || x.name != y.name || x.strategy_type != y.strategy_type ||
        x.latency.order_latency != y.latency.order_latency ||
        x.latency.cancel_latency != y.latency.cancel_latency ||
        x.latency.market_data_latency != y.latency.market_data_latency ||
        x.latency.private_data_latency != y.latency.private_data_latency ||
        x.params != y.params) {
      return false;
    }
  }
  return true;
}

} // namespace

TEST(ConfigLoaderTests, LoadScenarioConfigFromJsonString) {
  const auto loaded = load_scenario_config_from_json_string(valid_scenario_json());

  EXPECT_TRUE_MSG(loaded.ok, loaded.reason);
  EXPECT_EQ(loaded.config.seed, 42ULL);
  EXPECT_EQ(loaded.config.ticks, 100);
  EXPECT_EQ(loaded.config.bots.size(), 2UL);
  EXPECT_EQ(loaded.config.bots[0].name, std::string("mm"));
}

TEST(ConfigLoaderTests, LoadScenarioConfigFromJsonFile) {
  const std::string path = temp_path("scenario.json");
  write_file(path, valid_scenario_json());

  const auto loaded = load_scenario_config_from_json_file(path);

  EXPECT_TRUE_MSG(loaded.ok, loaded.reason);
  EXPECT_EQ(loaded.config.market_symbol, std::string("BTC-USDT"));
}

TEST(ConfigLoaderTests, ScenarioJsonRoundTripPreservesConfig) {
  const auto loaded = load_scenario_config_from_json_string(valid_scenario_json());
  EXPECT_TRUE_MSG(loaded.ok, loaded.reason);

  const auto reloaded = load_scenario_config_from_json_string(scenario_config_to_json(loaded.config));

  EXPECT_TRUE_MSG(reloaded.ok, reloaded.reason);
  EXPECT_TRUE(same_scenario_shape(loaded.config, reloaded.config));
}

TEST(ConfigLoaderTests, LoadScenarioRejectsMissingSeed) {
  const auto loaded = load_scenario_config_from_json_string(R"JSON({"ticks": 1, "market_symbol": "BTC-USDT", "bots": []})JSON");

  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.reason.find("seed") != std::string::npos);
}

TEST(ConfigLoaderTests, LoadScenarioRejectsMissingTicks) {
  const auto loaded = load_scenario_config_from_json_string(R"JSON({"seed": 1, "market_symbol": "BTC-USDT", "bots": []})JSON");

  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.reason.find("ticks") != std::string::npos);
}

TEST(ConfigLoaderTests, LoadScenarioRejectsMissingBots) {
  const auto loaded = load_scenario_config_from_json_string(R"JSON({"seed": 1, "ticks": 1, "market_symbol": "BTC-USDT"})JSON");

  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.reason.find("bots") != std::string::npos);
}

TEST(ConfigLoaderTests, LoadScenarioRejectsDuplicateUsersViaValidation) {
  std::string json = valid_scenario_json();
  const size_t pos = json.find("\"user\": 20");
  json.replace(pos, std::string("\"user\": 20").size(), "\"user\": 10");

  const auto loaded = load_scenario_config_from_json_string(json);

  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.reason.find("duplicate") != std::string::npos);
}

TEST(ConfigLoaderTests, LoadScenarioRejectsReservedFeeAccountViaValidation) {
  std::string json = valid_scenario_json();
  const std::string reserved = "\"user\": " + std::to_string(std::numeric_limits<lobx::UserId>::max());
  const size_t pos = json.find("\"user\": 10");
  json.replace(pos, std::string("\"user\": 10").size(), reserved);

  const auto loaded = load_scenario_config_from_json_string(json);

  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.reason.find("reserved") != std::string::npos);
}

TEST(ConfigLoaderTests, LoadScenarioRejectsUnknownStrategyViaValidation) {
  std::string json = valid_scenario_json();
  const size_t pos = json.find("\"strategy_type\": \"market_maker\"");
  json.replace(pos, std::string("\"strategy_type\": \"market_maker\"").size(), "\"strategy_type\": \"telepathy\"");

  const auto loaded = load_scenario_config_from_json_string(json);

  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.reason.find("unknown") != std::string::npos);
}

TEST(ConfigLoaderTests, LoadScenarioRejectsNegativeLatency) {
  std::string json = valid_scenario_json();
  const size_t pos = json.find("\"order\": 1");
  json.replace(pos, std::string("\"order\": 1").size(), "\"order\": -1");

  const auto loaded = load_scenario_config_from_json_string(json);

  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.reason.find("latency") != std::string::npos);
}

TEST(ConfigLoaderTests, LoadScenarioRejectsTrailingGarbage) {
  const auto loaded = load_scenario_config_from_json_string(valid_scenario_json() + " trailing");

  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.reason.find("trailing") != std::string::npos);
}

TEST(ConfigLoaderTests, LoadSweepConfigFromJsonString) {
  const auto loaded = load_sweep_config_from_json_string(valid_sweep_json());

  EXPECT_TRUE_MSG(loaded.ok, loaded.reason);
  EXPECT_EQ(loaded.params.size(), 2UL);
  EXPECT_EQ(loaded.params[0].values.size(), 2UL);
}

TEST(ConfigLoaderTests, SweepJsonRoundTripPreservesParams) {
  const auto loaded = load_sweep_config_from_json_string(valid_sweep_json());
  EXPECT_TRUE_MSG(loaded.ok, loaded.reason);

  const auto reloaded = load_sweep_config_from_json_string(sweep_config_to_json(loaded.params));

  EXPECT_TRUE_MSG(reloaded.ok, reloaded.reason);
  EXPECT_EQ(reloaded.params.size(), loaded.params.size());
  EXPECT_EQ(reloaded.params[0].bot_name, loaded.params[0].bot_name);
  EXPECT_EQ(reloaded.params[1].values[1], loaded.params[1].values[1]);
}

TEST(ConfigLoaderTests, LoadSweepRejectsEmptyValues) {
  const auto loaded = load_sweep_config_from_json_string(R"JSON({"params": [{"bot_name": "mm", "param_name": "bid_px", "values": []}]})JSON");

  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.reason.find("values") != std::string::npos);
}

TEST(ConfigLoaderTests, LoadSeedConfigFromJsonString) {
  const auto loaded = load_seed_config_from_json_string(valid_seed_json());

  EXPECT_TRUE_MSG(loaded.ok, loaded.reason);
  EXPECT_EQ(loaded.seeds.size(), 5UL);
  EXPECT_EQ(loaded.seeds[2], 3ULL);
}

TEST(ConfigLoaderTests, SeedJsonRoundTripPreservesSeeds) {
  const auto loaded = load_seed_config_from_json_string(valid_seed_json());
  EXPECT_TRUE_MSG(loaded.ok, loaded.reason);

  const auto reloaded = load_seed_config_from_json_string(seed_config_to_json(loaded.seeds));

  EXPECT_TRUE_MSG(reloaded.ok, reloaded.reason);
  EXPECT_EQ(reloaded.seeds.size(), loaded.seeds.size());
  for (size_t i = 0; i < loaded.seeds.size(); ++i) {
    EXPECT_EQ(reloaded.seeds[i], loaded.seeds[i]);
  }
}

TEST(ConfigLoaderTests, LoadSeedsRejectsEmptySeeds) {
  const auto loaded = load_seed_config_from_json_string(R"JSON({"seeds": []})JSON");

  EXPECT_FALSE(loaded.ok);
  EXPECT_TRUE(loaded.reason.find("seeds") != std::string::npos);
}
