#pragma once

#include <unordered_map>
#include <vector>

#include "lobx/types.hpp"

namespace lobx {

struct Candle {
  MarketId market_id{0};
  int64_t interval_ns{0};
  int64_t open_time_ns{0};
  int64_t close_time_ns{0};
  lob::Tick open{0};
  lob::Tick high{0};
  lob::Tick low{0};
  lob::Tick close{0};
  lob::Quantity volume{0};
  Amount quote_volume{0};
  uint64_t trade_count{0};
  bool overflowed{false};
};

class KlineAggregator {
public:
  explicit KlineAggregator(std::vector<int64_t> intervals_ns);
  std::vector<Candle> on_trade(const TradeEvent& trade);
  std::vector<Candle> flush_all();

private:
  struct State { Candle candle; bool initialized{false}; };
  static int64_t bucket_start(int64_t ts, int64_t interval);
  void update_state(State& st, const TradeEvent& trade, int64_t interval);

  std::vector<int64_t> intervals_;
  std::unordered_map<MarketId, std::unordered_map<int64_t, State>> states_;
};

} // namespace lobx
