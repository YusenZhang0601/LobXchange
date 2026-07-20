#pragma once

#include "lobx/exchange.hpp"
#include "lobx/market_engine.hpp"

#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <string>

namespace lobx_test {

using MarketSymbol = std::string;
using AssetSymbol = std::string;
using Decimal = lobx::Amount;

struct ExchangeFixture {
  lobx::Exchange exchange;

  lobx::UserId issuer{1};
  lobx::UserId alice{10};
  lobx::UserId bob{20};
  lobx::UserId carol{30};

  MarketSymbol spot_symbol{"BTC-USDT"};
  MarketSymbol eth_spot_symbol{"ETH-USDT"};
  MarketSymbol perp_symbol{"BTC-USDT-PERP"};
  lobx::MarketId spot_market_id{0};
  lobx::MarketId eth_spot_market_id{0};
  lobx::MarketId perp_market_id{0};

  lob::Tick tick_size{1};
  lob::Quantity lot_size{1};
  lob::Quantity min_qty{1};
  lobx::Amount min_notional{1};

  static ExchangeFixture Spot() {
    ExchangeFixture f;
    f.issue_default_assets();
    f.require(f.exchange.create_spot_market(f.spot_symbol, "BTC", "USDT", f.tick_size, f.lot_size, f.min_qty, f.min_notional, &f.spot_market_id), "create BTC spot");
    f.require(f.exchange.create_spot_market(f.eth_spot_symbol, "ETH", "USDT", f.tick_size, f.lot_size, f.min_qty, f.min_notional, &f.eth_spot_market_id), "create ETH spot");
    f.deposit_default_wallets();
    return f;
  }

  static ExchangeFixture Perp() {
    ExchangeFixture f;
    f.issue_default_assets();
    f.require(f.exchange.create_perpetual_market(f.perp_symbol, "BTC", "USDT", "USDT", f.tick_size, f.lot_size, f.min_qty, f.min_notional, 10, &f.perp_market_id), "create BTC perp");
    f.deposit_default_wallets();
    f.exchange.set_leverage(f.alice, f.perp_symbol, 5);
    f.exchange.set_leverage(f.bob, f.perp_symbol, 5);
    f.exchange.set_leverage(f.carol, f.perp_symbol, 5);
    return f;
  }

  static ExchangeFixture SpotAndPerp() {
    ExchangeFixture f;
    f.issue_default_assets();
    f.require(f.exchange.create_spot_market(f.spot_symbol, "BTC", "USDT", f.tick_size, f.lot_size, f.min_qty, f.min_notional, &f.spot_market_id), "create BTC spot");
    f.require(f.exchange.create_spot_market(f.eth_spot_symbol, "ETH", "USDT", f.tick_size, f.lot_size, f.min_qty, f.min_notional, &f.eth_spot_market_id), "create ETH spot");
    f.require(f.exchange.create_perpetual_market(f.perp_symbol, "BTC", "USDT", "USDT", f.tick_size, f.lot_size, f.min_qty, f.min_notional, 10, &f.perp_market_id), "create BTC perp");
    f.deposit_default_wallets();
    f.exchange.set_leverage(f.alice, f.perp_symbol, 5);
    f.exchange.set_leverage(f.bob, f.perp_symbol, 5);
    f.exchange.set_leverage(f.carol, f.perp_symbol, 5);
    return f;
  }

  void deposit(lobx::UserId user, const AssetSymbol& asset, Decimal amount) {
    require(exchange.deposit(user, asset, amount), "deposit " + asset);
  }

  void set_leverage(lobx::UserId user, int leverage) {
    exchange.set_leverage(user, perp_symbol, leverage);
  }

  std::string wallet_summary(lobx::UserId user) const {
    const auto usdt = const_cast<lobx::Exchange&>(exchange).balance(user, "USDT");
    const auto btc = const_cast<lobx::Exchange&>(exchange).balance(user, "BTC");
    std::ostringstream os;
    os << "user=" << user
       << " USDT(total=" << usdt.total << ",free=" << usdt.free << ",locked=" << usdt.locked << ")"
       << " BTC(total=" << btc.total << ",free=" << btc.free << ",locked=" << btc.locked << ")";
    return os.str();
  }

private:
  void issue_default_assets() {
    require(exchange.issue_asset("USDT", 6, 1000000000000000LL, issuer, 0), "issue USDT");
    require(exchange.issue_asset("BTC", 8, 1000000000LL, issuer, 0), "issue BTC");
    require(exchange.issue_asset("ETH", 8, 1000000000LL, issuer, 0), "issue ETH");
  }

