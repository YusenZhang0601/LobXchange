#include "lobx/exchange.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct ReplayOrder {
  lob::Timestamp ts{0};
  lobx::UserId user{0};
  lobx::OrderId order_id{0};
  lob::Side side{lob::Side::Bid};
  lob::Tick price{0};
  lob::Quantity qty{0};
  uint32_t flags{lob::NONE};
};

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

lob::Side parse_side(const std::string& raw) {
  const std::string s = upper(trim(raw));
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
    else throw std::runtime_error("unknown flag: " + token);
  }
  return flags;
}

std::vector<ReplayOrder> load_orders_csv(const std::string& path) {
  std::ifstream in(path);
  if (!in) throw std::runtime_error("failed to open orders CSV: " + path);
  std::vector<ReplayOrder> orders;
  std::string line;
  size_t line_no = 0;
  while (std::getline(in, line)) {
    ++line_no;
    line = trim(line);
    if (line.empty() || line[0] == '#') continue;
    if (line_no == 1 && line.find("ts") != std::string::npos && line.find("user") != std::string::npos) continue;
    const std::vector<std::string> cols = split(line, ',');
    if (cols.size() < 6) throw std::runtime_error("orders CSV needs ts,user,order_id,side,price,qty[,flags]");
    ReplayOrder o{};
    o.ts = std::stoll(cols[0]);
    o.user = std::stoull(cols[1]);
    o.order_id = std::stoull(cols[2]);
    o.side = parse_side(cols[3]);
    o.price = std::stoll(cols[4]);
    o.qty = std::stoll(cols[5]);
    o.flags = cols.size() >= 7 ? parse_flags(cols[6]) : lob::NONE;
    orders.push_back(o);
  }
  std::sort(orders.begin(), orders.end(), [](const ReplayOrder& a, const ReplayOrder& b) {
    if (a.ts != b.ts) return a.ts < b.ts;
    return a.order_id < b.order_id;
  });
  return orders;
}

std::vector<ReplayOrder> default_orders() {
  const int64_t s = 1000000000LL;
  return {
      // ?????????? ask ? bid?
      ReplayOrder{1000000LL, 100, 1001, lob::Side::Ask, 100, 10, lob::POST_ONLY},
      ReplayOrder{1000001LL, 100, 1002, lob::Side::Ask, 101, 10, lob::POST_ONLY},
      ReplayOrder{1000002LL, 100, 1003, lob::Side::Ask, 103, 10, lob::POST_ONLY},
      ReplayOrder{1000003LL, 101, 1004, lob::Side::Bid, 99, 10, lob::POST_ONLY},
      ReplayOrder{1000004LL, 101, 1005, lob::Side::Bid, 98, 10, lob::POST_ONLY},
      // ?????? 100/101??????
      ReplayOrder{1 * s + 1, 200, 2001, lob::Side::Bid, 101, 15, lob::NONE},
      // ?????? 99/98??????
      ReplayOrder{2 * s + 1, 201, 2002, lob::Side::Ask, 98, 12, lob::NONE},
      // ???????? 101/103????????
      ReplayOrder{2 * s + 500000000LL, 202, 2003, lob::Side::Bid, 103, 12, lob::NONE},
  };
}

void require_ok(const lobx::Result& r, const std::string& context) {
  if (!r.ok) throw std::runtime_error(context + ": " + r.reason);
}

lobx::Exchange bootstrap_exchange() {
  lobx::Exchange ex;
  require_ok(ex.issue_asset("USDT", 2, 1000000000000LL, 1, 100000000000LL), "issue USDT");
  require_ok(ex.issue_asset("SIM", 0, 1000000000LL, 100, 10000000LL), "issue SIM");
  require_ok(ex.create_spot_market("SIM-USDT", "SIM", "USDT", 1, 1, 1, 1), "create market");

  for (lobx::UserId user : {100ULL, 101ULL, 200ULL, 201ULL, 202ULL, 203ULL, 204ULL, 205ULL}) {
    require_ok(ex.deposit(user, "USDT", 1000000000LL), "deposit USDT");
    require_ok(ex.deposit(user, "SIM", 1000000LL), "deposit SIM");
  }
  return ex;
}

