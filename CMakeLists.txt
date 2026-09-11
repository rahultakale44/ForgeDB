cmake_minimum_required(VERSION 3.20)

project(ForgeDB
    VERSION 0.1.0
    DESCRIPTION "A lightweight relational database engine built from scratch"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --------------------------------------------------
# Compiler warnings
# --------------------------------------------------

if (MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# --------------------------------------------------
# ForgeDB core library
# --------------------------------------------------

add_library(forgedb_core
    src/storage/page.cpp
    src/storage/disk_manager.cpp
    src/buffer/buffer_pool_manager.cpp
)

target_include_directories(forgedb_core
    PUBLIC
        ${PROJECT_SOURCE_DIR}/include
)

# --------------------------------------------------
# ForgeDB executable
# --------------------------------------------------

add_executable(forgedb
    src/main.cpp
)

target_link_libraries(forgedb
    PRIVATE
        forgedb_core
)

# --------------------------------------------------
# Testing
# --------------------------------------------------

enable_testing()

find_package(GTest CONFIG REQUIRED)

add_executable(forgedb_tests
    tests/storage_test.cpp
    tests/buffer_pool_test.cpp
)

target_link_libraries(forgedb_tests
    PRIVATE
        forgedb_core
        GTest::gtest_main
)

include(GoogleTest)

gtest_discover_tests(forgedb_tests)