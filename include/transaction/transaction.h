#pragma once

#include <atomic>
#include <cstdint>

namespace forgedb::transaction {

using TransactionId = std::uint64_t;

constexpr TransactionId INVALID_TXN_ID = 0;

enum class TransactionState {
    ACTIVE,
    COMMITTED,
    ABORTED
};

class Transaction {
public:
    explicit Transaction(TransactionId txn_id);

    TransactionId transaction_id() const;
    TransactionState state() const;

    void set_state(TransactionState state);

    bool is_active() const;
    bool is_committed() const;
    bool is_aborted() const;

private:
    TransactionId txn_id_;
    TransactionState state_;
};

}  // namespace forgedb::transaction

