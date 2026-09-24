# ForgeDB

ForgeDB is a lightweight relational database engine built from scratch in C++20. The project focuses on understanding and implementing core database internals rather than relying on an existing database engine.

## Features

**Implemented:**
- ✅ Page-oriented storage engine (4KB pages with checksums)
- ✅ Disk manager with persistent storage
- ✅ Record management (tuple/schema serialization)
- ✅ Schema and catalog management
- ✅ SQL tokenizer and parser (CREATE TABLE, INSERT, SELECT)
- ✅ Query execution engine with sequential/index scan
- ✅ Query planner with cost-based optimization
- ✅ B+ tree indexing
- ✅ Buffer pool with LRU replacement policy
- ✅ Transactions and concurrency control (2PL)
- ✅ Write-ahead logging (WAL)
- ✅ Crash recovery (ARIES)
- ✅ Lock manager with deadlock detection
- ✅ Interactive CLI (REPL)
- ✅ Comprehensive test suite (206 tests)
- ✅ Performance benchmarks (30 benchmarks)

## Technology Stack

### Core
- **C++**: C++20 standard
- **CMake**: 3.20+ (minimum required)
- **Build System**: Ninja (CI), native make (local)

### Testing & Benchmarking
- **GoogleTest**: Version not pinned (uses system package or vcpkg)
- **Google Benchmark**: v1.8.3 (via CMake FetchContent)
- **CTest**: Integrated test runner

### CI/CD
- **GitHub Actions**: Ubuntu latest runner
- **Compiler (CI)**: g++ (system default on ubuntu-latest)
- **Build Configuration**: Debug (CI), Release (benchmarks)

### Development Tools
Note: The following tools are mentioned for documentation purposes but not currently configured in the repository:
- clang-format (no config file present)
- clang-tidy (no config file present)  
- AddressSanitizer (not configured in CMakeLists.txt or CI)
- UndefinedBehaviorSanitizer (not configured in CMakeLists.txt or CI)

**Not Used:**
- Python: Not used by ForgeDB
- FastAPI: Not implemented
- Docker: No containerization currently configured

## Project Status

ForgeDB is a complete, working database engine with:
- Full ACID transaction support
- SQL query execution
- Persistent storage with crash recovery
- Comprehensive test coverage (206 tests, 100% passing)
- Performance benchmarks demonstrating:
  - 124M buffer pool ops/sec
  - 2.5M B+ tree lookups/sec
  - 546 MiB/sec sequential I/O throughput
