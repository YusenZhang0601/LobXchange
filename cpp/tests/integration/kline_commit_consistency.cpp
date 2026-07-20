#include "lobx/kline_aggregator.hpp"
#include "test_helpers/exchange_fixture.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>

using namespace lobx_test;

namespace {

void create_three_committed_spot_trades(ExchangeFixture& f) {
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 54001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 54002, lob::Side::Ask, 105, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 54003, lob::Side::Ask, 95, 1, lob::POST_ONLY, 3).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 54004, lob::Side::Bid, 105, 3, lob::IOC, 4).accepted);
}

} // namespace

TEST(KlineCommitConsistency, KlineConsumesOnlyCommittedTrades) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 54011, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_FALSE(f.exchange.submit_limit(f.spot_symbol, f.bob, 54012, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 54013, lob::Side::Bid, 100, 1, lob::IOC, 3).accepted);

  auto candles = f.exchange.flush_candles();

  EXPECT_FALSE(candles.empty());
  EXPECT_EQ(candles.front().volume, 1);
}

TEST(KlineCommitConsistency, FailedOrderDoesNotChangeCandleHistory) {
  auto f = ExchangeFixture::Spot();
  auto rejected = f.exchange.submit_limit(f.spot_symbol, f.bob, 54021, lob::Side::Bid, 100, 1, 1u << 30, 1);

  auto candles = f.exchange.flush_candles();

  EXPECT_FALSE(rejected.accepted);
  EXPECT_TRUE(candles.empty());
}

TEST(KlineCommitConsistency, ExpiredFOKDoesNotChangeCandleHistory) {
  auto f = ExchangeFixture::Spot();
  auto expired = f.exchange.submit_limit(f.spot_symbol, f.bob, 54031, lob::Side::Bid, 100, 1, lob::FOK, 1);

  auto candles = f.exchange.flush_candles();

  EXPECT_TRUE(expired.accepted);
  EXPECT_EQ(expired.exec.filled, 0);
  EXPECT_TRUE(candles.empty());
}

TEST(KlineCommitConsistency, RejectedPostOnlyDoesNotChangeCandleHistory) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 54041, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  auto rejected = f.exchange.submit_limit(f.spot_symbol, f.bob, 54042, lob::Side::Bid, 100, 1, lob::POST_ONLY, 2);

  auto candles = f.exchange.flush_candles();

  EXPECT_FALSE(rejected.accepted);
  EXPECT_TRUE(candles.empty());
}

TEST(KlineCommitConsistency, CandleOHLCUsesCommittedTradePricesOnly) {
  auto f = ExchangeFixture::Spot();
  create_three_committed_spot_trades(f);

  auto candles = f.exchange.flush_candles();

  EXPECT_FALSE(candles.empty());
  EXPECT_EQ(candles.front().open, 95);
  EXPECT_EQ(candles.front().high, 105);
  EXPECT_EQ(candles.front().low, 95);
  EXPECT_EQ(candles.front().close, 105);
}

TEST(KlineCommitConsistency, CandleVolumeEqualsSumCommittedTradeQty) {
  auto f = ExchangeFixture::Spot();
  create_three_committed_spot_trades(f);

  auto candles = f.exchange.flush_candles();

  EXPECT_FALSE(candles.empty());
  EXPECT_EQ(candles.front().volume, 3);
}

TEST(KlineCommitConsistency, CandleQuoteVolumeMatchesSumPriceTimesQty) {
  auto f = ExchangeFixture::Spot();
  create_three_committed_spot_trades(f);

  auto candles = f.exchange.flush_candles();

  EXPECT_FALSE(candles.empty());
  EXPECT_EQ(candles.front().quote_volume, 300);
}

TEST(KlineCommitConsistency, CandleOverflowSaturatesAndSetsOverflowFlag) {
  lobx::KlineAggregator klines({100});
  lobx::TradeEvent trade{};
  trade.market_id = 1;
  trade.ts = 1;
  trade.price = std::numeric_limits<lob::Tick>::max();
  trade.qty = 2;

  klines.on_trade(trade);
  auto candles = klines.flush_all();

  EXPECT_EQ(candles.size(), 1UL);
  EXPECT_TRUE(candles.front().overflowed);
  EXPECT_EQ(candles.front().quote_volume, std::numeric_limits<lobx::Amount>::max());
}

TEST(KlineCommitConsistency, FlushCandlesWithoutTradesDoesNotCreateVolume) {
  auto f = ExchangeFixture::Spot();

  auto candles = f.exchange.flush_candles();

  EXPECT_TRUE(candles.empty());
}

TEST(KlineCommitConsistency, MultipleIntervalsReceiveCommittedTradeOnce) {
  auto f = ExchangeFixture::Spot();
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.alice, 54091, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1).accepted);
  EXPECT_TRUE(f.exchange.submit_limit(f.spot_symbol, f.bob, 54092, lob::Side::Bid, 100, 1, lob::IOC, 2).accepted);

  auto candles = f.exchange.flush_candles();

  EXPECT_EQ(candles.size(), 5UL);
  for (const auto& candle : candles) {
    EXPECT_EQ(candle.trade_count, 1ULL);
    EXPECT_EQ(candle.volume, 1);
  }
}
