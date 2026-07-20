#pragma once

#include "test_helpers/exchange_fixture.hpp"

#include "lobx/types.hpp"

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

namespace lobx_test {

struct SpotReplayTrade {
  lob::Timestamp ts{0};
  lob::Tick price{0};
  lob::Quantity qty{0};
  lobx::UserId buyer{0};
  lobx::UserId seller{0};
  lobx::OrderId buyer_order_id{0};
  lobx::OrderId seller_order_id{0};
  lob::Side liquidity_side{lob::Side::Ask};

  bool operator==(const SpotReplayTrade&) const = default;
};

struct SpotReplayBalance {
  lobx::UserId user{0};
  lobx::AssetId asset{0};
  lobx::Amount total{0};
  lobx::Amount locked{0};
  lobx::Amount free{0};

  bool operator==(const SpotReplayBalance&) const = default;
};

struct SpotReplayState {
  std::vector<std::string> action_trace;
  std::vector<std::pair<lob::Tick, lob::Quantity>> bids;
  std::vector<std::pair<lob::Tick, lob::Quantity>> asks;
  std::vector<SpotReplayTrade> trades;
  std::vector<SpotReplayBalance> balances;
  std::vector<std::string> event_types;
};

inline SpotReplayTrade replay_trade_view(const lobx::TradeEvent& trade) {
  return SpotReplayTrade{trade.ts,
                         trade.price,
                         trade.qty,
                         trade.buyer,
                         trade.seller,
                         trade.buyer_order_id,
                         trade.seller_order_id,
                         trade.liquidity_side};
}

inline bool same_replay_state(const SpotReplayState& a, const SpotReplayState& b) {
  return a.action_trace == b.action_trace && a.bids == b.bids && a.asks == b.asks &&
         a.trades == b.trades && a.balances == b.balances && a.event_types == b.event_types;
}

inline SpotReplayState run_seeded_spot_simulation(uint64_t seed, int steps) {
  ExchangeFixture f = ExchangeFixture::Spot();
  constexpr lobx::UserId dave = 40;
  f.deposit(dave, "BTC", 1000000LL);
  f.deposit(dave, "USDT", 1000000LL);

  const std::vector<lobx::UserId> users{f.alice, f.bob, f.carol, dave};
  std::vector<lobx::OrderId> candidate_order_ids;
  std::mt19937_64 rng(seed);
  lobx::OrderId next_order_id = 70000;
  SpotReplayState state;

  for (int step = 0; step < steps; ++step) {
    const lob::Timestamp ts = static_cast<lob::Timestamp>(step + 1);
    const int action = static_cast<int>(rng() % 8);
    const lobx::UserId user = users[static_cast<size_t>(rng() % users.size())];
    const lob::Tick price = 95 + static_cast<lob::Tick>(rng() % 11);
    const lob::Quantity qty = 1 + static_cast<lob::Quantity>(rng() % 3);

    if (action == 6 && !candidate_order_ids.empty()) {
      const auto id = candidate_order_ids[static_cast<size_t>(rng() % candidate_order_ids.size())];
      const bool canceled = f.exchange.cancel(f.spot_symbol, user, id, ts);
      state.action_trace.push_back("step=" + std::to_string(step) + " cancel_known user=" +
                                   std::to_string(user) + " order_id=" + std::to_string(id) +
                                   " ts=" + std::to_string(ts) + " canceled=" + std::to_string(canceled));
    } else if (action == 7) {
      const auto id = next_order_id + 100000;
      const bool canceled = f.exchange.cancel(f.spot_symbol, user, id, ts);
      state.action_trace.push_back("step=" + std::to_string(step) + " cancel_unknown user=" +
                                   std::to_string(user) + " order_id=" + std::to_string(id) +
                                   " ts=" + std::to_string(ts) + " canceled=" + std::to_string(canceled));
    } else {
      const lobx::OrderId id = next_order_id++;
      lob::Side side = (action % 2 == 0) ? lob::Side::Bid : lob::Side::Ask;
      uint32_t flags = lob::NONE;
      if (action == 2 || action == 3) flags = lob::IOC;
      if (action == 4) {
        flags = lob::FOK;
        side = lob::Side::Bid;
      }
      if (action == 5) flags = lob::POST_ONLY;

      const auto result = f.exchange.submit_limit(f.spot_symbol, user, id, side, price, qty, flags, ts);
      if (result.accepted) candidate_order_ids.push_back(id);
      state.action_trace.push_back("step=" + std::to_string(step) + " submit action=" +
                                   std::to_string(action) + " user=" + std::to_string(user) +
                                   " order_id=" + std::to_string(id) +
                                   " side=" + std::to_string(static_cast<int>(side)) +
                                   " price=" + std::to_string(price) +
                                   " qty=" + std::to_string(qty) +
                                   " flags=" + std::to_string(flags) +
                                   " ts=" + std::to_string(ts) +
                                   " accepted=" + std::to_string(result.accepted));
    }
  }

  state.bids = f.exchange.topN(f.spot_symbol, lob::Side::Bid, 1000);
  state.asks = f.exchange.topN(f.spot_symbol, lob::Side::Ask, 1000);
  for (const auto& trade : f.exchange.trades()) state.trades.push_back(replay_trade_view(trade));
  for (const auto& balance : f.exchange.ledger().balances()) {
    state.balances.push_back(SpotReplayBalance{balance.user, balance.asset, balance.total, balance.locked, balance.free});
  }
  std::sort(state.balances.begin(), state.balances.end(), [](const auto& a, const auto& b) {
    if (a.user != b.user) return a.user < b.user;
    return a.asset < b.asset;
  });
  for (const auto& event : f.exchange.events().records()) state.event_types.push_back(event.type);
  return state;
}

} // namespace lobx_test
