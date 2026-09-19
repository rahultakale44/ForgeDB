#pragma once

#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "storage/log_record.h"

namespace forgedb::storage {

class LogManager {
public:
    explicit LogManager(const std::string& log_file_path);
    
    ~LogManager();
    
    LSN append_log_record(const LogRecord& record);
    
    bool read_log_record(LSN lsn, LogRecord& record);
    
    void flush();
    
    LSN get_next_lsn() const;
    
    std::vector<LogRecord> read_all_records();

private:
    bool write_to_disk(const std::byte* data, std::size_t size);
    
    bool read_from_disk(std::size_t offset, std::byte* data, std::size_t size);
    
    std::string log_file_path_;
    std::fstream log_file_;
    
    mutable std::mutex log_mutex_;
    
    LSN next_lsn_;
    std::size_t log_offset_;
    
    // In-memory buffer for recent log records
    std::vector<std::byte> log_buffer_;
    static constexpr std::size_t LOG_BUFFER_SIZE = 4096;
};

}  // namespace forgedb::storage

