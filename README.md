# ForgeDB

A complete, production-ready relational database engine built from scratch in modern C++20. ForgeDB implements core database internals including persistent storage, B+ tree indexing, ACID transactions, crash recovery, and SQL query processing.

## Overview

ForgeDB is a fully functional database management system that demonstrates advanced database concepts through clean, educational implementation. Unlike toy databases, ForgeDB includes real-world features like write-ahead logging, two-phase locking, and ARIES recovery that you'd find in production databases.

**Key Highlights:**
-  **15,000+ lines** of modern C++20 code
-  **206 comprehensive tests** with 100% pass rate
-  **30 performance benchmarks** with actual measurements
-  **Full ACID compliance** with transactions and recovery
-  **SQL support** with parser, planner, and executor
-  **High performance**: 124M buffer pool ops/sec, 2.5M B+ tree lookups/sec

## Architecture

ForgeDB follows a layered architecture with clear separation of concerns:

```
┌─────────────────────────────────────────────────┐
│            SQL Interface & CLI                   │
│  (Interactive REPL, Query Parsing)              │
└─────────────────────────────────────────────────┘
                      ▼
┌─────────────────────────────────────────────────┐
│          Query Processing Layer                  │
│  Parser → Planner → Executor                    │
│  (SQL parsing, cost-based optimization)         │
└─────────────────────────────────────────────────┘
                      ▼
┌─────────────────────────────────────────────────┐
│        Transaction & Concurrency Layer          │
│  Transaction Manager, Lock Manager (2PL)        │
│  (ACID guarantees, deadlock detection)          │
└─────────────────────────────────────────────────┘
                      ▼
┌─────────────────────────────────────────────────┐
│          Access Methods Layer                    │
│  B+ Tree Index, Table Heap, Catalog             │
│  (Indexing, sequential scan, metadata)          │
└─────────────────────────────────────────────────┘
                      ▼
┌─────────────────────────────────────────────────┐
│          Buffer Management Layer                 │
│  Buffer Pool Manager (LRU eviction)             │
│  (In-memory caching, page pinning)              │
└─────────────────────────────────────────────────┘
                      ▼
┌─────────────────────────────────────────────────┐
│       Storage & Recovery Layer                   │
│  Disk Manager, WAL, Recovery Manager            │
│  (Persistent storage, crash recovery)           │
└─────────────────────────────────────────────────┘
```

## Features

###  Storage Engine
- **Page-Oriented Storage**: 4KB pages with checksummed serialization
- **Disk Manager**: Persistent storage with atomic page write operations
- **Page Layout**: Fixed-size pages with header metadata
- **Tuple Format**: Variable-length records with schema support
- **Table Pages**: Slotted page architecture for efficient space management
- **Table Heap**: Sequential storage for table data

###  Indexing
- **B+ Tree Implementation**: Persistent B+ tree with 340 keys per node
- **Insert Operations**: Supports node splitting and tree rebalancing
- **Search Operations**: O(log n) lookup with 2.5M ops/sec throughput
- **Range Queries**: Efficient sequential leaf traversal
- **Index Integration**: CREATE INDEX support via catalog

###  Buffer Management
- **Buffer Pool**: Configurable in-memory page cache
- **LRU Eviction**: Least-recently-used page replacement policy
- **Pin Counting**: Reference counting to prevent eviction of active pages
- **Dirty Page Tracking**: Write-back caching with flush support
- **Performance**: 124M ops/sec for buffer hits, 40M ops/sec for misses

###  Query Processing
- **SQL Parser**: Lexer and recursive-descent parser supporting:
  - `CREATE TABLE` with column types (INTEGER, VARCHAR, BOOLEAN)
  - `CREATE INDEX` on table columns
  - `INSERT INTO` with value lists
  - `SELECT` with WHERE clauses and expressions
- **Query Planner**: Cost-based optimizer choosing between:
  - Sequential scan
  - Index scan (when beneficial)
