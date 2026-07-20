#include "lobx/exchange.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <mutex>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <unistd.h>

namespace {

constexpr lobx::UserId PLAYER_ID = 9000;
constexpr lobx::UserId RETAIL_START_ID = 10000;
constexpr int RETAIL_ACCOUNT_COUNT = 1000;
constexpr int RETAIL_UI_SAMPLE_COUNT = 12;
constexpr int WHALE_ACCOUNT_COUNT = 4;
constexpr lobx::UserId WHALE_START_ID = 7000;
constexpr const char* MARKET = "SIM-USDT";

volatile std::sig_atomic_t g_stop = 0;
void handle_signal(int) { g_stop = 1; }

struct Args {
  uint64_t steps{0};
  int sleep_ms{50};
  double speed_x{0.0};
  uint64_t seed{42};
  lob::Tick base_price{1000};
  int stats_every{10};
};

struct AccountSpec {
  lobx::UserId user{0};
  std::string name;
  std::string role;
  lobx::Amount usdt{0};
  lobx::Amount sim{0};
};

enum class ManualOrderType { Limit, Market };

struct ManualCommand {
  lobx::UserId user{PLAYER_ID};
  ManualOrderType type{ManualOrderType::Limit};
  lob::Side side{lob::Side::Bid};
  lob::Tick price{0};
  lob::Quantity qty{0};
  int volatility_bps{100};
};

std::mutex g_command_mutex;
std::deque<ManualCommand> g_commands;

AccountSpec player_spec() {
  return AccountSpec{PLAYER_ID, "Player", "player", 1000000LL, 0LL};
}

int total_account_count() {
  return RETAIL_ACCOUNT_COUNT + 7 + WHALE_ACCOUNT_COUNT;
}

std::vector<AccountSpec> account_specs() {
  std::vector<AccountSpec> specs;
  specs.reserve(static_cast<size_t>(total_account_count()));
  specs.push_back(AccountSpec{100, "Maker Ask Bot", "liquidity_bot", 1000000000000LL, 1000000000LL});
  specs.push_back(AccountSpec{101, "Maker Bid Bot", "liquidity_bot", 1000000000000LL, 1000000000LL});
  specs.push_back(AccountSpec{3001, "Trend Strategy", "fixed_strategy", 500000000LL, 500000LL});
  specs.push_back(AccountSpec{3002, "Mean Reversion", "fixed_strategy", 500000000LL, 500000LL});
  specs.push_back(AccountSpec{3003, "Sweep Strategy", "fixed_strategy", 500000000LL, 500000LL});
  specs.push_back(AccountSpec{3004, "Grid Strategy", "fixed_strategy", 500000000LL, 500000LL});
  specs.push_back(AccountSpec{7000, "Whale Alpha", "predatory_whale", 5000000000LL, 5000000LL});
  specs.push_back(AccountSpec{7001, "Whale Bravo", "predatory_whale", 4500000000LL, 4500000LL});
  specs.push_back(AccountSpec{7002, "Whale Charlie", "predatory_whale", 6000000000LL, 6000000LL});
  specs.push_back(AccountSpec{7003, "Whale Delta", "predatory_whale", 5500000000LL, 5500000LL});
  specs.push_back(player_spec());
  for (int i = 0; i < RETAIL_ACCOUNT_COUNT; ++i) {
    const lobx::UserId user = RETAIL_START_ID + static_cast<lobx::UserId>(i);
    const lobx::Amount usdt = 50000LL + static_cast<lobx::Amount>((i % 37) * 2500);
    const lobx::Amount sim = 100LL + static_cast<lobx::Amount>((i % 11) * 25);
    specs.push_back(AccountSpec{user, "Retail " + std::to_string(i + 1), "retail", usdt, sim});
  }
  return specs;
}

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--steps" && i + 1 < argc) args.steps = std::stoull(argv[++i]);
    else if (a == "--sleep-ms" && i + 1 < argc) args.sleep_ms = std::stoi(argv[++i]);
    else if (a == "--speed-x" && i + 1 < argc) args.speed_x = std::stod(argv[++i]);
    else if (a == "--seed" && i + 1 < argc) args.seed = std::stoull(argv[++i]);
    else if (a == "--base-price" && i + 1 < argc) args.base_price = std::stoll(argv[++i]);
    else if (a == "--stats-every" && i + 1 < argc) args.stats_every = std::stoi(argv[++i]);
    else if (a == "--help") {
      std::cout << "Usage: lobx_realtime_simulator [--steps N] [--sleep-ms N] [--speed-x X] [--seed N] [--base-price N]\n";
      std::exit(0);
    } else {
      throw std::runtime_error("unknown argument: " + a);
    }
  }
  if (args.sleep_ms < 0) args.sleep_ms = 0;
  if (!std::isfinite(args.speed_x) || args.speed_x < 0.0) args.speed_x = 0.0;
  if (args.stats_every <= 0) args.stats_every = 10;
  return args;
}

