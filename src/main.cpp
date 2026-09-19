#include <chrono>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "execution/executor.h"
#include "execution/planner.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/disk_manager.h"
#include "storage/log_manager.h"
#include "storage/recovery_manager.h"
#include "transaction/lock_manager.h"
#include "transaction/transaction_manager.h"

using namespace forgedb;
using ExecutionResult = execution::ExecutionResult;

class ForgeDBShell {
public:
    ForgeDBShell(const std::string& db_file, const std::string& log_file)
        : db_file_(db_file),
          log_file_(log_file),
          disk_manager_(db_file),
          buffer_pool_(100, disk_manager_),
          log_manager_(log_file),
          lock_manager_(),
          txn_manager_(&log_manager_, &lock_manager_),
          catalog_(buffer_pool_),
          planner_(catalog_),
          executor_(buffer_pool_, catalog_) {
        
        // Perform recovery if database exists
        if (std::filesystem::exists(log_file)) {
            std::cout << "Performing recovery...\n";
            storage::RecoveryManager recovery_mgr(log_manager_, buffer_pool_);
            recovery_mgr.recover();
            std::cout << "Recovery complete.\n\n";
        }
    }
    
    void run() {
        print_banner();
        print_help();
        
        std::string line;
        std::string query;
        
        while (true) {
            if (query.empty()) {
                std::cout << "forgedb> ";
            } else {
                std::cout << "      -> ";
            }
            
            if (!std::getline(std::cin, line)) {
                break;
            }
            
            // Add line to query
            query += line;
            
            // Check for command terminator
            if (query.empty()) {
                continue;
            }
            
            // Handle meta-commands (commands starting with \)
            if (query[0] == '\\') {
                handle_meta_command(query);
                query.clear();
                continue;
            }
            
            // Check if query ends with semicolon
            if (query.back() != ';') {
                query += " ";
                continue;
            }
            
            // Execute the query
            execute_query(query);
            query.clear();
        }
        
        std::cout << "\nGoodbye!\n";
    }

private:
    void print_banner() {
        std::cout << "╔════════════════════════════════════════╗\n";
        std::cout << "║         ForgeDB v0.1.0                 ║\n";
        std::cout << "║   A Lightweight Database Engine        ║\n";
        std::cout << "╚════════════════════════════════════════╝\n\n";
    }
    
    void print_help() {
        std::cout << "Commands:\n";
        std::cout << "  \\help     - Show this help message\n";
        std::cout << "  \\tables   - List all tables\n";
        std::cout << "  \\desc <table> - Describe table schema\n";
        std::cout << "  \\quit     - Exit the shell\n";
        std::cout << "\nSQL Commands:\n";
        std::cout << "  CREATE TABLE, CREATE INDEX, INSERT, SELECT\n";
        std::cout << "  End statements with semicolon (;)\n\n";
    }
    
    void handle_meta_command(const std::string& cmd) {
        std::string command = cmd;
        
        // Remove leading backslash
        if (!command.empty() && command[0] == '\\') {
            command = command.substr(1);
        }
        
        // Remove trailing whitespace and semicolon
        while (!command.empty() && 
               (command.back() == ' ' || 
                command.back() == '\n' || 
                command.back() == ';')) {
            command.pop_back();
        }
        
        if (command == "quit" || command == "exit" || command == "q") {
            std::exit(0);
        } else if (command == "help" || command == "h" || command == "?") {
            print_help();
        } else if (command == "tables") {
            list_tables();
        } else if (command.substr(0, 4) == "desc") {
            std::string table_name = command.substr(4);
            // Trim leading whitespace
            size_t start = table_name.find_first_not_of(" \t\n");
            if (start != std::string::npos) {
                table_name = table_name.substr(start);
            }
            describe_table(table_name);
        } else {
            std::cout << "Unknown command: \\" << command << "\n";
            std::cout << "Type \\help for help.\n";
        }
    }
    
    void list_tables() {
        auto tables = catalog_.list_tables();
        
        if (tables.empty()) {
            std::cout << "No tables found.\n";
            return;
        }
        
        std::cout << "\nTables:\n";
        std::cout << "──────────────────────────────────────\n";
        
        for (const auto& table_name : tables) {
            auto metadata = catalog_.get_table(table_name);
            if (metadata) {
                std::cout << "  " << table_name 
                          << " (" << metadata->columns.size() 
                          << " columns)\n";
            }
        }
        
        std::cout << "\n";
    }
    
    void describe_table(const std::string& table_name) {
        auto metadata = catalog_.get_table(table_name);
        
        if (!metadata) {
            std::cout << "Table '" << table_name << "' not found.\n";
            return;
        }
        
        std::cout << "\nTable: " << table_name << "\n";
        std::cout << "──────────────────────────────────────\n";
        std::cout << std::left << std::setw(20) << "Column" 
                  << std::setw(15) << "Type" 
                  << std::setw(10) << "Nullable\n";
        std::cout << "──────────────────────────────────────\n";
        
        for (const auto& col : metadata->columns) {
            std::cout << std::left << std::setw(20) << col.name;
            
            std::string type_str;
            switch (col.type) {
                case storage::ValueType::INTEGER:
                    type_str = "INTEGER";
                    break;
                case storage::ValueType::BOOLEAN:
                    type_str = "BOOLEAN";
                    break;
                case storage::ValueType::VARCHAR:
                    type_str = "VARCHAR";
                    break;
            }
            
            std::cout << std::setw(15) << type_str
                      << std::setw(10) << (col.nullable ? "YES" : "NO")
                      << "\n";
        }
        
        // Show indexes
        if (!metadata->indexes.empty()) {
            std::cout << "\nIndexes:\n";
            for (const auto& idx : metadata->indexes) {
                std::cout << "  " << idx.index_name 
                          << " on column " << idx.column_index << "\n";
            }
        }
        
        std::cout << "\n";
    }
    
