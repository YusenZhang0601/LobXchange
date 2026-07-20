#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

#include "lobx/kline_aggregator.hpp"

#include <limits>

using namespace lobx_test;

namespace {

void create_failed_perp_close_setup(ExchangeFixture& f) {
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.bob, 48001, lob::Side::Ask, 100, 4, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.alice, 48002, lob::Side::Bid, 100, 4, lob::IOC, 2).accepted);
  const auto current = f.exchange.balance(f.alice, "USDT").total;
  EXPECT_TRUE(f.exchange.deposit(f.alice, "USDT", std::numeric_limits<lobx::Amount>::max() - current).ok);
  EXPECT_TRUE(f.exchange.submit_limit(f.perp_symbol, f.carol, 48003, lob::Side::Bid, 110, 4, lob::POST_ONLY, 3).accepted);
}

} // namespace

TEST(ExchangeKlineTradeCommitConsistency, KlineOnlyConsumesAcceptedTrades) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 48011, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);

  auto buy = f.exchange.submit_limit(f.spot_symbol, f.bob, 48012, lob::Side::Bid, 100, 2, lob::IOC, 2);
  auto candles = f.exchange.flush_candles();

  EXPECT_TRUE(buy.accepted);
  EXPECT_FALSE(candles.empty());
  EXPECT_EQ(candles.front().volume, 2);
}

TEST(ExchangeKlineTradeCommitConsistency, FailedSettlementDoesNotCreateKlineClosedEvent) {
  auto f = ExchangeFixture::Perp();
  create_failed_perp_close_setup(f);
  f.exchange.flush_candles();
  f.exchange.drain_candles();

  auto close = f.exchange.submit_limit(f.perp_symbol, f.alice, 48004, lob::Side::Ask, 110, 4, lobx::LOBX_REDUCE_ONLY | lob::IOC, 4);
  auto candles = f.exchange.flush_candles();

  EXPECT_FALSE(close.accepted);
  EXPECT_TRUE_MSG(candles.empty(), "failed settlement must not create candle volume");
}

TEST(ExchangeKlineTradeCommitConsistency, DrainTradesOnlyReturnsCommittedTrades) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 48021, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 48022, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);

  auto trades = f.exchange.drain_trades();
  auto second = f.exchange.drain_trades();

  EXPECT_EQ(trades.size(), 1UL);
  EXPECT_TRUE(second.empty());
}

TEST(ExchangeKlineTradeCommitConsistency, FlushCandlesWithoutTradesDoesNotCreateFakeVolume) {
  auto f = ExchangeFixture::Spot();

  auto candles = f.exchange.flush_candles();

  EXPECT_TRUE(candles.empty());
}

TEST(ExchangeKlineTradeCommitConsistency, CandleVolumeEqualsSumOfCommittedTradeQty) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 48031, lob::Side::Ask, 100, 2, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 48032, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.carol, 48033, lob::Side::Bid, 100, 1, lob::IOC, 3).accepted);

  auto candles = f.exchange.flush_candles();

  EXPECT_FALSE(candles.empty());
  EXPECT_EQ(candles.front().volume, 2);
}

TEST(ExchangeKlineTradeCommitConsistency, KlineQuoteVolumeOverflowIsFlagged) {
  lobx::KlineAggregator klines({100});
  lobx::TradeEvent trade{};
  trade.market_id = 1;
  trade.ts = 1;
  trade.price = std::numeric_limits<lob::Tick>::max();
  trade.qty = 2;

  klines.on_trade(trade);
  auto candles = klines.flush_all();

  EXPECT_EQ(candles.size(), 1UL);
  EXPECT_TRUE_MSG(candles.front().overflowed, "quote volume overflow should be flagged on the candle");
  EXPECT_EQ_MSG(candles.front().quote_volume, std::numeric_limits<lobx::Amount>::max(),
                "quote volume overflow should saturate instead of silently becoming zero");
}

TEST(ExchangeKlineTradeCommitConsistency, KlineVolumeAccumulationOverflowDoesNotWrap) {
  lobx::KlineAggregator klines({100});
  lobx::TradeEvent large{};
  large.market_id = 1;
  large.ts = 1;
  large.price = 1;
  large.qty = std::numeric_limits<lob::Quantity>::max();
  lobx::TradeEvent one = large;
  one.ts = 2;
  one.qty = 1;

  klines.on_trade(large);
  klines.on_trade(one);
  auto candles = klines.flush_all();

  EXPECT_EQ(candles.size(), 1UL);
  EXPECT_TRUE_MSG(candles.front().overflowed, "volume accumulation overflow should be flagged");
  EXPECT_EQ_MSG(candles.front().volume, std::numeric_limits<lob::Quantity>::max(),
                "volume accumulation must saturate instead of wrapping");
}
