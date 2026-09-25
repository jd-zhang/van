CREATE TABLE users (
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
INSERT INTO metrics VALUES (1, 'cpu', 12.5), (2, 'mem', 87.25);
SELECT * FROM metrics;
UPDATE users SET score = 99.5 WHERE id = 1;
SELECT * FROM metrics;
UPDATE users SET age = age + 1 WHERE age IS NOT NULL;
SELECT * FROM metrics;
UPDATE users SET id = 10 WHERE name = 'alice';
SELECT * FROM metrics;
DELETE FROM users WHERE id = 10;
SELECT * FROM metrics;