void replay(lobx::Exchange& ex, const std::vector<ReplayOrder>& orders, bool verbose) {
  for (const ReplayOrder& o : orders) {
    const auto before = ex.trades().size();
    lobx::SubmitResult r = ex.submit_limit("SIM-USDT", o.user, o.order_id, o.side, o.price, o.qty, o.flags, o.ts);
    if (!r.accepted) {
      std::ostringstream msg;
      msg << "order rejected id=" << o.order_id << " code=" << lobx::reject_code_name(r.code) << " reason=" << r.reason;
      throw std::runtime_error(msg.str());
    }
    if (verbose) {
      std::cout << "ORDER id=" << o.order_id << " user=" << o.user
                << " side=" << (o.side == lob::Side::Bid ? "BID" : "ASK")
                << " price=" << o.price << " qty=" << o.qty
                << " filled=" << r.exec.filled << " remaining=" << r.exec.remaining << "\n";
      for (size_t i = before; i < ex.trades().size(); ++i) {
        const lobx::TradeEvent& t = ex.trades()[i];
        std::cout << "  FILL ts=" << t.ts << " price=" << t.price << " qty=" << t.qty
                  << " buyer=" << t.buyer << " seller=" << t.seller << "\n";
      }
    }
  }
  ex.flush_candles();
}

std::ostream& trade_header(std::ostream& os) {
  return os << "market_id,ts,price,qty,buyer,seller,buyer_order_id,seller_order_id,liquidity_side\n";
}

std::ostream& candle_header(std::ostream& os) {
  return os << "market_id,interval_ns,open_time_ns,close_time_ns,open,high,low,close,volume,quote_volume,trade_count\n";
}

void write_trades(std::ostream& os, const std::vector<lobx::TradeEvent>& trades) {
  trade_header(os);
  for (const auto& t : trades) {
    os << t.market_id << ',' << t.ts << ',' << t.price << ',' << t.qty << ',' << t.buyer << ',' << t.seller << ','
       << t.buyer_order_id << ',' << t.seller_order_id << ',' << (t.liquidity_side == lob::Side::Bid ? "BID" : "ASK") << '\n';
  }
}

void write_candles(std::ostream& os, const std::vector<lobx::Candle>& candles) {
  candle_header(os);
  for (const auto& c : candles) {
    os << c.market_id << ',' << c.interval_ns << ',' << c.open_time_ns << ',' << c.close_time_ns << ','
       << c.open << ',' << c.high << ',' << c.low << ',' << c.close << ',' << c.volume << ','
       << c.quote_volume << ',' << c.trade_count << '\n';
  }
}

void write_file(const std::string& path, void (*writer)(std::ostream&, const std::vector<lobx::TradeEvent>&), const std::vector<lobx::TradeEvent>& data) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("failed to open output: " + path);
  writer(out, data);
}

void write_candle_file(const std::string& path, const std::vector<lobx::Candle>& data) {
  std::ofstream out(path);
  if (!out) throw std::runtime_error("failed to open output: " + path);
  write_candles(out, data);
}

} // namespace

int main(int argc, char** argv) {
  try {
    std::string orders_path;
    std::string trades_out;
    std::string candles_out;
    bool verbose = true;

    for (int i = 1; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--orders" && i + 1 < argc) orders_path = argv[++i];
      else if (arg == "--trades-out" && i + 1 < argc) trades_out = argv[++i];
      else if (arg == "--candles-out" && i + 1 < argc) candles_out = argv[++i];
      else if (arg == "--quiet") verbose = false;
      else if (arg == "--help") {
        std::cout << "Usage: lobx_simulator [--orders orders.csv] [--trades-out trades.csv] [--candles-out candles.csv] [--quiet]\n"
                  << "orders.csv columns: ts,user,order_id,side,price,qty,flags\n";
        return 0;
      } else {
        throw std::runtime_error("unknown argument: " + arg);
      }
    }

    lobx::Exchange ex = bootstrap_exchange();
    std::vector<ReplayOrder> orders = orders_path.empty() ? default_orders() : load_orders_csv(orders_path);
    replay(ex, orders, verbose);

    if (!trades_out.empty()) write_file(trades_out, write_trades, ex.trades());
    if (!candles_out.empty()) write_candle_file(candles_out, ex.candles());

    if (trades_out.empty()) {
      std::cout << "\nTRADES\n";
      write_trades(std::cout, ex.trades());
    }
    if (candles_out.empty()) {
      std::cout << "\nCANDLES\n";
      write_candles(std::cout, ex.candles());
    }

    const auto sim = ex.balance(200, "SIM");
    const auto usdt = ex.balance(200, "USDT");
    std::cout << "\nSUMMARY trades=" << ex.trades().size() << " candles=" << ex.candles().size()
              << " user200_SIM=" << sim.total << " user200_USDT=" << usdt.total << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "lobx_simulator error: " << e.what() << "\n";
    return 1;
  }
}