void sleep_after_step(const Args& args, int64_t tick_ns) {
  if (args.speed_x > 0.0) {
    const double sleep_us = (static_cast<double>(tick_ns) / 1000.0) / args.speed_x;
    if (sleep_us >= 1.0) {
      std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(std::llround(sleep_us))));
    }
    return;
  }
  if (args.sleep_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(args.sleep_ms));
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

std::string upper(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return s;
}

std::vector<std::string> split(const std::string& line, char delim) {
  std::vector<std::string> out;
  std::stringstream ss(line);
  std::string item;
  while (std::getline(ss, item, delim)) out.push_back(item);
  return out;
}

std::optional<ManualCommand> parse_command(const std::string& line) {
  const auto parts = split(line, ',');
  if (parts.empty()) return std::nullopt;
  const std::string op = upper(parts[0]);
  if (op == "STOP") {
    g_stop = 1;
    return std::nullopt;
  }
  ManualCommand cmd{};
  cmd.type = op == "MARKET" ? ManualOrderType::Market : ManualOrderType::Limit;
  if (cmd.type == ManualOrderType::Limit) {
    if (op != "ORDER" || parts.size() < 5) return std::nullopt;
    cmd.user = static_cast<lobx::UserId>(std::stoull(parts[1]));
    const std::string side = upper(parts[2]);
    if (side == "BID" || side == "BUY") cmd.side = lob::Side::Bid;
    else if (side == "ASK" || side == "SELL") cmd.side = lob::Side::Ask;
    else return std::nullopt;
    cmd.price = std::stoll(parts[3]);
    cmd.qty = std::stoll(parts[4]);
    if (cmd.price <= 0 || cmd.qty <= 0) return std::nullopt;
    return cmd;
  }
  if (parts.size() < 4) return std::nullopt;
  cmd.user = static_cast<lobx::UserId>(std::stoull(parts[1]));
  const std::string side = upper(parts[2]);
  if (side == "BID" || side == "BUY") cmd.side = lob::Side::Bid;
  else if (side == "ASK" || side == "SELL") cmd.side = lob::Side::Ask;
  else return std::nullopt;
  cmd.qty = std::stoll(parts[3]);
  cmd.volatility_bps = parts.size() >= 5 ? std::stoi(parts[4]) : 100;
  if (cmd.qty <= 0 || cmd.volatility_bps < 0) return std::nullopt;
  return cmd;
}

void input_thread_main() {
  std::string line;
  while (!g_stop && std::getline(std::cin, line)) {
    try {
      auto cmd = parse_command(line);
      if (!cmd) continue;
      std::lock_guard<std::mutex> lock(g_command_mutex);
      g_commands.push_back(*cmd);
    } catch (...) {
    }
  }
}

std::vector<ManualCommand> drain_commands() {
  std::vector<ManualCommand> out;
  std::lock_guard<std::mutex> lock(g_command_mutex);
  while (!g_commands.empty()) {
    out.push_back(g_commands.front());
    g_commands.pop_front();
  }
  return out;
}

void require_ok(const lobx::Result& r, const std::string& context) {
  if (!r.ok) throw std::runtime_error(context + ": " + r.reason);
}

