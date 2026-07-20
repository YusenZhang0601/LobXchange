#pragma once

#include <cstdint>
#include <string>

#include "lobx/types.hpp"

namespace lobx::agents {

using AgentId = std::uint64_t;
using AgentGroupId = std::uint32_t;
using ClientActionId = std::uint64_t;
using ClientOrderId = std::uint64_t;
using Timestamp = lob::Timestamp;
using Side = lob::Side;
using Price = lob::Tick;
using Quantity = lob::Quantity;

enum class AgentType : std::uint8_t {
  StaticMarketMaker = 0,
  NoiseTrader = 1,
  MomentumFollower = 2,
  MeanReverter = 3,
  WhaleSweeper = 4,
  DynamicMarketMaker = 5,
  LiquidityTaker = 6,
  AdversarialSweeper = 7,
  LiquidityWithdrawer = 8,
  Unknown = 255,
};

enum class TimeInForce : std::uint8_t {
  Gtc = 0,
  Ioc = 1,
  Fok = 2,
};

const char* agent_type_name(AgentType type);
AgentType agent_type_from_name(const std::string& name);
const char* side_name(Side side);

} // namespace lobx::agents