    void execute_query(const std::string& query) {
        try {
            // Tokenize the query
            parser::Lexer lexer(query);
            auto tokens = lexer.tokenize();
            
            // Parse the query
            parser::Parser parser(tokens);
            auto stmt = parser.parse();
            
            if (!stmt) {
                std::cout << "Error: " << parser.error() << "\n\n";
                return;
            }
            
            // Begin transaction
            auto txn = txn_manager_.begin_transaction();
            
            // Handle SELECT separately (uses planner)
            ExecutionResult result;
            auto start = std::chrono::high_resolution_clock::now();
            
            if (stmt.value()->type == parser::StatementType::SELECT) {
                auto select_stmt = dynamic_cast<const parser::SelectStatement*>(stmt.value().get());
                auto plan = planner_.plan_select(select_stmt);
                
                if (plan) {
                    // Use plan hint (for now, executor still does its own thing)
                    result = executor_.execute(stmt.value());
                } else {
                    result = executor_.execute(stmt.value());
                }
            } else {
                result = executor_.execute(stmt.value());
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            
            // Display results
            if (result.success) {
                display_result(result, stmt.value()->type);
                
                // Show execution time
                auto duration = std::chrono::duration_cast<
                    std::chrono::milliseconds>(end - start);
                std::cout << "Query executed in " << duration.count() 
                          << " ms\n\n";
                
                // Commit transaction
                txn_manager_.commit(txn);
            } else {
                std::cout << "Error: " << result.message << "\n\n";
                txn_manager_.abort(txn);
            }
            
        } catch (const std::exception& e) {
            std::cout << "Error: " << e.what() << "\n\n";
        }
    }
    
    void display_result(
        const execution::ExecutionResult& result,
        parser::StatementType stmt_type
    ) {
        if (stmt_type == parser::StatementType::SELECT) {
            display_select_result(result);
        } else {
            display_modification_result(result, stmt_type);
        }
    }
    
    void display_select_result(const execution::ExecutionResult& result) {
        if (result.tuples.empty()) {
            std::cout << "No rows returned.\n";
            return;
        }
        
        // For now, just show simple tuple output
        // TODO: Format properly once we have schema in ExecutionResult
        std::cout << "\n" << result.tuples.size() 
                  << (result.tuples.size() == 1 ? " row" : " rows") 
                  << " returned.\n";
    }
    
    void display_modification_result(
        const execution::ExecutionResult& result,
        parser::StatementType stmt_type
    ) {
        switch (stmt_type) {
            case parser::StatementType::CREATE_TABLE:
                std::cout << "Table created successfully.\n";
                break;
            case parser::StatementType::CREATE_INDEX:
                std::cout << "Index created successfully.\n";
                break;
            case parser::StatementType::INSERT:
                std::cout << result.rows_affected 
                          << (result.rows_affected == 1 ? " row" : " rows")
                          << " inserted.\n";
                break;
            default:
                std::cout << "Query executed successfully.\n";
                break;
        }
    }
    
    std::string value_to_string(const storage::Value& value) const {
        if (value.is_null()) {
            return "NULL";
        }
        
        switch (value.type()) {
            case storage::ValueType::INTEGER:
                if (value.as_int()) {
                    return std::to_string(*value.as_int());
                }
                return "NULL";
            case storage::ValueType::BOOLEAN:
                if (value.as_bool()) {
                    return *value.as_bool() ? "true" : "false";
                }
                return "NULL";
            case storage::ValueType::VARCHAR:
                if (value.as_string()) {
                    return *value.as_string();
                }
                return "NULL";
        }
        
        return "";
    }
    
    std::string db_file_;
    std::string log_file_;
    
    storage::DiskManager disk_manager_;
    buffer::BufferPoolManager buffer_pool_;
    storage::LogManager log_manager_;
    transaction::LockManager lock_manager_;
    transaction::TransactionManager txn_manager_;
    catalog::Catalog catalog_;
    execution::Planner planner_;
    execution::Executor executor_;
};

int main(int argc, char* argv[]) {
    std::string db_file = "forgedb.db";
    std::string log_file = "forgedb.log";
    
    // Parse command-line arguments
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        
        if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: forgedb [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --db <file>     Database file (default: forgedb.db)\n";
            std::cout << "  --log <file>    Log file (default: forgedb.log)\n";
            std::cout << "  --help, -h      Show this help message\n";
            return 0;
        } else if (arg == "--db" && i + 1 < argc) {
            db_file = argv[++i];
        } else if (arg == "--log" && i + 1 < argc) {
            log_file = argv[++i];
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            std::cerr << "Use --help for usage information.\n";
            return 1;
        }
    }
    
    try {
        ForgeDBShell shell(db_file, log_file);
        shell.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
    
    return 0;
}
