#pragma once

#include <unordered_map>
#include <vector>

#include "lobx/types.hpp"

namespace lobx {

struct WalletBalance {
  UserId user{0};
  AssetId asset{0};
  Amount total{0};
  Amount locked{0};
  Amount free{0};
};

class AccountLedger {
public:
  using Snapshot = std::unordered_map<UserId, std::unordered_map<AssetId, WalletBalance>>;

  Result deposit(UserId user, AssetId asset, Amount amount);
  Result withdraw(UserId user, AssetId asset, Amount amount);
  Result lock(UserId user, AssetId asset, Amount amount);
  Result release(UserId user, AssetId asset, Amount amount);
  Result debit_locked(UserId user, AssetId asset, Amount amount);
  Result credit(UserId user, AssetId asset, Amount amount);

  WalletBalance balance(UserId user, AssetId asset) const;
  Amount free(UserId user, AssetId asset) const;
  Amount locked(UserId user, AssetId asset) const;
  bool invariant_ok() const;
  std::vector<WalletBalance> balances() const;
  Snapshot snapshot() const;
  void restore(const Snapshot& snapshot);

private:
  WalletBalance& get_or_create(UserId user, AssetId asset);
  const WalletBalance* find(UserId user, AssetId asset) const;
  std::unordered_map<UserId, std::unordered_map<AssetId, WalletBalance>> balances_;
};

} // namespace lobx
