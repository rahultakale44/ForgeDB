#include <cstring>
#include <filesystem>
#include <memory>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "buffer/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/log_manager.h"
#include "storage/recovery_manager.h"
#include "transaction/lock_manager.h"
#include "transaction/transaction_manager.h"

namespace {

using forgedb::buffer::BufferPoolManager;
using forgedb::storage::DiskManager;
using forgedb::storage::LogManager;
using forgedb::storage::RecoveryManager;
using forgedb::storage::PageId;
using forgedb::storage::PAGE_SIZE;
using forgedb::transaction::LockManager;
using forgedb::transaction::TransactionManager;

class TransactionIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_file_ = "test_txn_integration.db";
        log_file_ = "test_txn_integration.log";
        
        CleanupFiles();
    }
    
    void TearDown() override {
        CleanupFiles();
    }
    
    void CleanupFiles() {
        if (std::filesystem::exists(db_file_)) {
            std::filesystem::remove(db_file_);
        }
        if (std::filesystem::exists(log_file_)) {
            std::filesystem::remove(log_file_);
        }
    }
    
    std::string db_file_;
    std::string log_file_;
};

TEST_F(TransactionIntegrationTest, BeginLogsToWAL) {
    LogManager log_mgr(log_file_);
    TransactionManager txn_mgr(&log_mgr);
    
    auto txn = txn_mgr.begin_transaction();
    
    log_mgr.flush();
    
    // Verify BEGIN was logged
    auto records = log_mgr.read_all_records();
    ASSERT_EQ(records.size(), 1);
    EXPECT_EQ(records[0].type, forgedb::storage::LogRecordType::BEGIN);
    EXPECT_EQ(records[0].txn_id, txn->transaction_id());
}

TEST_F(TransactionIntegrationTest, CommitLogsToWAL) {
    LogManager log_mgr(log_file_);
    TransactionManager txn_mgr(&log_mgr);
    
    auto txn = txn_mgr.begin_transaction();
    txn_mgr.commit(txn);
    
    // Verify BEGIN and COMMIT were logged
    auto records = log_mgr.read_all_records();
    ASSERT_EQ(records.size(), 2);
    EXPECT_EQ(records[0].type, forgedb::storage::LogRecordType::BEGIN);
    EXPECT_EQ(records[1].type, forgedb::storage::LogRecordType::COMMIT);
    EXPECT_EQ(records[0].txn_id, txn->transaction_id());
    EXPECT_EQ(records[1].txn_id, txn->transaction_id());
}

TEST_F(TransactionIntegrationTest, AbortLogsToWAL) {
    LogManager log_mgr(log_file_);
    TransactionManager txn_mgr(&log_mgr);
    
    auto txn = txn_mgr.begin_transaction();
    txn_mgr.abort(txn);
    
    // Verify BEGIN and ABORT were logged
    auto records = log_mgr.read_all_records();
    ASSERT_EQ(records.size(), 2);
    EXPECT_EQ(records[0].type, forgedb::storage::LogRecordType::BEGIN);
    EXPECT_EQ(records[1].type, forgedb::storage::LogRecordType::ABORT);
}

TEST_F(TransactionIntegrationTest, CommitReleasesLocks) {
    LogManager log_mgr(log_file_);
    LockManager lock_mgr;
    TransactionManager txn_mgr(&log_mgr, &lock_mgr);
    
    auto txn = txn_mgr.begin_transaction();
    
    // Acquire locks
    EXPECT_TRUE(lock_mgr.lock_exclusive(txn, 1));
    EXPECT_TRUE(lock_mgr.lock_shared(txn, 2));
    
    EXPECT_TRUE(lock_mgr.holds_lock(txn, 1, forgedb::transaction::LockMode::EXCLUSIVE));
    EXPECT_TRUE(lock_mgr.holds_lock(txn, 2, forgedb::transaction::LockMode::SHARED));
    
    // Commit should release locks
    txn_mgr.commit(txn);
    
    EXPECT_FALSE(lock_mgr.holds_lock(txn, 1, forgedb::transaction::LockMode::EXCLUSIVE));
    EXPECT_FALSE(lock_mgr.holds_lock(txn, 2, forgedb::transaction::LockMode::SHARED));
}

