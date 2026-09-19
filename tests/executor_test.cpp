#include <filesystem>
#include <memory>

#include <gtest/gtest.h>

#include "buffer/buffer_pool_manager.h"
#include "catalog/catalog.h"
#include "execution/executor.h"
#include "parser/lexer.h"
#include "parser/parser.h"
#include "storage/disk_manager.h"

namespace {

using forgedb::buffer::BufferPoolManager;
using forgedb::catalog::Catalog;
using forgedb::execution::Executor;
using forgedb::parser::Lexer;
using forgedb::parser::Parser;
using forgedb::storage::DiskManager;

class ExecutorTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path_ = "test_executor.db";

        if (std::filesystem::exists(test_db_path_)) {
            std::filesystem::remove(test_db_path_);
        }

        disk_manager_ = std::make_unique<DiskManager>(test_db_path_);
        buffer_pool_ =
            std::make_unique<BufferPoolManager>(50, *disk_manager_);
        catalog_ = std::make_unique<Catalog>(*buffer_pool_);
        executor_ = std::make_unique<Executor>(*buffer_pool_, *catalog_);
    }

    void TearDown() override {
        executor_.reset();
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
    std::unique_ptr<Executor> executor_;
};

TEST_F(ExecutorTest, ExecutesCreateTable) {
    Lexer lexer("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens = lexer.tokenize();

    Parser parser(tokens);
    auto stmt = parser.parse();

    ASSERT_TRUE(stmt.has_value());

    auto result = executor_->execute(stmt.value());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.rows_affected, 0);

    auto metadata = catalog_->get_table("users");
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->table_name, "users");
    EXPECT_EQ(metadata->columns.size(), 2);
}

TEST_F(ExecutorTest, RejectsDuplicateTable) {
    Lexer lexer1("CREATE TABLE users (id INTEGER)");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    ASSERT_TRUE(stmt1.has_value());

    auto result1 = executor_->execute(stmt1.value());
    EXPECT_TRUE(result1.success);

    Lexer lexer2("CREATE TABLE users (id INTEGER)");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    ASSERT_TRUE(stmt2.has_value());

    auto result2 = executor_->execute(stmt2.value());
    EXPECT_FALSE(result2.success);
}

TEST_F(ExecutorTest, ExecutesCreateIndex) {
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    ASSERT_TRUE(stmt1.has_value());
    executor_->execute(stmt1.value());

    Lexer lexer2("CREATE INDEX idx_id ON users (id)");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    ASSERT_TRUE(stmt2.has_value());

    auto result = executor_->execute(stmt2.value());

    EXPECT_TRUE(result.success);

    auto metadata = catalog_->get_table("users");
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->indexes.size(), 1);
    EXPECT_EQ(metadata->indexes[0].index_name, "idx_id");
}

TEST_F(ExecutorTest, ExecutesInsert) {
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    Lexer lexer2("INSERT INTO users VALUES (1, 'Alice')");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    ASSERT_TRUE(stmt2.has_value());

    auto result = executor_->execute(stmt2.value());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.rows_affected, 1);
}

TEST_F(ExecutorTest, ExecutesInsertWithColumns) {
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    Lexer lexer2("INSERT INTO users (name, id) VALUES ('Bob', 2)");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    ASSERT_TRUE(stmt2.has_value());

    auto result = executor_->execute(stmt2.value());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.rows_affected, 1);
}

TEST_F(ExecutorTest, ExecutesSelectAll) {
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    Lexer lexer2("INSERT INTO users VALUES (1, 'Alice')");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    executor_->execute(stmt2.value());

    Lexer lexer3("INSERT INTO users VALUES (2, 'Bob')");
    auto tokens3 = lexer3.tokenize();
    Parser parser3(tokens3);
    auto stmt3 = parser3.parse();
    executor_->execute(stmt3.value());

    Lexer lexer4("SELECT * FROM users");
    auto tokens4 = lexer4.tokenize();
    Parser parser4(tokens4);
    auto stmt4 = parser4.parse();
    ASSERT_TRUE(stmt4.has_value());

    auto result = executor_->execute(stmt4.value());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.tuples.size(), 2);
    EXPECT_EQ(result.rows_affected, 2);
}

TEST_F(ExecutorTest, ExecutesSelectWithColumns) {
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    Lexer lexer2("INSERT INTO users VALUES (1, 'Alice')");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    executor_->execute(stmt2.value());

    Lexer lexer3("SELECT name FROM users");
    auto tokens3 = lexer3.tokenize();
    Parser parser3(tokens3);
    auto stmt3 = parser3.parse();
    ASSERT_TRUE(stmt3.has_value());

    auto result = executor_->execute(stmt3.value());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.tuples.size(), 1);
    EXPECT_EQ(result.tuples[0].value_count(), 1);
    EXPECT_EQ(*result.tuples[0].get_value(0).as_string(), "Alice");
}

