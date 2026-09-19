#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "transaction/transaction.h"
#include "transaction/transaction_manager.h"

namespace {

using forgedb::transaction::Transaction;
using forgedb::transaction::TransactionId;
using forgedb::transaction::TransactionManager;
using forgedb::transaction::TransactionState;
using forgedb::transaction::INVALID_TXN_ID;

TEST(TransactionTest, CreatesTransaction) {
    Transaction txn(1);
    
    EXPECT_EQ(txn.transaction_id(), 1);
    EXPECT_EQ(txn.state(), TransactionState::ACTIVE);
    EXPECT_TRUE(txn.is_active());
    EXPECT_FALSE(txn.is_committed());
    EXPECT_FALSE(txn.is_aborted());
}

TEST(TransactionTest, CommitsTransaction) {
    Transaction txn(1);
    
    txn.set_state(TransactionState::COMMITTED);
    
    EXPECT_EQ(txn.state(), TransactionState::COMMITTED);
    EXPECT_FALSE(txn.is_active());
    EXPECT_TRUE(txn.is_committed());
    EXPECT_FALSE(txn.is_aborted());
}

TEST(TransactionTest, AbortsTransaction) {
    Transaction txn(1);
    
    txn.set_state(TransactionState::ABORTED);
    
    EXPECT_EQ(txn.state(), TransactionState::ABORTED);
    EXPECT_FALSE(txn.is_active());
    EXPECT_FALSE(txn.is_committed());
    EXPECT_TRUE(txn.is_aborted());
}

TEST(TransactionManagerTest, BeginsTransaction) {
    TransactionManager txn_mgr;
    
    auto txn = txn_mgr.begin_transaction();
    
    ASSERT_NE(txn, nullptr);
    EXPECT_GT(txn->transaction_id(), INVALID_TXN_ID);
    EXPECT_TRUE(txn->is_active());
    EXPECT_EQ(txn_mgr.active_transaction_count(), 1);
}

TEST(TransactionManagerTest, AssignsUniqueTransactionIds) {
    TransactionManager txn_mgr;
    
    auto txn1 = txn_mgr.begin_transaction();
    auto txn2 = txn_mgr.begin_transaction();
    auto txn3 = txn_mgr.begin_transaction();
    
    EXPECT_NE(txn1->transaction_id(), txn2->transaction_id());
    EXPECT_NE(txn2->transaction_id(), txn3->transaction_id());
    EXPECT_NE(txn1->transaction_id(), txn3->transaction_id());
    
    EXPECT_LT(txn1->transaction_id(), txn2->transaction_id());
    EXPECT_LT(txn2->transaction_id(), txn3->transaction_id());
}

TEST(TransactionManagerTest, CommitsTransaction) {
    TransactionManager txn_mgr;
    
    auto txn = txn_mgr.begin_transaction();
    EXPECT_EQ(txn_mgr.active_transaction_count(), 1);
    
    bool committed = txn_mgr.commit(txn);
    
    EXPECT_TRUE(committed);
    EXPECT_TRUE(txn->is_committed());
    EXPECT_EQ(txn_mgr.active_transaction_count(), 0);
}

TEST(TransactionManagerTest, AbortsTransaction) {
    TransactionManager txn_mgr;
    
    auto txn = txn_mgr.begin_transaction();
    EXPECT_EQ(txn_mgr.active_transaction_count(), 1);
    
    bool aborted = txn_mgr.abort(txn);
    
    EXPECT_TRUE(aborted);
    EXPECT_TRUE(txn->is_aborted());
    EXPECT_EQ(txn_mgr.active_transaction_count(), 0);
}

TEST(TransactionManagerTest, RejectsCommitOnNullTransaction) {
    TransactionManager txn_mgr;
    
    bool committed = txn_mgr.commit(nullptr);
    
    EXPECT_FALSE(committed);
}

TEST(TransactionManagerTest, RejectsAbortOnNullTransaction) {
    TransactionManager txn_mgr;
    
    bool aborted = txn_mgr.abort(nullptr);
    
    EXPECT_FALSE(aborted);
}

TEST(TransactionManagerTest, RejectsDoubleCommit) {
    TransactionManager txn_mgr;
    
    auto txn = txn_mgr.begin_transaction();
    
    EXPECT_TRUE(txn_mgr.commit(txn));
    EXPECT_FALSE(txn_mgr.commit(txn));
}

TEST(TransactionManagerTest, RejectsDoubleAbort) {
    TransactionManager txn_mgr;
    
    auto txn = txn_mgr.begin_transaction();
    
    EXPECT_TRUE(txn_mgr.abort(txn));
    EXPECT_FALSE(txn_mgr.abort(txn));
}

TEST(TransactionManagerTest, RejectsCommitAfterAbort) {
    TransactionManager txn_mgr;
    
    auto txn = txn_mgr.begin_transaction();
    
    EXPECT_TRUE(txn_mgr.abort(txn));
    EXPECT_FALSE(txn_mgr.commit(txn));
}

TEST(TransactionManagerTest, RejectsAbortAfterCommit) {
    TransactionManager txn_mgr;
    
    auto txn = txn_mgr.begin_transaction();
    
    EXPECT_TRUE(txn_mgr.commit(txn));
    EXPECT_FALSE(txn_mgr.abort(txn));
}

TEST(TransactionManagerTest, GetsActiveTransaction) {
    TransactionManager txn_mgr;
    
    auto txn = txn_mgr.begin_transaction();
    TransactionId txn_id = txn->transaction_id();
    
    auto retrieved_txn = txn_mgr.get_transaction(txn_id);
    
    ASSERT_NE(retrieved_txn, nullptr);
    EXPECT_EQ(retrieved_txn->transaction_id(), txn_id);
}

TEST(TransactionManagerTest, ReturnsNullForCompletedTransaction) {
    TransactionManager txn_mgr;
    
    auto txn = txn_mgr.begin_transaction();
    TransactionId txn_id = txn->transaction_id();
    
    txn_mgr.commit(txn);
    
    auto retrieved_txn = txn_mgr.get_transaction(txn_id);
    
    EXPECT_EQ(retrieved_txn, nullptr);
}

TEST(TransactionManagerTest, ReturnsNullForInvalidTransactionId) {
    TransactionManager txn_mgr;
    
    auto txn = txn_mgr.get_transaction(999999);
    
    EXPECT_EQ(txn, nullptr);
}

TEST(TransactionManagerTest, HandlesMultipleActiveTransactions) {
    TransactionManager txn_mgr;
    
    auto txn1 = txn_mgr.begin_transaction();
    auto txn2 = txn_mgr.begin_transaction();
    auto txn3 = txn_mgr.begin_transaction();
    
    EXPECT_EQ(txn_mgr.active_transaction_count(), 3);
    
    txn_mgr.commit(txn2);
    EXPECT_EQ(txn_mgr.active_transaction_count(), 2);
    
    txn_mgr.abort(txn1);
    EXPECT_EQ(txn_mgr.active_transaction_count(), 1);
    
    txn_mgr.commit(txn3);
    EXPECT_EQ(txn_mgr.active_transaction_count(), 0);
}

TEST(TransactionManagerTest, ThreadSafeTransactionCreation) {
    TransactionManager txn_mgr;
    
    const int num_threads = 10;
    const int txns_per_thread = 100;
    
    std::vector<std::thread> threads;
    std::vector<std::vector<TransactionId>> txn_ids(num_threads);
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&txn_mgr, &txn_ids, i, txns_per_thread]() {
            for (int j = 0; j < txns_per_thread; ++j) {
                auto txn = txn_mgr.begin_transaction();
                txn_ids[i].push_back(txn->transaction_id());
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify all transaction IDs are unique
    std::unordered_set<TransactionId> all_ids;
    for (const auto& ids : txn_ids) {
        for (TransactionId id : ids) {
            EXPECT_TRUE(all_ids.insert(id).second);
        }
    }
    
    EXPECT_EQ(all_ids.size(), num_threads * txns_per_thread);
}

TEST(TransactionManagerTest, ThreadSafeCommitAbort) {
    TransactionManager txn_mgr;
    
    const int num_threads = 10;
    std::vector<std::thread> threads;
    
    // Create transactions
    std::vector<std::shared_ptr<Transaction>> txns;
    for (int i = 0; i < num_threads * 2; ++i) {
        txns.push_back(txn_mgr.begin_transaction());
    }
    
    // Commit/abort transactions concurrently
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&txn_mgr, &txns, i]() {
            txn_mgr.commit(txns[i * 2]);
            txn_mgr.abort(txns[i * 2 + 1]);
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(txn_mgr.active_transaction_count(), 0);
}

TEST(TransactionManagerTest, CleansUpOnDestruction) {
    auto txn_mgr = std::make_unique<TransactionManager>();
    
    txn_mgr->begin_transaction();
    txn_mgr->begin_transaction();
    txn_mgr->begin_transaction();
    
    EXPECT_EQ(txn_mgr->active_transaction_count(), 3);
    
    // Destructor should clean up active transactions
    txn_mgr.reset();
    
    // If we get here without crash, cleanup worked
    EXPECT_TRUE(true);
}

}  // namespace

