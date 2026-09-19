#include "transaction/transaction_manager.h"

#include "storage/log_record.h"

namespace forgedb::transaction {

TransactionManager::TransactionManager()
    : next_txn_id_(1),
      log_manager_(nullptr),
      lock_manager_(nullptr) {}

TransactionManager::TransactionManager(
    storage::LogManager* log_manager,
    LockManager* lock_manager
)
    : next_txn_id_(1),
      log_manager_(log_manager),
      lock_manager_(lock_manager) {}

TransactionManager::~TransactionManager() {
    // Abort any remaining active transactions
    std::lock_guard<std::mutex> lock(txn_map_mutex_);
    
    for (auto& [txn_id, txn] : active_transactions_) {
        if (txn && txn->is_active()) {
            // Log abort if we have a log manager
            if (log_manager_) {
                storage::LogRecord abort_record;
                abort_record.txn_id = txn_id;
                abort_record.type = storage::LogRecordType::ABORT;
                log_manager_->append_log_record(abort_record);
                log_manager_->flush();
            }
            
            // Release locks if we have a lock manager
            if (lock_manager_) {
                lock_manager_->unlock_all(txn);
            }
            
            txn->set_state(TransactionState::ABORTED);
        }
    }
    
    active_transactions_.clear();
}

std::shared_ptr<Transaction> TransactionManager::begin_transaction() {
    TransactionId txn_id = next_txn_id_.fetch_add(1);
    
    auto txn = std::make_shared<Transaction>(txn_id);
    
    // Log BEGIN record if we have a log manager
    if (log_manager_) {
        storage::LogRecord begin_record;
        begin_record.txn_id = txn_id;
        begin_record.type = storage::LogRecordType::BEGIN;
        log_manager_->append_log_record(begin_record);
    }
    
    std::lock_guard<std::mutex> lock(txn_map_mutex_);
    active_transactions_[txn_id] = txn;
    
    return txn;
}

bool TransactionManager::commit(
    const std::shared_ptr<Transaction>& txn
) {
    if (!txn) {
        return false;
    }
    
    if (!txn->is_active()) {
        return false;
    }
    
    // Log COMMIT record if we have a log manager
    if (log_manager_) {
        storage::LogRecord commit_record;
        commit_record.txn_id = txn->transaction_id();
        commit_record.type = storage::LogRecordType::COMMIT;
        log_manager_->append_log_record(commit_record);
        
        // Force log to disk for durability
        log_manager_->flush();
    }
    
    // Mark transaction as committed
    txn->set_state(TransactionState::COMMITTED);
    
    // Release all locks if we have a lock manager
    if (lock_manager_) {
        lock_manager_->unlock_all(txn);
    }
    
    // Clean up from active transactions
    cleanup_transaction(txn->transaction_id());
    
    return true;
}

bool TransactionManager::abort(
    const std::shared_ptr<Transaction>& txn
) {
    if (!txn) {
        return false;
    }
    
    if (!txn->is_active()) {
        return false;
    }
    
    // Log ABORT record if we have a log manager
    if (log_manager_) {
        storage::LogRecord abort_record;
        abort_record.txn_id = txn->transaction_id();
        abort_record.type = storage::LogRecordType::ABORT;
        log_manager_->append_log_record(abort_record);
        log_manager_->flush();
    }
    
    // Mark transaction as aborted
    txn->set_state(TransactionState::ABORTED);
    
    // Release all locks if we have a lock manager
    if (lock_manager_) {
        lock_manager_->unlock_all(txn);
    }
    
    // Clean up from active transactions
    cleanup_transaction(txn->transaction_id());
    
    return true;
}

std::shared_ptr<Transaction> TransactionManager::get_transaction(
    TransactionId txn_id
) {
    std::lock_guard<std::mutex> lock(txn_map_mutex_);
    
    auto it = active_transactions_.find(txn_id);
    
    if (it == active_transactions_.end()) {
        return nullptr;
    }
    
    return it->second;
}

std::size_t TransactionManager::active_transaction_count() const {
    std::lock_guard<std::mutex> lock(txn_map_mutex_);
    return active_transactions_.size();
}

TransactionId TransactionManager::next_transaction_id() {
    return next_txn_id_.load();
}

void TransactionManager::cleanup_transaction(TransactionId txn_id) {
    std::lock_guard<std::mutex> lock(txn_map_mutex_);
    active_transactions_.erase(txn_id);
}

}  // namespace forgedb::transaction