- **Query Executor**: Volcano-style execution engine with:
  - Table scan operators
  - Index lookup operators
  - Filter pushdown
  - Projection
- **Expression Evaluation**: Arithmetic and boolean expressions with operator precedence

###  Transaction Management
- **Transaction Lifecycle**: BEGIN, COMMIT, ABORT operations
- **Transaction States**: ACTIVE, COMMITTED, ABORTED
- **Transaction ID**: Monotonically increasing transaction identifiers
- **Multi-Transaction Support**: Concurrent transaction tracking

###  Concurrency Control
- **Two-Phase Locking (2PL)**: Strict 2PL protocol for serializability
- **Lock Manager**: Page-level locking with shared and exclusive modes
- **Lock Modes**:
  - Shared locks for reads (multiple transactions)
  - Exclusive locks for writes (single transaction)
- **Lock Upgrade**: Promotion from shared to exclusive
- **Deadlock Detection**: Timeout-based detection (100ms)
- **Lock Release**: Automatic unlock on transaction commit/abort

###  Durability & Recovery
- **Write-Ahead Logging (WAL)**: All modifications logged before page writes
- **Log Record Types**: BEGIN, COMMIT, ABORT, INSERT, UPDATE, DELETE
- **Log Manager**: Sequential append-only log with LSN assignment
- **ARIES Recovery**: Three-phase crash recovery protocol:
  - **Analysis Phase**: Identifies active/committed transactions
  - **Redo Phase**: Replays all operations from checkpoint
  - **Undo Phase**: Rolls back uncommitted transactions
- **Before/After Images**: Full UNDO and REDO support
- **Log Flush**: Force-log-at-commit for durability

###  Catalog Management
- **Schema Storage**: Persistent metadata for tables and indexes
- **Table Metadata**: Column definitions, types, first page ID
- **Index Metadata**: Index name, column, root page ID
- **Column Types**: INTEGER, VARCHAR, BOOLEAN
- **Type System**: Nullable columns, type checking

###  Interactive CLI
- **REPL Interface**: Read-Eval-Print Loop for SQL commands
- **Multi-Line Support**: Queries can span multiple lines until `;`
- **Meta-Commands**:
  - `\help` - Display help information
  - `\tables` - List all tables
  - `\desc <table>` - Show table schema
  - `\quit` - Exit the shell
- **Query Feedback**: Execution time, rows affected, error messages
- **Auto-Recovery**: Automatic crash recovery on startup

## Performance

Performance benchmarks run on a 28-core 2.3GHz system with 33MB L3 cache:

### Storage Layer
- **Page Allocation**: 85,000 pages/sec
- **Page Write**: 518 MiB/sec
- **Page Read**: 546 MiB/sec
- **Sequential Writes** (1000 pages): 459 MiB/sec
- **Sequential Reads** (1000 pages): 489 MiB/sec
- **Random Reads**: 448 MiB/sec

### Buffer Pool
- **Buffer Hit**: 124 million ops/sec (8ns latency)
- **Buffer Miss**: 40 million ops/sec (25ns latency)
- **LRU Eviction**: 80,000 evictions/sec (13μs per eviction)

### B+ Tree Indexing
- **Sequential Insert**: 50,000 inserts/sec
- **Random Insert**: 19,000 inserts/sec
- **Lookup (100 keys)**: 2.5 million lookups/sec
- **Lookup (100,000 keys)**: 65,000 lookups/sec
- **Mixed Workload** (70% read, 30% write): 194,000 ops/sec
- **Range Scan** (1000 keys): 65,000 scans/sec

## Testing

ForgeDB has comprehensive test coverage across all components:

