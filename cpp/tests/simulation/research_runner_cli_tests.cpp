#include "lobx/simulation/research_cli.hpp"

#include "test_helpers/test_framework.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace lobx::sim;

namespace {

std::string scenario_json() {
  return R"JSON({
    "seed": 42,
    "ticks": 20,
    "market_symbol": "BTC-USDT",
    "bots": [
      {
        "user": 10,
        "name": "mm",
        "strategy_type": "market_maker",
        "latency": {"order": 0, "cancel": 0, "market_data": 1, "private_data": 1},
        "params": {"bid_px": 99, "ask_px": 101, "qty": 1}
      },
      {
        "user": 20,
        "name": "taker",
        "strategy_type": "taker_sweep",
        "latency": {"order": 0, "cancel": 0, "market_data": 1, "private_data": 1},
        "params": {"side": 0, "target_qty": 1, "limit_price": 101, "max_avg_price": 101}
      },
      {
        "user": 30,
        "name": "noise",
        "strategy_type": "noise_trader",
        "latency": {"order": 1, "cancel": 0, "market_data": 1, "private_data": 1},
        "params": {"seed": 7}
      }
    ]
  })JSON";
}

std::string sweep_json() {
  return R"JSON({
    "params": [
      {"bot_name": "mm", "param_name": "bid_px", "values": [98, 99]},
      {"bot_name": "mm", "param_name": "ask_px", "values": [101, 102]}
    ]
  })JSON";
}

std::string seeds_json() {
  return R"JSON({"seeds": [1, 2, 3]})JSON";
}

struct CliTestFiles {
  std::filesystem::path dir;
  std::string scenario_path;
  std::string sweep_path;
  std::string seeds_path;
  std::string output_dir;
};

std::string sanitize(const std::string& name) {
  std::string out = name;
  for (char& c : out) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_')) {
      c = '_';
    }
  }
  return out;
}

void write_file(const std::filesystem::path& path, const std::string& content) {
  std::ofstream out(path, std::ios::binary);
  out << content;
}

CliTestFiles write_valid_cli_test_files(const std::string& test_name) {
  const std::filesystem::path root =
      std::filesystem::temp_directory_path() / ("lobx_exchange_research_cli_" + sanitize(test_name));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  const std::filesystem::path scenario_path = root / "scenario.json";
  const std::filesystem::path sweep_path = root / "sweep.json";
  const std::filesystem::path seeds_path = root / "seeds.json";
  write_file(scenario_path, scenario_json());
  write_file(sweep_path, sweep_json());
  write_file(seeds_path, seeds_json());

  return CliTestFiles{root,
                      scenario_path.string(),
                      sweep_path.string(),
                      seeds_path.string(),
                      (root / "results").string()};
}

std::vector<std::string> valid_args(const CliTestFiles& files,
                                    const std::string& metric = "net_pnl",
                                    const std::string& output_dir = {}) {
  return {"--scenario", files.scenario_path,
          "--sweep", files.sweep_path,
          "--seeds", files.seeds_path,
          "--rank-bot", "mm",
          "--metric", metric,
          "--out", output_dir.empty() ? files.output_dir : output_dir};
}

std::string read_file(const std::filesystem::path& path) {
  std::ifstream in(path, std::ios::binary);
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

int non_empty_csv_data_rows(const std::string& csv) {
  std::istringstream in(csv);
  std::string line;
  int lines = 0;
  while (std::getline(in, line)) {
    if (!line.empty()) ++lines;
  }
  return lines == 0 ? 0 : lines - 1;
}

void expect_successful_cli_run(const std::string& name, const std::string& metric) {
  const CliTestFiles files = write_valid_cli_test_files(name);
  const ResearchCliResult result = run_research_cli(valid_args(files, metric));
  EXPECT_EQ_MSG(result.exit_code, 0, result.stderr_text);
  EXPECT_TRUE(result.stdout_text.find("completed") != std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(files.output_dir) / "ranking.csv"));
  EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(files.output_dir) / "aggregate.csv"));
  EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(files.output_dir) / "summary.json"));
}

} // namespace

