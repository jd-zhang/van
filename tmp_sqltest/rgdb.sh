#!/bin/bash
# Debug the smoke run under gdb (run on 192.168.0.107).
cd ~/van || exit 1
rm -rf /tmp/sqlrun
mkdir -p /tmp/sqlrun
cd /tmp/sqlrun || exit 1
gdb -batch -ex 'handle SIGSEGV stop' -ex run -ex 'bt 20' \
    --args ~/van/cli/vansql -f ~/van/tmp_sqltest/smoke.sql . 2>&1 | tail -45