lobx::Exchange bootstrap_exchange() {
  lobx::Exchange ex;
  require_ok(ex.issue_asset("USDT", 2, 900000000000000000LL, 1, 90000000000000000LL), "issue USDT");
  require_ok(ex.issue_asset("SIM", 0, 900000000000000000LL, 100, 90000000000000000LL), "issue SIM");
  require_ok(ex.create_spot_market(MARKET, "SIM", "USDT", 1, 1, 1, 1), "create market");

  for (const AccountSpec& spec : account_specs()) {
    if (spec.usdt > 0) require_ok(ex.deposit(spec.user, "USDT", spec.usdt), "deposit USDT");
    if (spec.sim > 0) require_ok(ex.deposit(spec.user, "SIM", spec.sim), "deposit SIM");
  }
  return ex;
}

long rss_kb() {
  std::ifstream in("/proc/self/statm");
  long pages_total = 0;
  long pages_rss = 0;
  if (!(in >> pages_total >> pages_rss)) return 0;
  const long page_kb = static_cast<long>(::sysconf(_SC_PAGESIZE) / 1024);
  return pages_rss * page_kb;
}

void emit_user(const AccountSpec& spec) {
  std::cout << "{\"type\":\"user\",\"user\":" << spec.user
            << ",\"name\":\"" << json_escape(spec.name)
            << "\",\"role\":\"" << json_escape(spec.role)
            << "\",\"initial_usdt\":" << spec.usdt
            << ",\"initial_sim\":" << spec.sim
            << "}\n";
}

void emit_account(lobx::Exchange& ex, const AccountSpec& spec) {
  const auto usdt = ex.balance(spec.user, "USDT");
  const auto sim = ex.balance(spec.user, "SIM");
  std::cout << "{\"type\":\"account\",\"user\":" << spec.user
            << ",\"name\":\"" << json_escape(spec.name)
            << "\",\"role\":\"" << json_escape(spec.role)
            << "\",\"usdt_total\":" << usdt.total
            << ",\"usdt_free\":" << usdt.free
            << ",\"usdt_locked\":" << usdt.locked
            << ",\"sim_total\":" << sim.total
            << ",\"sim_free\":" << sim.free
            << ",\"sim_locked\":" << sim.locked
            << "}\n";
}

bool should_emit_account_snapshot(const AccountSpec& spec) {
  return spec.role != "retail" || spec.user < RETAIL_START_ID + RETAIL_UI_SAMPLE_COUNT;
}

void emit_all_accounts(lobx::Exchange& ex) {
  for (const AccountSpec& spec : account_specs()) {
    if (!should_emit_account_snapshot(spec)) continue;
    emit_user(spec);
    emit_account(ex, spec);
  }
  std::cout << std::flush;
}

void emit_trade(const lobx::TradeEvent& t) {
  std::cout << "{\"type\":\"trade\",\"market_id\":" << t.market_id
            << ",\"ts\":" << t.ts
            << ",\"price\":" << t.price
            << ",\"qty\":" << t.qty
            << ",\"buyer\":" << t.buyer
            << ",\"seller\":" << t.seller
            << ",\"buyer_order_id\":" << t.buyer_order_id
            << ",\"seller_order_id\":" << t.seller_order_id
            << "}\n";
}

void emit_candle(const lobx::Candle& c) {
  std::cout << "{\"type\":\"candle\",\"market_id\":" << c.market_id
            << ",\"interval_ns\":" << c.interval_ns
            << ",\"open_time_ns\":" << c.open_time_ns
            << ",\"close_time_ns\":" << c.close_time_ns
            << ",\"open\":" << c.open
            << ",\"high\":" << c.high
            << ",\"low\":" << c.low
            << ",\"close\":" << c.close
            << ",\"volume\":" << c.volume
            << ",\"quote_volume\":" << c.quote_volume
            << ",\"trade_count\":" << c.trade_count
            << "}\n";
}

void emit_order(const std::string& source, const std::string& order_type, lobx::UserId user, lobx::OrderId order_id,
                lob::Side side, lob::Tick price, lob::Quantity qty, int volatility_bps, const lobx::SubmitResult& r) {
  std::cout << "{\"type\":\"order\",\"source\":\"" << json_escape(source)
            << "\",\"order_type\":\"" << json_escape(order_type)
            << "\",\"user\":" << user
            << ",\"order_id\":" << order_id
            << ",\"side\":\"" << (side == lob::Side::Bid ? "BUY" : "SELL")
            << "\",\"price\":" << price
            << ",\"qty\":" << qty
            << ",\"volatility_bps\":" << volatility_bps
            << ",\"accepted\":" << (r.accepted ? "true" : "false")
            << ",\"filled\":" << r.exec.filled
            << ",\"remaining\":" << r.exec.remaining
            << ",\"code\":\"" << lobx::reject_code_name(r.code)
            << "\",\"reason\":\"" << json_escape(r.reason)
            << "\"}\n";
}

