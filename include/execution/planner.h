#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "catalog/catalog.h"
#include "parser/ast.h"

namespace forgedb::execution {

enum class ScanType {
    SEQUENTIAL,
    INDEX
};

struct ExecutionPlan {
    ScanType scan_type;
    std::optional<std::string> index_name;
    std::optional<std::size_t> index_column;
    parser::ExpressionPtr filter_predicate;
    std::vector<std::string> projection_columns;
    
    // Cost estimates
    double estimated_cost;
    std::size_t estimated_rows;

    ExecutionPlan()
        : scan_type(ScanType::SEQUENTIAL),
          filter_predicate(nullptr),
          estimated_cost(0.0),
          estimated_rows(0) {}
};

class Planner {
public:
    explicit Planner(catalog::Catalog& catalog);

    std::optional<ExecutionPlan> plan_select(
        const parser::SelectStatement* stmt
    );

private:
    bool can_use_index_scan(
        const parser::Expression* where_clause,
        const catalog::TableMetadata& metadata,
        std::size_t& index_column,
        std::optional<std::int32_t>& search_key
    ) const;

    double estimate_sequential_scan_cost(
        const catalog::TableMetadata& metadata
    ) const;

    double estimate_index_scan_cost(
        const catalog::TableMetadata& metadata,
        std::size_t index_column
    ) const;

    bool is_equality_predicate_on_column(
        const parser::Expression* expr,
        std::size_t column_index,
        std::optional<std::int32_t>& key_value
    ) const;

    std::optional<std::size_t> find_column_index(
        const std::string& column_name,
        const catalog::TableMetadata& metadata
    ) const;

    catalog::Catalog& catalog_;
};

}  // namespace forgedb::execution