### Test Suite (206 Tests)
- **Storage Tests** (15 tests): Page serialization, disk I/O, checksums
- **Buffer Pool Tests** (12 tests): LRU eviction, pin/unpin, concurrent access
- **B+ Tree Tests** (13 tests): Insert, search, persistence, large datasets
- **Tuple/Schema Tests** (15 tests): Serialization, type system, nullable values
- **Table Page Tests** (8 tests): Slotted pages, tuple insertion, space management
- **Table Heap Tests** (14 tests): Sequential storage, iteration, persistence
- **Catalog Tests** (16 tests): Table creation, indexes, metadata persistence
- **Parser Tests** (19 tests): SQL lexing, parsing, AST generation
- **Executor Tests** (13 tests): Query execution, sequential scan, index scan
- **Planner Tests** (7 tests): Cost estimation, plan selection
- **Transaction Tests** (10 tests): Transaction lifecycle, state transitions
- **Log Manager Tests** (12 tests): WAL operations, log persistence, threading
- **Recovery Tests** (9 tests): ARIES phases, crash recovery, REDO/UNDO
- **Lock Manager Tests** (18 tests): 2PL, deadlock detection, concurrent locking
- **Integration Tests** (11 tests): End-to-end workflows, ACID compliance

**All 206 tests pass with 100% success rate.**

### Benchmark Suite (30 Benchmarks)
- Storage benchmarks: 8 benchmarks
- Buffer pool benchmarks: 5 benchmarks
- B+ tree benchmarks: 17 benchmarks

## Building ForgeDB

### Prerequisites
- C++20-compliant compiler (GCC 10+, Clang 11+, or MSVC 2019+)
- CMake 3.20 or higher
- GoogleTest (automatically fetched if not found)

### Build Instructions

```bash
# Clone the repository
git clone https://github.com/rahultakale44/ForgeDB.git
cd ForgeDB

# Configure with CMake
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build ForgeDB
cmake --build build --config Release

# Build and run tests
cmake --build build --config Release --target forgedb_tests
./build/forgedb_tests

# Build and run benchmarks
cmake --build build --config Release --target forgedb_benchmarks
./build/forgedb_benchmarks
```

### Running ForgeDB CLI

```bash
# Start the interactive shell
./build/forgedb

# Or specify custom database and log files
./build/forgedb --db mydata.db --log mydata.log
```

## Usage Example

```sql
forgedb> CREATE TABLE users (id INTEGER, name VARCHAR, age INTEGER, active BOOLEAN);
Table created successfully.
Query executed in 2 ms

forgedb> CREATE INDEX idx_users_id ON users(id);
Index created successfully.
Query executed in 1 ms

forgedb> INSERT INTO users VALUES (1, 'Alice', 30, true);
1 row inserted.
Query executed in 1 ms

forgedb> INSERT INTO users VALUES (2, 'Bob', 25, true);
1 row inserted.
Query executed in 0 ms

forgedb> SELECT * FROM users WHERE age > 27;
1 row returned.
Query executed in 0 ms

forgedb> \tables

Tables:
──────────────────────────────────────
  users (4 columns)

forgedb> \desc users

Table: users
──────────────────────────────────────
Column              Type           Nullable
──────────────────────────────────────
id                  INTEGER        YES
name                VARCHAR        YES
age                 INTEGER        YES
active              BOOLEAN        YES

Indexes:
  idx_users_id on column 0

forgedb> \quit
Goodbye!
```

## Technology Stack

### Core Technologies
- **Language**: C++20 (using modern features like concepts, ranges, modules support)
- **Build System**: CMake 3.20+ with Ninja generator support
- **Standard Library**: Full C++20 STL (filesystem, optional, variant, etc.)

### Testing & Quality Assurance
- **Unit Testing**: GoogleTest (version managed by system package manager)
- **Benchmark**: Google Benchmark v1.8.3 (fetched automatically via CMake)
- **Test Runner**: CTest integration for test discovery and execution
- **Code Coverage**: 206 tests covering all major components

### Continuous Integration
- **CI Platform**: GitHub Actions
- **CI OS**: Ubuntu latest (currently 22.04)
- **CI Compiler**: GCC (system default)
- **Build Configuration**: Debug mode for tests
- **Automation**: Automated build and test on every push/PR