TEST(ResearchRunnerCliTests, CliHelpPrintsUsage) {
  const ResearchCliResult result = run_research_cli({"--help"});

  EXPECT_EQ(result.exit_code, 0);
  EXPECT_TRUE(result.stdout_text.find("usage: lobx_research_runner") != std::string::npos);
  EXPECT_TRUE(result.stderr_text.empty());
}

TEST(ResearchRunnerCliTests, CliRejectsMissingScenarioArg) {
  const ResearchCliResult result =
      run_research_cli({"--sweep", "sweep.json", "--seeds", "seeds.json", "--rank-bot", "mm", "--metric", "net_pnl", "--out", "out"});

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.find("--scenario") != std::string::npos);
}

TEST(ResearchRunnerCliTests, CliRejectsMissingSweepArg) {
  const ResearchCliResult result =
      run_research_cli({"--scenario", "scenario.json", "--seeds", "seeds.json", "--rank-bot", "mm", "--metric", "net_pnl", "--out", "out"});

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.find("--sweep") != std::string::npos);
}

TEST(ResearchRunnerCliTests, CliRejectsMissingSeedsArg) {
  const ResearchCliResult result =
      run_research_cli({"--scenario", "scenario.json", "--sweep", "sweep.json", "--rank-bot", "mm", "--metric", "net_pnl", "--out", "out"});

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.find("--seeds") != std::string::npos);
}

TEST(ResearchRunnerCliTests, CliRejectsMissingRankBotArg) {
  const ResearchCliResult result =
      run_research_cli({"--scenario", "scenario.json", "--sweep", "sweep.json", "--seeds", "seeds.json", "--metric", "net_pnl", "--out", "out"});

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.find("--rank-bot") != std::string::npos);
}

TEST(ResearchRunnerCliTests, CliRejectsMissingMetricArg) {
  const ResearchCliResult result =
      run_research_cli({"--scenario", "scenario.json", "--sweep", "sweep.json", "--seeds", "seeds.json", "--rank-bot", "mm", "--out", "out"});

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.find("--metric") != std::string::npos);
}

TEST(ResearchRunnerCliTests, CliRejectsMissingOutputDirArg) {
  const ResearchCliResult result =
      run_research_cli({"--scenario", "scenario.json", "--sweep", "sweep.json", "--seeds", "seeds.json", "--rank-bot", "mm", "--metric", "net_pnl"});

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.find("--out") != std::string::npos);
}

TEST(ResearchRunnerCliTests, CliRejectsUnknownMetric) {
  const CliTestFiles files = write_valid_cli_test_files("unknown_metric");
  const ResearchCliResult result = run_research_cli(valid_args(files, "bad_metric"));

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.find("unknown ranking metric") != std::string::npos);
}

TEST(ResearchRunnerCliTests, CliRejectsNegativeTopN) {
  const CliTestFiles files = write_valid_cli_test_files("negative_top_n");
  std::vector<std::string> args = valid_args(files);
  args.push_back("--top-n");
  args.push_back("-1");

  const ResearchCliResult result = run_research_cli(args);

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.find("top-n") != std::string::npos);
}

TEST(ResearchRunnerCliTests, CliRejectsNonNumericTopN) {
  const CliTestFiles files = write_valid_cli_test_files("nonnumeric_top_n");
  std::vector<std::string> args = valid_args(files);
  args.push_back("--top-n");
  args.push_back("abc");

  const ResearchCliResult result = run_research_cli(args);

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.find("top-n") != std::string::npos);
}

TEST(ResearchRunnerCliTests, CliMetricNetPnlWorks) {
  expect_successful_cli_run("metric_net_pnl", "net_pnl");
}

