#!/bin/bash
# Run a single sql script in a clean directory (run on 192.168.0.107).
cd ~/van || exit 1
rm -rf /tmp/sqlone
mkdir -p /tmp/sqlone
cd /tmp/sqlone || exit 1
~/van/cli/vansql -f ~/van/tmp_sqltest/$1 . > out.txt 2> err.txt
echo "exit=$?"
echo "----- stdout"
cat out.txt
echo "----- stderr"
cat err.txt
