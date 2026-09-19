#include "execution/executor.h"

#include <sstream>

#include "storage/table_heap.h"

namespace forgedb::execution {

Executor::Executor(
    buffer::BufferPoolManager& buffer_pool,
    catalog::Catalog& catalog
)
    : buffer_pool_(buffer_pool),
      catalog_(catalog) {}

ExecutionResult Executor::execute(const parser::StatementPtr& stmt) {
    if (!stmt) {
        ExecutionResult result;
        result.success = false;
        result.message = "Null statement";
        return result;
    }

    switch (stmt->type) {
        case parser::StatementType::CREATE_TABLE: {
            auto* create_table =
                dynamic_cast<parser::CreateTableStatement*>(stmt.get());
            return execute_create_table(create_table);
        }

        case parser::StatementType::CREATE_INDEX: {
            auto* create_index =
                dynamic_cast<parser::CreateIndexStatement*>(stmt.get());
            return execute_create_index(create_index);
        }

        case parser::StatementType::SELECT: {
            auto* select =
                dynamic_cast<parser::SelectStatement*>(stmt.get());
            return execute_select(select);
        }

        case parser::StatementType::INSERT: {
            auto* insert =
                dynamic_cast<parser::InsertStatement*>(stmt.get());
            return execute_insert(insert);
        }
    }

    ExecutionResult result;
    result.success = false;
    result.message = "Unknown statement type";
    return result;
}

ExecutionResult Executor::execute_create_table(
    const parser::CreateTableStatement* stmt
) {
    ExecutionResult result;

    std::vector<catalog::ColumnDefinition> columns;

    for (const auto& col : stmt->columns) {
        columns.emplace_back(
            col.column_name,
            col.type,
            col.max_length,
            true  // nullable
        );
    }

    storage::Schema schema;
    for (const auto& col : columns) {
        schema.add_column(col.type, col.max_length);
    }

    storage::TableHeap heap(buffer_pool_, schema);
    storage::PageId first_page_id = heap.first_page_id();

    if (!catalog_.create_table(
            stmt->table_name,
            columns,
            first_page_id
        )) {
        result.success = false;
        result.message = "Table already exists: " + stmt->table_name;
        return result;
    }

    result.success = true;
    result.message = "Table created: " + stmt->table_name;
    result.rows_affected = 0;

    return result;
}

ExecutionResult Executor::execute_create_index(
    const parser::CreateIndexStatement* stmt
) {
    ExecutionResult result;

    auto metadata = catalog_.get_table(stmt->table_name);

    if (!metadata.has_value()) {
        result.success = false;
        result.message = "Table does not exist: " + stmt->table_name;
        return result;
    }

    auto column_index = find_column_index(
        stmt->column_name,
        *metadata
    );

    if (!column_index.has_value()) {
        result.success = false;
        result.message = "Column does not exist: " + stmt->column_name;
        return result;
    }

    storage::Schema schema = metadata->to_schema();

    storage::TableHeap heap(
        buffer_pool_,
        schema,
        metadata->first_page_id
    );

    indexing::BPlusTree index(buffer_pool_);

    storage::PageId index_root_page_id = index.root_page_id();

    if (!catalog_.add_index(
            stmt->table_name,
            stmt->index_name,
            *column_index,
            index_root_page_id
        )) {
        result.success = false;
        result.message = "Index already exists: " + stmt->index_name;
        return result;
    }

    result.success = true;
    result.message = "Index created: " + stmt->index_name;
    result.rows_affected = 0;

    return result;
}

ExecutionResult Executor::execute_select(
    const parser::SelectStatement* stmt
) {
    ExecutionResult result;

    auto metadata = catalog_.get_table(stmt->table_name);

    if (!metadata.has_value()) {
        result.success = false;
        result.message = "Table does not exist: " + stmt->table_name;
        return result;
    }

    storage::Schema schema = metadata->to_schema();

    storage::TableHeap heap(
        buffer_pool_,
        schema,
        metadata->first_page_id
    );

    std::vector<storage::PageId> page_ids;
    page_ids.push_back(metadata->first_page_id);

    heap.tuple_count();

    for (storage::PageId page_id = metadata->first_page_id;
         page_id < metadata->first_page_id + 100;
         ++page_id) {
        storage::Page* page = buffer_pool_.fetch_page(page_id);

        if (page == nullptr) {
            break;
        }

        storage::TablePage table_page(*page);

        for (storage::SlotId slot_id = 0;
             slot_id < 1000;
             ++slot_id) {
            storage::Tuple tuple(schema);

            if (table_page.get_tuple(slot_id, tuple, schema)) {
                if (!stmt->where_clause ||
                    evaluate_predicate(
                        stmt->where_clause.get(),
                        tuple,
                        *metadata
                    )) {
                    storage::Tuple projected_tuple =
                        project_tuple(
                            tuple,
                            stmt->columns,
                            *metadata
                        );

                    result.tuples.push_back(projected_tuple);
                }
            }
        }

        buffer_pool_.unpin_page(page_id, false);
    }

    result.success = true;
    result.message = "SELECT completed";
    result.rows_affected = result.tuples.size();

    return result;
}

ExecutionResult Executor::execute_insert(
    const parser::InsertStatement* stmt
) {
    ExecutionResult result;

    auto metadata = catalog_.get_table(stmt->table_name);

    if (!metadata.has_value()) {
        result.success = false;
        result.message = "Table does not exist: " + stmt->table_name;
        return result;
    }

    storage::Schema schema = metadata->to_schema();

    if (!stmt->columns.empty() &&
        stmt->columns.size() != stmt->values.size()) {
        result.success = false;
        result.message = "Column count does not match value count";
        return result;
    }

    if (stmt->columns.empty() &&
        stmt->values.size() != schema.column_count()) {
        result.success = false;
        result.message = "Value count does not match table schema";
        return result;
    }

    storage::Tuple tuple(schema);

    for (std::size_t i = 0; i < stmt->values.size(); ++i) {
        std::size_t column_index = i;

        if (!stmt->columns.empty()) {
            auto col_idx = find_column_index(
                stmt->columns[i],
                *metadata
            );

            if (!col_idx.has_value()) {
                result.success = false;
                result.message = "Column does not exist: " +
                                 stmt->columns[i];
                return result;
            }

            column_index = *col_idx;
        }

        storage::Tuple dummy_tuple(schema);

        auto value = evaluate_expression(
            stmt->values[i].get(),
            dummy_tuple,
            *metadata
        );

        if (!value.has_value()) {
            result.success = false;
            result.message = "Failed to evaluate expression";
            return result;
        }

        tuple.set_value(column_index, *value);
    }

    storage::TableHeap heap(
        buffer_pool_,
        schema,
        metadata->first_page_id
    );

    storage::RecordId rid{};

    if (!heap.insert_tuple(tuple, rid)) {
        result.success = false;
        result.message = "Failed to insert tuple";
        return result;
    }

    result.success = true;
    result.message = "INSERT completed";
    result.rows_affected = 1;

    return result;
}

std::optional<storage::Value> Executor::evaluate_expression(
    const parser::Expression* expr,
    const storage::Tuple& tuple,
    const catalog::TableMetadata& metadata
) const {
    if (!expr) {
        return std::nullopt;
    }

    switch (expr->type) {
        case parser::ExpressionType::LITERAL: {
            auto* literal =
                dynamic_cast<const parser::LiteralExpression*>(expr);
            return literal->value;
        }

        case parser::ExpressionType::COLUMN_REF: {
            auto* column_ref =
                dynamic_cast<const parser::ColumnRefExpression*>(expr);

            auto col_idx = find_column_index(
                column_ref->column_name,
                metadata
            );

            if (!col_idx.has_value()) {
                return std::nullopt;
            }

            return tuple.get_value(*col_idx);
        }

        case parser::ExpressionType::BINARY_OP: {
            auto* binary_op =
                dynamic_cast<const parser::BinaryOpExpression*>(expr);

            auto left_value = evaluate_expression(
                binary_op->left.get(),
                tuple,
                metadata
            );

            auto right_value = evaluate_expression(
                binary_op->right.get(),
                tuple,
                metadata
            );

            if (!left_value.has_value() ||
                !right_value.has_value()) {
                return std::nullopt;
            }

            switch (binary_op->op) {
                case parser::BinaryOperator::EQUALS:
                    return storage::Value(
                        *left_value == *right_value
                    );

                case parser::BinaryOperator::NOT_EQUALS:
                    return storage::Value(
                        *left_value != *right_value
                    );

                case parser::BinaryOperator::LESS_THAN:
                    if (left_value->type() == storage::ValueType::INTEGER &&
                        right_value->type() == storage::ValueType::INTEGER) {
                        return storage::Value(
                            *left_value->as_int() < *right_value->as_int()
                        );
                    }
                    break;

                case parser::BinaryOperator::GREATER_THAN:
                    if (left_value->type() == storage::ValueType::INTEGER &&
                        right_value->type() == storage::ValueType::INTEGER) {
                        return storage::Value(
                            *left_value->as_int() > *right_value->as_int()
                        );
                    }
                    break;

                case parser::BinaryOperator::LESS_OR_EQUAL:
                    if (left_value->type() == storage::ValueType::INTEGER &&
                        right_value->type() == storage::ValueType::INTEGER) {
                        return storage::Value(
                            *left_value->as_int() <= *right_value->as_int()
                        );
                    }
                    break;

                case parser::BinaryOperator::GREATER_OR_EQUAL:
                    if (left_value->type() == storage::ValueType::INTEGER &&
                        right_value->type() == storage::ValueType::INTEGER) {
                        return storage::Value(
                            *left_value->as_int() >= *right_value->as_int()
                        );
                    }
                    break;

                case parser::BinaryOperator::AND:
                    if (left_value->type() == storage::ValueType::BOOLEAN &&
                        right_value->type() == storage::ValueType::BOOLEAN) {
                        return storage::Value(
                            *left_value->as_bool() && *right_value->as_bool()
                        );
                    }
                    break;

                case parser::BinaryOperator::OR:
                    if (left_value->type() == storage::ValueType::BOOLEAN &&
                        right_value->type() == storage::ValueType::BOOLEAN) {
                        return storage::Value(
                            *left_value->as_bool() || *right_value->as_bool()
                        );
                    }
                    break;

                case parser::BinaryOperator::PLUS:
                    if (left_value->type() == storage::ValueType::INTEGER &&
                        right_value->type() == storage::ValueType::INTEGER) {
                        return storage::Value(
                            *left_value->as_int() + *right_value->as_int()
                        );
                    }
                    break;

                case parser::BinaryOperator::MINUS:
                    if (left_value->type() == storage::ValueType::INTEGER &&
                        right_value->type() == storage::ValueType::INTEGER) {
                        return storage::Value(
                            *left_value->as_int() - *right_value->as_int()
                        );
                    }
                    break;

                case parser::BinaryOperator::MULTIPLY:
                    if (left_value->type() == storage::ValueType::INTEGER &&
                        right_value->type() == storage::ValueType::INTEGER) {
                        return storage::Value(
                            *left_value->as_int() * *right_value->as_int()
                        );
                    }
                    break;

                case parser::BinaryOperator::DIVIDE:
                    if (left_value->type() == storage::ValueType::INTEGER &&
                        right_value->type() == storage::ValueType::INTEGER) {
                        if (*right_value->as_int() == 0) {
                            return std::nullopt;
                        }
                        return storage::Value(
                            *left_value->as_int() / *right_value->as_int()
                        );
                    }
                    break;
            }

            return std::nullopt;
        }

        case parser::ExpressionType::UNARY_OP: {
            auto* unary_op =
                dynamic_cast<const parser::UnaryOpExpression*>(expr);

            auto operand_value = evaluate_expression(
                unary_op->operand.get(),
                tuple,
                metadata
            );

            if (!operand_value.has_value()) {
                return std::nullopt;
            }

            switch (unary_op->op) {
                case parser::UnaryOperator::NOT:
                    if (operand_value->type() ==
                        storage::ValueType::BOOLEAN) {
                        return storage::Value(
                            !(*operand_value->as_bool())
                        );
                    }
                    break;

                case parser::UnaryOperator::MINUS:
                    if (operand_value->type() ==
                        storage::ValueType::INTEGER) {
                        return storage::Value(
                            -(*operand_value->as_int())
                        );
                    }
                    break;
            }

            return std::nullopt;
        }
    }

    return std::nullopt;
}

bool Executor::evaluate_predicate(
    const parser::Expression* expr,
    const storage::Tuple& tuple,
    const catalog::TableMetadata& metadata
) const {
    auto value = evaluate_expression(expr, tuple, metadata);

    if (!value.has_value()) {
        return false;
    }

    if (value->type() != storage::ValueType::BOOLEAN) {
        return false;
    }

    return *value->as_bool();
}

storage::Tuple Executor::project_tuple(
    const storage::Tuple& tuple,
    const std::vector<std::string>& columns,
    const catalog::TableMetadata& metadata
) const {
    if (columns.empty()) {
        return tuple;
    }

    storage::Schema projected_schema;

    for (const auto& col_name : columns) {
        auto col_idx = find_column_index(col_name, metadata);

        if (col_idx.has_value()) {
            projected_schema.add_column(
                metadata.columns[*col_idx].type,
                metadata.columns[*col_idx].max_length
            );
        }
    }

    storage::Tuple projected_tuple(projected_schema);

    for (std::size_t i = 0; i < columns.size(); ++i) {
        auto col_idx = find_column_index(columns[i], metadata);

        if (col_idx.has_value()) {
            projected_tuple.set_value(i, tuple.get_value(*col_idx));
        }
    }

    return projected_tuple;
}

std::optional<std::size_t> Executor::find_column_index(
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

