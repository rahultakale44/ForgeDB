#include "transaction/lock_manager.h"

#include <algorithm>
#include <chrono>

namespace forgedb::transaction {

bool LockManager::lock_shared(
    const std::shared_ptr<Transaction>& txn,
    storage::PageId page_id
) {
    if (!txn || !txn->is_active()) {
        return false;
    }
    
    std::unique_lock<std::mutex> lock(latch_);
    
    auto& queue = lock_table_[page_id];
    
    // Check if transaction already holds a lock on this page
    auto it = std::find_if(
        queue.request_queue_.begin(),
        queue.request_queue_.end(),
        [&txn](const LockRequest& req) {
            return req.txn_id_ == txn->transaction_id() && req.granted_;
        }
    );
    
    if (it != queue.request_queue_.end()) {
        // Already have a lock - if exclusive, we're good; if shared, we're good
        return true;
    }
    
    // Add lock request to queue
    queue.request_queue_.emplace_back(txn->transaction_id(), LockMode::SHARED);
    
    // Wait until lock can be granted
    auto& request = queue.request_queue_.back();
    
    while (!can_grant_lock(queue, LockMode::SHARED) || 
           !request.granted_) {
        
        // Try to grant locks
        grant_locks(queue);
        
        if (request.granted_) {
            break;
        }
        
        // Wait with timeout to avoid deadlocks
        auto status = queue.cv_.wait_for(
            lock,
            std::chrono::milliseconds(100)
        );
        
        if (status == std::cv_status::timeout) {
            // Check if transaction is still active
            if (!txn->is_active()) {
                queue.request_queue_.remove_if(
                    [&txn](const LockRequest& req) {
                        return req.txn_id_ == txn->transaction_id();
                    }
                );
                return false;
            }
        }
    }
    
    // Lock granted - record it
    txn_lock_map_[txn->transaction_id()].insert(page_id);
    
    return true;
}

bool LockManager::lock_exclusive(
    const std::shared_ptr<Transaction>& txn,
    storage::PageId page_id
) {
    if (!txn || !txn->is_active()) {
        return false;
    }
    
    std::unique_lock<std::mutex> lock(latch_);
    
    auto& queue = lock_table_[page_id];
    
    // Check if transaction already holds an exclusive lock
    auto it = std::find_if(
        queue.request_queue_.begin(),
        queue.request_queue_.end(),
        [&txn](const LockRequest& req) {
            return req.txn_id_ == txn->transaction_id() && 
                   req.granted_ &&
                   req.mode_ == LockMode::EXCLUSIVE;
        }
    );
    
    if (it != queue.request_queue_.end()) {
        // Already have exclusive lock
        return true;
    }
    
    // Add lock request to queue
    queue.request_queue_.emplace_back(
        txn->transaction_id(),
        LockMode::EXCLUSIVE
    );
    
    // Wait until lock can be granted
    auto& request = queue.request_queue_.back();
    
    while (!can_grant_lock(queue, LockMode::EXCLUSIVE) || 
           !request.granted_) {
        
        // Try to grant locks
        grant_locks(queue);
        
        if (request.granted_) {
            break;
        }
        
        // Wait with timeout
        auto status = queue.cv_.wait_for(
            lock,
            std::chrono::milliseconds(100)
        );
        
        if (status == std::cv_status::timeout) {
            // Check if transaction is still active
            if (!txn->is_active()) {
                queue.request_queue_.remove_if(
                    [&txn](const LockRequest& req) {
                        return req.txn_id_ == txn->transaction_id();
                    }
                );
                return false;
            }
        }
    }
    
    // Lock granted - record it
    txn_lock_map_[txn->transaction_id()].insert(page_id);
    
    return true;
}

bool LockManager::lock_upgrade(
    const std::shared_ptr<Transaction>& txn,
    storage::PageId page_id
) {
    if (!txn || !txn->is_active()) {
        return false;
    }
    
    std::unique_lock<std::mutex> lock(latch_);
    
    auto& queue = lock_table_[page_id];
    
    // Find the transaction's shared lock
    auto it = std::find_if(
        queue.request_queue_.begin(),
        queue.request_queue_.end(),
        [&txn](const LockRequest& req) {
            return req.txn_id_ == txn->transaction_id() && 
                   req.granted_ &&
                   req.mode_ == LockMode::SHARED;
        }
    );
    
    if (it == queue.request_queue_.end()) {
        // No shared lock to upgrade
        return false;
    }
    
    // Check if already upgrading
    if (queue.upgrading_) {
        return false;
    }
    
    // Mark as upgrading
    queue.upgrading_ = true;
    
    // Change lock mode to exclusive
    it->mode_ = LockMode::EXCLUSIVE;
    it->granted_ = false;
    
    // Wait until upgrade can be granted
    while (!can_grant_lock(queue, LockMode::EXCLUSIVE) || 
           !it->granted_) {
        
        // Try to grant locks
        grant_locks(queue);
        
        if (it->granted_) {
            break;
        }
        
        // Wait with timeout
        auto status = queue.cv_.wait_for(
            lock,
            std::chrono::milliseconds(100)
        );
        
        if (status == std::cv_status::timeout) {
            // Check if transaction is still active
            if (!txn->is_active()) {
                queue.request_queue_.erase(it);
                queue.upgrading_ = false;
                return false;
            }
        }
    }
    
    queue.upgrading_ = false;
    
    return true;
}

bool LockManager::unlock(
    const std::shared_ptr<Transaction>& txn,
    storage::PageId page_id
) {
    if (!txn) {
        return false;
    }
    
    std::unique_lock<std::mutex> lock(latch_);
    
    auto queue_it = lock_table_.find(page_id);
    if (queue_it == lock_table_.end()) {
        return false;
    }
    
    auto& queue = queue_it->second;
    
    // Remove the transaction's lock request
    queue.request_queue_.remove_if(
        [&txn](const LockRequest& req) {
            return req.txn_id_ == txn->transaction_id();
        }
    );
    
    // Remove from transaction's lock set
    auto txn_it = txn_lock_map_.find(txn->transaction_id());
    if (txn_it != txn_lock_map_.end()) {
        txn_it->second.erase(page_id);
        
        if (txn_it->second.empty()) {
            txn_lock_map_.erase(txn_it);
        }
    }
    
    // Try to grant waiting locks
    grant_locks(queue);
    
    // Notify waiting threads
    queue.cv_.notify_all();
    
    // Clean up empty queue
    if (queue.request_queue_.empty()) {
        lock_table_.erase(queue_it);
    }
    
    return true;
}

void LockManager::unlock_all(const std::shared_ptr<Transaction>& txn) {
    if (!txn) {
        return;
    }
    
    std::unique_lock<std::mutex> lock(latch_);
    
    auto txn_it = txn_lock_map_.find(txn->transaction_id());
    if (txn_it == txn_lock_map_.end()) {
        return;
    }
    
    // Copy the set of pages to avoid iterator invalidation
    auto pages = txn_it->second;
    
    for (auto page_id : pages) {
        auto queue_it = lock_table_.find(page_id);
        if (queue_it == lock_table_.end()) {
            continue;
        }
        
        auto& queue = queue_it->second;
        
        // Remove transaction's lock requests
        queue.request_queue_.remove_if(
            [&txn](const LockRequest& req) {
                return req.txn_id_ == txn->transaction_id();
            }
        );
        
        // Try to grant waiting locks
        grant_locks(queue);
        
        // Notify waiting threads
        queue.cv_.notify_all();
        
        // Clean up empty queue
        if (queue.request_queue_.empty()) {
            lock_table_.erase(queue_it);
        }
    }
    
    // Remove transaction from map
    txn_lock_map_.erase(txn_it);
}

bool LockManager::holds_lock(
    const std::shared_ptr<Transaction>& txn,
    storage::PageId page_id,
    LockMode mode
) const {
    if (!txn) {
        return false;
    }
    
    std::unique_lock<std::mutex> lock(latch_);
    
    auto queue_it = lock_table_.find(page_id);
    if (queue_it == lock_table_.end()) {
        return false;
    }
    
    const auto& queue = queue_it->second;
    
    auto it = std::find_if(
        queue.request_queue_.begin(),
        queue.request_queue_.end(),
        [&txn, mode](const LockRequest& req) {
            return req.txn_id_ == txn->transaction_id() && 
                   req.granted_ &&
                   req.mode_ == mode;
        }
    );
    
    return it != queue.request_queue_.end();
}

bool LockManager::can_grant_lock(
    const LockRequestQueue& queue,
    LockMode mode
) const {
    if (queue.request_queue_.empty()) {
        return true;
    }
    
    // Count granted locks by mode
    int granted_shared = 0;
    int granted_exclusive = 0;
    
    for (const auto& req : queue.request_queue_) {
        if (req.granted_) {
            if (req.mode_ == LockMode::SHARED) {
                granted_shared++;
            } else {
                granted_exclusive++;
            }
        }
    }
    
    if (mode == LockMode::SHARED) {
        // Can grant shared if no exclusive locks are held
        return granted_exclusive == 0 && !queue.upgrading_;
    } else {
        // Can grant exclusive only if no locks are held
        return granted_shared == 0 && granted_exclusive == 0;
    }
}

void LockManager::grant_locks(LockRequestQueue& queue) {
    for (auto& req : queue.request_queue_) {
        if (req.granted_) {
            continue;
        }
        
        if (can_grant_lock(queue, req.mode_)) {
            req.granted_ = true;
            
            // For exclusive locks, stop granting
            if (req.mode_ == LockMode::EXCLUSIVE) {
                break;
            }
        } else {
            // Can't grant this request, stop trying
            break;
        }
    }
}

}  // namespace forgedb::transaction
