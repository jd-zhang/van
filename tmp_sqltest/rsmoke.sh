#!/bin/bash
# Run the smoke script in a clean directory (run on 192.168.0.107).
cd ~/van || exit 1
rm -rf /tmp/sqlrun
mkdir -p /tmp/sqlrun
cd /tmp/sqlrun || exit 1
~/van/cli/vansql -f ~/van/tmp_sqltest/smoke.sql . > out.txt 2> err.txt
echo "exit=$?"
echo "----- stdout"
cat out.txt
echo "----- stderr"
cat err.txt
