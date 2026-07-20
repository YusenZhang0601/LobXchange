#include "lobx/exchange.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <numeric>
#include <string>
#include <unistd.h>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

struct Args {
  int orders{100000};
  bool fast_sim{false};
};

struct InsuranceAdlCounters {
  int insurance_fund_credits{0};
  int insurance_fund_debits{0};
  int bad_debt_records{0};
  int adl_required_events{0};
  int adl_candidate_rankings{0};
};

struct OrderTriggerCounters {
  int market_orders{0};
  int market_order_rejects{0};
  int trigger_orders_created{0};
  int trigger_orders_fired{0};
  int trigger_orders_cancelled{0};
  int trigger_child_orders{0};
  int trigger_failures{0};
  int trigger_evaluations{0};
};

Args parse_args(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--orders" && i + 1 < argc) args.orders = std::stoi(argv[++i]);
    else if (arg == "--fast-sim") args.fast_sim = true;
    else if (arg == "--help") {
      std::cout << "Usage: bench_perp [--orders N] [--fast-sim]\n";
      std::exit(0);
    }
  }
  return args;
}

lobx::RuntimeRetentionOptions retention_options_for(const Args& args) {
  lobx::RuntimeRetentionOptions options{};
  if (args.fast_sim) {
    options.record_events = false;
    options.build_event_payloads = false;
    options.record_trade_history = false;
    options.update_klines = false;
    options.record_candle_history = false;
  }
  return options;
}

void require_ok(const lobx::Result& result, const std::string& context) {
  if (!result.ok) throw std::runtime_error(context + ": " + result.reason);
}

void require_submit(const lobx::SubmitResult& result, const std::string& context) {
  if (!result.accepted) throw std::runtime_error(context + ": " + result.reason);
}

lobx::Exchange bootstrap(lobx::MarketId& market_id, const lobx::RuntimeRetentionOptions& retention_options) {
  lobx::Exchange ex;
  ex.set_retention_options(retention_options);
  require_ok(ex.issue_asset("USDT", 6, 900000000000000000LL, 1, 0), "issue USDT");
  require_ok(ex.issue_asset("BTC", 8, 900000000000000000LL, 1, 0), "issue BTC");
  require_ok(ex.create_perpetual_market("BTC-USDT-PERP", "BTC", "USDT", "USDT", 1, 1, 1, 1, 10, &market_id), "create perp");
  require_ok(ex.set_index_price(market_id, 100), "set index");
  require_ok(ex.set_mark_price_mode(market_id, lobx::MarkPriceMode::IndexPrice), "set mark mode");
  require_ok(ex.set_perp_risk_tiers(market_id, {lobx::PerpRiskTier{0, 100000, 1000, 500, 10},
                                                lobx::PerpRiskTier{100000, 0, 5000, 1000, 3}}),
             "set tiers");
  require_ok(ex.set_perp_fee_config("BTC-USDT-PERP", lobx::PerpFeeConfig{100, 200, 0}), "set fees");
  require_ok(ex.set_funding_rate("BTC-USDT-PERP", 10), "set funding");
  for (lobx::UserId user = 100; user < 140; ++user) {
    require_ok(ex.deposit(user, "USDT", 1000000000LL), "deposit");
    ex.set_leverage(user, "BTC-USDT-PERP", 5);
  }
  return ex;
}

std::vector<lobx::UserId> bench_users() {
  std::vector<lobx::UserId> users;
  for (lobx::UserId user = 100; user < 140; ++user) users.push_back(user);
  return users;
}

lobx::Amount sum_account_fees(const lobx::Exchange& ex, const std::vector<lobx::UserId>& users) {
  lobx::Amount total = 0;
  for (lobx::UserId user : users) total += ex.account_fee_total(user, "BTC-USDT-PERP");
  return total;
}

