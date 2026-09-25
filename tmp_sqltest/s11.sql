CREATE TABLE users (id INT PRIMARY KEY, name VARCHAR(32));
CREATE TABLE metrics (mid INT PRIMARY KEY, label VARCHAR(16), value DOUBLE);
INSERT INTO metrics VALUES (1, 'cpu', 12.5), (2, 'mem', 87.25);
INSERT INTO users VALUES (1, 'alice'), (2, 'bob');
SELECT * FROM metrics;
DELETE FROM users;
SELECT * FROM metrics;
SELECT * FROM users;
SELECT * FROM metrics;
