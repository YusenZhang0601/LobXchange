#pragma once

#include "test_helpers/bot_strategy_harness.hpp"
#include "test_helpers/exchange_fixture.hpp"

#include <algorithm>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace lobx_test {

struct PublicMarketData {
  std::vector<std::pair<lob::Tick, lob::Quantity>> bids;
  std::vector<std::pair<lob::Tick, lob::Quantity>> asks;
  std::vector<lobx::TradeEvent> trades;
};

struct MarketMakerBot {
  lobx::UserId user{0};
  lobx::OrderId next_order_id{0};
  lob::Tick mid{100};
  lob::Quantity qty{1};

  std::vector<BotAction> quote() {
    return {{user, next_order_id++, lob::Side::Bid, mid - 1, qty, lob::POST_ONLY},
            {user, next_order_id++, lob::Side::Ask, mid + 1, qty, lob::POST_ONLY}};
  }
};

struct TakerBot {
  lobx::UserId user{0};
  lobx::OrderId next_order_id{0};

  BotAction sweep_ask(lob::Tick price, lob::Quantity qty) {
    return BotAction{user, next_order_id++, lob::Side::Bid, price, qty, lob::IOC};
  }
};

struct NoiseTraderBot {
  lobx::UserId user{0};
  lobx::OrderId next_order_id{0};
  std::mt19937 rng;

  NoiseTraderBot(lobx::UserId u, lobx::OrderId first_id, uint32_t seed) : user(u), next_order_id(first_id), rng(seed) {}

  BotAction next() {
    const bool bid = (rng() % 2) == 0;
    return BotAction{user,
                     next_order_id++,
                     bid ? lob::Side::Bid : lob::Side::Ask,
                     static_cast<lob::Tick>(95 + (rng() % 11)),
                     1,
                     (rng() % 3) == 0 ? lob::IOC : lob::POST_ONLY};
  }
};

struct AdversarialBot {
  lobx::UserId user{0};
  lobx::OrderId next_order_id{0};

  BotAction self_cross_bid(lob::Tick price) {
    return BotAction{user, next_order_id++, lob::Side::Bid, price, 1, lob::IOC | lob::STP};
  }

  BotAction invalid_bid(lob::Tick price) {
    return BotAction{user, next_order_id++, lob::Side::Bid, price, 1, 1u << 30};
  }
};

struct UserStrategyBot {
  lobx::UserId user{0};
  lobx::OrderId next_order_id{0};
  uint32_t seed{0};

  std::vector<BotAction> deterministic_actions(int count) {
    std::mt19937 rng(seed);
    std::vector<BotAction> out;
    for (int i = 0; i < count; ++i) {
      const bool bid = (rng() % 2) == 0;
      out.push_back(BotAction{user,
                              next_order_id++,
                              bid ? lob::Side::Bid : lob::Side::Ask,
                              static_cast<lob::Tick>(98 + (rng() % 5)),
                              1,
                              lob::IOC});
    }
    return out;
  }
};

inline lobx::SubmitResult submit_bot_action(SpotEngineFixture& f, const BotAction& action, lob::Timestamp ts) {
  return f.submit(action.user, action.order_id, action.side, action.price, action.qty, action.flags, ts);
}

class MarketMakerStrategy final : public Strategy {
public:
  MarketMakerStrategy(lob::Tick bid_px, lob::Tick ask_px, lob::Quantity qty, lobx::OrderId first_order_id = 900000)
      : bid_px_(bid_px), ask_px_(ask_px), qty_(qty), next_order_id_(first_order_id) {}

  std::vector<BotAction> on_tick(const BotContext& ctx) override {
    bool has_bid = false;
    bool has_ask = false;
    for (const auto& order : ctx.own_open_orders) {
      if (order.side == lob::Side::Bid) has_bid = true;
      if (order.side == lob::Side::Ask) has_ask = true;
    }
    const bool bid_would_cross = !ctx.public_asks.empty() && ctx.public_asks.front().first <= bid_px_;
    const bool ask_would_cross = !ctx.public_bids.empty() && ctx.public_bids.front().first >= ask_px_;
    std::vector<BotAction> out;
    if (!has_bid && !bid_would_cross) {
      out.push_back(BotAction{ctx.user, next_order_id_++, lob::Side::Bid, bid_px_, qty_, lob::POST_ONLY});
    }
    if (!has_ask && !ask_would_cross) {
      out.push_back(BotAction{ctx.user, next_order_id_++, lob::Side::Ask, ask_px_, qty_, lob::POST_ONLY});
    }
    return out;
  }

private:
  lob::Tick bid_px_{0};
  lob::Tick ask_px_{0};
  lob::Quantity qty_{0};
  lobx::OrderId next_order_id_{0};
};

