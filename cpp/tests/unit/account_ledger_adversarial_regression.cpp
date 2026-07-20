#include "lobx/account_ledger.hpp"
#include "test_helpers/test_framework.hpp"

#include <limits>

TEST(AccountLedgerAdversarialRegression, DepositRejectsZeroAndNegativeAmount) {
  lobx::AccountLedger ledger;

  EXPECT_FALSE(ledger.deposit(10, 1, 0).ok);
  EXPECT_FALSE(ledger.deposit(10, 1, -1).ok);
  EXPECT_EQ(ledger.balance(10, 1).total, 0);
}

TEST(AccountLedgerAdversarialRegression, WithdrawRejectsZeroNegativeAndMoreThanFree) {
  lobx::AccountLedger ledger;
  EXPECT_TRUE(ledger.deposit(10, 1, 100).ok);
  const auto before = ledger.balance(10, 1);

  EXPECT_FALSE(ledger.withdraw(10, 1, 0).ok);
  EXPECT_FALSE(ledger.withdraw(10, 1, -1).ok);
  EXPECT_FALSE(ledger.withdraw(10, 1, 101).ok);

  const auto after = ledger.balance(10, 1);
  EXPECT_EQ(after.total, before.total);
  EXPECT_EQ(after.free, before.free);
  EXPECT_EQ(after.locked, before.locked);
}

TEST(AccountLedgerAdversarialRegression, LockRejectsNegativeAndMoreThanFreeWithoutMutation) {
  lobx::AccountLedger ledger;
  EXPECT_TRUE(ledger.deposit(10, 1, 100).ok);
  const auto before = ledger.balance(10, 1);

  EXPECT_FALSE(ledger.lock(10, 1, -1).ok);
  EXPECT_FALSE(ledger.lock(10, 1, 101).ok);

  const auto after = ledger.balance(10, 1);
  EXPECT_EQ(after.total, before.total);
  EXPECT_EQ(after.free, before.free);
  EXPECT_EQ(after.locked, before.locked);
}

TEST(AccountLedgerAdversarialRegression, ReleaseRejectsMoreThanLockedWithoutMutation) {
  lobx::AccountLedger ledger;
  EXPECT_TRUE(ledger.deposit(10, 1, 100).ok);
  EXPECT_TRUE(ledger.lock(10, 1, 40).ok);
  const auto before = ledger.balance(10, 1);

  EXPECT_FALSE(ledger.release(10, 1, 41).ok);

  const auto after = ledger.balance(10, 1);
  EXPECT_EQ(after.total, before.total);
  EXPECT_EQ(after.free, before.free);
  EXPECT_EQ(after.locked, before.locked);
}

TEST(AccountLedgerAdversarialRegression, DebitLockedRejectsMoreThanLockedWithoutMutation) {
  lobx::AccountLedger ledger;
  EXPECT_TRUE(ledger.deposit(10, 1, 100).ok);
  EXPECT_TRUE(ledger.lock(10, 1, 40).ok);
  const auto before = ledger.balance(10, 1);

  EXPECT_FALSE(ledger.debit_locked(10, 1, 41).ok);

  const auto after = ledger.balance(10, 1);
  EXPECT_EQ(after.total, before.total);
  EXPECT_EQ(after.free, before.free);
  EXPECT_EQ(after.locked, before.locked);
}

TEST(AccountLedgerAdversarialRegression, CreditOverflowRejectedWithoutMutation) {
  lobx::AccountLedger ledger;
  EXPECT_TRUE(ledger.deposit(10, 1, std::numeric_limits<lobx::Amount>::max()).ok);
  const auto before = ledger.balance(10, 1);

  EXPECT_FALSE(ledger.credit(10, 1, 1).ok);

  const auto after = ledger.balance(10, 1);
  EXPECT_EQ(after.total, before.total);
  EXPECT_EQ(after.free, before.free);
  EXPECT_EQ(after.locked, before.locked);
}

TEST(AccountLedgerAdversarialRegression, TotalEqualsFreePlusLockedAfterEveryOperation) {
  lobx::AccountLedger ledger;
  EXPECT_TRUE(ledger.deposit(10, 1, 100).ok);
  EXPECT_TRUE(ledger.lock(10, 1, 40).ok);
  EXPECT_TRUE(ledger.release(10, 1, 10).ok);
  EXPECT_TRUE(ledger.debit_locked(10, 1, 20).ok);
  EXPECT_TRUE(ledger.withdraw(10, 1, 5).ok);
  EXPECT_TRUE(ledger.credit(10, 1, 7).ok);

  EXPECT_TRUE(ledger.invariant_ok());
  const auto balance = ledger.balance(10, 1);
  EXPECT_EQ(balance.total, balance.free + balance.locked);
}