void emit_stats(uint64_t step, uint64_t accepted, uint64_t rejected, uint64_t total_trades,
                uint64_t total_candles, lob::Tick last_price) {
  std::cout << "{\"type\":\"stats\",\"step\":" << step
            << ",\"accepted\":" << accepted
            << ",\"rejected\":" << rejected
            << ",\"trades\":" << total_trades
            << ",\"candles\":" << total_candles
            << ",\"last_price\":" << last_price
            << ",\"accounts\":" << total_account_count()
            << ",\"retail_accounts\":" << RETAIL_ACCOUNT_COUNT
            << ",\"rss_kb\":" << rss_kb()
            << "}\n" << std::flush;
}

lobx::SubmitResult submit(lobx::Exchange& ex, lobx::UserId user, lobx::OrderId order_id,
                          lob::Side side, lob::Tick price, lob::Quantity qty, uint32_t flags, lob::Timestamp ts) {
  return ex.submit_limit(MARKET, user, order_id, side, price, qty, flags, ts);
}

void process_outputs(lobx::Exchange& ex, lob::Tick& last_price, uint64_t& total_trades, uint64_t& total_candles) {
  for (const auto& trade : ex.drain_trades()) {
    last_price = trade.price;
    ++total_trades;
    emit_trade(trade);
  }
  for (const auto& candle : ex.drain_candles()) {
    ++total_candles;
    emit_candle(candle);
  }
}

lobx::SubmitResult submit_counted(lobx::Exchange& ex, lobx::UserId user, lobx::OrderId order_id,
                                  lob::Side side, lob::Tick price, lob::Quantity qty, uint32_t flags,
                                  lob::Timestamp ts, uint64_t& accepted, uint64_t& rejected) {
  auto result = submit(ex, user, order_id, side, price, qty, flags, ts);
  accepted += result.accepted ? 1 : 0;
  rejected += result.accepted ? 0 : 1;
  return result;
}

void seed_book(lobx::Exchange& ex, lob::Tick mid, lobx::OrderId& next_order_id, lob::Timestamp ts, std::deque<lobx::OrderId>& maker_orders) {
  for (int level = 1; level <= 30; ++level) {
    const lobx::OrderId ask_id = next_order_id++;
    const lobx::OrderId bid_id = next_order_id++;
    if (submit(ex, 100, ask_id, lob::Side::Ask, mid + level, 1000, lob::POST_ONLY, ts + level).accepted) maker_orders.push_back(ask_id);
    if (submit(ex, 101, bid_id, lob::Side::Bid, std::max<lob::Tick>(1, mid - level), 1000, lob::POST_ONLY, ts + level).accepted) maker_orders.push_back(bid_id);
  }
  ex.drain_trades();
  ex.drain_candles();
}

} // namespace

