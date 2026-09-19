#include "storage/log_record.h"

#include <cstring>

namespace forgedb::storage {

std::size_t LogRecord::serialized_size() const {
    std::size_t size = 0;
    
    size += sizeof(LSN);                             // lsn
    size += sizeof(transaction::TransactionId);      // txn_id
    size += sizeof(std::uint8_t);                    // type
    size += sizeof(PageId);                          // page_id
    size += sizeof(std::uint32_t);                   // before_image size
    size += before_image.size();                     // before_image data
    size += sizeof(std::uint32_t);                   // after_image size
    size += after_image.size();                      // after_image data
    
    return size;
}

bool LogRecord::serialize(
    std::byte* buffer,
    std::size_t buffer_size
) const {
    if (buffer_size < serialized_size()) {
        return false;
    }
    
    std::size_t offset = 0;
    
    // LSN
    std::memcpy(buffer + offset, &lsn, sizeof(LSN));
    offset += sizeof(LSN);
    
    // Transaction ID
    std::memcpy(buffer + offset, &txn_id, sizeof(transaction::TransactionId));
    offset += sizeof(transaction::TransactionId);
    
    // Record type
    std::uint8_t type_byte = static_cast<std::uint8_t>(type);
    std::memcpy(buffer + offset, &type_byte, sizeof(std::uint8_t));
    offset += sizeof(std::uint8_t);
    
    // Page ID
    std::memcpy(buffer + offset, &page_id, sizeof(PageId));
    offset += sizeof(PageId);
    
    // Before image
    std::uint32_t before_size = static_cast<std::uint32_t>(before_image.size());
    std::memcpy(buffer + offset, &before_size, sizeof(std::uint32_t));
    offset += sizeof(std::uint32_t);
    
    if (before_size > 0) {
        std::memcpy(buffer + offset, before_image.data(), before_size);
        offset += before_size;
    }
    
    // After image
    std::uint32_t after_size = static_cast<std::uint32_t>(after_image.size());
    std::memcpy(buffer + offset, &after_size, sizeof(std::uint32_t));
    offset += sizeof(std::uint32_t);
    
    if (after_size > 0) {
        std::memcpy(buffer + offset, after_image.data(), after_size);
        offset += after_size;
    }
    
    return true;
}

bool LogRecord::deserialize(
    const std::byte* buffer,
    std::size_t buffer_size,
    LogRecord& record
) {
    std::size_t offset = 0;
    
    // LSN
    if (offset + sizeof(LSN) > buffer_size) {
        return false;
    }
    std::memcpy(&record.lsn, buffer + offset, sizeof(LSN));
    offset += sizeof(LSN);
    
    // Transaction ID
    if (offset + sizeof(transaction::TransactionId) > buffer_size) {
        return false;
    }
    std::memcpy(&record.txn_id, buffer + offset, sizeof(transaction::TransactionId));
    offset += sizeof(transaction::TransactionId);
    
    // Record type
    if (offset + sizeof(std::uint8_t) > buffer_size) {
        return false;
    }
    std::uint8_t type_byte = 0;
    std::memcpy(&type_byte, buffer + offset, sizeof(std::uint8_t));
    record.type = static_cast<LogRecordType>(type_byte);
    offset += sizeof(std::uint8_t);
    
    // Page ID
    if (offset + sizeof(PageId) > buffer_size) {
        return false;
    }
    std::memcpy(&record.page_id, buffer + offset, sizeof(PageId));
    offset += sizeof(PageId);
    
    // Before image
    if (offset + sizeof(std::uint32_t) > buffer_size) {
        return false;
    }
    std::uint32_t before_size = 0;
    std::memcpy(&before_size, buffer + offset, sizeof(std::uint32_t));
    offset += sizeof(std::uint32_t);
    
    if (before_size > 0) {
        if (offset + before_size > buffer_size) {
            return false;
        }
        record.before_image.resize(before_size);
        std::memcpy(record.before_image.data(), buffer + offset, before_size);
        offset += before_size;
    }
    
    // After image
    if (offset + sizeof(std::uint32_t) > buffer_size) {
        return false;
    }
    std::uint32_t after_size = 0;
    std::memcpy(&after_size, buffer + offset, sizeof(std::uint32_t));
    offset += sizeof(std::uint32_t);
    
    if (after_size > 0) {
        if (offset + after_size > buffer_size) {
            return false;
        }
        record.after_image.resize(after_size);
        std::memcpy(record.after_image.data(), buffer + offset, after_size);
        offset += after_size;
    }
    
    return true;
}

}  // namespace forgedb::storage

