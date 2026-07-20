#include "lobx/simulation/mesa_agent_sim.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Args {
  int steps{1000};
  int depth_levels{20};
  uint64_t seed{42};
};

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--steps" && i + 1 < argc) args.steps = std::stoi(argv[++i]);
    else if (arg == "--depth-levels" && i + 1 < argc) args.depth_levels = std::stoi(argv[++i]);
    else if (arg == "--seed" && i + 1 < argc) args.seed = std::stoull(argv[++i]);
    else if (arg == "--help") {
      std::cout << "Usage: bench_events [--steps N] [--depth-levels N] [--seed N]\n";
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

std::string depth_json(const std::vector<std::pair<lob::Tick, lob::Quantity>>& bids,
                       const std::vector<std::pair<lob::Tick, lob::Quantity>>& asks,
                       int step) {
  std::ostringstream os;
  os << "{\"type\":\"book\",\"step\":" << step << ",\"bids\":[";
  for (size_t i = 0; i < bids.size(); ++i) {
    if (i > 0) os << ',';
    os << "[" << bids[i].first << "," << bids[i].second << "]";
  }
  os << "],\"asks\":[";
  for (size_t i = 0; i < asks.size(); ++i) {
    if (i > 0) os << ',';
    os << "[" << asks[i].first << "," << asks[i].second << "]";
  }
  os << "]}";
  return os.str();
}

} // namespace

int main(int argc, char** argv) {
  try {
    const Args args = parse_args(argc, argv);
    lobx::sim::MesaAgentSimConfig config{};
    config.seed = args.seed;
    config.steps = 0;
    config.agents = lobx::sim::MesaAgentCounts{8, 16, 4, 4, 2};
    config.book_levels = std::max(20, args.depth_levels);

    lobx::sim::MesaAgentSimulation sim(config);
    std::vector<long long> snapshot_latency_us;
    std::vector<long long> json_latency_us;
    snapshot_latency_us.reserve(static_cast<size_t>(args.steps));
    json_latency_us.reserve(static_cast<size_t>(args.steps));
    size_t bytes = 0;
    size_t max_event_size = 0;
    int messages = 0;

    const auto start = Clock::now();
    for (int i = 0; i < args.steps; ++i) {
      const lobx::sim::MesaStepEvents events = sim.step();
      const auto snap0 = Clock::now();
      const auto bids = sim.bids(args.depth_levels);
      const auto asks = sim.asks(args.depth_levels);
      const auto snap1 = Clock::now();
      snapshot_latency_us.push_back(std::chrono::duration_cast<std::chrono::microseconds>(snap1 - snap0).count());

      const auto json0 = Clock::now();
      std::vector<std::string> payloads;
      payloads.push_back(lobx::sim::mesa_step_stats_json(events.stats));
      payloads.push_back(depth_json(bids, asks, events.step));
      for (const auto& trade : events.trades) payloads.push_back(lobx::sim::mesa_trade_json(trade, events.step));
      for (const auto& candle : events.candles) payloads.push_back(lobx::sim::mesa_step_candle_json(candle));
      const auto json1 = Clock::now();
      json_latency_us.push_back(std::chrono::duration_cast<std::chrono::microseconds>(json1 - json0).count());

      for (const auto& payload : payloads) {
        const std::string sse = "data: " + payload + "\n\n";
        bytes += sse.size();
        max_event_size = std::max(max_event_size, sse.size());
        ++messages;
      }
    }
    const auto end = Clock::now();
    const double elapsed_ms = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
    const double seconds = elapsed_ms / 1000.0;

    std::cout << "{"
              << "\"benchmark\":\"events\","
              << "\"steps\":" << args.steps << ","
              << "\"depth_levels\":" << args.depth_levels << ","
              << "\"elapsed_ms\":" << elapsed_ms << ","
              << "\"events_per_sec\":" << (seconds > 0.0 ? static_cast<double>(messages) / seconds : 0.0) << ","
              << "\"bytes_per_sec\":" << (seconds > 0.0 ? static_cast<double>(bytes) / seconds : 0.0) << ","
              << "\"snapshot_latency_p50_us\":" << percentile(snapshot_latency_us, 50.0) << ","
              << "\"snapshot_latency_p95_us\":" << percentile(snapshot_latency_us, 95.0) << ","
              << "\"json_latency_p95_us\":" << percentile(json_latency_us, 95.0) << ","
              << "\"avg_event_size_bytes\":" << (messages > 0 ? static_cast<double>(bytes) / static_cast<double>(messages) : 0.0) << ","
              << "\"max_event_size_bytes\":" << max_event_size
              << "}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "bench_events error: " << e.what() << "\n";
    return 1;
  }
}
