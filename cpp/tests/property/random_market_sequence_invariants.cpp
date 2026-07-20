#include "test_helpers/market_microstructure_helpers.hpp"

#include <random>
#include <sstream>

using namespace lobx_test;

namespace {

struct SequenceResult {
  std::string summary;
  int committed_trades{0};
};

std::string summarize_state(SpotEngineFixture& f) {
  std::ostringstream os;
  os << "bids:";
  for (const auto& level : f.engine.topN(lob::Side::Bid, 1000)) os << level.first << '@' << level.second << ',';
  os << "asks:";
  for (const auto& level : f.engine.topN(lob::Side::Ask, 1000)) os << level.first << '@' << level.second << ',';
  for (const auto& balance : f.ledger.balances()) {
    os << "bal:" << balance.user << ':' << balance.asset << ':' << balance.total << ':' << balance.free << ':' << balance.locked << ',';
  }
  os << "events:" << f.events.records().size();
  return os.str();
}

void check_sequence_invariants(SpotEngineFixture& f, int expected_trade_events) {
  EXPECT_TRUE(f.ledger.invariant_ok());
  expect_topN_matches_open_orders(f.engine);
  for (const auto& order : f.engine.open_orders()) {
    EXPECT_TRUE_MSG((order.flags & (lob::IOC | lob::FOK)) == 0u,
                    "IOC/FOK order must not rest order_id=" + std::to_string(order.id));
  }
  EXPECT_EQ(event_count(f.events, "trade"), expected_trade_events);
}

SequenceResult run_sequence(uint32_t seed, int steps) {
  SpotEngineFixture f(/*maker_fee_bps=*/0, /*taker_fee_bps=*/10);
  const lobx::UserId dave = 40;
  const lobx::UserId erin = 50;
  deposit_spot_user(f, dave);
  deposit_spot_user(f, erin);
  const std::vector<lobx::UserId> users{f.alice, f.bob, f.carol, dave, erin};
  std::mt19937 rng(seed);
  lobx::OrderId next_order_id = 60000 + static_cast<lobx::OrderId>(seed) * 10000;
  int committed_trade_events = 0;
  const auto base_before = total_asset(f.ledger, f.base_asset);
  const auto quote_before = total_asset(f.ledger, f.quote_asset);

  for (int step = 0; step < steps; ++step) {
    const int action = static_cast<int>(rng() % 10);
    const lobx::UserId user = users[rng() % users.size()];
    const lob::Side side = (rng() % 2) == 0 ? lob::Side::Bid : lob::Side::Ask;
    const lob::Tick price = 95 + static_cast<lob::Tick>(rng() % 11);
    const lob::Quantity qty = 1 + static_cast<lob::Quantity>(rng() % 3);
    uint32_t flags = lob::NONE;
    lobx::OrderId order_id = next_order_id++;

    if (action == 1) flags = lob::IOC;
    if (action == 2) flags = lob::FOK;
    if (action == 3) flags = lob::POST_ONLY;
    if (action == 4) flags = lob::IOC | lob::STP;
    if (action == 5) order_id -= 1;

    if (action == 6) {
      const auto open = f.engine.open_orders();
      if (!open.empty()) {
        const auto& order = open[rng() % open.size()];
        (void)f.engine.cancel(order.id, order.user, 1000 + step);
      }
    } else if (action == 7) {
      (void)f.engine.cancel(next_order_id + 999, user, 1000 + step);
    } else if (action == 8) {
      auto rejected = f.submit(user, order_id, side, 0, qty, flags, 1000 + step);
      EXPECT_FALSE(rejected.accepted);
    } else if (action == 9) {
      auto rejected = f.submit(user, order_id, side, price, 0, flags, 1000 + step);
      EXPECT_FALSE(rejected.accepted);
    } else {
      const int before = committed_trade_events;
      auto result = f.submit(user, order_id, side, price, qty, flags, 1000 + step);
      if (result.accepted) {
        committed_trade_events += static_cast<int>(result.trades.size());
        if ((flags & lob::POST_ONLY) != 0u) EXPECT_EQ(result.exec.filled, 0);
      }
      EXPECT_EQ(event_count(f.events, "trade"), before + static_cast<int>(result.trades.size()));
    }

    check_sequence_invariants(f, committed_trade_events);
    EXPECT_EQ(total_asset(f.ledger, f.base_asset), base_before);
    EXPECT_EQ(total_asset(f.ledger, f.quote_asset), quote_before);
  }

  return SequenceResult{summarize_state(f), committed_trade_events};
}

} // namespace

TEST(RandomMarketSequenceInvariants, LedgerInvariantAlwaysHoldsAcrossRandomSequences) {
  for (uint32_t seed = 1; seed <= 20; ++seed) {
    const auto result = run_sequence(seed, 200);
    EXPECT_TRUE_MSG(result.committed_trades >= 0, "seed=" + std::to_string(seed));
  }
}

TEST(RandomMarketSequenceInvariants, SameSeedReplayProducesSameFinalState) {
  const auto first = run_sequence(777, 200);
  const auto second = run_sequence(777, 200);

  EXPECT_EQ(first.summary, second.summary);
  EXPECT_EQ(first.committed_trades, second.committed_trades);
}
