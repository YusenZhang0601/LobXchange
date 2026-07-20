#pragma once

#include "test_helpers/invariant_checker.hpp"

#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace lobx_test {

class ScenarioBuilder {
public:
  explicit ScenarioBuilder(lobx::Exchange& exchange) : exchange_(exchange) {}

  class OrderStep {
  public:
    OrderStep(ScenarioBuilder& builder, lobx::UserId user, std::string symbol, lob::Side side, lob::Quantity qty, lob::Tick price)
        : builder_(builder), user_(user), symbol_(std::move(symbol)), side_(side), qty_(qty), price_(price) {}

    OrderStep& ioc() { flags_ |= lob::IOC; return *this; }
    OrderStep& fok() { flags_ |= lob::FOK; return *this; }
    OrderStep& post_only() { flags_ |= lob::POST_ONLY; return *this; }
    OrderStep& reduce_only() { flags_ |= lobx::LOBX_REDUCE_ONLY; return *this; }
    OrderStep& stp_cancel_newest() { flags_ |= lob::STP; return *this; }

    lobx::SubmitResult gtc() { return submit(); }
    lobx::SubmitResult submit() {
      const lobx::OrderId id = builder_.next_order_id_++;
      builder_.record(user_, id, symbol_, side_, price_, qty_, flags_);
      return builder_.exchange_.submit_limit(symbol_, user_, id, side_, price_, qty_, flags_, builder_.next_ts_++);
    }

  private:
    ScenarioBuilder& builder_;
    lobx::UserId user_;
    std::string symbol_;
    lob::Side side_;
    lob::Quantity qty_;
    lob::Tick price_;
    uint32_t flags_{lob::NONE};
  };

  class UserStep {
  public:
    UserStep(ScenarioBuilder& builder, lobx::UserId user) : builder_(builder), user_(user) {}
    OrderStep buy(const std::string& symbol, lob::Quantity qty, lob::Tick price) { return OrderStep(builder_, user_, symbol, lob::Side::Bid, qty, price); }
    OrderStep sell(const std::string& symbol, lob::Quantity qty, lob::Tick price) { return OrderStep(builder_, user_, symbol, lob::Side::Ask, qty, price); }

  private:
    ScenarioBuilder& builder_;
    lobx::UserId user_;
  };

  UserStep user(lobx::UserId user) { return UserStep(*this, user); }

  void expect_no_self_trade() const {
    for (const auto& trade : exchange_.trades()) {
      if (trade.buyer == trade.seller) throw std::runtime_error("self trade detected\n" + sequence());
    }
  }

  void expect_no_resting_order(lob::Side side, const std::string& symbol, lob::Tick price) {
    for (const auto& level : exchange_.topN(symbol, side, 1000)) {
      if (level.first == price) throw std::runtime_error("unexpected resting order at price=" + std::to_string(price) + "\n" + sequence());
    }
  }

  void expect_invariants_hold() const {
    try {
      require_invariants(exchange_);
    } catch (const std::exception& e) {
      throw std::runtime_error(std::string(e.what()) + "\n" + sequence());
    }
  }

  std::string sequence() const {
    std::ostringstream os;
    os << "order sequence:\n";
    for (const auto& line : sequence_) os << "  " << line << '\n';
    return os.str();
  }

private:
  void record(lobx::UserId user, lobx::OrderId order_id, const std::string& symbol, lob::Side side,
              lob::Tick price, lob::Quantity qty, uint32_t flags) {
    std::ostringstream os;
    os << "user=" << user << " symbol=" << symbol << " order_id=" << order_id
       << " side=" << (side == lob::Side::Bid ? "BUY" : "SELL")
       << " price=" << price << " qty=" << qty << " flags=" << flags;
    sequence_.push_back(os.str());
  }

  lobx::Exchange& exchange_;
  lobx::OrderId next_order_id_{1000};
  lob::Timestamp next_ts_{1};
  std::vector<std::string> sequence_;
};

} // namespace lobx_test
