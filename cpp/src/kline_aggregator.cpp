#include "lobx/kline_aggregator.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace lobx {

KlineAggregator::KlineAggregator(std::vector<int64_t> intervals_ns) : intervals_(std::move(intervals_ns)) {
  intervals_.erase(std::remove_if(intervals_.begin(), intervals_.end(), [](int64_t v) { return v <= 0; }), intervals_.end());
  std::sort(intervals_.begin(), intervals_.end());
  intervals_.erase(std::unique(intervals_.begin(), intervals_.end()), intervals_.end());
}

int64_t KlineAggregator::bucket_start(int64_t ts, int64_t interval) {
  if (ts < 0) return 0;
  return (ts / interval) * interval;
}

void KlineAggregator::update_state(State& st, const TradeEvent& trade, int64_t interval) {
  Amount quote_volume = 0;
  bool overflowed = !mul_amount(trade.price, trade.qty, quote_volume);
  if (overflowed) quote_volume = std::numeric_limits<Amount>::max();
  if (!st.initialized) {
    const int64_t open_time = bucket_start(trade.ts, interval);
    st.candle = Candle{trade.market_id,
                       interval,
                       open_time,
                       open_time + interval,
                       trade.price,
                       trade.price,
                       trade.price,
                       trade.price,
                       trade.qty,
                       quote_volume,
                       1,
                       overflowed};
    st.initialized = true;
    return;
  }
  st.candle.high = std::max(st.candle.high, trade.price);
  st.candle.low = std::min(st.candle.low, trade.price);
  st.candle.close = trade.price;
  if (trade.qty > 0 && st.candle.volume > std::numeric_limits<lob::Quantity>::max() - trade.qty) {
    st.candle.volume = std::numeric_limits<lob::Quantity>::max();
    st.candle.overflowed = true;
  } else {
    st.candle.volume += trade.qty;
  }
  if (overflowed || st.candle.quote_volume > std::numeric_limits<Amount>::max() - quote_volume) {
    st.candle.quote_volume = std::numeric_limits<Amount>::max();
    st.candle.overflowed = true;
  } else {
    st.candle.quote_volume += quote_volume;
  }
  if (st.candle.trade_count == std::numeric_limits<uint64_t>::max()) {
    st.candle.overflowed = true;
  } else {
    st.candle.trade_count += 1;
  }
}

std::vector<Candle> KlineAggregator::on_trade(const TradeEvent& trade) {
  std::vector<Candle> closed;
  for (const int64_t interval : intervals_) {
    State& st = states_[trade.market_id][interval];
    const int64_t bucket = bucket_start(trade.ts, interval);
    if (st.initialized && bucket > st.candle.open_time_ns) {
      closed.push_back(st.candle);
      st.initialized = false;
    }
    if (st.initialized && bucket < st.candle.open_time_ns) continue;
    update_state(st, trade, interval);
  }
  return closed;
}

std::vector<Candle> KlineAggregator::flush_all() {
  std::vector<Candle> out;
  for (auto& by_market : states_) {
    for (auto& by_interval : by_market.second) {
      State& st = by_interval.second;
      if (st.initialized) {
        out.push_back(st.candle);
        st.initialized = false;
      }
    }
  }
  std::sort(out.begin(), out.end(), [](const Candle& a, const Candle& b) {
    if (a.market_id != b.market_id) return a.market_id < b.market_id;
    if (a.interval_ns != b.interval_ns) return a.interval_ns < b.interval_ns;
    return a.open_time_ns < b.open_time_ns;
  });
  return out;
}

} // namespace lobx
