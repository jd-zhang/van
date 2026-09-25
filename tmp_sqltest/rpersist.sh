#!/bin/bash
# Persistence check: write, close, reopen in a new process.
cd ~/van || exit 1
rm -rf /tmp/sqlpersist
mkdir -p /tmp/sqlpersist
cd /tmp/sqlpersist || exit 1
echo "=== first process"
~/van/cli/vansql -f ~/van/tmp_sqltest/s14.sql . 2>&1
echo "=== second process"
~/van/cli/vansql -f ~/van/tmp_sqltest/s15.sql . 2>&1
echo "=== third process (read only, list and dump)"
printf '.tables\n.schema\n' | ~/van/cli/vansql -readonly . 2>&1
echo "=== exit=$?"
