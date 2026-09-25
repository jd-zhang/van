#!/bin/bash
# Run a script under valgrind (run on 192.168.0.107).
cd ~/van || exit 1
rm -rf /tmp/sqlvg
mkdir -p /tmp/sqlvg
cd /tmp/sqlvg || exit 1
valgrind --error-limit=no --num-callers=15 \
    ~/van/cli/vansql -f ~/van/tmp_sqltest/$1 . > out.txt 2> err.txt
echo "exit=$?"
echo "----- stderr (first errors)"
grep -n -A14 -E 'Invalid (write|read|free)|Mismatched|uninitialised|Source and destination overlap' err.txt | head -80
echo "----- summary"
grep -E 'ERROR SUMMARY|definitely lost|indirectly lost|still reachable' err.txt | head
