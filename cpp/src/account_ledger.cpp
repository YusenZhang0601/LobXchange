#include "lobx/account_ledger.hpp"

namespace lobx {

WalletBalance& AccountLedger::get_or_create(UserId user, AssetId asset) {
  auto& by_asset = balances_[user];
  auto it = by_asset.find(asset);
  if (it == by_asset.end()) it = by_asset.emplace(asset, WalletBalance{user, asset, 0, 0, 0}).first;
  return it->second;
}

const WalletBalance* AccountLedger::find(UserId user, AssetId asset) const {
  auto uit = balances_.find(user);
  if (uit == balances_.end()) return nullptr;
  auto ait = uit->second.find(asset);
  return ait == uit->second.end() ? nullptr : &ait->second;
}

Result AccountLedger::deposit(UserId user, AssetId asset, Amount amount) {
  if (amount <= 0) return Result::fail(RejectCode::InvalidQuantity, "deposit amount must be positive");
  auto* existing = const_cast<WalletBalance*>(find(user, asset));
  if (existing &&
      (existing->total > std::numeric_limits<Amount>::max() - amount ||
       existing->free > std::numeric_limits<Amount>::max() - amount)) {
    return Result::fail(RejectCode::InvalidQuantity, "deposit overflow");
  }
  auto& b = existing ? *existing : get_or_create(user, asset);
  if (b.total > std::numeric_limits<Amount>::max() - amount || b.free > std::numeric_limits<Amount>::max() - amount) {
    return Result::fail(RejectCode::InvalidQuantity, "deposit overflow");
  }
  b.total += amount;
  b.free += amount;
  return Result::success();
}

Result AccountLedger::withdraw(UserId user, AssetId asset, Amount amount) {
  if (amount <= 0) return Result::fail(RejectCode::InvalidQuantity, "withdraw amount must be positive");
  auto* existing = const_cast<WalletBalance*>(find(user, asset));
  if (!existing) return Result::fail(RejectCode::InsufficientBalance, "insufficient free balance");
  auto& b = *existing;
  if (b.free < amount) return Result::fail(RejectCode::InsufficientBalance, "insufficient free balance");
  b.free -= amount;
  b.total -= amount;
  return Result::success();
}

Result AccountLedger::lock(UserId user, AssetId asset, Amount amount) {
  if (amount < 0) return Result::fail(RejectCode::InvalidQuantity, "lock amount must be non-negative");
  if (amount == 0) return Result::success();
  auto* existing = const_cast<WalletBalance*>(find(user, asset));
  if (!existing) return Result::fail(RejectCode::InsufficientBalance, "insufficient free balance");
  auto& b = *existing;
  if (b.free < amount) return Result::fail(RejectCode::InsufficientBalance, "insufficient free balance");
  b.free -= amount;
  b.locked += amount;
  return Result::success();
}

Result AccountLedger::release(UserId user, AssetId asset, Amount amount) {
  if (amount < 0) return Result::fail(RejectCode::InvalidQuantity, "release amount must be non-negative");
  if (amount == 0) return Result::success();
  auto* existing = const_cast<WalletBalance*>(find(user, asset));
  if (!existing) return Result::fail(RejectCode::InternalError, "release exceeds locked balance");
  auto& b = *existing;
  if (b.locked < amount) return Result::fail(RejectCode::InternalError, "release exceeds locked balance");
  b.locked -= amount;
  b.free += amount;
  return Result::success();
}

Result AccountLedger::debit_locked(UserId user, AssetId asset, Amount amount) {
  if (amount < 0) return Result::fail(RejectCode::InvalidQuantity, "debit amount must be non-negative");
  if (amount == 0) return Result::success();
  auto* existing = const_cast<WalletBalance*>(find(user, asset));
  if (!existing) return Result::fail(RejectCode::InternalError, "debit exceeds locked balance");
  auto& b = *existing;
  if (b.locked < amount || b.total < amount) return Result::fail(RejectCode::InternalError, "debit exceeds locked balance");
  b.locked -= amount;
  b.total -= amount;
  return Result::success();
}

Result AccountLedger::credit(UserId user, AssetId asset, Amount amount) {
  if (amount < 0) return Result::fail(RejectCode::InvalidQuantity, "credit amount must be non-negative");
  if (amount == 0) return Result::success();
  return deposit(user, asset, amount);
}

WalletBalance AccountLedger::balance(UserId user, AssetId asset) const { const auto* b = find(user, asset); return b ? *b : WalletBalance{user, asset, 0, 0, 0}; }
Amount AccountLedger::free(UserId user, AssetId asset) const { return balance(user, asset).free; }
Amount AccountLedger::locked(UserId user, AssetId asset) const { return balance(user, asset).locked; }

bool AccountLedger::invariant_ok() const {
  for (const auto& ukv : balances_) for (const auto& akv : ukv.second) {
    const auto& b = akv.second;
    if (b.total < 0 || b.locked < 0 || b.free < 0) return false;
    if (b.total != b.locked + b.free) return false;
  }
  return true;
}

std::vector<WalletBalance> AccountLedger::balances() const {
  std::vector<WalletBalance> out;
  for (const auto& ukv : balances_) for (const auto& akv : ukv.second) out.push_back(akv.second);
  return out;
}

AccountLedger::Snapshot AccountLedger::snapshot() const {
  return balances_;
}

void AccountLedger::restore(const Snapshot& snapshot) {
  balances_ = snapshot;
}

} // namespace lobx
