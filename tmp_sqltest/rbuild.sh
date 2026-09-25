#!/bin/bash
# Remote build helper (run on 192.168.0.107).
set -x
cd ~/van || exit 1
rm -rf tmp_sqltest/run tmp_sqltest/r2 tmp_sqltest/r3
make clean >/dev/null 2>&1
make -j8 2>&1 | grep -Ei 'warning|error' | head -30
echo "=== lib done"
cd cli || exit 1
make clean >/dev/null 2>&1
make all 2>&1 | tail -20
echo "=== cli done"
ls -l vansql