  void deposit_default_wallets() {
    for (lobx::UserId user : {alice, bob, carol}) {
      deposit(user, "USDT", 1000000LL);
      deposit(user, "BTC", 1000000LL);
      deposit(user, "ETH", 1000000LL);
    }
  }

  void require(const lobx::Result& result, const std::string& context) {
    if (result.ok) return;
    std::ostringstream os;
    os << "fixture setup failed: " << context << ": " << result.reason;
    throw std::runtime_error(os.str());
  }
};

struct SpotEngineFixture {
  lobx::UserId alice{10};
  lobx::UserId bob{20};
  lobx::UserId carol{30};
  lobx::AssetId base_asset{1};
  lobx::AssetId quote_asset{2};

  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  lobx::PositionEngine positions;
  lobx::EventStore events;
  lobx::Market market;
  lobx::MarketEngine engine;

  explicit SpotEngineFixture(int maker_fee_bps = 0, int taker_fee_bps = 0)
      : market{1,
               "BTC-USDT",
               base_asset,
               quote_asset,
               quote_asset,
               lobx::MarketType::Spot,
               lobx::MarketStatus::Active,
               1,
               1,
               1,
               1,
               maker_fee_bps,
               taker_fee_bps,
               1},
        engine(market, ledger, risk, &positions, &events) {
    for (lobx::UserId user : {alice, bob, carol}) {
      require(ledger.deposit(user, base_asset, 1000000LL), "deposit base");
      require(ledger.deposit(user, quote_asset, 1000000LL), "deposit quote");
    }
  }

  lobx::SubmitResult submit(lobx::UserId user, lobx::OrderId order_id, lob::Side side,
                            lob::Tick price, lob::Quantity qty, uint32_t flags = lob::NONE, lob::Timestamp ts = 0) {
    lobx::OrderRequest req{market.id, user, order_id, events.next_seq(), ts, side, price, qty, flags};
    return engine.submit_limit(req);
  }

  std::string ledger_summary(lobx::UserId user) const {
    const auto base = ledger.balance(user, base_asset);
    const auto quote = ledger.balance(user, quote_asset);
    std::ostringstream os;
    os << "user=" << user
       << " base(total=" << base.total << ",free=" << base.free << ",locked=" << base.locked << ")"
       << " quote(total=" << quote.total << ",free=" << quote.free << ",locked=" << quote.locked << ")";
    return os.str();
  }

private:
  void require(const lobx::Result& result, const std::string& context) {
    if (result.ok) return;
    throw std::runtime_error("spot engine fixture setup failed: " + context + ": " + result.reason);
  }
};

struct PerpEngineFixture {
  lobx::UserId alice{10};
  lobx::UserId bob{20};
  lobx::UserId carol{30};
  lobx::AssetId base_asset{1};
  lobx::AssetId quote_asset{2};
  lobx::AssetId margin_asset{2};

  lobx::AccountLedger ledger;
  lobx::RiskEngine risk;
  lobx::PositionEngine positions;
  lobx::EventStore events;
  lobx::Market market;
  lobx::MarketEngine engine;

  explicit PerpEngineFixture(lobx::Amount initial_margin = 1000000LL)
      : market{1,
               "BTC-USDT-PERP",
               base_asset,
               quote_asset,
               margin_asset,
               lobx::MarketType::Perpetual,
               lobx::MarketStatus::Active,
               1,
               1,
               1,
               1,
               0,
               0,
               10},
        engine(market, ledger, risk, &positions, &events) {
    for (lobx::UserId user : {alice, bob, carol}) {
      require(ledger.deposit(user, margin_asset, initial_margin), "deposit margin");
      positions.set_leverage(user, market.id, 5);
    }
  }

  lobx::SubmitResult submit(lobx::UserId user, lobx::OrderId order_id, lob::Side side,
                            lob::Tick price, lob::Quantity qty, uint32_t flags = lob::NONE, lob::Timestamp ts = 0) {
    lobx::OrderRequest req{market.id, user, order_id, events.next_seq(), ts, side, price, qty, flags};
    return engine.submit_limit(req);
  }

  std::string margin_summary(lobx::UserId user) const {
    const auto balance = ledger.balance(user, margin_asset);
    const auto position = positions.position(user, market.id);
    std::ostringstream os;
    os << "user=" << user
       << " margin(total=" << balance.total << ",free=" << balance.free << ",locked=" << balance.locked << ")"
       << " position(qty=" << position.signed_qty << ",entry=" << position.entry_price
       << ",realized=" << position.realized_pnl << ")";
    return os.str();
  }

private:
  void require(const lobx::Result& result, const std::string& context) {
    if (result.ok) return;
    throw std::runtime_error("perp engine fixture setup failed: " + context + ": " + result.reason);
  }
};

} // namespace lobx_test
