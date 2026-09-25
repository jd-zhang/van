CREATE TABLE IF NOT EXISTS users (
	id INT PRIMARY KEY,
	name VARCHAR(32) NOT NULL,
	email VARCHAR(64),
	age SMALLINT DEFAULT 18,
	score DOUBLE,
	joined DATE,
	seen TIMESTAMP,
	note TEXT,
	headshot BLOB
);
CREATE TABLE metrics (
	mid INT PRIMARY KEY,
	label VARCHAR(16),
	value DOUBLE
);
INSERT INTO users (id, name, email, age, score, joined, seen, note)
	VALUES (1, 'alice', 'alice@example.com', 30, 95.5, '2024-01-15',
	    '2024-01-15 08:30:00', 'first row');
INSERT INTO users VALUES (2, 'bob', 'bob@example.com', 25, 80.25,
	'2024-02-20', '2024-02-20 09:00:00', NULL, X'0A0B');
INSERT INTO users (id, name) VALUES (3, 'carol');
INSERT INTO users (id, name, age) VALUES (4, 'dave', 41), (5, 'erin', 22);
INSERT INTO metrics VALUES (1, 'cpu', 12.5), (2, 'mem', 87.25);
SELECT * FROM users;
SELECT id, name, age, score FROM users WHERE age >= 25;
SELECT name, joined FROM users WHERE name LIKE 'a%';
SELECT id FROM users WHERE note IS NULL;
SELECT id, name FROM users WHERE id IN (1, 3, 5);
SELECT id FROM users WHERE age BETWEEN 20 AND 40;
SELECT id, age FROM users WHERE NOT (age > 40) AND email IS NOT NULL;
SELECT id, name, age FROM users ORDER BY age DESC, id ASC LIMIT 3;
SELECT DISTINCT age FROM users ORDER BY age;
SELECT id, name FROM users WHERE id > 1 LIMIT 2 OFFSET 1;
SELECT id, age * 2 AS doubled FROM users ORDER BY id;
UPDATE users SET score = 99.5 WHERE id = 1;
UPDATE users SET age = age + 1 WHERE age IS NOT NULL;
UPDATE users SET id = 10 WHERE name = 'bob';
DELETE FROM users WHERE id = 10;
SELECT id, name, age, score FROM users ORDER BY id;
DELETE FROM users;
SELECT * FROM users;
SELECT * FROM metrics;
DROP TABLE metrics;
