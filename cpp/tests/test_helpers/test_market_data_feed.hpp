#pragma once

#include "test_helpers/simulation_test_harness.hpp"

#include "lobx/account_ledger.hpp"
#include "lobx/types.hpp"

#include <cstdint>
#include <vector>

namespace lobx_test {

struct PublicTrade {
  lobx::MarketId market_id{0};
  lob::Timestamp ts{0};
  lob::Tick price{0};
  lob::Quantity qty{0};
  lob::Side liquidity_side{lob::Side::Ask};
  uint64_t public_trade_id{0};
};

inline PublicTrade to_public_trade(const lobx::TradeEvent& trade, uint64_t public_trade_id) {
  return PublicTrade{trade.market_id, trade.ts, trade.price, trade.qty, trade.liquidity_side, public_trade_id};
}

struct VisiblePublicTrade {
  lob::Timestamp exchange_ts{0};
  lob::Timestamp visible_ts{0};
  PublicTrade trade;
};

class TestMarketDataFeed {
public:
  explicit TestMarketDataFeed(TestLatencyModel latency) : latency_(latency) {}

  void publish_trade(const lobx::TradeEvent& trade) {
    trades_.push_back(VisiblePublicTrade{trade.ts, latency_.market_data_arrival(trade.ts),
                                         to_public_trade(trade, next_public_trade_id_++)});
  }

  std::vector<PublicTrade> visible_trades(lob::Timestamp bot_ts) const {
    std::vector<PublicTrade> out;
    for (const auto& visible : trades_) {
      if (visible.visible_ts <= bot_ts) out.push_back(visible.trade);
    }
    return out;
  }

private:
  TestLatencyModel latency_;
  uint64_t next_public_trade_id_{1};
  std::vector<VisiblePublicTrade> trades_;
};

struct VisiblePrivateFill {
  lobx::UserId user{0};
  lob::Timestamp visible_ts{0};
  lobx::TradeEvent trade;
};

struct PrivateFill {
  lobx::MarketId market_id{0};
  lob::Timestamp ts{0};
  lob::Tick price{0};
  lob::Quantity qty{0};
  lobx::UserId user{0};
  lobx::OrderId own_order_id{0};
  bool is_buyer{false};
};

inline PrivateFill to_private_fill(lobx::UserId user, const lobx::TradeEvent& trade) {
  const bool is_buyer = trade.buyer == user;
  return PrivateFill{trade.market_id,
                     trade.ts,
                     trade.price,
                     trade.qty,
                     user,
                     is_buyer ? trade.buyer_order_id : trade.seller_order_id,
                     is_buyer};
}

class TestPrivateFeed {
public:
  explicit TestPrivateFeed(lob::Timestamp private_latency) : private_latency_(private_latency) {}

  void publish_fill(lobx::UserId user, const lobx::TradeEvent& trade) {
    fills_.push_back(VisiblePrivateFill{user, trade.ts + private_latency_, trade});
  }

  std::vector<lobx::TradeEvent> visible_fills(lobx::UserId user, lob::Timestamp bot_ts) const {
    std::vector<lobx::TradeEvent> out;
    for (const auto& fill : fills_) {
      if (fill.user == user && fill.visible_ts <= bot_ts) out.push_back(fill.trade);
    }
    return out;
  }

  std::vector<PrivateFill> visible_private_fills(lobx::UserId user, lob::Timestamp bot_ts) const {
    std::vector<PrivateFill> out;
    for (const auto& fill : fills_) {
      if (fill.user == user && fill.visible_ts <= bot_ts) out.push_back(to_private_fill(user, fill.trade));
    }
    return out;
  }

private:
  lob::Timestamp private_latency_{0};
  std::vector<VisiblePrivateFill> fills_;
};

struct TestBotContext {
  std::vector<PublicTrade> public_trades;
  std::vector<PrivateFill> own_fills;
  lobx::WalletBalance own_balance;
};

inline TestBotContext make_bot_context(lobx::UserId user, lob::Timestamp bot_ts,
                                       const TestMarketDataFeed& public_feed,
                                       const TestPrivateFeed& private_feed,
                                       const lobx::AccountLedger& ledger,
                                       lobx::AssetId balance_asset) {
  return TestBotContext{public_feed.visible_trades(bot_ts),
                        private_feed.visible_private_fills(user, bot_ts),
                        ledger.balance(user, balance_asset)};
}

} // namespace lobx_test