TEST_F(TransactionIntegrationTest, AbortReleasesLocks) {
    LogManager log_mgr(log_file_);
    LockManager lock_mgr;
    TransactionManager txn_mgr(&log_mgr, &lock_mgr);
    
    auto txn = txn_mgr.begin_transaction();
    
    // Acquire locks
    EXPECT_TRUE(lock_mgr.lock_exclusive(txn, 1));
    EXPECT_TRUE(lock_mgr.lock_shared(txn, 2));
    
    // Abort should release locks
    txn_mgr.abort(txn);
    
    EXPECT_FALSE(lock_mgr.holds_lock(txn, 1, forgedb::transaction::LockMode::EXCLUSIVE));
    EXPECT_FALSE(lock_mgr.holds_lock(txn, 2, forgedb::transaction::LockMode::SHARED));
}

TEST_F(TransactionIntegrationTest, MultipleTransactionsWithLogging) {
    LogManager log_mgr(log_file_);
    TransactionManager txn_mgr(&log_mgr);
    
    auto txn1 = txn_mgr.begin_transaction();
    auto txn2 = txn_mgr.begin_transaction();
    
    txn_mgr.commit(txn1);
    txn_mgr.abort(txn2);
    
    // Verify all records were logged
    auto records = log_mgr.read_all_records();
    ASSERT_EQ(records.size(), 4);
    
    // txn1: BEGIN, COMMIT
    EXPECT_EQ(records[0].type, forgedb::storage::LogRecordType::BEGIN);
    EXPECT_EQ(records[0].txn_id, txn1->transaction_id());
    
    // txn2: BEGIN
    EXPECT_EQ(records[1].type, forgedb::storage::LogRecordType::BEGIN);
    EXPECT_EQ(records[1].txn_id, txn2->transaction_id());
    
    // txn1: COMMIT
    EXPECT_EQ(records[2].type, forgedb::storage::LogRecordType::COMMIT);
    EXPECT_EQ(records[2].txn_id, txn1->transaction_id());
    
    // txn2: ABORT
    EXPECT_EQ(records[3].type, forgedb::storage::LogRecordType::ABORT);
    EXPECT_EQ(records[3].txn_id, txn2->transaction_id());
}

TEST_F(TransactionIntegrationTest, ConcurrentTransactionsWithLocking) {
    LogManager log_mgr(log_file_);
    LockManager lock_mgr;
    TransactionManager txn_mgr(&log_mgr, &lock_mgr);
    
    auto txn1 = txn_mgr.begin_transaction();
    auto txn2 = txn_mgr.begin_transaction();
    
    // txn1 locks page 1
    EXPECT_TRUE(lock_mgr.lock_exclusive(txn1, 1));
    
    bool txn2_acquired = false;
    
    // txn2 tries to lock page 1 (should block)
    std::thread t2([&]() {
        txn2_acquired = lock_mgr.lock_exclusive(txn2, 1);
    });
    
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_FALSE(txn2_acquired);
    
    // txn1 commits and releases locks
    txn_mgr.commit(txn1);
    
    t2.join();
    EXPECT_TRUE(txn2_acquired);
    
    txn_mgr.commit(txn2);
}

