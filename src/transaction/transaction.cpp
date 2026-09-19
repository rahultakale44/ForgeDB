#include "transaction/transaction.h"

namespace forgedb::transaction {

Transaction::Transaction(TransactionId txn_id)
    : txn_id_(txn_id),
      state_(TransactionState::ACTIVE) {}

TransactionId Transaction::transaction_id() const {
    return txn_id_;
}

TransactionState Transaction::state() const {
    return state_;
}

void Transaction::set_state(TransactionState state) {
    state_ = state;
}

bool Transaction::is_active() const {
    return state_ == TransactionState::ACTIVE;
}

bool Transaction::is_committed() const {
    return state_ == TransactionState::COMMITTED;
}

bool Transaction::is_aborted() const {
    return state_ == TransactionState::ABORTED;
}

}  // namespace forgedb::transaction

