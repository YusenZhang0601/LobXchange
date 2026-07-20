#pragma once

#include "lobx/account_ledger.hpp"
#include "lobx/market_registry.hpp"
#include "lobx/position_engine.hpp"

namespace lobx {

struct RiskDecision {
  bool accepted{false};
  RejectCode code{RejectCode::None};
  std::string reason;
  AssetId lock_asset{0};
  Amount lock_amount{0};
};

class RiskEngine {
public:
  RiskDecision check_limit_order(const OrderRequest& req,
                                 const Market& market,
                                 const AccountLedger& ledger,
                                 const PositionEngine* positions,
                                 int leverage,
                                 lob::Tick best_bid,
                                 lob::Tick best_ask,
                                 bool duplicate_order_id) const;
};

} // namespace lobx
