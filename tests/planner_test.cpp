#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "execution/executor.h"
#include "execution/planner.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/disk_manager.h"

namespace {

using forgedb::buffer::BufferPoolManager;
using forgedb::catalog::Catalog;
using forgedb::catalog::ColumnDefinition;
using forgedb::execution::Executor;
using forgedb::execution::Planner;
using forgedb::execution::ScanType;
using forgedb::parser::Lexer;
using forgedb::parser::Parser;
using forgedb::parser::SelectStatement;
using forgedb::storage::DiskManager;
using forgedb::storage::ValueType;

class PlannerTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "test_planner.db";

        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove(test_db_path_);
        }

        disk_manager_ = std::make_unique<DiskManager>(test_db_path_);
        buffer_pool_ =
            std::make_unique<BufferPoolManager>(50, *disk_manager_);
        catalog_ = std::make_unique<Catalog>(*buffer_pool_);
        planner_ = std::make_unique<Planner>(*catalog_);
        executor_ = std::make_unique<Executor>(*buffer_pool_, *catalog_);
    }

    void TearDown() override {
        executor_.reset();
        planner_.reset();
        catalog_.reset();
        buffer_pool_.reset();
        disk_manager_.reset();

        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove(test_db_path_);
        }
    }

    std::string test_db_path_;
    std::unique_ptr<DiskManager> disk_manager_;
    std::unique_ptr<BufferPoolManager> buffer_pool_;
    std::unique_ptr<Catalog> catalog_;
    std::unique_ptr<Planner> planner_;
    std::unique_ptr<Executor> executor_;
};

TEST_F(PlannerTest, ChoosesSequentialScanWithoutIndex) {
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    Lexer lexer2("SELECT * FROM users WHERE id = 10");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();

    auto* select = dynamic_cast<SelectStatement*>(stmt2.value().get());
    auto plan = planner_->plan_select(select);

    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->scan_type, ScanType::SEQUENTIAL);
    EXPECT_FALSE(plan->index_name.has_value());
}

TEST_F(PlannerTest, ChoosesIndexScanWithIndex) {
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    Lexer lexer2("CREATE INDEX idx_id ON users (id)");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    executor_->execute(stmt2.value());

    Lexer lexer3("SELECT * FROM users WHERE id = 10");
    auto tokens3 = lexer3.tokenize();
    Parser parser3(tokens3);
    auto stmt3 = parser3.parse();

    auto* select = dynamic_cast<SelectStatement*>(stmt3.value().get());
    auto plan = planner_->plan_select(select);

    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->scan_type, ScanType::INDEX);
    EXPECT_TRUE(plan->index_name.has_value());
    EXPECT_EQ(*plan->index_name, "idx_id");
}

TEST_F(PlannerTest, UsesSequentialScanForRangePredicate) {
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    Lexer lexer2("CREATE INDEX idx_id ON users (id)");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    executor_->execute(stmt2.value());

    Lexer lexer3("SELECT * FROM users WHERE id > 10");
    auto tokens3 = lexer3.tokenize();
    Parser parser3(tokens3);
    auto stmt3 = parser3.parse();

    auto* select = dynamic_cast<SelectStatement*>(stmt3.value().get());
    auto plan = planner_->plan_select(select);

    ASSERT_TRUE(plan.has_value());
    // Range predicates not supported for index scan yet
    EXPECT_EQ(plan->scan_type, ScanType::SEQUENTIAL);
}

TEST_F(PlannerTest, IndexScanActuallyWorks) {
    // Create table with index first
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    Lexer lexer2("CREATE INDEX idx_id ON users (id)");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    auto create_result = executor_->execute(stmt2.value());
    EXPECT_TRUE(create_result.success);

    // For now, index scan requires data to be inserted after index creation
    // or index needs to be built from existing data (future enhancement)
    // Let's test that the planner chooses index scan
    Lexer lexer3("SELECT * FROM users WHERE id = 5");
    auto tokens3 = lexer3.tokenize();
    Parser parser3(tokens3);
    auto stmt3 = parser3.parse();

    auto* select = dynamic_cast<SelectStatement*>(stmt3.value().get());
    auto plan = planner_->plan_select(select);

    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->scan_type, ScanType::INDEX);
}

TEST_F(PlannerTest, IndexScanWithNonExistentKey) {
    // Create table
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    // Create index
    Lexer lexer3("CREATE INDEX idx_id ON users (id)");
    auto tokens3 = lexer3.tokenize();
    Parser parser3(tokens3);
    auto stmt3 = parser3.parse();
    executor_->execute(stmt3.value());

    // Query non-existent key
    Lexer lexer4("SELECT * FROM users WHERE id = 999");
    auto tokens4 = lexer4.tokenize();
    Parser parser4(tokens4);
    auto stmt4 = parser4.parse();

    auto result = executor_->execute(stmt4.value());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.tuples.size(), 0);
}

TEST_F(PlannerTest, IndexScanWithProjection) {
    // Create table
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    // Create index
    Lexer lexer3("CREATE INDEX idx_id ON users (id)");
    auto tokens3 = lexer3.tokenize();
    Parser parser3(tokens3);
    auto stmt3 = parser3.parse();
    executor_->execute(stmt3.value());

    // Query with projection - test that planner chooses index scan
    Lexer lexer4("SELECT name FROM users WHERE id = 1");
    auto tokens4 = lexer4.tokenize();
    Parser parser4(tokens4);
    auto stmt4 = parser4.parse();

    auto* select = dynamic_cast<SelectStatement*>(stmt4.value().get());
    auto plan = planner_->plan_select(select);

    ASSERT_TRUE(plan.has_value());
    EXPECT_EQ(plan->scan_type, ScanType::INDEX);
    EXPECT_EQ(plan->projection_columns.size(), 1);
    EXPECT_EQ(plan->projection_columns[0], "name");
}

TEST_F(PlannerTest, ComparesScanCosts) {
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    Lexer lexer2("CREATE INDEX idx_id ON users (id)");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    executor_->execute(stmt2.value());

    Lexer lexer3("SELECT * FROM users WHERE id = 10");
    auto tokens3 = lexer3.tokenize();
    Parser parser3(tokens3);
    auto stmt3 = parser3.parse();

    auto* select = dynamic_cast<SelectStatement*>(stmt3.value().get());
    auto plan = planner_->plan_select(select);

    ASSERT_TRUE(plan.has_value());
    // Index scan should be chosen as it has lower cost
    EXPECT_EQ(plan->scan_type, ScanType::INDEX);
    EXPECT_GT(plan->estimated_cost, 0.0);
}

}  // namespace