class TakerSweepStrategy final : public Strategy {
public:
  TakerSweepStrategy(lob::Side side, lob::Quantity target_qty, lob::Tick limit_price,
                     long double max_avg_price, lobx::OrderId first_order_id = 910000)
      : side_(side),
        target_qty_(target_qty),
        limit_price_(limit_price),
        max_avg_price_(max_avg_price),
        next_order_id_(first_order_id) {}

  std::vector<BotAction> on_tick(const BotContext& ctx) override {
    if (done_) return {};
    if (ctx.simulate_fill) {
      const lobx::SimulatedFill sim = ctx.simulate_fill(side_, limit_price_, target_qty_, lob::IOC);
      if (sim.code != lobx::RejectCode::None || sim.fillable_qty < target_qty_) return {};
      if (sim.avg_price > max_avg_price_) return {};
      done_ = true;
      return {BotAction{ctx.user, next_order_id_++, side_, limit_price_, target_qty_, lob::IOC}};
    }

    const auto& depth = side_ == lob::Side::Bid ? ctx.public_asks : ctx.public_bids;
    lob::Quantity remaining = target_qty_;
    lob::Quantity fillable = 0;
    lobx::Amount notional = 0;
    for (const auto& [price, qty] : depth) {
      const bool crosses = side_ == lob::Side::Bid ? price <= limit_price_ : price >= limit_price_;
      if (!crosses || remaining <= 0) break;
      const lob::Quantity fill_qty = std::min(remaining, qty);
      fillable += fill_qty;
      notional += price * fill_qty;
      remaining -= fill_qty;
    }
    if (fillable < target_qty_) return {};
    const long double avg_price = static_cast<long double>(notional) / static_cast<long double>(fillable);
    if (avg_price > max_avg_price_) return {};
    done_ = true;
    return {BotAction{ctx.user, next_order_id_++, side_, limit_price_, target_qty_, lob::IOC}};
  }

private:
  lob::Side side_{lob::Side::Bid};
  lob::Quantity target_qty_{0};
  lob::Tick limit_price_{0};
  long double max_avg_price_{0.0L};
  lobx::OrderId next_order_id_{0};
  bool done_{false};
};

class NoiseTraderStrategy final : public Strategy {
public:
  explicit NoiseTraderStrategy(uint64_t seed, lobx::OrderId first_order_id = 920000)
      : rng_(static_cast<uint32_t>(seed)), next_order_id_(first_order_id) {}

  std::vector<BotAction> on_tick(const BotContext& ctx) override {
    const bool bid = (rng_() % 2) == 0;
    const uint32_t flags = (rng_() % 4) == 0 ? lob::IOC : lob::POST_ONLY;
    return {BotAction{ctx.user,
                      next_order_id_++,
                      bid ? lob::Side::Bid : lob::Side::Ask,
                      static_cast<lob::Tick>(98 + (rng_() % 5)),
                      1,
                      flags}};
  }

private:
  std::mt19937 rng_;
  lobx::OrderId next_order_id_{0};
};

class UserStrategyStub final : public Strategy {
public:
  explicit UserStrategyStub(std::vector<BotAction> scripted_actions)
      : scripted_actions_(std::move(scripted_actions)) {}

  std::vector<BotAction> on_tick(const BotContext& ctx) override {
    std::vector<BotAction> out;
    while (next_ < scripted_actions_.size()) {
      BotAction action = scripted_actions_[next_];
      if (action.decision_ts > 0 && action.decision_ts > ctx.now) break;
      if (action.user == 0) action.user = ctx.user;
      if (action.decision_ts == 0) action.decision_ts = ctx.now;
      out.push_back(action);
      ++next_;
      if (action.decision_ts == ctx.now) break;
    }
    return out;
  }

private:
  std::vector<BotAction> scripted_actions_;
  size_t next_{0};
};

} // namespace lobx_test
