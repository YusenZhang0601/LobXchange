#include "lobx/simulation/mesa_agent_sim.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <numeric>
#include <random>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Args {
  int iterations{100000};
  uint64_t seed{42};
};

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--iterations" && i + 1 < argc) args.iterations = std::stoi(argv[++i]);
    else if (arg == "--seed" && i + 1 < argc) args.seed = std::stoull(argv[++i]);
    else if (arg == "--help") {
      std::cout << "Usage: bench_agents [--iterations N] [--seed N]\n";
      std::exit(0);
    }
  }
  return args;
}

template <typename T>
T percentile(std::vector<T> values, double p) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const size_t index = static_cast<size_t>((p * static_cast<double>(values.size() - 1)) / 100.0);
  return values[index];
}

lobx::sim::MesaAgentSimConfig config_for(const std::string& kind, uint64_t seed) {
  lobx::sim::MesaAgentSimConfig config{};
  config.seed = seed;
  config.steps = 0;
  config.agents = lobx::sim::MesaAgentCounts{0, 0, 0, 0, 0};
  if (kind == "market_maker") config.agents.market_makers = 1;
  else if (kind == "noise_trader") config.agents.noise_traders = 1;
  else if (kind == "momentum") {
    config.agents.momentum = 1;
    config.initial_trade_prices = {100, 101};
  } else if (kind == "mean_reversion") {
    config.agents.mean_reversion = 1;
    config.initial_trade_prices = {104};
  } else if (kind == "whale_sweeper") {
    config.agents.whale_sweepers = 1;
  }
  return config;
}

struct AgentBenchResult {
  std::string name;
  int iterations{0};
  int orders{0};
  double elapsed_ms{0.0};
  long long p50_ns{0};
  long long p95_ns{0};
  long long p99_ns{0};
};

AgentBenchResult bench_agent(const std::string& kind, int iterations, uint64_t seed) {
  lobx::sim::MesaAgentSimulation sim(config_for(kind, seed));
  std::vector<long long> latency;
  latency.reserve(static_cast<size_t>(iterations));
  int orders = 0;
  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) {
    const auto t0 = Clock::now();
    const lobx::sim::MesaStepEvents events = sim.step();
    const auto t1 = Clock::now();
    latency.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    orders += static_cast<int>(events.orders.size());
  }
  const auto end = Clock::now();
  return AgentBenchResult{kind,
                          iterations,
                          orders,
                          static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0,
                          percentile(latency, 50.0),
                          percentile(latency, 95.0),
                          percentile(latency, 99.0)};
}

long long bench_shuffle(int agents, int iterations, uint64_t seed) {
  std::vector<int> order(static_cast<size_t>(agents));
  std::iota(order.begin(), order.end(), 0);
  std::mt19937_64 rng(seed);
  const auto start = Clock::now();
  for (int i = 0; i < iterations; ++i) std::shuffle(order.begin(), order.end(), rng);
  const auto end = Clock::now();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() / std::max(1, iterations);
}

} // namespace

int main(int argc, char** argv) {
  try {
    const Args args = parse_args(argc, argv);
    const std::vector<std::string> kinds{"market_maker", "noise_trader", "momentum", "mean_reversion", "whale_sweeper"};
    std::vector<AgentBenchResult> results;
    for (const auto& kind : kinds) results.push_back(bench_agent(kind, args.iterations, args.seed));

    int total_orders = 0;
    double total_elapsed_ms = 0.0;
    for (const auto& result : results) {
      total_orders += result.orders;
      total_elapsed_ms += result.elapsed_ms;
    }
    const double seconds = total_elapsed_ms / 1000.0;

    std::cout << "{"
              << "\"benchmark\":\"agents\","
              << "\"iterations\":" << args.iterations << ","
              << "\"agent_steps_per_sec\":" << (seconds > 0.0 ? static_cast<double>(args.iterations * static_cast<int>(results.size())) / seconds : 0.0) << ","
              << "\"orders_generated\":" << total_orders << ","
              << "\"decision_latency_p50_ns\":" << results.front().p50_ns << ","
              << "\"decision_latency_p95_ns\":" << results.front().p95_ns << ","
              << "\"decision_latency_p99_ns\":" << results.front().p99_ns << ","
              << "\"shuffle_1k_agents_ns\":" << bench_shuffle(1000, 1000, args.seed) << ","
              << "\"shuffle_10k_agents_ns\":" << bench_shuffle(10000, 200, args.seed) << ","
              << "\"agent_breakdown\":[";
    for (size_t i = 0; i < results.size(); ++i) {
      const auto& r = results[i];
      if (i > 0) std::cout << ",";
      const double sec = r.elapsed_ms / 1000.0;
      std::cout << "{"
                << "\"agent\":\"" << r.name << "\","
                << "\"decisions_per_sec\":" << (sec > 0.0 ? static_cast<double>(r.iterations) / sec : 0.0) << ","
                << "\"orders\":" << r.orders << ","
                << "\"latency_p50_ns\":" << r.p50_ns << ","
                << "\"latency_p95_ns\":" << r.p95_ns << ","
                << "\"latency_p99_ns\":" << r.p99_ns
                << "}";
    }
    std::cout << "]}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "bench_agents error: " << e.what() << "\n";
    return 1;
  }
}
