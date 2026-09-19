#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "storage/page.h"
#include "transaction/transaction.h"

namespace forgedb::storage {

using LSN = std::uint64_t;

constexpr LSN INVALID_LSN = 0;

enum class LogRecordType : std::uint8_t {
    BEGIN,
    COMMIT,
    ABORT,
    INSERT,
    UPDATE,
    DELETE
};

struct LogRecord {
    LSN lsn;
    transaction::TransactionId txn_id;
    LogRecordType type;
    
    // For data modification records
    PageId page_id;
    std::vector<std::byte> before_image;
    std::vector<std::byte> after_image;
    
    LogRecord()
        : lsn(INVALID_LSN),
          txn_id(transaction::INVALID_TXN_ID),
          type(LogRecordType::BEGIN),
          page_id(0) {}
    
    std::size_t serialized_size() const;
    
    bool serialize(std::byte* buffer, std::size_t buffer_size) const;
    
    static bool deserialize(
        const std::byte* buffer,
        std::size_t buffer_size,
        LogRecord& record
    );
};

}  // namespace forgedb::storage