int main(int argc, char** argv) {
  try {
    std::signal(SIGTERM, handle_signal);
    std::signal(SIGINT, handle_signal);
    const Args args = parse_args(argc, argv);
    lobx::Exchange ex = bootstrap_exchange();
    ex.events().set_memory_enabled(false);
    std::thread input_thread(input_thread_main);
    input_thread.detach();

    std::mt19937_64 rng(args.seed);
    std::uniform_int_distribution<int> qty_dist(1, 40);
    std::uniform_int_distribution<int> user_dist(static_cast<int>(RETAIL_START_ID), static_cast<int>(RETAIL_START_ID + RETAIL_ACCOUNT_COUNT - 1));
    std::uniform_int_distribution<int> noise_dist(-4, 4);
    std::bernoulli_distribution side_flip(0.5);

    lobx::OrderId next_order_id = 1;
    lob::Tick reference = args.base_price;
    lob::Tick last_price = args.base_price;
    uint64_t accepted = 0;
    uint64_t rejected = 0;
    uint64_t total_trades = 0;
    uint64_t total_candles = 0;
    const int64_t tick_ns = 100000000LL;

    std::deque<lobx::OrderId> maker_orders;
    seed_book(ex, reference, next_order_id, 0, maker_orders);
    emit_all_accounts(ex);
    emit_stats(0, accepted, rejected, total_trades, total_candles, last_price);

    for (uint64_t step = 1; !g_stop && (args.steps == 0 || step <= args.steps); ++step) {
      const lob::Timestamp ts = static_cast<lob::Timestamp>(step) * tick_ns;
      const double wave = std::sin(static_cast<double>(step) / 45.0) * 3.0;
      reference = std::max<lob::Tick>(10, reference + noise_dist(rng) + static_cast<int>(std::llround(wave)));

      if (step % 8 == 0) {
        for (int level = 1; level <= 4; ++level) {
          const lob::Quantity maker_qty = 100 + (level * 25);
          const lobx::OrderId ask_id = next_order_id++;
          const lobx::OrderId bid_id = next_order_id++;
          auto ask = submit_counted(ex, 100, ask_id, lob::Side::Ask, reference + level, maker_qty, lob::POST_ONLY, ts, accepted, rejected);
          auto bid = submit_counted(ex, 101, bid_id, lob::Side::Bid, std::max<lob::Tick>(1, reference - level), maker_qty, lob::POST_ONLY, ts, accepted, rejected);
          if (ask.accepted) maker_orders.push_back(ask_id);
          if (bid.accepted) maker_orders.push_back(bid_id);
        }
      }

      if (step % 10 == 0) {
        const bool buy = reference >= last_price;
        const lob::Tick px = buy ? reference + 12 : std::max<lob::Tick>(1, reference - 12);
        const lobx::OrderId id = next_order_id++;
        auto result = submit_counted(ex, 3001, id, buy ? lob::Side::Bid : lob::Side::Ask, px, 12, lob::IOC, ts, accepted, rejected);
        emit_order("trend_strategy", "LIMIT", 3001, id, buy ? lob::Side::Bid : lob::Side::Ask, px, 12, 0, result);
      }
      if (step % 15 == 0) {
        const bool sell = last_price > args.base_price + 30;
        const bool buy = last_price < args.base_price - 30;
        if (sell || buy) {
          const lob::Side side = buy ? lob::Side::Bid : lob::Side::Ask;
          const lob::Tick px = buy ? reference + 10 : std::max<lob::Tick>(1, reference - 10);
          const lobx::OrderId id = next_order_id++;
          auto result = submit_counted(ex, 3002, id, side, px, 10, lob::IOC, ts, accepted, rejected);
          emit_order("mean_reversion", "LIMIT", 3002, id, side, px, 10, 0, result);
        }
      }
      if (step % 37 == 0) {
        const bool buy = side_flip(rng);
        const lob::Tick px = buy ? reference + 25 : std::max<lob::Tick>(1, reference - 25);
        const lobx::OrderId id = next_order_id++;
        auto result = submit_counted(ex, 3003, id, buy ? lob::Side::Bid : lob::Side::Ask, px, 60, lob::IOC, ts, accepted, rejected);
        emit_order("sweep_strategy", "LIMIT", 3003, id, buy ? lob::Side::Bid : lob::Side::Ask, px, 60, 0, result);
      }
      if (step % 53 == 0 || step % 89 == 0 || step % 137 == 0) {
        const int whale_slot = static_cast<int>((step / 53 + step / 89 + step / 137) % WHALE_ACCOUNT_COUNT);
        const lobx::UserId whale = WHALE_START_ID + static_cast<lobx::UserId>(whale_slot);
        const bool pressure_buy = (step % 106 == 0) || (last_price <= args.base_price) || (step % 137 == 0 && side_flip(rng));
        const lob::Quantity whale_qty = 180 + static_cast<lob::Quantity>((step % 7) * 45);
        const lob::Tick band = 45 + static_cast<lob::Tick>((step % 5) * 12);
        const lob::Tick px = pressure_buy ? reference + band : std::max<lob::Tick>(1, reference - band);
        const lobx::OrderId id = next_order_id++;
        auto result = submit_counted(ex, whale, id, pressure_buy ? lob::Side::Bid : lob::Side::Ask, px, whale_qty, lob::IOC, ts, accepted, rejected);
        emit_order("predatory_whale", "LIMIT", whale, id, pressure_buy ? lob::Side::Bid : lob::Side::Ask, px, whale_qty, 0, result);
      }
      if (step % 211 == 0) {
        const bool buy = side_flip(rng);
        for (int burst = 0; burst < 3; ++burst) {
          const lobx::UserId whale = WHALE_START_ID + static_cast<lobx::UserId>((burst + step / 211) % WHALE_ACCOUNT_COUNT);
          const lob::Quantity whale_qty = 260 + burst * 80;
          const lob::Tick px = buy ? reference + 80 + burst * 20 : std::max<lob::Tick>(1, reference - 80 - burst * 20);
          const lobx::OrderId id = next_order_id++;
          auto result = submit_counted(ex, whale, id, buy ? lob::Side::Bid : lob::Side::Ask, px, whale_qty, lob::IOC, ts + burst, accepted, rejected);
          emit_order("whale_burst", "LIMIT", whale, id, buy ? lob::Side::Bid : lob::Side::Ask, px, whale_qty, 0, result);
        }
      }
      if (step % 16 == 0) {
        const lobx::OrderId ask_id = next_order_id++;
        const lobx::OrderId bid_id = next_order_id++;
        auto ask = submit_counted(ex, 3004, ask_id, lob::Side::Ask, reference + 6, 50, lob::POST_ONLY, ts, accepted, rejected);
        auto bid = submit_counted(ex, 3004, bid_id, lob::Side::Bid, std::max<lob::Tick>(1, reference - 6), 50, lob::POST_ONLY, ts, accepted, rejected);
        if (ask.accepted) maker_orders.push_back(ask_id);
        if (bid.accepted) maker_orders.push_back(bid_id);
      }

      bool buy = reference >= last_price;
      if (reference == last_price) buy = side_flip(rng);
      const lob::Quantity qty = qty_dist(rng);
      const lob::Tick aggressive_price = buy ? reference + 20 : std::max<lob::Tick>(1, reference - 20);
      const lobx::UserId taker = static_cast<lobx::UserId>(user_dist(rng));
      submit_counted(ex, taker, next_order_id++, buy ? lob::Side::Bid : lob::Side::Ask, aggressive_price, qty, lob::IOC, ts, accepted, rejected);

      for (const ManualCommand& cmd : drain_commands()) {
        const lobx::OrderId id = next_order_id++;
        lob::Tick execution_price = cmd.price;
        uint32_t flags = lob::NONE;
        std::string order_type = "LIMIT";
        if (cmd.type == ManualOrderType::Market) {
          order_type = "MARKET";
          const int max_bps = std::max(0, cmd.volatility_bps);
          std::uniform_int_distribution<int> vol_dist(0, max_bps);
          const int realized_bps = vol_dist(rng);
          const lob::Tick base = std::max<lob::Tick>(1, last_price);
          if (cmd.side == lob::Side::Bid) {
            execution_price = std::max<lob::Tick>(1, base + std::max<lob::Tick>(1, (base * realized_bps) / 10000));
          } else {
            execution_price = std::max<lob::Tick>(1, base - std::max<lob::Tick>(1, (base * realized_bps) / 10000));
          }
          flags = lob::IOC;
        }
        auto result = submit_counted(ex, cmd.user, id, cmd.side, execution_price, cmd.qty, flags, ts, accepted, rejected);
        emit_order("manual", order_type, cmd.user, id, cmd.side, execution_price, cmd.qty, cmd.volatility_bps, result);
      }

      process_outputs(ex, last_price, total_trades, total_candles);

      while (maker_orders.size() > 5000) {
        const lobx::OrderId old_id = maker_orders.front();
        maker_orders.pop_front();
        ex.cancel(MARKET, old_id);
      }

      if (step % static_cast<uint64_t>(args.stats_every) == 0) {
        emit_account(ex, player_spec());
        emit_stats(step, accepted, rejected, total_trades, total_candles, last_price);
      }
      sleep_after_step(args, tick_ns);
    }

    for (const auto& candle : ex.flush_candles()) {
      ++total_candles;
      emit_candle(candle);
    }
    emit_all_accounts(ex);
    emit_stats(args.steps, accepted, rejected, total_trades, total_candles, last_price);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "lobx_realtime_simulator error: " << e.what() << "\n";
    return 1;
  }
}
