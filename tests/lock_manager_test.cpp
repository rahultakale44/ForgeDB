#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "transaction/lock_manager.h"
#include "transaction/transaction.h"

namespace {

using forgedb::transaction::LockManager;
using forgedb::transaction::LockMode;
using forgedb::transaction::Transaction;
using forgedb::transaction::TransactionState;

class LockManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        lock_mgr_ = std::make_unique<LockManager>();
    }
    
    std::unique_ptr<LockManager> lock_mgr_;
};

TEST_F(LockManagerTest, AcquireSharedLock) {
    auto txn = std::make_shared<Transaction>(1);
    
    EXPECT_TRUE(lock_mgr_->lock_shared(txn, 1));
    EXPECT_TRUE(lock_mgr_->holds_lock(txn, 1, LockMode::SHARED));
}

TEST_F(LockManagerTest, AcquireExclusiveLock) {
    auto txn = std::make_shared<Transaction>(1);
    
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn, 1));
    EXPECT_TRUE(lock_mgr_->holds_lock(txn, 1, LockMode::EXCLUSIVE));
}

TEST_F(LockManagerTest, MultipleSharedLocks) {
    auto txn1 = std::make_shared<Transaction>(1);
    auto txn2 = std::make_shared<Transaction>(2);
    
    EXPECT_TRUE(lock_mgr_->lock_shared(txn1, 1));
    EXPECT_TRUE(lock_mgr_->lock_shared(txn2, 1));
    
    EXPECT_TRUE(lock_mgr_->holds_lock(txn1, 1, LockMode::SHARED));
    EXPECT_TRUE(lock_mgr_->holds_lock(txn2, 1, LockMode::SHARED));
}

TEST_F(LockManagerTest, ExclusiveLockBlocksShared) {
    auto txn1 = std::make_shared<Transaction>(1);
    auto txn2 = std::make_shared<Transaction>(2);
    
    // Transaction 1 gets exclusive lock
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn1, 1));
    
    bool txn2_acquired = false;
    
    // Transaction 2 tries to get shared lock (should block)
    std::thread t2([&]() {
        txn2_acquired = lock_mgr_->lock_shared(txn2, 1);
    });
    
    // Give t2 time to attempt lock
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // txn2 should not have acquired lock yet
    EXPECT_FALSE(txn2_acquired);
    
    // Release txn1's lock
    EXPECT_TRUE(lock_mgr_->unlock(txn1, 1));
    
    // Now txn2 should acquire lock
    t2.join();
    EXPECT_TRUE(txn2_acquired);
}

TEST_F(LockManagerTest, SharedLockBlocksExclusive) {
    auto txn1 = std::make_shared<Transaction>(1);
    auto txn2 = std::make_shared<Transaction>(2);
    
    // Transaction 1 gets shared lock
    EXPECT_TRUE(lock_mgr_->lock_shared(txn1, 1));
    
    bool txn2_acquired = false;
    
    // Transaction 2 tries to get exclusive lock (should block)
    std::thread t2([&]() {
        txn2_acquired = lock_mgr_->lock_exclusive(txn2, 1);
    });
    
    // Give t2 time to attempt lock
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // txn2 should not have acquired lock yet
    EXPECT_FALSE(txn2_acquired);
    
    // Release txn1's lock
    EXPECT_TRUE(lock_mgr_->unlock(txn1, 1));
    
    // Now txn2 should acquire lock
    t2.join();
    EXPECT_TRUE(txn2_acquired);
}

TEST_F(LockManagerTest, UnlockReleasesLock) {
    auto txn = std::make_shared<Transaction>(1);
    
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn, 1));
    EXPECT_TRUE(lock_mgr_->holds_lock(txn, 1, LockMode::EXCLUSIVE));
    
    EXPECT_TRUE(lock_mgr_->unlock(txn, 1));
    EXPECT_FALSE(lock_mgr_->holds_lock(txn, 1, LockMode::EXCLUSIVE));
}

TEST_F(LockManagerTest, UnlockAllReleasesAllLocks) {
    auto txn = std::make_shared<Transaction>(1);
    
    EXPECT_TRUE(lock_mgr_->lock_shared(txn, 1));
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn, 2));
    EXPECT_TRUE(lock_mgr_->lock_shared(txn, 3));
    
    lock_mgr_->unlock_all(txn);
    
    EXPECT_FALSE(lock_mgr_->holds_lock(txn, 1, LockMode::SHARED));
    EXPECT_FALSE(lock_mgr_->holds_lock(txn, 2, LockMode::EXCLUSIVE));
    EXPECT_FALSE(lock_mgr_->holds_lock(txn, 3, LockMode::SHARED));
}

TEST_F(LockManagerTest, UpgradeSharedToExclusive) {
    auto txn = std::make_shared<Transaction>(1);
    
    // Acquire shared lock
    EXPECT_TRUE(lock_mgr_->lock_shared(txn, 1));
    EXPECT_TRUE(lock_mgr_->holds_lock(txn, 1, LockMode::SHARED));
    
    // Upgrade to exclusive
    EXPECT_TRUE(lock_mgr_->lock_upgrade(txn, 1));
    EXPECT_TRUE(lock_mgr_->holds_lock(txn, 1, LockMode::EXCLUSIVE));
}

