#include "lobx/exchange.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

constexpr const char* kMarket = "BTC-USDT";

std::string trim(std::string s) {
  auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
  s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space));
  s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end());
  return s;
}

std::vector<std::string> split(const std::string& line, char delim) {
  std::vector<std::string> out;
  std::stringstream ss(line);
  std::string item;
  while (std::getline(ss, item, delim)) out.push_back(trim(item));
  return out;
}

std::string upper(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return s;
}

std::string json_escape(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (char ch : s) {
    switch (ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default: out += ch; break;
    }
  }
  return out;
}

lob::Side parse_side(const std::string& raw) {
  const std::string s = upper(raw);
  if (s == "BID" || s == "BUY" || s == "B") return lob::Side::Bid;
  if (s == "ASK" || s == "SELL" || s == "S") return lob::Side::Ask;
  throw std::runtime_error("unknown side: " + raw);
}

uint32_t parse_flags(const std::string& raw) {
  std::string normalized = raw;
  for (char& ch : normalized) {
    if (ch == '|' || ch == '+' || ch == ';') ch = ',';
  }
  uint32_t flags = lob::NONE;
  for (std::string token : split(normalized, ',')) {
    token = upper(token);
    if (token.empty() || token == "NONE" || token == "0") continue;
    if (token == "IOC") flags |= lob::IOC;
    else if (token == "FOK") flags |= lob::FOK;
    else if (token == "POST_ONLY" || token == "POSTONLY") flags |= lob::POST_ONLY;
    else if (token == "STP") flags |= lob::STP;
    else if (token == "REDUCE_ONLY" || token == "REDUCEONLY") flags |= lobx::LOBX_REDUCE_ONLY;
    else flags |= static_cast<uint32_t>(std::stoul(token));
  }
  return flags;
}

void emit_error(const std::string& reason) {
  std::cout << "{\"type\":\"error\",\"reason\":\"" << json_escape(reason) << "\"}\n" << std::flush;
}

void emit_result(const lobx::Result& r, const std::string& type) {
  std::cout << "{\"type\":\"" << type << "\",\"ok\":" << (r.ok ? "true" : "false")
            << ",\"code\":\"" << lobx::reject_code_name(r.code)
            << "\",\"reason\":\"" << json_escape(r.reason) << "\"}\n" << std::flush;
}

void emit_depth_side(const std::vector<std::pair<lob::Tick, lob::Quantity>>& depth) {
  std::cout << '[';
  for (size_t i = 0; i < depth.size(); ++i) {
    if (i > 0) std::cout << ',';
    std::cout << "{\"price\":" << depth[i].first << ",\"qty\":" << depth[i].second << '}';
  }
  std::cout << ']';
}

void emit_trade(const lobx::TradeEvent& trade) {
  std::cout << "{\"market_id\":" << trade.market_id
            << ",\"ts\":" << trade.ts
            << ",\"price\":" << trade.price
            << ",\"qty\":" << trade.qty
            << ",\"buyer\":" << trade.buyer
            << ",\"seller\":" << trade.seller
            << ",\"buyer_order_id\":" << trade.buyer_order_id
            << ",\"seller_order_id\":" << trade.seller_order_id
            << ",\"liquidity_side\":\"" << (trade.liquidity_side == lob::Side::Bid ? "BID" : "ASK")
            << "\"}";
}

void emit_candle(const lobx::Candle& candle) {
  std::cout << "{\"market_id\":" << candle.market_id
            << ",\"interval_ns\":" << candle.interval_ns
            << ",\"open_time_ns\":" << candle.open_time_ns
            << ",\"close_time_ns\":" << candle.close_time_ns
            << ",\"open\":" << candle.open
            << ",\"high\":" << candle.high
            << ",\"low\":" << candle.low
            << ",\"close\":" << candle.close
            << ",\"volume\":" << candle.volume
            << ",\"quote_volume\":" << candle.quote_volume
            << ",\"trade_count\":" << candle.trade_count
            << '}';
}

void require_ok(const lobx::Result& r, const std::string& context) {
  if (!r.ok) throw std::runtime_error(context + ": " + r.reason);
}

lobx::Exchange bootstrap_exchange() {
  lobx::Exchange ex;
  require_ok(ex.issue_asset("USDT", 6, 900000000000000000LL, 1, 0), "issue USDT");
  require_ok(ex.issue_asset("BTC", 8, 900000000000000000LL, 1, 0), "issue BTC");
  require_ok(ex.create_spot_market(kMarket, "BTC", "USDT", 1, 1, 1, 1), "create market");
  ex.events().set_memory_enabled(false);
  return ex;
}

void handle_deposit(lobx::Exchange& ex, const std::vector<std::string>& parts) {
  if (parts.size() != 4) {
    emit_error("DEPOSIT requires user,asset,amount");
    return;
  }
  const lobx::UserId user = static_cast<lobx::UserId>(std::stoull(parts[1]));
  const lobx::Amount amount = std::stoll(parts[3]);
  emit_result(ex.deposit(user, parts[2], amount), "deposit_result");
}

