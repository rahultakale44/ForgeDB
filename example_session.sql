-- ForgeDB Example Session
-- This file demonstrates the CLI functionality

-- Create a users table
CREATE TABLE users (
    id INTEGER,
    name VARCHAR,
    age INTEGER,
    active BOOLEAN
);

-- Create an index on the id column
CREATE INDEX idx_users_id ON users(id);

-- Insert some data
INSERT INTO users VALUES (1, 'Alice', 30, true);
INSERT INTO users VALUES (2, 'Bob', 25, true);
INSERT INTO users VALUES (3, 'Charlie', 35, false);
INSERT INTO users VALUES (4, 'Diana', 28, true);

-- Query all users
SELECT * FROM users;

-- Query with WHERE clause
SELECT * FROM users WHERE age > 27;

-- Query with index scan
SELECT * FROM users WHERE id = 2;

-- List all tables
\tables

-- Describe the users table
\desc users
