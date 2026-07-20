#pragma once

#include "lobx/types.hpp"

#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace lobx_test {

enum class RandomOperationType {
  SubmitLimit,
  SubmitIoc,
  SubmitFok,
  SubmitPostOnly,
  SubmitReduceOnly,
  Cancel,
  Halt,
  Activate,
  PerpOpen,
  PerpClose,
  MarkPriceUpdate,
};

struct RandomOperation {
  RandomOperationType type{RandomOperationType::SubmitLimit};
  lobx::UserId user{0};
  lobx::OrderId order_id{0};
  lob::Side side{lob::Side::Bid};
  lob::Tick price{0};
  lob::Quantity qty{0};
  uint32_t flags{lob::NONE};

  std::string describe() const {
    std::ostringstream os;
    os << "type=" << static_cast<int>(type)
       << " user=" << user
       << " order_id=" << order_id
       << " side=" << (side == lob::Side::Bid ? "BUY" : "SELL")
       << " price=" << price
       << " qty=" << qty
       << " flags=" << flags;
    return os.str();
  }
};

class RandomOrderGenerator {
public:
  explicit RandomOrderGenerator(uint64_t seed) : seed_(seed), rng_(seed) {}

  uint64_t seed() const { return seed_; }

  RandomOperation next_spot_operation(lobx::OrderId order_id) {
    std::uniform_int_distribution<int> type_dist(0, 4);
    std::uniform_int_distribution<int> user_dist(0, 2);
    std::uniform_int_distribution<int> side_dist(0, 1);
    std::uniform_int_distribution<int> price_dist(95, 105);
    std::uniform_int_distribution<int> qty_dist(1, 10);

    RandomOperation op;
    op.order_id = order_id;
    op.user = users_[static_cast<size_t>(user_dist(rng_))];
    op.side = side_dist(rng_) == 0 ? lob::Side::Bid : lob::Side::Ask;
    op.price = price_dist(rng_);
    op.qty = qty_dist(rng_);

    switch (type_dist(rng_)) {
      case 1: op.type = RandomOperationType::SubmitIoc; op.flags = lob::IOC; break;
      case 2: op.type = RandomOperationType::SubmitFok; op.flags = lob::FOK; break;
      case 3: op.type = RandomOperationType::SubmitPostOnly; op.flags = lob::POST_ONLY; break;
      case 4: op.type = RandomOperationType::SubmitLimit; op.flags = lob::STP; break;
      default: op.type = RandomOperationType::SubmitLimit; op.flags = lob::NONE; break;
    }
    return op;
  }

  RandomOperation next_operation(lobx::OrderId order_id) {
    std::uniform_int_distribution<int> type_dist(0, 10);
    RandomOperation op = next_spot_operation(order_id);
    switch (type_dist(rng_)) {
      case 0: op.type = RandomOperationType::SubmitLimit; op.flags = lob::NONE; break;
      case 1: op.type = RandomOperationType::SubmitIoc; op.flags = lob::IOC; break;
      case 2: op.type = RandomOperationType::SubmitFok; op.flags = lob::FOK; break;
      case 3: op.type = RandomOperationType::SubmitPostOnly; op.flags = lob::POST_ONLY; break;
      case 4: op.type = RandomOperationType::SubmitReduceOnly; op.flags = lobx::LOBX_REDUCE_ONLY | lob::IOC; break;
      case 5: op.type = RandomOperationType::Cancel; break;
      case 6: op.type = RandomOperationType::Halt; break;
      case 7: op.type = RandomOperationType::Activate; break;
      case 8: op.type = RandomOperationType::PerpOpen; op.flags = lob::IOC; break;
      case 9: op.type = RandomOperationType::PerpClose; op.flags = lobx::LOBX_REDUCE_ONLY | lob::IOC; break;
      case 10: op.type = RandomOperationType::MarkPriceUpdate; break;
      default: break;
    }
    return op;
  }

private:
  uint64_t seed_;
  std::mt19937_64 rng_;
  std::vector<lobx::UserId> users_{10, 20, 30};
};

} // namespace lobx_test