lobx::Amount sum_abs_funding(const lobx::Exchange& ex, const std::vector<lobx::UserId>& users) {
  lobx::Amount total = 0;
  for (lobx::UserId user : users) {
    const lobx::Amount value = ex.account_funding_total(user, "BTC-USDT-PERP");
    total += value < 0 ? -value : value;
  }
  return total;
}

template <typename T>
T percentile(std::vector<T> values, double p) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const size_t index = static_cast<size_t>((p * static_cast<double>(values.size() - 1)) / 100.0);
  return values[index];
}

long rss_mb() {
  std::ifstream in("/proc/self/statm");
  long pages_total = 0;
  long pages_rss = 0;
  if (!(in >> pages_total >> pages_rss)) return 0;
  const long page_kb = static_cast<long>(::sysconf(_SC_PAGESIZE) / 1024);
  return (pages_rss * page_kb) / 1024;
}

int count_events(lobx::Exchange& ex, const std::string& type) {
  int count = 0;
  for (const auto& record : ex.events().records()) {
    if (record.type == type) ++count;
  }
  return count;
}

InsuranceAdlCounters run_insurance_adl_probe(const lobx::RuntimeRetentionOptions& retention_options) {
  InsuranceAdlCounters counters;
  lobx::Exchange ex;
  ex.set_retention_options(retention_options);
  lobx::MarketId market_id = 0;
  require_ok(ex.issue_asset("USDT", 6, 1000000000LL, 1, 0), "probe issue USDT");
  require_ok(ex.issue_asset("BTC", 8, 1000000000LL, 1, 0), "probe issue BTC");
  require_ok(ex.create_perpetual_market("BTC-USDT-PERP", "BTC", "USDT", "USDT", 1, 1, 1, 1, 10, &market_id), "probe create perp");
  require_ok(ex.deposit(10, "USDT", 300), "probe deposit alice");
  require_ok(ex.deposit(20, "USDT", 300), "probe deposit bob");
  require_ok(ex.deposit(30, "USDT", 1000), "probe deposit carol");
  ex.set_leverage(10, "BTC-USDT-PERP", 5);
  ex.set_leverage(20, "BTC-USDT-PERP", 5);
  require_ok(ex.credit_insurance_fund("BTC-USDT-PERP", 500, "bench_seed", 1), "probe credit insurance");
  ++counters.insurance_fund_credits;
  require_submit(ex.submit_limit("BTC-USDT-PERP", 20, 900001, lob::Side::Ask, 100, 10, lob::POST_ONLY, 2), "probe maker");
  require_submit(ex.submit_limit("BTC-USDT-PERP", 10, 900002, lob::Side::Bid, 100, 10, lob::IOC, 3), "probe taker");
  require_ok(ex.set_index_price(market_id, 1), "probe mark");
  require_ok(ex.set_mark_price_mode(market_id, lobx::MarkPriceMode::IndexPrice), "probe mark mode");
  require_ok(ex.liquidate_position("BTC-USDT-PERP", 10, 30, 4), "probe liquidation");
  counters.insurance_fund_debits = count_events(ex, "insurance_fund.debited");
  counters.bad_debt_records = count_events(ex, "perp.bad_debt_recorded");
  counters.adl_required_events = count_events(ex, "ADL_REQUIRED");
  (void)ex.rank_adl_candidates("BTC-USDT-PERP");
  ++counters.adl_candidate_rankings;
  return counters;
}

