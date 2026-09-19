#pragma once

#include <condition_variable>
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "storage/page.h"
#include "transaction/transaction.h"

namespace forgedb::transaction {

enum class LockMode {
    SHARED,
    EXCLUSIVE
};

class LockRequest {
public:
    LockRequest(TransactionId txn_id, LockMode mode)
        : txn_id_(txn_id), mode_(mode), granted_(false) {}
    
    TransactionId txn_id_;
    LockMode mode_;
    bool granted_;
};

class LockRequestQueue {
public:
    std::list<LockRequest> request_queue_;
    std::condition_variable cv_;
    bool upgrading_ = false;
};

class LockManager {
public:
    LockManager() = default;
    
    ~LockManager() = default;
    
    // Acquire a shared lock on a page
    bool lock_shared(
        const std::shared_ptr<Transaction>& txn,
        storage::PageId page_id
    );
    
    // Acquire an exclusive lock on a page
    bool lock_exclusive(
        const std::shared_ptr<Transaction>& txn,
        storage::PageId page_id
    );
    
    // Upgrade a shared lock to exclusive
    bool lock_upgrade(
        const std::shared_ptr<Transaction>& txn,
        storage::PageId page_id
    );
    
    // Release a lock on a page
    bool unlock(
        const std::shared_ptr<Transaction>& txn,
        storage::PageId page_id
    );
    
    // Release all locks held by a transaction
    void unlock_all(const std::shared_ptr<Transaction>& txn);
    
    // Check if transaction holds a specific lock
    bool holds_lock(
        const std::shared_ptr<Transaction>& txn,
        storage::PageId page_id,
        LockMode mode
    ) const;

private:
    bool can_grant_lock(
        const LockRequestQueue& queue,
        LockMode mode
    ) const;
    
    void grant_locks(LockRequestQueue& queue);
    
    mutable std::mutex latch_;
    
    // Map from page_id to lock request queue
    std::unordered_map<storage::PageId, LockRequestQueue> lock_table_;
    
    // Map from transaction to set of pages it holds locks on
    std::unordered_map<TransactionId, std::unordered_set<storage::PageId>>
        txn_lock_map_;
};

}  // namespace forgedb::transaction
