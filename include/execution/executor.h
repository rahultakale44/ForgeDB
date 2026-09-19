#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "parser/ast.h"
#include "storage/tuple.h"

namespace forgedb::execution {

struct ExecutionResult {
    bool success;
    std::string message;
    std::vector<storage::Tuple> tuples;
    std::size_t rows_affected;

    ExecutionResult()
        : success(false),
          rows_affected(0) {}
};

class Executor {
public:
    Executor(
        buffer::BufferPoolManager& buffer_pool,
        catalog::Catalog& catalog
    );

    ExecutionResult execute(const parser::StatementPtr& stmt);

private:
    ExecutionResult execute_create_table(
        const parser::CreateTableStatement* stmt
    );

    ExecutionResult execute_create_index(
        const parser::CreateIndexStatement* stmt
    );

    ExecutionResult execute_select(
        const parser::SelectStatement* stmt
    );

    ExecutionResult execute_sequential_scan(
        const parser::SelectStatement* stmt,
        const catalog::TableMetadata& metadata
    );

    ExecutionResult execute_index_scan(
        const parser::SelectStatement* stmt,
        const catalog::TableMetadata& metadata,
        std::size_t index_column,
        const std::string& index_name
    );

    ExecutionResult execute_insert(
        const parser::InsertStatement* stmt
    );

    std::optional<storage::Value> evaluate_expression(
        const parser::Expression* expr,
        const storage::Tuple& tuple,
        const catalog::TableMetadata& metadata
    ) const;

    bool evaluate_predicate(
        const parser::Expression* expr,
        const storage::Tuple& tuple,
        const catalog::TableMetadata& metadata
    ) const;

    storage::Tuple project_tuple(
        const storage::Tuple& tuple,
        const std::vector<std::string>& columns,
        const catalog::TableMetadata& metadata
    ) const;

    std::optional<std::size_t> find_column_index(
        const std::string& column_name,
        const catalog::TableMetadata& metadata
    ) const;

    buffer::BufferPoolManager& buffer_pool_;
    catalog::Catalog& catalog_;
};

}  // namespace forgedb::execution