OrderTriggerCounters run_order_trigger_probe(const lobx::RuntimeRetentionOptions& retention_options) {
  OrderTriggerCounters counters;
  lobx::Exchange ex;
  ex.set_retention_options(retention_options);
  lobx::MarketId market_id = 0;
  require_ok(ex.issue_asset("USDT", 6, 1000000000LL, 1, 0), "order probe issue USDT");
  require_ok(ex.issue_asset("BTC", 8, 1000000000LL, 1, 0), "order probe issue BTC");
  require_ok(ex.create_perpetual_market("BTC-USDT-PERP", "BTC", "USDT", "USDT", 1, 1, 1, 1, 10, &market_id), "order probe create perp");
  for (lobx::UserId user : {10, 20, 30}) {
    require_ok(ex.deposit(user, "USDT", 1000000), "order probe deposit");
    ex.set_leverage(user, "BTC-USDT-PERP", 5);
  }

  require_submit(ex.submit_limit("BTC-USDT-PERP", 20, 910001, lob::Side::Ask, 100, 1, lob::POST_ONLY, 1), "order probe ask");
  const auto market_ok = ex.submit_market("BTC-USDT-PERP", 10, 910002, lob::Side::Bid, 1, 100, lob::NONE, 2);
  if (market_ok.accepted) ++counters.market_orders;
  require_submit(ex.submit_limit("BTC-USDT-PERP", 20, 910003, lob::Side::Ask, 101, 1, lob::POST_ONLY, 3), "order probe reject ask");
  const auto market_reject = ex.submit_market("BTC-USDT-PERP", 10, 910004, lob::Side::Bid, 1, 100, lob::NONE, 4);
  if (!market_reject.accepted) ++counters.market_order_rejects;

  require_submit(ex.submit_limit("BTC-USDT-PERP", 20, 910005, lob::Side::Ask, 100, 1, lob::POST_ONLY, 5), "trigger probe ask");
  require_ok(ex.create_trigger_order("BTC-USDT-PERP", 10, 910006, lob::Side::Bid, 1, 100,
                                     lobx::TriggerPriceType::Index, lobx::TriggerCondition::AboveOrEqual,
                                     lobx::TriggerChildOrderType::Market, 0, 100, lob::NONE, 6),
             "trigger probe create");
  ++counters.trigger_orders_created;
  require_ok(ex.set_index_price(market_id, 100), "trigger probe index");
  counters.trigger_orders_fired += ex.evaluate_triggers("BTC-USDT-PERP", lobx::TriggerPriceType::Index, 7);
  ++counters.trigger_evaluations;

  require_ok(ex.create_trigger_order("BTC-USDT-PERP", 10, 910007, lob::Side::Bid, 1, 200,
                                     lobx::TriggerPriceType::Index, lobx::TriggerCondition::AboveOrEqual,
                                     lobx::TriggerChildOrderType::Limit, 99, 0, lob::NONE, 8),
             "trigger probe create cancel");
  ++counters.trigger_orders_created;
  if (ex.cancel_trigger_order("BTC-USDT-PERP", 10, 910007, 9)) ++counters.trigger_orders_cancelled;

  require_submit(ex.submit_limit("BTC-USDT-PERP", 20, 910008, lob::Side::Ask, 100, 1, lob::POST_ONLY, 10), "trigger fail ask");
  require_ok(ex.create_trigger_order("BTC-USDT-PERP", 10, 910009, lob::Side::Bid, 1, 100,
                                     lobx::TriggerPriceType::Index, lobx::TriggerCondition::AboveOrEqual,
                                     lobx::TriggerChildOrderType::Market, 0, 100, lobx::LOBX_REDUCE_ONLY, 11),
             "trigger probe create fail");
  ++counters.trigger_orders_created;
  (void)ex.evaluate_triggers("BTC-USDT-PERP", lobx::TriggerPriceType::Index, 12);
  ++counters.trigger_evaluations;

  counters.trigger_child_orders = count_events(ex, "trigger.child_order");
  counters.trigger_failures = count_events(ex, "trigger.failed");
  return counters;
}

} // namespace

