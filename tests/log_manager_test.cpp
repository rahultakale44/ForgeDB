#include <filesystem>
#include <memory>
#include <thread>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "storage/log_manager.h"
#include "storage/log_record.h"

namespace {

using forgedb::storage::LogManager;
using forgedb::storage::LogRecord;
using forgedb::storage::LogRecordType;
using forgedb::storage::LSN;
using forgedb::storage::INVALID_LSN;

class LogManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        log_file_path_ = "test_log.log";
        
        if (std::filesystem::exists(log_file_path_)) {
            std::filesystem::remove(log_file_path_);
        }
    }
    
    void TearDown() override {
        if (std::filesystem::exists(log_file_path_)) {
            std::filesystem::remove(log_file_path_);
        }
    }
    
    std::string log_file_path_;
};

TEST_F(LogManagerTest, CreatesLogFile) {
    {
        LogManager log_mgr(log_file_path_);
    }
    
    EXPECT_TRUE(std::filesystem::exists(log_file_path_));
}

TEST_F(LogManagerTest, AppendsLogRecord) {
    LogManager log_mgr(log_file_path_);
    
    LogRecord record;
    record.txn_id = 1;
    record.type = LogRecordType::BEGIN;
    
    LSN lsn = log_mgr.append_log_record(record);
    
    EXPECT_NE(lsn, INVALID_LSN);
    EXPECT_GT(lsn, 0);
}

TEST_F(LogManagerTest, AssignsSequentialLSNs) {
    LogManager log_mgr(log_file_path_);
    
    LogRecord record1;
    record1.txn_id = 1;
    record1.type = LogRecordType::BEGIN;
    
    LogRecord record2;
    record2.txn_id = 1;
    record2.type = LogRecordType::INSERT;
    
    LogRecord record3;
    record3.txn_id = 1;
    record3.type = LogRecordType::COMMIT;
    
    LSN lsn1 = log_mgr.append_log_record(record1);
    LSN lsn2 = log_mgr.append_log_record(record2);
    LSN lsn3 = log_mgr.append_log_record(record3);
    
    EXPECT_EQ(lsn1 + 1, lsn2);
    EXPECT_EQ(lsn2 + 1, lsn3);
}

TEST_F(LogManagerTest, ReadsLogRecord) {
    LogManager log_mgr(log_file_path_);
    
    LogRecord original;
    original.txn_id = 42;
    original.type = LogRecordType::INSERT;
    original.page_id = 123;
    
    LSN lsn = log_mgr.append_log_record(original);
    log_mgr.flush();
    
    LogRecord retrieved;
    ASSERT_TRUE(log_mgr.read_log_record(lsn, retrieved));
    
    EXPECT_EQ(retrieved.lsn, lsn);
    EXPECT_EQ(retrieved.txn_id, 42);
    EXPECT_EQ(retrieved.type, LogRecordType::INSERT);
    EXPECT_EQ(retrieved.page_id, 123);
}

TEST_F(LogManagerTest, ReadsAllRecords) {
    LogManager log_mgr(log_file_path_);
    
    for (int i = 0; i < 10; ++i) {
        LogRecord record;
        record.txn_id = i;
        record.type = LogRecordType::BEGIN;
        log_mgr.append_log_record(record);
    }
    
    log_mgr.flush();
    
    auto records = log_mgr.read_all_records();
    
    EXPECT_EQ(records.size(), 10);
    
    for (size_t i = 0; i < records.size(); ++i) {
        EXPECT_EQ(records[i].txn_id, i);
        EXPECT_EQ(records[i].type, LogRecordType::BEGIN);
    }
}

TEST_F(LogManagerTest, PersistsAcrossReopen) {
    LSN lsn1, lsn2;
    
    {
        LogManager log_mgr(log_file_path_);
        
        LogRecord record1;
        record1.txn_id = 1;
        record1.type = LogRecordType::BEGIN;
        lsn1 = log_mgr.append_log_record(record1);
        
        LogRecord record2;
        record2.txn_id = 1;
        record2.type = LogRecordType::COMMIT;
        lsn2 = log_mgr.append_log_record(record2);
        
        log_mgr.flush();
    }
    
    {
        LogManager log_mgr(log_file_path_);
        
        LogRecord retrieved1, retrieved2;
        
        EXPECT_TRUE(log_mgr.read_log_record(lsn1, retrieved1));
        EXPECT_EQ(retrieved1.type, LogRecordType::BEGIN);
        
        EXPECT_TRUE(log_mgr.read_log_record(lsn2, retrieved2));
        EXPECT_EQ(retrieved2.type, LogRecordType::COMMIT);
    }
}

