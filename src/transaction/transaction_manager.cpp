#include "transaction/transaction_manager.h"

namespace forgedb::transaction {

TransactionManager::TransactionManager()
    : next_txn_id_(1) {}

TransactionManager::~TransactionManager() {
    // Abort any remaining active transactions
    std::lock_guard<std::mutex> lock(txn_map_mutex_);
    
    for (auto& [txn_id, txn] : active_transactions_) {
        if (txn && txn->is_active()) {
            txn->set_state(TransactionState::ABORTED);
        }
    }
    
    active_transactions_.clear();
}

std::shared_ptr<Transaction> TransactionManager::begin_transaction() {
    TransactionId txn_id = next_txn_id_.fetch_add(1);
    
    auto txn = std::make_shared<Transaction>(txn_id);
    
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
    
    // Mark transaction as committed
    txn->set_state(TransactionState::COMMITTED);
    
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
    
    // Mark transaction as aborted
    txn->set_state(TransactionState::ABORTED);
    
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

