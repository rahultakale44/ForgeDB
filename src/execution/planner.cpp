#include "execution/planner.h"

#include <cmath>

namespace forgedb::execution {

Planner::Planner(catalog::Catalog& catalog)
    : catalog_(catalog) {}

std::optional<ExecutionPlan> Planner::plan_select(
    const parser::SelectStatement* stmt
) {
    if (!stmt) {
        return std::nullopt;
    }

    auto metadata = catalog_.get_table(stmt->table_name);

    if (!metadata.has_value()) {
        return std::nullopt;
    }

    ExecutionPlan plan;

    // Copy projection columns
    plan.projection_columns = stmt->columns;

    // Default to sequential scan
    plan.scan_type = ScanType::SEQUENTIAL;
    plan.estimated_cost = estimate_sequential_scan_cost(*metadata);
    plan.estimated_rows = 100;  // Rough estimate

    // Try to find an index scan opportunity
    if (stmt->where_clause) {
        std::size_t index_column = 0;
        std::optional<std::int32_t> search_key;

        if (can_use_index_scan(
                stmt->where_clause.get(),
                *metadata,
                index_column,
                search_key
            )) {
            // Check if there's actually an index on this column
            bool has_index = false;
            std::string index_name;

            for (const auto& idx : metadata->indexes) {
                if (idx.column_index == index_column) {
                    has_index = true;
                    index_name = idx.index_name;
                    break;
                }
            }

            if (has_index) {
                double index_cost = estimate_index_scan_cost(
                    *metadata,
                    index_column
                );

                // Use index if it's cheaper
                if (index_cost < plan.estimated_cost) {
                    plan.scan_type = ScanType::INDEX;
                    plan.index_name = index_name;
                    plan.index_column = index_column;
                    plan.estimated_cost = index_cost;
                    plan.estimated_rows = 1;  // Assume point query

                    // For index scan, we still need to apply other filters
                    // Clone the WHERE clause for runtime filtering
                    // (In a real system, we'd decompose the predicate)
                }
            }
        }

        // If not using index scan, keep the WHERE clause for filtering
        if (plan.scan_type == ScanType::SEQUENTIAL) {
            // Note: We don't clone the expression here since we'll
            // use the original from the statement during execution
        }
    }

    return plan;
}

bool Planner::can_use_index_scan(
    const parser::Expression* where_clause,
    const catalog::TableMetadata& metadata,
    std::size_t& index_column,
    std::optional<std::int32_t>& search_key
) const {
    if (!where_clause) {
        return false;
    }

    // Check if this is a simple equality predicate on an indexed column
    if (where_clause->type == parser::ExpressionType::BINARY_OP) {
        auto* binary_op = dynamic_cast<const parser::BinaryOpExpression*>(
            where_clause
        );

        if (binary_op->op == parser::BinaryOperator::EQUALS) {
            // Check if left side is a column reference
            if (binary_op->left->type ==
                parser::ExpressionType::COLUMN_REF) {
                auto* col_ref = dynamic_cast<
                    const parser::ColumnRefExpression*
                >(binary_op->left.get());

                auto col_idx = find_column_index(
                    col_ref->column_name,
                    metadata
                );

                if (col_idx.has_value()) {
                    // Check if right side is an integer literal
                    if (binary_op->right->type ==
                        parser::ExpressionType::LITERAL) {
                        auto* literal = dynamic_cast<
                            const parser::LiteralExpression*
                        >(binary_op->right.get());

                        if (literal->value.type() ==
                            storage::ValueType::INTEGER) {
                            index_column = *col_idx;
                            search_key = literal->value.as_int();
                            return true;
                        }
                    }
                }
            }

            // Also check the reverse (literal = column)
            if (binary_op->right->type ==
                parser::ExpressionType::COLUMN_REF) {
                auto* col_ref = dynamic_cast<
                    const parser::ColumnRefExpression*
                >(binary_op->right.get());

                auto col_idx = find_column_index(
                    col_ref->column_name,
                    metadata
                );

                if (col_idx.has_value()) {
                    if (binary_op->left->type ==
                        parser::ExpressionType::LITERAL) {
                        auto* literal = dynamic_cast<
                            const parser::LiteralExpression*
                        >(binary_op->left.get());

                        if (literal->value.type() ==
                            storage::ValueType::INTEGER) {
                            index_column = *col_idx;
                            search_key = literal->value.as_int();
                            return true;
                        }
                    }
                }
            }
        }
    }

    // TODO: Handle AND predicates where one clause is an equality
    // For now, we only optimize simple equality predicates

    return false;
}

double Planner::estimate_sequential_scan_cost(
    const catalog::TableMetadata& /* metadata */
) const {
    // Simple cost model: assume ~100 tuples per table
    // Cost = number of pages to scan
    // Each page can hold ~20-50 tuples depending on size
    
    const double estimated_tuples = 100.0;
    const double tuples_per_page = 30.0;
    const double pages = estimated_tuples / tuples_per_page;
    
    // Sequential scan cost = page reads
    const double seq_scan_cost = pages * 1.0;
    
    return seq_scan_cost;
}

double Planner::estimate_index_scan_cost(
    const catalog::TableMetadata& /* metadata */,
    std::size_t /* index_column */
) const {
    // Index scan cost model:
    // - B+ tree traversal: log(N) page reads
    // - Tuple fetch: 1 page read
    
    const double estimated_tuples = 100.0;
    const double btree_fanout = 10.0;
    
    // Height of B+ tree
    const double height = std::log(estimated_tuples) / 
                          std::log(btree_fanout);
    
    // Cost = tree traversal + tuple fetch
    const double index_scan_cost = height + 1.0;
    
    return index_scan_cost;
}

bool Planner::is_equality_predicate_on_column(
    const parser::Expression* expr,
    std::size_t /* column_index */,
    std::optional<std::int32_t>& key_value
) const {
    if (!expr || expr->type != parser::ExpressionType::BINARY_OP) {
        return false;
    }

    auto* binary_op = dynamic_cast<const parser::BinaryOpExpression*>(expr);

    if (binary_op->op != parser::BinaryOperator::EQUALS) {
        return false;
    }

    // Check if one side is the column we're looking for
    // and the other side is a literal
    
    bool left_is_column = false;
    bool right_is_literal = false;

    if (binary_op->left->type == parser::ExpressionType::COLUMN_REF) {
        // We'd need metadata to verify column_index matches
        // For now, this is a simplified check
        left_is_column = true;
    }

    if (binary_op->right->type == parser::ExpressionType::LITERAL) {
        auto* literal = dynamic_cast<const parser::LiteralExpression*>(
            binary_op->right.get()
        );
        
        if (literal->value.type() == storage::ValueType::INTEGER) {
            key_value = literal->value.as_int();
            right_is_literal = true;
        }
    }

    return left_is_column && right_is_literal;
}

std::optional<std::size_t> Planner::find_column_index(
    const std::string& column_name,
    const catalog::TableMetadata& metadata
) const {
    for (std::size_t i = 0; i < metadata.columns.size(); ++i) {
        if (metadata.columns[i].name == column_name) {
            return i;
        }
    }

    return std::nullopt;
}

}  // namespace forgedb::execution