TEST_F(TransactionIntegrationTest, RecoveryAfterCrash) {
    PageId page_id;
    
    // Phase 1: Run transactions and "crash"
    {
        DiskManager disk_mgr(db_file_);
        BufferPoolManager buffer_pool(10, disk_mgr);
        LogManager log_mgr(log_file_);
        TransactionManager txn_mgr(&log_mgr);
        
        // Create a page
        auto* page = buffer_pool.new_page(page_id);
        ASSERT_NE(page, nullptr);
        std::memset(page->data(), 0xAA, PAGE_SIZE);
        buffer_pool.unpin_page(page_id, true);
        buffer_pool.flush_page(page_id);
        
        // Transaction 1: commits and modifies page
        auto txn1 = txn_mgr.begin_transaction();
        
        page = buffer_pool.fetch_page(page_id);
        forgedb::storage::LogRecord update1;
        update1.txn_id = txn1->transaction_id();
        update1.type = forgedb::storage::LogRecordType::UPDATE;
        update1.page_id = page_id;
        update1.before_image.resize(PAGE_SIZE);
        update1.after_image.resize(PAGE_SIZE);
        std::memset(update1.before_image.data(), 0xAA, PAGE_SIZE);
        std::memset(update1.after_image.data(), 0xBB, PAGE_SIZE);
        log_mgr.append_log_record(update1);
        
        // Actually apply the change
        std::memset(page->data(), 0xBB, PAGE_SIZE);
        buffer_pool.unpin_page(page_id, true);
        
        txn_mgr.commit(txn1);
        
        // Flush to persist committed transaction
        buffer_pool.flush_page(page_id);
        
        // Transaction 2: does not commit (crash) - operates on different page
        auto txn2 = txn_mgr.begin_transaction();
        
        PageId page_id2;
        auto* page2 = buffer_pool.new_page(page_id2);
        std::memset(page2->data(), 0xDD, PAGE_SIZE);
        
        forgedb::storage::LogRecord update2;
        update2.txn_id = txn2->transaction_id();
        update2.type = forgedb::storage::LogRecordType::UPDATE;
        update2.page_id = page_id2;
        update2.before_image.resize(PAGE_SIZE);
        update2.after_image.resize(PAGE_SIZE);
        std::memset(update2.before_image.data(), 0xDD, PAGE_SIZE);
        std::memset(update2.after_image.data(), 0xEE, PAGE_SIZE);
        log_mgr.append_log_record(update2);
        
        // Actually apply the change (but don't commit or flush)
        std::memset(page2->data(), 0xEE, PAGE_SIZE);
        buffer_pool.unpin_page(page_id2, true);
        
        // No commit - simulated crash
    }
    
    // Phase 2: Recover
    {
        DiskManager disk_mgr(db_file_);
        BufferPoolManager buffer_pool(10, disk_mgr);
        LogManager log_mgr(log_file_);
        RecoveryManager recovery_mgr(log_mgr, buffer_pool);
        
        recovery_mgr.recover();
        
        // Verify: txn1 committed and persisted (0xBB)
        auto* page = buffer_pool.fetch_page(page_id);
        ASSERT_NE(page, nullptr);
        EXPECT_EQ(static_cast<unsigned char>(page->data()[0]), 0xBB);
        buffer_pool.unpin_page(page_id, false);
        
        // txn2's page should not exist or should be rolled back to 0xDD
        // Since it was never flushed, recovery won't find it
    }
}

TEST_F(TransactionIntegrationTest, TwoPhaseLockingSerializability) {
    LogManager log_mgr(log_file_);
    LockManager lock_mgr;
    TransactionManager txn_mgr(&log_mgr, &lock_mgr);
    DiskManager disk_mgr(db_file_);
    BufferPoolManager buffer_pool(10, disk_mgr);
    
    // Create a page with initial value
    PageId page_id;
    auto* page = buffer_pool.new_page(page_id);
    ASSERT_NE(page, nullptr);
    
    int* value = reinterpret_cast<int*>(page->data());
    *value = 100;
    buffer_pool.unpin_page(page_id, true);
    buffer_pool.flush_page(page_id);
    
    std::atomic<int> final_value{0};
    
    // Two transactions incrementing the same value
    auto increment_txn = [&](int amount) {
        auto txn = txn_mgr.begin_transaction();
        
        // Lock page
        lock_mgr.lock_exclusive(txn, page_id);
        
        // Read current value
        auto* p = buffer_pool.fetch_page(page_id);
        int current = *reinterpret_cast<int*>(p->data());
        
        // Simulate work
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        
        // Write new value
        *reinterpret_cast<int*>(p->data()) = current + amount;
        buffer_pool.unpin_page(page_id, true);
        
        // Commit releases lock
        txn_mgr.commit(txn);
    };
    
    std::thread t1([&]() { increment_txn(10); });
    std::thread t2([&]() { increment_txn(20); });
    
    t1.join();
    t2.join();
    
    // Verify final value is correct (100 + 10 + 20 = 130)
    page = buffer_pool.fetch_page(page_id);
    ASSERT_NE(page, nullptr);
    final_value = *reinterpret_cast<int*>(page->data());
    buffer_pool.unpin_page(page_id, false);
    
    EXPECT_EQ(final_value, 130);
}

