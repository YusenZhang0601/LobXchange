#include "lobx/exchange.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <random>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Args {
  int orders{50000};
  uint64_t seed{42};
};

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--orders" && i + 1 < argc) args.orders = std::stoi(argv[++i]);
    else if (arg == "--seed" && i + 1 < argc) args.seed = std::stoull(argv[++i]);
    else if (arg == "--help") {
      std::cout << "Usage: bench_no_snapshot [--orders N] [--seed N]\n";
      std::exit(0);
    }
  }
  return args;
}

void require_ok(const lobx::Result& result, const std::string& context) {
  if (!result.ok) throw std::runtime_error(context + ": " + result.reason);
}

lobx::Exchange bootstrap_exchange() {
  lobx::Exchange ex;
  require_ok(ex.issue_asset("USDT", 6, 900000000000000000LL, 1, 0), "issue USDT");
  require_ok(ex.issue_asset("BTC", 8, 900000000000000000LL, 1, 0), "issue BTC");
  require_ok(ex.create_spot_market("BTC-USDT", "BTC", "USDT", 1, 1, 1, 1), "create market");
  ex.events().set_memory_enabled(false);
  for (lobx::UserId user = 100; user < 200; ++user) {
    require_ok(ex.deposit(user, "USDT", 1000000000000LL), "deposit USDT");
    require_ok(ex.deposit(user, "BTC", 1000000000LL), "deposit BTC");
  }
  return ex;
}

long rss_mb() {
  std::ifstream in("/proc/self/statm");
  long pages_total = 0;
  long pages_rss = 0;
  if (!(in >> pages_total >> pages_rss)) return 0;
  const long page_kb = static_cast<long>(::sysconf(_SC_PAGESIZE) / 1024);
  return (pages_rss * page_kb) / 1024;
}

template <typename T>
T percentile(std::vector<T> values, double p) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const size_t index = static_cast<size_t>((p * static_cast<double>(values.size() - 1)) / 100.0);
  return values[index];
}

lob::Tick best_or(lobx::Exchange& ex, lob::Side side, lob::Tick fallback) {
  const auto levels = ex.topN("BTC-USDT", side, 1);
  return levels.empty() ? fallback : levels.front().first;
}

} // namespace

int main(int argc, char** argv) {
  try {
    const Args args = parse_args(argc, argv);
    lobx::Exchange ex = bootstrap_exchange();
    std::mt19937_64 rng(args.seed);
    std::uniform_int_distribution<int> user_dist(100, 199);
    std::uniform_int_distribution<int> op_dist(1, 100);
    std::uniform_int_distribution<int> qty_dist(1, 5);
    std::vector<lobx::OrderId> live_orders;
    std::vector<long long> submit_latency_ns;
    live_orders.reserve(static_cast<size_t>(args.orders / 2));
    submit_latency_ns.reserve(static_cast<size_t>(args.orders));

    lobx::OrderId next_order_id = 1;
    int accepted = 0;
    int rejected = 0;
    int cancels = 0;
    int snapshots = 0;
    long long trade_count = 0;

    const auto start = Clock::now();
    for (int i = 0; i < args.orders; ++i) {
      const int op = op_dist(rng);
      const lobx::UserId user = static_cast<lobx::UserId>(user_dist(rng));
      if (op <= 50) {
        const bool buy = (rng() & 1ULL) != 0;
        const lob::Tick bid = best_or(ex, lob::Side::Bid, 9998);
        const lob::Tick ask = best_or(ex, lob::Side::Ask, 10002);
        const lob::Tick price = buy ? std::min<lob::Tick>(bid + 1, ask - 1) : std::max<lob::Tick>(ask - 1, bid + 1);
        const auto t0 = Clock::now();
        // Option A Optimization Test: Direct matching without make_submit_snapshot()
        auto result = ex.submit_limit("BTC-USDT", user, next_order_id++, buy ? lob::Side::Bid : lob::Side::Ask,
                                      std::max<lob::Tick>(1, price), qty_dist(rng), lob::POST_ONLY);
        const auto t1 = Clock::now();
        submit_latency_ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        if (result.accepted) {
          ++accepted;
          live_orders.push_back(next_order_id - 1);
        } else {
          ++rejected;
        }
        trade_count += static_cast<long long>(result.trades.size());
      } else if (op <= 85) {
        const bool buy = (rng() & 1ULL) != 0;
        const lob::Tick price = buy ? best_or(ex, lob::Side::Ask, 10010) : best_or(ex, lob::Side::Bid, 9990);
        const auto t0 = Clock::now();
        auto result = ex.submit_limit("BTC-USDT", user, next_order_id++, buy ? lob::Side::Bid : lob::Side::Ask,
                                      std::max<lob::Tick>(1, price), qty_dist(rng), lob::IOC);
        const auto t1 = Clock::now();
        submit_latency_ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        if (result.accepted) ++accepted;
        else ++rejected;
        trade_count += static_cast<long long>(result.trades.size());
      } else if (op <= 95) {
        if (!live_orders.empty()) {
          const size_t index = static_cast<size_t>(rng() % live_orders.size());
          if (ex.cancel("BTC-USDT", live_orders[index])) ++cancels;
          live_orders[index] = live_orders.back();
          live_orders.pop_back();
        }
      } else {
        (void)ex.topN("BTC-USDT", lob::Side::Bid, 20);
        (void)ex.topN("BTC-USDT", lob::Side::Ask, 20);
        ++snapshots;
      }
    }
    const auto end = Clock::now();
    const double elapsed_ms = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
    const double seconds = elapsed_ms / 1000.0;

    std::cout << "{"
              << "\"benchmark\":\"option_a_no_snapshot\","
              << "\"elapsed_ms\":" << elapsed_ms << ","
              << "\"total_orders\":" << args.orders << ","
              << "\"accepted_orders\":" << accepted << ","
              << "\"rejected_orders\":" << rejected << ","
              << "\"trade_count\":" << trade_count << ","
              << "\"orders_per_sec\":" << (seconds > 0.0 ? static_cast<double>(args.orders) / seconds : 0.0) << ","
              << "\"trades_per_sec\":" << (seconds > 0.0 ? static_cast<double>(trade_count) / seconds : 0.0) << ","
              << "\"cancels_per_sec\":" << (seconds > 0.0 ? static_cast<double>(cancels) / seconds : 0.0) << ","
              << "\"snapshot_per_sec\":" << (seconds > 0.0 ? static_cast<double>(snapshots) / seconds : 0.0) << ","
              << "\"submit_latency_p50_ns\":" << percentile(submit_latency_ns, 50.0) << ","
              << "\"submit_latency_p95_ns\":" << percentile(submit_latency_ns, 95.0) << ","
              << "\"submit_latency_p99_ns\":" << percentile(submit_latency_ns, 99.0) << ","
              << "\"submit_latency_max_ns\":" << (submit_latency_ns.empty() ? 0 : *std::max_element(submit_latency_ns.begin(), submit_latency_ns.end())) << ","
              << "\"rss_mb\":" << rss_mb()
              << "}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "bench_no_snapshot error: " << e.what() << "\n";
    return 1;
  }
}