TEST_F(ExecutorTest, ExecutesSelectWithWhere) {
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    for (int i = 1; i <= 5; ++i) {
        std::string sql =
            "INSERT INTO users VALUES (" + std::to_string(i) +
            ", 'User" + std::to_string(i) + "')";
        Lexer lexer(sql);
        auto tokens = lexer.tokenize();
        Parser parser(tokens);
        auto stmt = parser.parse();
        executor_->execute(stmt.value());
    }

    Lexer lexer2("SELECT * FROM users WHERE id > 3");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    ASSERT_TRUE(stmt2.has_value());

    auto result = executor_->execute(stmt2.value());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.tuples.size(), 2);

    EXPECT_EQ(*result.tuples[0].get_value(0).as_int(), 4);
    EXPECT_EQ(*result.tuples[1].get_value(0).as_int(), 5);
}

TEST_F(ExecutorTest, ExecutesSelectWithComplexWhere) {
    Lexer lexer1(
        "CREATE TABLE users (id INTEGER, age INTEGER, name VARCHAR(50))"
    );
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    Lexer lexer2("INSERT INTO users VALUES (1, 25, 'Alice')");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    executor_->execute(stmt2.value());

    Lexer lexer3("INSERT INTO users VALUES (2, 30, 'Bob')");
    auto tokens3 = lexer3.tokenize();
    Parser parser3(tokens3);
    auto stmt3 = parser3.parse();
    executor_->execute(stmt3.value());

    Lexer lexer4("INSERT INTO users VALUES (3, 20, 'Charlie')");
    auto tokens4 = lexer4.tokenize();
    Parser parser4(tokens4);
    auto stmt4 = parser4.parse();
    executor_->execute(stmt4.value());

    Lexer lexer5("SELECT * FROM users WHERE id > 1 AND age >= 25");
    auto tokens5 = lexer5.tokenize();
    Parser parser5(tokens5);
    auto stmt5 = parser5.parse();
    ASSERT_TRUE(stmt5.has_value());

    auto result = executor_->execute(stmt5.value());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.tuples.size(), 1);
    EXPECT_EQ(*result.tuples[0].get_value(0).as_int(), 2);
    EXPECT_EQ(*result.tuples[0].get_value(2).as_string(), "Bob");
}

TEST_F(ExecutorTest, HandlesEmptyResult) {
    Lexer lexer1("CREATE TABLE users (id INTEGER, name VARCHAR(50))");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    Lexer lexer2("SELECT * FROM users WHERE id = 999");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    ASSERT_TRUE(stmt2.has_value());

    auto result = executor_->execute(stmt2.value());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.tuples.size(), 0);
}

TEST_F(ExecutorTest, HandlesNonExistentTable) {
    Lexer lexer("SELECT * FROM nonexistent");
    auto tokens = lexer.tokenize();
    Parser parser(tokens);
    auto stmt = parser.parse();
    ASSERT_TRUE(stmt.has_value());

    auto result = executor_->execute(stmt.value());

    EXPECT_FALSE(result.success);
}

TEST_F(ExecutorTest, ExecutesArithmeticInWhere) {
    Lexer lexer1("CREATE TABLE numbers (x INTEGER, y INTEGER)");
    auto tokens1 = lexer1.tokenize();
    Parser parser1(tokens1);
    auto stmt1 = parser1.parse();
    executor_->execute(stmt1.value());

    Lexer lexer2("INSERT INTO numbers VALUES (10, 5)");
    auto tokens2 = lexer2.tokenize();
    Parser parser2(tokens2);
    auto stmt2 = parser2.parse();
    executor_->execute(stmt2.value());

    Lexer lexer3("INSERT INTO numbers VALUES (20, 3)");
    auto tokens3 = lexer3.tokenize();
    Parser parser3(tokens3);
    auto stmt3 = parser3.parse();
    executor_->execute(stmt3.value());

    Lexer lexer4("SELECT * FROM numbers WHERE x > 15");
    auto tokens4 = lexer4.tokenize();
    Parser parser4(tokens4);
    auto stmt4 = parser4.parse();
    ASSERT_TRUE(stmt4.has_value());

    auto result = executor_->execute(stmt4.value());

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.tuples.size(), 1);
    EXPECT_EQ(*result.tuples[0].get_value(0).as_int(), 20);
}

}  // namespace

