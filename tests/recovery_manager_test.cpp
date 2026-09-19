#include <cstring>
#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#include "buffer/buffer_pool_manager.h"
#include "storage/disk_manager.h"
#include "storage/log_manager.h"
#include "storage/log_record.h"
#include "storage/recovery_manager.h"

namespace {

using forgedb::buffer::BufferPoolManager;
using forgedb::storage::DiskManager;
using forgedb::storage::LogManager;
using forgedb::storage::LogRecord;
using forgedb::storage::LogRecordType;
using forgedb::storage::RecoveryManager;
using forgedb::storage::PageId;
using forgedb::storage::PAGE_SIZE;

class RecoveryManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        db_file_ = "test_recovery.db";
        log_file_ = "test_recovery.log";
        
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

TEST_F(RecoveryManagerTest, AnalysisPhaseIdentifiesActiveTransactions) {
    LogManager log_mgr(log_file_);
    
    // Transaction 1: BEGIN -> INSERT -> COMMIT
    LogRecord begin1;
    begin1.txn_id = 1;
    begin1.type = LogRecordType::BEGIN;
    log_mgr.append_log_record(begin1);
    
    LogRecord insert1;
    insert1.txn_id = 1;
    insert1.type = LogRecordType::INSERT;
    insert1.page_id = 1;
    log_mgr.append_log_record(insert1);
    
    LogRecord commit1;
    commit1.txn_id = 1;
    commit1.type = LogRecordType::COMMIT;
    log_mgr.append_log_record(commit1);
    
    // Transaction 2: BEGIN -> UPDATE (no commit - active at crash)
    LogRecord begin2;
    begin2.txn_id = 2;
    begin2.type = LogRecordType::BEGIN;
    log_mgr.append_log_record(begin2);
    
    LogRecord update2;
    update2.txn_id = 2;
    update2.type = LogRecordType::UPDATE;
    update2.page_id = 2;
    log_mgr.append_log_record(update2);
    
    log_mgr.flush();
    
    DiskManager disk_mgr(db_file_);
    BufferPoolManager buffer_pool(10, disk_mgr);
    RecoveryManager recovery_mgr(log_mgr, buffer_pool);
    
    forgedb::storage::RecoveryState state;
    recovery_mgr.analysis_phase(state);
    
    // Transaction 1 should be committed
    EXPECT_EQ(state.committed_txns.count(1), 1);
    EXPECT_EQ(state.active_txns.count(1), 0);
    
    // Transaction 2 should be active
    EXPECT_EQ(state.active_txns.count(2), 1);
    EXPECT_EQ(state.committed_txns.count(2), 0);
}

TEST_F(RecoveryManagerTest, AnalysisPhaseBuildsDirtyPageTable) {
    LogManager log_mgr(log_file_);
    
    LogRecord begin1;
    begin1.txn_id = 1;
    begin1.type = LogRecordType::BEGIN;
    auto lsn1 = log_mgr.append_log_record(begin1);
    
    LogRecord insert1;
    insert1.txn_id = 1;
    insert1.type = LogRecordType::INSERT;
    insert1.page_id = 10;
    auto lsn2 = log_mgr.append_log_record(insert1);
    
    LogRecord update1;
    update1.txn_id = 1;
    update1.type = LogRecordType::UPDATE;
    update1.page_id = 10;  // Same page
    auto lsn3 = log_mgr.append_log_record(update1);
    
    LogRecord insert2;
    insert2.txn_id = 1;
    insert2.type = LogRecordType::INSERT;
    insert2.page_id = 20;  // Different page
    auto lsn4 = log_mgr.append_log_record(insert2);
    
    log_mgr.flush();
    
    DiskManager disk_mgr(db_file_);
    BufferPoolManager buffer_pool(10, disk_mgr);
    RecoveryManager recovery_mgr(log_mgr, buffer_pool);
    
    forgedb::storage::RecoveryState state;
    recovery_mgr.analysis_phase(state);
    
    // Should have 2 dirty pages
    EXPECT_EQ(state.dirty_page_table.size(), 2);
    
    // Page 10 should have first LSN = lsn2
    EXPECT_EQ(state.dirty_page_table[10], lsn2);
    
    // Page 20 should have first LSN = lsn4
    EXPECT_EQ(state.dirty_page_table[20], lsn4);
}

TEST_F(RecoveryManagerTest, RedoPhaseAppliesAfterImages) {
    LogManager log_mgr(log_file_);
    DiskManager disk_mgr(db_file_);
    BufferPoolManager buffer_pool(10, disk_mgr);
    
    // Create a page
    PageId page_id;
    auto* page = buffer_pool.new_page(page_id);
    ASSERT_NE(page, nullptr);
    
    // Write original data
    std::memset(page->data(), 0xAA, PAGE_SIZE);
    buffer_pool.unpin_page(page_id, true);
    buffer_pool.flush_page(page_id);
    
    // Simulate a transaction that modifies the page
    LogRecord begin;
    begin.txn_id = 1;
    begin.type = LogRecordType::BEGIN;
    log_mgr.append_log_record(begin);
    
    LogRecord update;
    update.txn_id = 1;
    update.type = LogRecordType::UPDATE;
    update.page_id = page_id;
    
    // Before image (original data)
    update.before_image.resize(PAGE_SIZE);
    std::memset(update.before_image.data(), 0xAA, PAGE_SIZE);
    
    // After image (modified data)
    update.after_image.resize(PAGE_SIZE);
    std::memset(update.after_image.data(), 0xBB, PAGE_SIZE);
    
    log_mgr.append_log_record(update);
    
    LogRecord commit;
    commit.txn_id = 1;
    commit.type = LogRecordType::COMMIT;
    log_mgr.append_log_record(commit);
    
    log_mgr.flush();
    
    // Simulate recovery
    RecoveryManager recovery_mgr(log_mgr, buffer_pool);
    recovery_mgr.recover();
    
    // Verify page was updated with after-image
    page = buffer_pool.fetch_page(page_id);
    ASSERT_NE(page, nullptr);
    
    EXPECT_EQ(static_cast<unsigned char>(page->data()[0]), 0xBB);
    EXPECT_EQ(static_cast<unsigned char>(page->data()[100]), 0xBB);
    
    buffer_pool.unpin_page(page_id, false);
    
    // Check statistics
    EXPECT_EQ(recovery_mgr.get_num_redone(), 1);
    EXPECT_EQ(recovery_mgr.get_num_undone(), 0);
}

TEST_F(RecoveryManagerTest, UndoPhaseRollsBackIncompleteTransactions) {
    LogManager log_mgr(log_file_);
    DiskManager disk_mgr(db_file_);
    BufferPoolManager buffer_pool(10, disk_mgr);
    
    // Create a page
    PageId page_id;
    auto* page = buffer_pool.new_page(page_id);
    ASSERT_NE(page, nullptr);
    
    // Write original data
    std::memset(page->data(), 0xAA, PAGE_SIZE);
    buffer_pool.unpin_page(page_id, true);
    buffer_pool.flush_page(page_id);
    
    // Simulate an incomplete transaction (no commit)
    LogRecord begin;
    begin.txn_id = 1;
    begin.type = LogRecordType::BEGIN;
    log_mgr.append_log_record(begin);
    
    LogRecord update;
    update.txn_id = 1;
    update.type = LogRecordType::UPDATE;
    update.page_id = page_id;
    
    // Before image (original data)
    update.before_image.resize(PAGE_SIZE);
    std::memset(update.before_image.data(), 0xAA, PAGE_SIZE);
    
    // After image (modified data)
    update.after_image.resize(PAGE_SIZE);
    std::memset(update.after_image.data(), 0xBB, PAGE_SIZE);
    
    log_mgr.append_log_record(update);
    
    // No commit - transaction incomplete
    log_mgr.flush();
    
    // Simulate recovery
    RecoveryManager recovery_mgr(log_mgr, buffer_pool);
    recovery_mgr.recover();
    
    // Verify page was restored to before-image
    page = buffer_pool.fetch_page(page_id);
    ASSERT_NE(page, nullptr);
    
    EXPECT_EQ(static_cast<unsigned char>(page->data()[0]), 0xAA);
    EXPECT_EQ(static_cast<unsigned char>(page->data()[100]), 0xAA);
    
    buffer_pool.unpin_page(page_id, false);
    
    // Check statistics
    EXPECT_EQ(recovery_mgr.get_num_redone(), 1);
    EXPECT_EQ(recovery_mgr.get_num_undone(), 1);
}

TEST_F(RecoveryManagerTest, HandlesMultipleTransactions) {
    LogManager log_mgr(log_file_);
    DiskManager disk_mgr(db_file_);
    BufferPoolManager buffer_pool(10, disk_mgr);
    
    // Create two pages
    PageId page_id1, page_id2;
    auto* page1 = buffer_pool.new_page(page_id1);
    auto* page2 = buffer_pool.new_page(page_id2);
    ASSERT_NE(page1, nullptr);
    ASSERT_NE(page2, nullptr);
    
    std::memset(page1->data(), 0x11, PAGE_SIZE);
    std::memset(page2->data(), 0x22, PAGE_SIZE);
    
    buffer_pool.unpin_page(page_id1, true);
    buffer_pool.unpin_page(page_id2, true);
    buffer_pool.flush_all_pages();
    
    // Transaction 1: modifies page1, commits
    LogRecord begin1;
    begin1.txn_id = 1;
    begin1.type = LogRecordType::BEGIN;
    log_mgr.append_log_record(begin1);
    
    LogRecord update1;
    update1.txn_id = 1;
    update1.type = LogRecordType::UPDATE;
    update1.page_id = page_id1;
    update1.before_image.resize(PAGE_SIZE);
    update1.after_image.resize(PAGE_SIZE);
    std::memset(update1.before_image.data(), 0x11, PAGE_SIZE);
    std::memset(update1.after_image.data(), 0x33, PAGE_SIZE);
    log_mgr.append_log_record(update1);
    
    LogRecord commit1;
    commit1.txn_id = 1;
    commit1.type = LogRecordType::COMMIT;
    log_mgr.append_log_record(commit1);
    
    // Transaction 2: modifies page2, does NOT commit
    LogRecord begin2;
    begin2.txn_id = 2;
    begin2.type = LogRecordType::BEGIN;
    log_mgr.append_log_record(begin2);
    
    LogRecord update2;
    update2.txn_id = 2;
    update2.type = LogRecordType::UPDATE;
    update2.page_id = page_id2;
    update2.before_image.resize(PAGE_SIZE);
    update2.after_image.resize(PAGE_SIZE);
    std::memset(update2.before_image.data(), 0x22, PAGE_SIZE);
    std::memset(update2.after_image.data(), 0x44, PAGE_SIZE);
    log_mgr.append_log_record(update2);
    
    // No commit for transaction 2
    log_mgr.flush();
    
    // Recover
    RecoveryManager recovery_mgr(log_mgr, buffer_pool);
    recovery_mgr.recover();
    
    // Page1 should have committed changes
    page1 = buffer_pool.fetch_page(page_id1);
    ASSERT_NE(page1, nullptr);
    EXPECT_EQ(static_cast<unsigned char>(page1->data()[0]), 0x33);
    buffer_pool.unpin_page(page_id1, false);
    
    // Page2 should be rolled back
    page2 = buffer_pool.fetch_page(page_id2);
    ASSERT_NE(page2, nullptr);
    EXPECT_EQ(static_cast<unsigned char>(page2->data()[0]), 0x22);
    buffer_pool.unpin_page(page_id2, false);
    
    // Check statistics
    EXPECT_EQ(recovery_mgr.get_num_redone(), 2);
    EXPECT_EQ(recovery_mgr.get_num_undone(), 1);
}

TEST_F(RecoveryManagerTest, HandlesAbortedTransaction) {
    LogManager log_mgr(log_file_);
    DiskManager disk_mgr(db_file_);
    BufferPoolManager buffer_pool(10, disk_mgr);
    
    // Transaction with explicit ABORT
    LogRecord begin;
    begin.txn_id = 1;
    begin.type = LogRecordType::BEGIN;
    log_mgr.append_log_record(begin);
    
    LogRecord insert;
    insert.txn_id = 1;
    insert.type = LogRecordType::INSERT;
    insert.page_id = 1;
    log_mgr.append_log_record(insert);
    
    LogRecord abort;
    abort.txn_id = 1;
    abort.type = LogRecordType::ABORT;
    log_mgr.append_log_record(abort);
    
    log_mgr.flush();
    
    RecoveryManager recovery_mgr(log_mgr, buffer_pool);
    
    forgedb::storage::RecoveryState state;
    recovery_mgr.analysis_phase(state);
    
    // Transaction should not be active or committed
    EXPECT_EQ(state.active_txns.count(1), 0);
    EXPECT_EQ(state.committed_txns.count(1), 0);
}

TEST_F(RecoveryManagerTest, EmptyLogRecovery) {
    LogManager log_mgr(log_file_);
    DiskManager disk_mgr(db_file_);
    BufferPoolManager buffer_pool(10, disk_mgr);
    
    RecoveryManager recovery_mgr(log_mgr, buffer_pool);
    
    // Should not crash on empty log
    EXPECT_NO_THROW(recovery_mgr.recover());
    
    EXPECT_EQ(recovery_mgr.get_num_redone(), 0);
    EXPECT_EQ(recovery_mgr.get_num_undone(), 0);
}

TEST_F(RecoveryManagerTest, MultipleUpdatesToSamePage) {
    LogManager log_mgr(log_file_);
    DiskManager disk_mgr(db_file_);
    BufferPoolManager buffer_pool(10, disk_mgr);
    
    PageId page_id;
    auto* page = buffer_pool.new_page(page_id);
    ASSERT_NE(page, nullptr);
    
    std::memset(page->data(), 0x00, PAGE_SIZE);
    buffer_pool.unpin_page(page_id, true);
    buffer_pool.flush_page(page_id);
    
    // Transaction with multiple updates to same page
    LogRecord begin;
    begin.txn_id = 1;
    begin.type = LogRecordType::BEGIN;
    log_mgr.append_log_record(begin);
    
    // Update 1: 0x00 -> 0x11
    LogRecord update1;
    update1.txn_id = 1;
    update1.type = LogRecordType::UPDATE;
    update1.page_id = page_id;
    update1.before_image.resize(PAGE_SIZE);
    update1.after_image.resize(PAGE_SIZE);
    std::memset(update1.before_image.data(), 0x00, PAGE_SIZE);
    std::memset(update1.after_image.data(), 0x11, PAGE_SIZE);
    log_mgr.append_log_record(update1);
    
    // Update 2: 0x11 -> 0x22
    LogRecord update2;
    update2.txn_id = 1;
    update2.type = LogRecordType::UPDATE;
    update2.page_id = page_id;
    update2.before_image.resize(PAGE_SIZE);
    update2.after_image.resize(PAGE_SIZE);
    std::memset(update2.before_image.data(), 0x11, PAGE_SIZE);
    std::memset(update2.after_image.data(), 0x22, PAGE_SIZE);
    log_mgr.append_log_record(update2);
    
    LogRecord commit;
    commit.txn_id = 1;
    commit.type = LogRecordType::COMMIT;
    log_mgr.append_log_record(commit);
    
    log_mgr.flush();
    
    RecoveryManager recovery_mgr(log_mgr, buffer_pool);
    recovery_mgr.recover();
    
    // Final state should be 0x22
    page = buffer_pool.fetch_page(page_id);
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(static_cast<unsigned char>(page->data()[0]), 0x22);
    buffer_pool.unpin_page(page_id, false);
    
    EXPECT_EQ(recovery_mgr.get_num_redone(), 2);
}

TEST_F(RecoveryManagerTest, UndoInReverseOrder) {
    LogManager log_mgr(log_file_);
    DiskManager disk_mgr(db_file_);
    BufferPoolManager buffer_pool(10, disk_mgr);
    
    PageId page_id;
    auto* page = buffer_pool.new_page(page_id);
    ASSERT_NE(page, nullptr);
    
    std::memset(page->data(), 0x00, PAGE_SIZE);
    buffer_pool.unpin_page(page_id, true);
    buffer_pool.flush_page(page_id);
    
    // Incomplete transaction with multiple updates
    LogRecord begin;
    begin.txn_id = 1;
    begin.type = LogRecordType::BEGIN;
    log_mgr.append_log_record(begin);
    
    // Update 1: 0x00 -> 0x11
    LogRecord update1;
    update1.txn_id = 1;
    update1.type = LogRecordType::UPDATE;
    update1.page_id = page_id;
    update1.before_image.resize(PAGE_SIZE);
    update1.after_image.resize(PAGE_SIZE);
    std::memset(update1.before_image.data(), 0x00, PAGE_SIZE);
    std::memset(update1.after_image.data(), 0x11, PAGE_SIZE);
    log_mgr.append_log_record(update1);
    
    // Update 2: 0x11 -> 0x22
    LogRecord update2;
    update2.txn_id = 1;
    update2.type = LogRecordType::UPDATE;
    update2.page_id = page_id;
    update2.before_image.resize(PAGE_SIZE);
    update2.after_image.resize(PAGE_SIZE);
    std::memset(update2.before_image.data(), 0x11, PAGE_SIZE);
    std::memset(update2.after_image.data(), 0x22, PAGE_SIZE);
    log_mgr.append_log_record(update2);
    
    // No commit - needs undo
    log_mgr.flush();
    
    RecoveryManager recovery_mgr(log_mgr, buffer_pool);
    recovery_mgr.recover();
    
    // Should be rolled back to original state (0x00)
    page = buffer_pool.fetch_page(page_id);
    ASSERT_NE(page, nullptr);
    EXPECT_EQ(static_cast<unsigned char>(page->data()[0]), 0x00);
    buffer_pool.unpin_page(page_id, false);
    
    EXPECT_EQ(recovery_mgr.get_num_undone(), 2);
}

}  // namespace
