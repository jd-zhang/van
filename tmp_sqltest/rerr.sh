#!/bin/bash
# Error path checks (run on 192.168.0.107).  Each statement runs in its
# own process so that one failure does not stop the others.
cd ~/van || exit 1
rm -rf /tmp/sqlerr
mkdir -p /tmp/sqlerr
cd /tmp/sqlerr || exit 1

run() {
	echo "--- $1"
	~/van/cli/vansql -c "$1" . 2>&1 | head -6
}

run "CREATE TABLE e (id INT PRIMARY KEY, v VARCHAR(4) NOT NULL);"
run "CREATE TABLE e (id INT PRIMARY KEY, v VARCHAR(4) NOT NULL);"
run "CREATE TABLE IF NOT EXISTS e (id INT, x INT);"
run "DROP TABLE nope;"
run "DROP TABLE IF EXISTS nope;"
run "SELECT * FROM nope;"
run "SELECT * FROM e JOIN x ON 1=1;"
run "SELECT COUNT(*) FROM e;"
run "INSERT INTO e VALUES (1, 'ok');"
run "INSERT INTO e VALUES (1, 'dup');"
run "INSERT INTO e (id) VALUES (2);"
run "INSERT INTO e VALUES (3, 'toolongvalue');"
run "INSERT INTO e VALUES (4, 'x', 5);"
run "UPDATE e SET nope = 1;"
run "DELETE FROM e WHERE nope = 1;"
run "INSERT INTO e VALUES (5, 'five');"
run "SELECT * FROM e ORDER BY id;"
run "SELECT badcol FROM e;"
run "ALTER TABLE e ADD COLUMN z INT;"
run "SELECT * FROM e ORDER BY v LIMIT -1;"
run "CREATE TABLE f (id INT, id INT);"
echo "--- readonly write"
~/van/cli/vansql -readonly -c "INSERT INTO e VALUES (9, 'ro');" . 2>&1 | head -3
echo "--- readonly read"
~/van/cli/vansql -readonly -c "SELECT * FROM e ORDER BY id;" . 2>&1 | head -5
echo "=== done"