TEST_F(LogManagerTest, HandlesDataImages) {
    LogManager log_mgr(log_file_path_);
    
    LogRecord record;
    record.txn_id = 1;
    record.type = LogRecordType::UPDATE;
    record.page_id = 100;
    
    // Create some test data
    record.before_image.resize(50);
    for (size_t i = 0; i < 50; ++i) {
        record.before_image[i] = static_cast<std::byte>(i);
    }
    
    record.after_image.resize(50);
    for (size_t i = 0; i < 50; ++i) {
        record.after_image[i] = static_cast<std::byte>(i + 100);
    }
    
    LSN lsn = log_mgr.append_log_record(record);
    log_mgr.flush();
    
    LogRecord retrieved;
    ASSERT_TRUE(log_mgr.read_log_record(lsn, retrieved));
    
    EXPECT_EQ(retrieved.before_image.size(), 50);
    EXPECT_EQ(retrieved.after_image.size(), 50);
    
    for (size_t i = 0; i < 50; ++i) {
        EXPECT_EQ(retrieved.before_image[i], static_cast<std::byte>(i));
        EXPECT_EQ(retrieved.after_image[i], static_cast<std::byte>(i + 100));
    }
}

TEST_F(LogManagerTest, ThreadSafeAppend) {
    LogManager log_mgr(log_file_path_);
    
    const int num_threads = 10;
    const int records_per_thread = 50;
    
    std::vector<std::thread> threads;
    std::vector<std::vector<LSN>> lsns(num_threads);
    
    for (int i = 0; i < num_threads; ++i) {
        threads.emplace_back([&log_mgr, &lsns, i, records_per_thread]() {
            for (int j = 0; j < records_per_thread; ++j) {
                LogRecord record;
                record.txn_id = i * records_per_thread + j;
                record.type = LogRecordType::BEGIN;
                
                LSN lsn = log_mgr.append_log_record(record);
                lsns[i].push_back(lsn);
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    // Verify all LSNs are unique
    std::unordered_set<LSN> all_lsns;
    for (const auto& thread_lsns : lsns) {
        for (LSN lsn : thread_lsns) {
            EXPECT_TRUE(all_lsns.insert(lsn).second);
        }
    }
    
    EXPECT_EQ(all_lsns.size(), num_threads * records_per_thread);
}

TEST_F(LogManagerTest, HandlesEmptyImages) {
    LogManager log_mgr(log_file_path_);
    
    LogRecord record;
    record.txn_id = 1;
    record.type = LogRecordType::DELETE;
    record.page_id = 50;
    // Leave before_image and after_image empty
    
    LSN lsn = log_mgr.append_log_record(record);
    log_mgr.flush();
    
    LogRecord retrieved;
    ASSERT_TRUE(log_mgr.read_log_record(lsn, retrieved));
    
    EXPECT_TRUE(retrieved.before_image.empty());
    EXPECT_TRUE(retrieved.after_image.empty());
}

TEST_F(LogManagerTest, ContinuesLSNAfterReopen) {
    LSN last_lsn;
    
    {
        LogManager log_mgr(log_file_path_);
        
        LogRecord record;
        record.txn_id = 1;
        record.type = LogRecordType::BEGIN;
        
        last_lsn = log_mgr.append_log_record(record);
        log_mgr.flush();
    }
    
    {
        LogManager log_mgr(log_file_path_);
        
        LogRecord record;
        record.txn_id = 2;
        record.type = LogRecordType::BEGIN;
        
        LSN new_lsn = log_mgr.append_log_record(record);
        
        EXPECT_GT(new_lsn, last_lsn);
        EXPECT_EQ(new_lsn, last_lsn + 1);
    }
}

}  // namespace

