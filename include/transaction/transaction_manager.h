#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "transaction/transaction.h"

namespace forgedb::transaction {

class TransactionManager {
public:
    TransactionManager();

    ~TransactionManager();

    std::shared_ptr<Transaction> begin_transaction();

    bool commit(const std::shared_ptr<Transaction>& txn);

    bool abort(const std::shared_ptr<Transaction>& txn);

    std::shared_ptr<Transaction> get_transaction(TransactionId txn_id);

    std::size_t active_transaction_count() const;

    TransactionId next_transaction_id();

private:
    void cleanup_transaction(TransactionId txn_id);

    std::atomic<TransactionId> next_txn_id_;
    
    mutable std::mutex txn_map_mutex_;
    std::unordered_map<TransactionId, std::shared_ptr<Transaction>> 
        active_transactions_;
};

}  // namespace forgedb::transaction

