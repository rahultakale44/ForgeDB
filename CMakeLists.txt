cmake_minimum_required(VERSION 3.20)

project(ForgeDB
    VERSION 0.1.0
    DESCRIPTION "A lightweight relational database engine built from scratch"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(forgedb
    src/main.cpp
    src/storage/page.cpp

)

target_include_directories(forgedb
    PRIVATE
        ${PROJECT_SOURCE_DIR}/include
)

if (MSVC)
    target_compile_options(forgedb PRIVATE /W4)
else()
    target_compile_options(forgedb PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()