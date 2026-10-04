-- table->traverse based SELECT: range pushdown, filtering, paging, order.
CREATE TABLE t (
	id INT PRIMARY KEY,
	name VARCHAR(32),
	age INT
);
INSERT INTO t VALUES
	(1, 'alice', 30), (2, 'bob', 25), (3, 'carol', 41),
	(4, 'dave', 22), (5, 'erin', 35), (6, 'frank', 29),
	(7, 'grace', 33), (8, 'heidi', 27), (9, 'ivan', 31),
	(10, 'judy', 24), (11, 'ken', 38), (12, 'leo', 26);

-- full scan
SELECT id, name FROM t;

-- rowid range pushdown
SELECT id, name FROM t WHERE id >= 3 AND id < 8;
SELECT id, name FROM t WHERE id = 5;
SELECT id, name FROM t WHERE id > 9;
SELECT id, name FROM t WHERE id <= 4;
SELECT id, name FROM t WHERE 3 <= id AND id <= 5;

-- non rowid filter: full scan plus callback filtering
SELECT id, name FROM t WHERE name = 'carol';
SELECT id, name FROM t WHERE age >= 33;

-- mixed: range pushdown plus callback filtering
SELECT id, name FROM t WHERE id >= 3 AND id < 8 AND age >= 30;

-- empty ranges
SELECT id, name FROM t WHERE id > 100;
SELECT id, name FROM t WHERE id >= 8 AND id < 3;

-- LIMIT / OFFSET
SELECT id, name FROM t LIMIT 3;
SELECT id, name FROM t LIMIT 10 OFFSET 5;
SELECT id, name FROM t LIMIT 3 OFFSET 20;

-- ORDER BY
SELECT id, name FROM t ORDER BY id DESC;
SELECT id, name FROM t ORDER BY id DESC LIMIT 3;
SELECT id, name, age FROM t ORDER BY age DESC, id ASC LIMIT 3;

-- DISTINCT still forces a full scan
SELECT DISTINCT age FROM t ORDER BY age;
