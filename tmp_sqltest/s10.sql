CREATE TABLE metrics (
	mid INT PRIMARY KEY,
	label VARCHAR(16),
	value DOUBLE
);
INSERT INTO metrics VALUES (1, 'cpu', 12.5), (2, 'mem', 87.25);
SELECT * FROM metrics;
SELECT mid, label, value FROM metrics WHERE value > 50;
UPDATE metrics SET value = 99.9 WHERE mid = 2;
SELECT * FROM metrics;
DELETE FROM metrics WHERE mid = 1;
SELECT * FROM metrics;
DROP TABLE metrics;