int main(int argc, char** argv) {
  try {
    const Args args = parse_args(argc, argv);
    const lobx::RuntimeRetentionOptions retention_options = retention_options_for(args);
    lobx::MarketId market_id = 0;
    lobx::Exchange ex = bootstrap(market_id, retention_options);
    std::vector<long long> settlement_latency_ns;
    settlement_latency_ns.reserve(static_cast<size_t>(args.orders));

    lobx::OrderId next_id = 1;
    int accepted = 0;
    int rejected = 0;
    int fills = 0;
    int margin_checks = 0;
    int liquidation_checks = 0;
    int liquidations = 0;
    int rollback_count = 0;
    int funding_settlements = 0;
    int funding_failures = 0;
    int simulate_fill_calls = 0;
    int simulate_fill_accepts = 0;
    int simulate_fill_rejects = 0;
    InsuranceAdlCounters insurance_adl = run_insurance_adl_probe(retention_options);
    OrderTriggerCounters order_trigger = run_order_trigger_probe(retention_options);
    const std::vector<lobx::UserId> users = bench_users();
    auto record_simulate_fill = [&](const lobx::SimulatedFill& sim) {
      ++simulate_fill_calls;
      if (sim.would_accept) ++simulate_fill_accepts;
      else ++simulate_fill_rejects;
    };

    const auto start = Clock::now();
    for (int i = 0; i < args.orders; ++i) {
      const lobx::UserId maker = static_cast<lobx::UserId>(100 + (i % 20));
      const lobx::UserId taker = static_cast<lobx::UserId>(120 + (i % 20));
      const lob::Tick price = 100 + static_cast<lob::Tick>((i % 7) - 3);
      const lob::Quantity qty = 1 + (i % 5);
      const bool buy_taker = (i % 2) == 0;
      const auto t0 = Clock::now();
      auto maker_result = ex.submit_limit("BTC-USDT-PERP", maker, next_id++,
                                          buy_taker ? lob::Side::Ask : lob::Side::Bid,
                                          price, qty, lob::POST_ONLY);
      const auto sim = ex.simulate_fill("BTC-USDT-PERP", taker,
                                        buy_taker ? lob::Side::Bid : lob::Side::Ask,
                                        price, qty, lob::IOC);
      record_simulate_fill(sim);
      if ((i % 10) == 0) {
        record_simulate_fill(ex.simulate_fill("BTC-USDT-PERP", 900001,
                                              lob::Side::Bid, price, qty, lob::IOC));
        record_simulate_fill(ex.simulate_fill("BTC-USDT-PERP", 900002,
                                              lob::Side::Bid, price, qty, lobx::LOBX_REDUCE_ONLY | lob::IOC));
        if (maker_result.accepted) {
          record_simulate_fill(ex.simulate_fill("BTC-USDT-PERP", taker,
                                                buy_taker ? lob::Side::Bid : lob::Side::Ask,
                                                price, qty, lob::POST_ONLY));
        }
      }
      auto taker_result = ex.submit_limit("BTC-USDT-PERP", taker, next_id++,
                                          buy_taker ? lob::Side::Bid : lob::Side::Ask,
                                          price, qty, lob::IOC);
      const auto t1 = Clock::now();
      settlement_latency_ns.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
      accepted += maker_result.accepted ? 1 : 0;
      accepted += taker_result.accepted ? 1 : 0;
      rejected += maker_result.accepted ? 0 : 1;
      rejected += taker_result.accepted ? 0 : 1;
      fills += static_cast<int>(maker_result.trades.size() + taker_result.trades.size());
      ++margin_checks;

      if ((i % 64) == 0) {
        require_ok(ex.set_index_price(market_id, 80 + (i % 41)), "update index");
      }
      if ((i % 128) == 127) {
        const auto funding = ex.settle_funding("BTC-USDT-PERP", static_cast<lob::Timestamp>(1000 + i));
        if (funding.ok) ++funding_settlements;
        else ++funding_failures;
      }
      (void)ex.unrealized_pnl(taker, "BTC-USDT-PERP");
      (void)ex.maintenance_margin(taker, "BTC-USDT-PERP");
      if ((i % 64) == 0) {
        (void)ex.rank_adl_candidates("BTC-USDT-PERP");
        ++insurance_adl.adl_candidate_rankings;
      }
      if ((i % 16) == 0) {
        ++liquidation_checks;
        if (ex.is_liquidatable(taker, "BTC-USDT-PERP")) {
          const auto liq = ex.liquidate_position("BTC-USDT-PERP", taker, 999999);
          if (liq.ok) ++liquidations;
          else ++rollback_count;
        }
      }
    }
    const auto end = Clock::now();
    const double elapsed_ms = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
    const double seconds = elapsed_ms / 1000.0;

    std::cout << "{"
              << "\"case\":\"mixed_perp_workload\","
              << "\"fast_sim\":" << (args.fast_sim ? "true" : "false") << ","
              << "\"record_events\":" << (retention_options.record_events ? "true" : "false") << ","
              << "\"build_event_payloads\":" << (retention_options.build_event_payloads ? "true" : "false") << ","
              << "\"record_trade_history\":" << (retention_options.record_trade_history ? "true" : "false") << ","
              << "\"update_klines\":" << (retention_options.update_klines ? "true" : "false") << ","
              << "\"record_candle_history\":" << (retention_options.record_candle_history ? "true" : "false") << ","
              << "\"orders\":" << args.orders << ","
              << "\"fills\":" << fills << ","
              << "\"position_updates\":" << fills * 2 << ","
              << "\"margin_checks\":" << margin_checks << ","
              << "\"fees_charged\":" << sum_account_fees(ex, users) << ","
              << "\"funding_settlements\":" << funding_settlements << ","
              << "\"funding_payments\":" << sum_abs_funding(ex, users) << ","
              << "\"liquidation_checks\":" << liquidation_checks << ","
              << "\"liquidations\":" << liquidations << ","
              << "\"rejects\":" << rejected << ","
              << "\"events\":" << ex.events().records().size() << ","
              << "\"trades_retained\":" << ex.trades().size() << ","
              << "\"candles_retained\":" << ex.candles().size() << ","
              << "\"rollback_count\":" << rollback_count << ","
              << "\"funding_failures\":" << funding_failures << ","
              << "\"simulate_fill_calls\":" << simulate_fill_calls << ","
              << "\"simulate_fill_accepts\":" << simulate_fill_accepts << ","
              << "\"simulate_fill_rejects\":" << simulate_fill_rejects << ","
              << "\"insurance_fund_credits\":" << insurance_adl.insurance_fund_credits << ","
              << "\"insurance_fund_debits\":" << insurance_adl.insurance_fund_debits << ","
              << "\"bad_debt_records\":" << insurance_adl.bad_debt_records << ","
              << "\"adl_required_events\":" << insurance_adl.adl_required_events << ","
              << "\"adl_candidate_rankings\":" << insurance_adl.adl_candidate_rankings << ","
              << "\"market_orders\":" << order_trigger.market_orders << ","
              << "\"market_order_rejects\":" << order_trigger.market_order_rejects << ","
              << "\"trigger_orders_created\":" << order_trigger.trigger_orders_created << ","
              << "\"trigger_orders_fired\":" << order_trigger.trigger_orders_fired << ","
              << "\"trigger_orders_cancelled\":" << order_trigger.trigger_orders_cancelled << ","
              << "\"trigger_child_orders\":" << order_trigger.trigger_child_orders << ","
              << "\"trigger_failures\":" << order_trigger.trigger_failures << ","
              << "\"trigger_evaluations\":" << order_trigger.trigger_evaluations << ","
              << "\"elapsed_ms\":" << elapsed_ms << ","
              << "\"orders_per_sec\":" << (seconds > 0.0 ? static_cast<double>(args.orders * 2) / seconds : 0.0) << ","
              << "\"fills_per_sec\":" << (seconds > 0.0 ? static_cast<double>(fills) / seconds : 0.0) << ","
              << "\"settlement_latency_p50_ns\":" << percentile(settlement_latency_ns, 50.0) << ","
              << "\"settlement_latency_p95_ns\":" << percentile(settlement_latency_ns, 95.0) << ","
              << "\"settlement_latency_p99_ns\":" << percentile(settlement_latency_ns, 99.0) << ","
              << "\"rss_mb\":" << rss_mb()
              << "}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "bench_perp error: " << e.what() << "\n";
    return 1;
  }
}