void handle_book(lobx::Exchange& ex, const std::vector<std::string>& parts) {
  const int levels = parts.size() >= 2 ? std::stoi(parts[1]) : 10;
  std::cout << "{\"type\":\"book\",\"market\":\"" << kMarket << "\",\"bids\":";
  emit_depth_side(ex.topN(kMarket, lob::Side::Bid, levels));
  std::cout << ",\"asks\":";
  emit_depth_side(ex.topN(kMarket, lob::Side::Ask, levels));
  std::cout << "}\n" << std::flush;
}

void handle_balance(lobx::Exchange& ex, const std::vector<std::string>& parts) {
  if (parts.size() != 3) {
    emit_error("BALANCE requires user,asset");
    return;
  }
  const lobx::UserId user = static_cast<lobx::UserId>(std::stoull(parts[1]));
  const auto b = ex.balance(user, parts[2]);
  std::cout << "{\"type\":\"balance\",\"user\":" << b.user
            << ",\"asset\":\"" << json_escape(parts[2])
            << "\",\"total\":" << b.total
            << ",\"locked\":" << b.locked
            << ",\"free\":" << b.free << "}\n" << std::flush;
}

void handle_order(lobx::Exchange& ex, const std::vector<std::string>& parts) {
  if (parts.size() != 8) {
    emit_error("ORDER requires user,order_id,side,price,qty,flags,ts");
    return;
  }
  const lobx::UserId user = static_cast<lobx::UserId>(std::stoull(parts[1]));
  const lobx::OrderId order_id = static_cast<lobx::OrderId>(std::stoull(parts[2]));
  const lob::Side side = parse_side(parts[3]);
  const lob::Tick price = std::stoll(parts[4]);
  const lob::Quantity qty = std::stoll(parts[5]);
  const uint32_t flags = parse_flags(parts[6]);
  const lob::Timestamp ts = std::stoll(parts[7]);
  const lobx::SubmitResult r = ex.submit_limit(kMarket, user, order_id, side, price, qty, flags, ts);

  std::cout << "{\"type\":\"order_result\",\"accepted\":" << (r.accepted ? "true" : "false")
            << ",\"code\":\"" << lobx::reject_code_name(r.code)
            << "\",\"reason\":\"" << json_escape(r.reason)
            << "\",\"filled\":" << r.exec.filled
            << ",\"remaining\":" << r.exec.remaining
            << ",\"trades\":[";
  for (size_t i = 0; i < r.trades.size(); ++i) {
    if (i > 0) std::cout << ',';
    emit_trade(r.trades[i]);
  }
  std::cout << "]}\n" << std::flush;
}

void handle_cancel(lobx::Exchange& ex, const std::vector<std::string>& parts) {
  if (parts.size() != 4) {
    emit_error("CANCEL requires user,order_id,ts");
    return;
  }
  const lobx::UserId user = static_cast<lobx::UserId>(std::stoull(parts[1]));
  const lobx::OrderId order_id = static_cast<lobx::OrderId>(std::stoull(parts[2]));
  const lob::Timestamp ts = std::stoll(parts[3]);
  const bool ok = ex.cancel(kMarket, user, order_id, ts);
  std::cout << "{\"type\":\"cancel_result\",\"ok\":" << (ok ? "true" : "false") << "}\n" << std::flush;
}

void handle_flush(lobx::Exchange& ex) {
  const std::vector<lobx::Candle> candles = ex.flush_candles();
  std::cout << "{\"type\":\"flush_result\",\"candles\":[";
  for (size_t i = 0; i < candles.size(); ++i) {
    if (i > 0) std::cout << ',';
    emit_candle(candles[i]);
  }
  std::cout << "]}\n" << std::flush;
}

} // namespace

int main() {
  try {
    lobx::Exchange ex = bootstrap_exchange();
    std::cout << "{\"type\":\"ready\",\"market\":\"" << kMarket << "\"}\n" << std::flush;

    std::string line;
    while (std::getline(std::cin, line)) {
      line = trim(line);
      if (line.empty()) continue;
      try {
        const std::vector<std::string> parts = split(line, ',');
        const std::string op = upper(parts.empty() ? "" : parts[0]);
        if (op == "STOP") {
          std::cout << "{\"type\":\"stopped\"}\n" << std::flush;
          return 0;
        } else if (op == "PING") {
          std::cout << "{\"type\":\"pong\"}\n" << std::flush;
        } else if (op == "DEPOSIT") {
          handle_deposit(ex, parts);
        } else if (op == "BOOK") {
          handle_book(ex, parts);
        } else if (op == "BALANCE") {
          handle_balance(ex, parts);
        } else if (op == "ORDER") {
          handle_order(ex, parts);
        } else if (op == "CANCEL") {
          handle_cancel(ex, parts);
        } else if (op == "FLUSH") {
          handle_flush(ex);
        } else {
          emit_error("unknown command: " + op);
        }
      } catch (const std::exception& e) {
        emit_error(e.what());
      }
    }
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "lobx_step_exchange error: " << e.what() << "\n";
    return 1;
  }
}
