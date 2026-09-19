# ForgeDB CLI Usage

## Running ForgeDB

Start the interactive shell:
```bash
./build/forgedb.exe
```

With custom database and log files:
```bash
./build/forgedb.exe --db mydb.db --log mydb.log
```

## Meta-Commands

Meta-commands start with a backslash (`\`):

- `\help` - Show help message
- `\tables` - List all tables in the database
- `\desc <table>` - Show schema for a specific table
- `\quit` - Exit the shell

## SQL Commands

All SQL statements must end with a semicolon (`;`).

### CREATE TABLE
```sql
CREATE TABLE users (
    id INTEGER,
    name VARCHAR,
    age INTEGER,
    active BOOLEAN
);
```

### CREATE INDEX
```sql
CREATE INDEX idx_users_id ON users(id);
```

### INSERT
```sql
INSERT INTO users VALUES (1, 'Alice', 30, true);
```

### SELECT
```sql
-- Select all
SELECT * FROM users;

-- Select with WHERE clause
SELECT * FROM users WHERE age > 25;

-- Select with equality (can use index if available)
SELECT * FROM users WHERE id = 1;
```

## Features

- **Interactive REPL**: Multi-line query support
- **Transaction Management**: Each query runs in its own transaction
- **Crash Recovery**: Automatic recovery on startup
- **WAL Logging**: Write-ahead logging for durability
- **Concurrency Control**: 2PL locking for isolation
- **Query Planning**: Cost-based optimizer for SELECT queries

## Example Session

```
forgedb> CREATE TABLE users (id INTEGER, name VARCHAR);
Table created successfully.
Query executed in 2 ms

forgedb> INSERT INTO users VALUES (1, 'Alice');
1 row inserted.
Query executed in 1 ms

forgedb> SELECT * FROM users;
1 row returned.
Query executed in 0 ms

forgedb> \tables

Tables:
──────────────────────────────────────
  users (2 columns)

forgedb> \quit
Goodbye!
```

## Command-Line Options

- `--db <file>` - Specify database file (default: forgedb.db)
- `--log <file>` - Specify log file (default: forgedb.log)
- `--help, -h` - Show help message