TEST(ResearchRunnerCliTests, CliMetricGrossPnlWorks) {
  expect_successful_cli_run("metric_gross_pnl", "gross_pnl");
}

TEST(ResearchRunnerCliTests, CliMetricFeesPaidInverseWorks) {
  expect_successful_cli_run("metric_fees_paid_inverse", "fees_paid_inverse");
}

TEST(ResearchRunnerCliTests, CliRunsEndToEndAndWritesBundle) {
  const CliTestFiles files = write_valid_cli_test_files("end_to_end");

  const ResearchCliResult result = run_research_cli(valid_args(files));

  EXPECT_EQ_MSG(result.exit_code, 0, result.stderr_text);
  EXPECT_TRUE(result.stdout_text.find("completed") != std::string::npos);
  EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(files.output_dir) / "ranking.csv"));
  EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(files.output_dir) / "aggregate.csv"));
  EXPECT_TRUE(std::filesystem::exists(std::filesystem::path(files.output_dir) / "summary.json"));
  EXPECT_TRUE(read_file(std::filesystem::path(files.output_dir) / "ranking.csv").find("rank,bot_name,user") != std::string::npos);
  EXPECT_TRUE(read_file(std::filesystem::path(files.output_dir) / "summary.json").find("\"invariants\"") != std::string::npos);
}

TEST(ResearchRunnerCliTests, CliTopNLimitsRankingRows) {
  const CliTestFiles files = write_valid_cli_test_files("top_n");
  std::vector<std::string> args = valid_args(files);
  args.push_back("--top-n");
  args.push_back("1");

  const ResearchCliResult result = run_research_cli(args);

  EXPECT_EQ_MSG(result.exit_code, 0, result.stderr_text);
  EXPECT_EQ(non_empty_csv_data_rows(read_file(std::filesystem::path(files.output_dir) / "ranking.csv")), 1);
}

TEST(ResearchRunnerCliTests, CliOutputIsDeterministicForSameInputs) {
  const CliTestFiles files = write_valid_cli_test_files("deterministic");
  const std::string out_a = (files.dir / "out_a").string();
  const std::string out_b = (files.dir / "out_b").string();

  const ResearchCliResult a = run_research_cli(valid_args(files, "net_pnl", out_a));
  const ResearchCliResult b = run_research_cli(valid_args(files, "net_pnl", out_b));

  EXPECT_EQ_MSG(a.exit_code, 0, a.stderr_text);
  EXPECT_EQ_MSG(b.exit_code, 0, b.stderr_text);
  EXPECT_EQ(read_file(std::filesystem::path(out_a) / "ranking.csv"),
            read_file(std::filesystem::path(out_b) / "ranking.csv"));
  EXPECT_EQ(read_file(std::filesystem::path(out_a) / "aggregate.csv"),
            read_file(std::filesystem::path(out_b) / "aggregate.csv"));
  EXPECT_EQ(read_file(std::filesystem::path(out_a) / "summary.json"),
            read_file(std::filesystem::path(out_b) / "summary.json"));
}

TEST(ResearchRunnerCliTests, CliRejectsInvalidScenarioJson) {
  CliTestFiles files = write_valid_cli_test_files("invalid_scenario");
  write_file(files.scenario_path, R"JSON({"seed": 1})JSON");

  const ResearchCliResult result = run_research_cli(valid_args(files));

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.find("failed to load scenario") != std::string::npos);
}

TEST(ResearchRunnerCliTests, CliRejectsInvalidOutputPath) {
  const CliTestFiles files = write_valid_cli_test_files("invalid_output");
  const std::filesystem::path blocked = files.dir / "not_a_directory";
  write_file(blocked, "plain file");

  const ResearchCliResult result = run_research_cli(valid_args(files, "net_pnl", blocked.string()));

  EXPECT_NE(result.exit_code, 0);
  EXPECT_TRUE(result.stderr_text.find("failed to write output bundle") != std::string::npos);
}
