#!/bin/bash
# Check whether the duplicate key insert hangs.
cd ~/van || exit 1
rm -rf /tmp/sqldup
mkdir -p /tmp/sqldup
cd /tmp/sqldup || exit 1
echo "--- create"
timeout 20 ~/van/cli/vansql -c "CREATE TABLE e (id INT PRIMARY KEY, v VARCHAR(4) NOT NULL);" . 2>&1
echo "exit=$?"
echo "--- insert 1"
timeout 20 ~/van/cli/vansql -c "INSERT INTO e VALUES (1, 'ok');" . 2>&1
echo "exit=$?"
echo "--- insert duplicate"
timeout 20 ~/van/cli/vansql -c "INSERT INTO e VALUES (1, 'dup');" . 2>&1
echo "exit=$?"
echo "=== done"
