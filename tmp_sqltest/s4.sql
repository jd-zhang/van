CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(32), email VARCHAR(64), age SMALLINT DEFAULT 18, score DOUBLE, joined DATE, seen TIMESTAMP, note TEXT, headshot BLOB);
INSERT INTO users (id, name, email, age, score, joined, seen, note) VALUES (1, 'alice', 'alice@example.com', 30, 95.5, '2024-01-15', '2024-01-15 08:30:00', 'first row');
INSERT INTO users VALUES (2, 'bob', 'bob@example.com', 25, 80.25, '2024-02-20', '2024-02-20 09:00:00', NULL, X'0A0B');
INSERT INTO users (id, name) VALUES (3, 'carol');
INSERT INTO users (id, name, age) VALUES (4, 'dave', 41), (5, 'erin', 22);
SELECT * FROM users;