TEST_F(LockManagerTest, UpgradeBlockedByOtherSharedLock) {
    auto txn1 = std::make_shared<Transaction>(1);
    auto txn2 = std::make_shared<Transaction>(2);
    
    // Both acquire shared locks
    EXPECT_TRUE(lock_mgr_->lock_shared(txn1, 1));
    EXPECT_TRUE(lock_mgr_->lock_shared(txn2, 1));
    
    bool upgraded = false;
    
    // txn1 tries to upgrade (should block on txn2's shared lock)
    std::thread t1([&]() {
        upgraded = lock_mgr_->lock_upgrade(txn1, 1);
    });
    
    // Give t1 time to attempt upgrade
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // Upgrade should not have succeeded yet
    EXPECT_FALSE(upgraded);
    
    // Release txn2's lock
    EXPECT_TRUE(lock_mgr_->unlock(txn2, 1));
    
    // Now upgrade should succeed
    t1.join();
    EXPECT_TRUE(upgraded);
}

TEST_F(LockManagerTest, MultiplePagesIndependentLocks) {
    auto txn1 = std::make_shared<Transaction>(1);
    auto txn2 = std::make_shared<Transaction>(2);
    
    // Different pages can be locked independently
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn1, 1));
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn2, 2));
    
    EXPECT_TRUE(lock_mgr_->holds_lock(txn1, 1, LockMode::EXCLUSIVE));
    EXPECT_TRUE(lock_mgr_->holds_lock(txn2, 2, LockMode::EXCLUSIVE));
}

TEST_F(LockManagerTest, InactiveTransactionCannotAcquireLock) {
    auto txn = std::make_shared<Transaction>(1);
    txn->set_state(TransactionState::ABORTED);
    
    EXPECT_FALSE(lock_mgr_->lock_shared(txn, 1));
    EXPECT_FALSE(lock_mgr_->lock_exclusive(txn, 1));
}

TEST_F(LockManagerTest, SharedLockIdempotent) {
    auto txn = std::make_shared<Transaction>(1);
    
    // Acquiring same shared lock multiple times should succeed
    EXPECT_TRUE(lock_mgr_->lock_shared(txn, 1));
    EXPECT_TRUE(lock_mgr_->lock_shared(txn, 1));
    EXPECT_TRUE(lock_mgr_->lock_shared(txn, 1));
}

TEST_F(LockManagerTest, ExclusiveLockIdempotent) {
    auto txn = std::make_shared<Transaction>(1);
    
    // Acquiring same exclusive lock multiple times should succeed
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn, 1));
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn, 1));
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn, 1));
}

TEST_F(LockManagerTest, ConcurrentSharedLocks) {
    const int num_threads = 10;
    std::vector<std::thread> threads;
    std::vector<bool> results(num_threads, false);
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&, i]() {
            auto txn = std::make_shared<Transaction>(i + 1);
            results[i] = lock_mgr_->lock_shared(txn, 1);
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // All should succeed
    for (int i = 0; i < num_threads; ++i) {
        EXPECT_TRUE(results[i]);
    }
}

TEST_F(LockManagerTest, SequentialExclusiveLocks) {
    const int num_txns = 5;
    std::vector<std::shared_ptr<Transaction>> txns;
    
    for (int i = 0; i < num_txns; ++i) {
        txns.push_back(std::make_shared<Transaction>(i + 1));
    }
    
    // Each transaction locks, does work, then unlocks
    for (auto& txn : txns) {
        EXPECT_TRUE(lock_mgr_->lock_exclusive(txn, 1));
        EXPECT_TRUE(lock_mgr_->holds_lock(txn, 1, LockMode::EXCLUSIVE));
        EXPECT_TRUE(lock_mgr_->unlock(txn, 1));
    }
}

TEST_F(LockManagerTest, UnlockAllAllowsOtherTransaction) {
    auto txn1 = std::make_shared<Transaction>(1);
    auto txn2 = std::make_shared<Transaction>(2);
    
    // txn1 locks multiple pages
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn1, 1));
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn1, 2));
    
    bool txn2_acquired = false;
    
    // txn2 tries to lock page 1 (should block)
    std::thread t2([&]() {
        txn2_acquired = lock_mgr_->lock_exclusive(txn2, 1);
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(txn2_acquired);
    
    // Release all of txn1's locks
    lock_mgr_->unlock_all(txn1);
    
    t2.join();
    EXPECT_TRUE(txn2_acquired);
}

TEST_F(LockManagerTest, ExclusiveAfterExclusiveIdempotent) {
    auto txn = std::make_shared<Transaction>(1);
    
    // Get exclusive lock
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn, 1));
    
    // Try to get exclusive again (should succeed immediately)
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn, 1));
    
    EXPECT_TRUE(lock_mgr_->holds_lock(txn, 1, LockMode::EXCLUSIVE));
}

TEST_F(LockManagerTest, SharedAfterExclusiveSucceeds) {
    auto txn = std::make_shared<Transaction>(1);
    
    // Get exclusive lock first
    EXPECT_TRUE(lock_mgr_->lock_exclusive(txn, 1));
    
    // Try to get shared lock (already have exclusive, so this is fine)
    EXPECT_TRUE(lock_mgr_->lock_shared(txn, 1));
    
    EXPECT_TRUE(lock_mgr_->holds_lock(txn, 1, LockMode::EXCLUSIVE));
}

}  // namespace
