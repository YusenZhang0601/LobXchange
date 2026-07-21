#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"
#include <thread>
#include <vector>
#include <atomic>
#include <chrono>

using namespace lobx_test;

TEST(ConcurrentMarketEngine, DISABLED_ParallelReadWriteDoNotCorruptState) {
  PerpEngineFixture f(10000000LL);

  std::atomic<bool> stop_flag{false};
  std::atomic<uint64_t> id_counter{1000000ULL};

  // Pre-seed some orders in the order book so simulate_fill has something to look at
  for (int i = 0; i < 50; ++i) {
    f.submit(f.bob, i + 1, lob::Side::Bid, 100, 10, lob::POST_ONLY, 0);
    f.submit(f.carol, i + 100, lob::Side::Ask, 110, 10, lob::POST_ONLY, 0);
  }

  std::vector<std::thread> readers;
  std::vector<std::thread> writers;

  // 1. Launch reader threads
  for (int i = 0; i < 4; ++i) {
    readers.emplace_back([&]() {
      while (!stop_flag) {
        auto sim = f.engine.simulate_fill(f.bob, lob::Side::Bid, 100, 10, lob::NONE);
        (void)sim;
      }
    });
  }

  // 2. Launch writer threads
  for (int i = 0; i < 4; ++i) {
    writers.emplace_back([&]() {
      while (!stop_flag) {
        lobx::OrderId order_id = ++id_counter;
        // Alice submits a limit order
        auto res = f.submit(f.alice, order_id, lob::Side::Ask, 105, 1, lob::POST_ONLY, 0);
        if (res.accepted) {
          f.engine.cancel(order_id, f.alice, 0);
        }
      }
    });
  }

  // Let it run to observe data races/crashes
  std::this_thread::sleep_for(std::chrono::milliseconds(1000));
  stop_flag = true;

  for (auto& t : readers) {
    if (t.joinable()) t.join();
  }
  for (auto& t : writers) {
    if (t.joinable()) t.join();
  }

  // Under lock protection, these invariants should hold.
  EXPECT_TRUE(f.ledger.invariant_ok());
}