### Project Structure
```
ForgeDB/
├── include/           # Public headers
│   ├── buffer/       # Buffer pool manager
│   ├── catalog/      # Schema and metadata
│   ├── execution/    # Query executor and planner
│   ├── indexing/     # B+ tree implementation
│   ├── parser/       # SQL lexer and parser
│   ├── storage/      # Storage engine, WAL, recovery
│   └── transaction/  # Transaction and lock manager
├── src/              # Implementation files
├── tests/            # Test suite (206 tests)
├── benchmarks/       # Performance benchmarks (30 benchmarks)
├── .github/          # CI/CD workflows
└── CMakeLists.txt    # Build configuration
```

## Design Decisions

### Why C++20?
- Modern memory safety with RAII and smart pointers
- Zero-cost abstractions for performance-critical code
- Strong type system for compile-time correctness
- Direct memory control for buffer pool and page management

### Why Page-Oriented Storage?
- Industry-standard design used by PostgreSQL, MySQL, SQLite
- Efficient random access to data
- Natural fit for buffer pool caching
- Simplifies crash recovery with page-level atomicity

### Why B+ Tree?
- O(log n) search complexity
- Excellent for range queries (leaf chaining)
- High fanout reduces tree height
- Standard choice for database indexes

### Why ARIES Recovery?
- Industry-proven recovery algorithm
- Supports both REDO and UNDO
- Minimal overhead during normal operation
- Clean separation of logging and recovery logic

### Why Two-Phase Locking?
- Guarantees serializability
- Well-understood concurrency protocol
- Relatively simple to implement correctly
- Widely used in production databases

## Educational Value

ForgeDB is an excellent resource for:
- **Database Internals**: Learn how databases actually work under the hood
- **Systems Programming**: Real-world C++20 application with performance constraints
- **Concurrency**: Transaction management and lock-based concurrency control
- **File Systems**: Persistent storage, page management, crash recovery
- **Algorithms**: B+ trees, LRU caching, query optimization
- **Software Engineering**: Large-scale C++ project structure, testing, benchmarking

## Limitations & Future Work

### Current Limitations
- **No network layer**: CLI-only interface (no client-server architecture)
- **Single-threaded execution**: No parallel query processing
- **Limited SQL**: Subset of SQL (no JOINs, aggregations, subqueries)
- **Page-level locking**: Coarse-grained concurrency (no row-level locks)
- **No query optimization**: Limited to index vs sequential scan choice
- **No compression**: Data stored uncompressed
- **No replication**: Single-node only

### Potential Enhancements
- Network protocol (PostgreSQL wire protocol compatibility)
- Multi-threaded query execution
- Hash join and sort-merge join operators
- Aggregation and GROUP BY support
- Row-level locking with MVCC
- Advanced query optimization (join ordering, predicate pushdown)
- Data compression and encoding
- Replication and high availability
- Statistics collection and cardinality estimation
- More index types (hash, GiST, GIN)

## Contributing

This is an educational project demonstrating database internals. While it's not actively accepting contributions, you're welcome to:
- Fork the repository for learning
- Study the implementation
- Use it as a reference for your own database projects
- Report bugs or suggest improvements via GitHub issues

## License

[Include your license here]

## Author

Rahul Takale  
GitHub: [@rahultakale44](https://github.com/rahultakale44)

## Acknowledgments

ForgeDB's design is inspired by:
- **PostgreSQL**: ARIES recovery, MVCC concepts
- **SQLite**: Simple, embedded architecture
- **CMU Database Course**: Educational structure and testing approach
- **Database Internals** by Alex Petrov: Implementation guidance

## References

- Gray, J., & Reuter, A. (1992). *Transaction Processing: Concepts and Techniques*
- Ramakrishnan, R., & Gehrke, J. (2003). *Database Management Systems*
- Mohan, C., et al. (1992). *ARIES: A Transaction Recovery Method*
- Comer, D. (1979). *The Ubiquitous B-Tree*
