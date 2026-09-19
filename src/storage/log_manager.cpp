#include "storage/log_manager.h"

#include <filesystem>

namespace forgedb::storage {

LogManager::LogManager(const std::string& log_file_path)
    : log_file_path_(log_file_path),
      next_lsn_(1),
      log_offset_(0) {
    
    // Open log file for reading and writing
    bool file_exists = std::filesystem::exists(log_file_path_);
    
    if (file_exists) {
        log_file_.open(
            log_file_path_,
            std::ios::in | std::ios::out | std::ios::binary
        );
        
        if (log_file_.is_open()) {
            // Determine current log offset and next LSN
            log_file_.seekg(0, std::ios::end);
            log_offset_ = log_file_.tellg();
            
            // Read existing log to determine next LSN
            if (log_offset_ > 0) {
                auto records = read_all_records();
                if (!records.empty()) {
                    next_lsn_ = records.back().lsn + 1;
                }
            }
        }
    } else {
        log_file_.open(
            log_file_path_,
            std::ios::out | std::ios::binary
        );
        log_file_.close();
        
        log_file_.open(
            log_file_path_,
            std::ios::in | std::ios::out | std::ios::binary
        );
    }
    
    log_buffer_.reserve(LOG_BUFFER_SIZE);
}

LogManager::~LogManager() {
    flush();
    
    if (log_file_.is_open()) {
        log_file_.close();
    }
}

LSN LogManager::append_log_record(const LogRecord& record) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    // Assign LSN to the record
    LogRecord mutable_record = record;
    mutable_record.lsn = next_lsn_;
    
    // Serialize the record
    std::size_t record_size = mutable_record.serialized_size();
    std::vector<std::byte> serialized_data(record_size);
    
    if (!mutable_record.serialize(serialized_data.data(), record_size)) {
        return INVALID_LSN;
    }
    
    // Write size prefix
    std::uint32_t size_prefix = static_cast<std::uint32_t>(record_size);
    if (!write_to_disk(
            reinterpret_cast<const std::byte*>(&size_prefix),
            sizeof(std::uint32_t)
        )) {
        return INVALID_LSN;
    }
    
    // Write record data
    if (!write_to_disk(serialized_data.data(), record_size)) {
        return INVALID_LSN;
    }
    
    log_offset_ += sizeof(std::uint32_t) + record_size;
    
    LSN assigned_lsn = next_lsn_;
    next_lsn_++;
    
    return assigned_lsn;
}

bool LogManager::read_log_record(LSN lsn, LogRecord& record) {
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    // For simplicity, read all records and find the one with matching LSN
    // In a real system, we'd maintain an index of LSN -> offset
    
    if (!log_file_.is_open()) {
        return false;
    }
    
    log_file_.clear();
    log_file_.seekg(0, std::ios::beg);
    
    while (log_file_.tellg() < static_cast<std::streamoff>(log_offset_)) {
        // Read size prefix
        std::uint32_t record_size = 0;
        log_file_.read(
            reinterpret_cast<char*>(&record_size),
            sizeof(std::uint32_t)
        );
        
        if (log_file_.gcount() != sizeof(std::uint32_t)) {
            break;
        }
        
        // Read record data
        std::vector<std::byte> record_data(record_size);
        log_file_.read(
            reinterpret_cast<char*>(record_data.data()),
            record_size
        );
        
        if (log_file_.gcount() != static_cast<std::streamsize>(record_size)) {
            break;
        }
        
        // Deserialize
        LogRecord temp_record;
        if (LogRecord::deserialize(
                record_data.data(),
                record_size,
                temp_record
            )) {
            if (temp_record.lsn == lsn) {
                record = temp_record;
                return true;
            }
        }
    }
    
    return false;
}

void LogManager::flush() {
    std::lock_guard<std::mutex> lock(log_mutex_);
    
    if (log_file_.is_open()) {
        log_file_.flush();
    }
}

LSN LogManager::get_next_lsn() const {
    std::lock_guard<std::mutex> lock(log_mutex_);
    return next_lsn_;
}

std::vector<LogRecord> LogManager::read_all_records() {
    std::vector<LogRecord> records;
    
    if (!log_file_.is_open()) {
        return records;
    }
    
    log_file_.clear();
    log_file_.seekg(0, std::ios::beg);
    
    while (log_file_.tellg() < static_cast<std::streamoff>(log_offset_)) {
        // Read size prefix
        std::uint32_t record_size = 0;
        log_file_.read(
            reinterpret_cast<char*>(&record_size),
            sizeof(std::uint32_t)
        );
        
        if (log_file_.gcount() != sizeof(std::uint32_t)) {
            break;
        }
        
        // Read record data
        std::vector<std::byte> record_data(record_size);
        log_file_.read(
            reinterpret_cast<char*>(record_data.data()),
            record_size
        );
        
        if (log_file_.gcount() != static_cast<std::streamsize>(record_size)) {
            break;
        }
        
        // Deserialize
        LogRecord record;
        if (LogRecord::deserialize(
                record_data.data(),
                record_size,
                record
            )) {
            records.push_back(record);
        }
    }
    
    return records;
}

bool LogManager::write_to_disk(const std::byte* data, std::size_t size) {
    if (!log_file_.is_open()) {
        return false;
    }
    
    log_file_.write(reinterpret_cast<const char*>(data), size);
    
    return log_file_.good();
}

bool LogManager::read_from_disk(
    std::size_t offset,
    std::byte* data,
    std::size_t size
) {
    if (!log_file_.is_open()) {
        return false;
    }
    
    log_file_.clear();
    log_file_.seekg(offset, std::ios::beg);
    log_file_.read(reinterpret_cast<char*>(data), size);
    
    return log_file_.gcount() == static_cast<std::streamsize>(size);
}

}  // namespace forgedb::storage

