#include "storage/recovery_manager.h"

#include <algorithm>
#include <cstring>

namespace forgedb::storage {

RecoveryManager::RecoveryManager(
    LogManager& log_manager,
    buffer::BufferPoolManager& buffer_pool
)
    : log_manager_(log_manager),
      buffer_pool_(buffer_pool),
      num_redone_(0),
      num_undone_(0) {}

void RecoveryManager::recover() {
    RecoveryState state;
    
    // Phase 1: Analysis - determine which transactions need redo/undo
    analysis_phase(state);
    
    // Phase 2: Redo - replay all operations to restore database state
    redo_phase(state);
    
    // Phase 3: Undo - rollback incomplete transactions
    undo_phase(state);
    
    // Flush all dirty pages to disk
    buffer_pool_.flush_all_pages();
}

void RecoveryManager::analysis_phase(RecoveryState& state) {
    auto all_records = log_manager_.read_all_records();
    
    for (const auto& record : all_records) {
        auto txn_id = record.txn_id;
        
        // Update transaction's last LSN
        state.txn_last_lsn[txn_id] = record.lsn;
        
        switch (record.type) {
            case LogRecordType::BEGIN:
                // Transaction started
                state.active_txns.insert(txn_id);
                break;
                
            case LogRecordType::COMMIT:
                // Transaction committed successfully
                state.active_txns.erase(txn_id);
                state.committed_txns.insert(txn_id);
                break;
                
            case LogRecordType::ABORT:
                // Transaction aborted
                state.active_txns.erase(txn_id);
                break;
                
            case LogRecordType::INSERT:
            case LogRecordType::UPDATE:
            case LogRecordType::DELETE:
                // Data modification - update dirty page table
                if (state.dirty_page_table.find(record.page_id) == 
                    state.dirty_page_table.end()) {
                    // First time this page was dirtied
                    state.dirty_page_table[record.page_id] = record.lsn;
                }
                
                // Ensure transaction is tracked as active
                state.active_txns.insert(txn_id);
                break;
        }
    }
}

void RecoveryManager::redo_phase(const RecoveryState& state) {
    auto all_records = log_manager_.read_all_records();
    
    // Find the minimum LSN to start redo from (earliest dirtied page)
    LSN min_lsn = INVALID_LSN;
    for (const auto& [page_id, lsn] : state.dirty_page_table) {
        if (min_lsn == INVALID_LSN || lsn < min_lsn) {
            min_lsn = lsn;
        }
    }
    
    // If no dirty pages, nothing to redo
    if (min_lsn == INVALID_LSN) {
        return;
    }
    
    // Redo all operations starting from min_lsn
    for (const auto& record : all_records) {
        if (record.lsn < min_lsn) {
            continue;
        }
        
        // Only redo data modification records
        if (record.type == LogRecordType::INSERT ||
            record.type == LogRecordType::UPDATE ||
            record.type == LogRecordType::DELETE) {
            
            // Check if page was dirty at crash
            if (state.dirty_page_table.find(record.page_id) != 
                state.dirty_page_table.end()) {
                
                apply_log_record(record, true);
                num_redone_++;
            }
        }
    }
}

void RecoveryManager::undo_phase(const RecoveryState& state) {
    // Get all log records
    auto all_records = log_manager_.read_all_records();
    
    // Build a vector of records that need to be undone
    // (from transactions that were active at crash)
    std::vector<LogRecord> undo_records;
    
    for (const auto& record : all_records) {
        // Only undo records from transactions that were active at crash
        if (state.active_txns.find(record.txn_id) != state.active_txns.end()) {
            // Only undo data modifications
            if (record.type == LogRecordType::INSERT ||
                record.type == LogRecordType::UPDATE ||
                record.type == LogRecordType::DELETE) {
                undo_records.push_back(record);
            }
        }
    }
    
    // Sort in reverse LSN order (undo from most recent to oldest)
    std::sort(
        undo_records.begin(),
        undo_records.end(),
        [](const LogRecord& a, const LogRecord& b) {
            return a.lsn > b.lsn;
        }
    );
    
    // Apply undo operations
    for (const auto& record : undo_records) {
        apply_log_record(record, false);
        num_undone_++;
    }
}

void RecoveryManager::apply_log_record(
    const LogRecord& record,
    bool is_redo
) {
    // Fetch the page from buffer pool
    Page* page = buffer_pool_.fetch_page(record.page_id);
    
    if (!page) {
        // Page doesn't exist yet (e.g., for INSERT on new page)
        // This is okay - the page will be created when needed
        return;
    }
    
    if (is_redo) {
        // REDO: Apply after-image
        if (!record.after_image.empty()) {
            // Check if we have enough space
            std::size_t copy_size = std::min(
                record.after_image.size(),
                PAGE_DATA_SIZE
            );
            
            std::memcpy(
                page->data(),
                record.after_image.data(),
                copy_size
            );
            
            // Mark page as dirty
            buffer_pool_.unpin_page(record.page_id, true);
        } else {
            buffer_pool_.unpin_page(record.page_id, false);
        }
    } else {
        // UNDO: Apply before-image
        if (!record.before_image.empty()) {
            // Check if we have enough space
            std::size_t copy_size = std::min(
                record.before_image.size(),
                PAGE_DATA_SIZE
            );
            
            std::memcpy(
                page->data(),
                record.before_image.data(),
                copy_size
            );
            
            // Mark page as dirty
            buffer_pool_.unpin_page(record.page_id, true);
        } else {
            buffer_pool_.unpin_page(record.page_id, false);
        }
    }
}

}  // namespace forgedb::storage
