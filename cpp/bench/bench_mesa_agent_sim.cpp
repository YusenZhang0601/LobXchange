#include "lobx/simulation/mesa_agent_sim.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Args {
  std::string case_name{"tiny"};
  int steps{-1};
  int agents{-1};
  uint64_t seed{42};
};

struct CaseSpec {
  std::string name;
  int steps;
  int agents;
};

const std::vector<CaseSpec>& cases() {
  static const std::vector<CaseSpec> specs{{"tiny", 100, 15},
                                           {"small", 1000, 100},
                                           {"medium", 10000, 1000},
                                           {"large", 100000, 1000},
                                           {"huge-agents", 10000, 10000},
                                           {"maker-heavy", 10000, 1000},
                                           {"noise-heavy", 10000, 1000},
                                           {"whale-heavy", 10000, 1000},
                                           {"momentum-heavy", 10000, 1000},
                                           {"meanrev-heavy", 10000, 1000}};
  return specs;
}

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--case" && i + 1 < argc) args.case_name = argv[++i];
    else if (arg == "--steps" && i + 1 < argc) args.steps = std::stoi(argv[++i]);
    else if (arg == "--agents" && i + 1 < argc) args.agents = std::stoi(argv[++i]);
    else if (arg == "--seed" && i + 1 < argc) args.seed = std::stoull(argv[++i]);
    else if (arg == "--help") {
      std::cout << "Usage: bench_mesa_agent_sim [--case NAME] [--steps N] [--agents N] [--seed N]\n";
      std::exit(0);
    }
  }
  return args;
}

CaseSpec resolve_case(const Args& args) {
  CaseSpec spec{"custom", args.steps > 0 ? args.steps : 100, args.agents > 0 ? args.agents : 15};
  for (const auto& item : cases()) {
    if (item.name == args.case_name) {
      spec = item;
      break;
    }
  }
  if (args.steps > 0) spec.steps = args.steps;
  if (args.agents > 0) spec.agents = args.agents;
  if (args.case_name != "custom") spec.name = args.case_name;
  return spec;
}

lobx::sim::MesaAgentCounts counts_for(const std::string& name, int agents) {
  lobx::sim::MesaAgentCounts counts{};
  if (name == "maker-heavy") {
    counts.market_makers = agents * 8 / 10;
    counts.noise_traders = agents - counts.market_makers;
  } else if (name == "noise-heavy") {
    counts.noise_traders = agents * 8 / 10;
    counts.market_makers = agents - counts.noise_traders;
  } else if (name == "whale-heavy") {
    counts.whale_sweepers = std::max(1, agents * 3 / 10);
    counts.market_makers = agents - counts.whale_sweepers;
  } else if (name == "momentum-heavy") {
    counts.momentum = agents * 6 / 10;
    counts.market_makers = agents * 3 / 10;
    counts.noise_traders = agents - counts.momentum - counts.market_makers;
  } else if (name == "meanrev-heavy") {
    counts.mean_reversion = agents * 6 / 10;
    counts.market_makers = agents * 3 / 10;
    counts.noise_traders = agents - counts.mean_reversion - counts.market_makers;
  } else {
    counts.market_makers = std::max(1, agents * 25 / 100);
    counts.noise_traders = std::max(0, agents * 50 / 100);
    counts.momentum = std::max(0, agents * 10 / 100);
    counts.mean_reversion = std::max(0, agents * 10 / 100);
    counts.whale_sweepers = agents - counts.market_makers - counts.noise_traders - counts.momentum - counts.mean_reversion;
  }
  return counts;
}

template <typename T>
T percentile(std::vector<T> values, double p) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const size_t index = static_cast<size_t>((p * static_cast<double>(values.size() - 1)) / 100.0);
  return values[index];
}

long rss_mb() {
  std::ifstream in("/proc/self/statm");
  long pages_total = 0;
  long pages_rss = 0;
  if (!(in >> pages_total >> pages_rss)) return 0;
  const long page_kb = static_cast<long>(::sysconf(_SC_PAGESIZE) / 1024);
  return (pages_rss * page_kb) / 1024;
}

lob::Quantity total_volume(const std::vector<lobx::TradeEvent>& trades) {
  lob::Quantity volume = 0;
  for (const auto& trade : trades) volume += trade.qty;
  return volume;
}

lob::Quantity book_depth(const std::vector<std::pair<lob::Tick, lob::Quantity>>& side) {
  lob::Quantity depth = 0;
  for (const auto& level : side) depth += level.second;
  return depth;
}

} // namespace

int main(int argc, char** argv) {
  try {
    const Args args = parse_args(argc, argv);
    const CaseSpec spec = resolve_case(args);
    lobx::sim::MesaAgentSimConfig config{};
    config.seed = args.seed;
    config.steps = spec.steps;
    config.agents = counts_for(spec.name, spec.agents);

    lobx::sim::MesaAgentSimulation sim(config);
    std::vector<lob::Tick> spreads;
    spreads.reserve(static_cast<size_t>(spec.steps));
    lob::Quantity max_depth = 0;

    const auto start = Clock::now();
    for (int i = 0; i < spec.steps; ++i) {
      const lobx::sim::MesaStepEvents events = sim.step();
      spreads.push_back(events.stats.spread);
      max_depth = std::max(max_depth, book_depth(sim.bids(100)) + book_depth(sim.asks(100)));
    }
    const auto end = Clock::now();
    const lobx::sim::MesaAgentSimSummary summary = sim.summary();
    const double elapsed_ms = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
    const double seconds = elapsed_ms / 1000.0;

    std::cout << "{"
              << "\"engine\":\"cpp\","
              << "\"case\":\"" << spec.name << "\","
              << "\"steps\":" << spec.steps << ","
              << "\"agents\":" << spec.agents << ","
              << "\"seed\":" << args.seed << ","
              << "\"elapsed_ms\":" << elapsed_ms << ","
              << "\"steps_per_sec\":" << (seconds > 0.0 ? static_cast<double>(spec.steps) / seconds : 0.0) << ","
              << "\"orders_per_sec\":" << (seconds > 0.0 ? static_cast<double>(summary.accepted_orders + summary.rejected_orders) / seconds : 0.0) << ","
              << "\"trades_per_sec\":" << (seconds > 0.0 ? static_cast<double>(summary.trade_count) / seconds : 0.0) << ","
              << "\"accepted_orders\":" << summary.accepted_orders << ","
              << "\"rejected_orders\":" << summary.rejected_orders << ","
              << "\"trade_count\":" << summary.trade_count << ","
              << "\"total_volume\":" << total_volume(sim.trades()) << ","
              << "\"final_best_bid\":" << summary.final_best_bid << ","
              << "\"final_best_ask\":" << summary.final_best_ask << ","
              << "\"spread_mean\":" << summary.mean_spread << ","
              << "\"spread_p95\":" << percentile(spreads, 95.0) << ","
              << "\"spread_p99\":" << percentile(spreads, 99.0) << ","
              << "\"max_book_depth\":" << max_depth << ","
              << "\"rss_mb\":" << rss_mb()
              << "}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "bench_mesa_agent_sim error: " << e.what() << "\n";
    return 1;
  }
}
