#pragma once

#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "storage/log_manager.h"
#include "transaction/transaction.h"

namespace forgedb::storage {

struct RecoveryState {
    // Transactions that were active at crash
    std::unordered_set<transaction::TransactionId> active_txns;
    
    // Transactions that committed
    std::unordered_set<transaction::TransactionId> committed_txns;
    
    // Mapping from transaction ID to last LSN
    std::unordered_map<transaction::TransactionId, LSN> txn_last_lsn;
    
    // Dirty page table: page_id -> first LSN that dirtied the page
    std::unordered_map<PageId, LSN> dirty_page_table;
};

class RecoveryManager {
public:
    RecoveryManager(
        LogManager& log_manager,
        buffer::BufferPoolManager& buffer_pool
    );
    
    // Main recovery entry point
    void recover();
    
    // ARIES phases
    void analysis_phase(RecoveryState& state);
    
    void redo_phase(const RecoveryState& state);
    
    void undo_phase(const RecoveryState& state);
    
    // Helper to apply a single log record (for redo/undo)
    void apply_log_record(const LogRecord& record, bool is_redo);
    
    // Get recovery statistics
    std::size_t get_num_redone() const { return num_redone_; }
    std::size_t get_num_undone() const { return num_undone_; }

private:
    LogManager& log_manager_;
    buffer::BufferPoolManager& buffer_pool_;
    
    // Statistics
    std::size_t num_redone_;
    std::size_t num_undone_;
};

}  // namespace forgedb::storage