TEST_F(TransactionIntegrationTest, DestructorAbortsActiveTransactions) {
    LogManager log_mgr(log_file_);
    
    {
        TransactionManager txn_mgr(&log_mgr);
        
        auto txn1 = txn_mgr.begin_transaction();
        auto txn2 = txn_mgr.begin_transaction();
        
        txn_mgr.commit(txn1);
        // txn2 still active - should be aborted by destructor
    }
    
    // Verify txn2 was aborted
    auto records = log_mgr.read_all_records();
    
    // Should have: BEGIN(txn1), BEGIN(txn2), COMMIT(txn1), ABORT(txn2)
    ASSERT_EQ(records.size(), 4);
    EXPECT_EQ(records[2].type, forgedb::storage::LogRecordType::COMMIT);
    EXPECT_EQ(records[3].type, forgedb::storage::LogRecordType::ABORT);
}

TEST_F(TransactionIntegrationTest, FullACIDWorkflow) {
    DiskManager disk_mgr(db_file_);
    BufferPoolManager buffer_pool(10, disk_mgr);
    LogManager log_mgr(log_file_);
    LockManager lock_mgr;
    TransactionManager txn_mgr(&log_mgr, &lock_mgr);
    
    // Create two pages
    PageId page_id1, page_id2;
    auto* p1 = buffer_pool.new_page(page_id1);
    auto* p2 = buffer_pool.new_page(page_id2);
    
    *reinterpret_cast<int*>(p1->data()) = 100;
    *reinterpret_cast<int*>(p2->data()) = 200;
    
    buffer_pool.unpin_page(page_id1, true);
    buffer_pool.unpin_page(page_id2, true);
    buffer_pool.flush_all_pages();
    
    // Transaction: transfer 50 from page1 to page2
    auto txn = txn_mgr.begin_transaction();
    
    // Lock both pages
    EXPECT_TRUE(lock_mgr.lock_exclusive(txn, page_id1));
    EXPECT_TRUE(lock_mgr.lock_exclusive(txn, page_id2));
    
    // Read-modify-write page1
    p1 = buffer_pool.fetch_page(page_id1);
    int val1 = *reinterpret_cast<int*>(p1->data());
    
    forgedb::storage::LogRecord log1;
    log1.txn_id = txn->transaction_id();
    log1.type = forgedb::storage::LogRecordType::UPDATE;
    log1.page_id = page_id1;
    log1.before_image.resize(sizeof(int));
    log1.after_image.resize(sizeof(int));
    std::memcpy(log1.before_image.data(), &val1, sizeof(int));
    
    val1 -= 50;
    *reinterpret_cast<int*>(p1->data()) = val1;
    std::memcpy(log1.after_image.data(), &val1, sizeof(int));
    log_mgr.append_log_record(log1);
    
    buffer_pool.unpin_page(page_id1, true);
    
    // Read-modify-write page2
    p2 = buffer_pool.fetch_page(page_id2);
    int val2 = *reinterpret_cast<int*>(p2->data());
    
    forgedb::storage::LogRecord log2;
    log2.txn_id = txn->transaction_id();
    log2.type = forgedb::storage::LogRecordType::UPDATE;
    log2.page_id = page_id2;
    log2.before_image.resize(sizeof(int));
    log2.after_image.resize(sizeof(int));
    std::memcpy(log2.before_image.data(), &val2, sizeof(int));
    
    val2 += 50;
    *reinterpret_cast<int*>(p2->data()) = val2;
    std::memcpy(log2.after_image.data(), &val2, sizeof(int));
    log_mgr.append_log_record(log2);
    
    buffer_pool.unpin_page(page_id2, true);
    
    // Commit
    txn_mgr.commit(txn);
    buffer_pool.flush_all_pages();
    
    // Verify final state
    p1 = buffer_pool.fetch_page(page_id1);
    p2 = buffer_pool.fetch_page(page_id2);
    
    EXPECT_EQ(*reinterpret_cast<int*>(p1->data()), 50);
    EXPECT_EQ(*reinterpret_cast<int*>(p2->data()), 250);
    
    buffer_pool.unpin_page(page_id1, false);
    buffer_pool.unpin_page(page_id2, false);
}

}  // namespace
