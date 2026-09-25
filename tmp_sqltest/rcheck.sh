#!/bin/bash
cd ~/van || exit 1
echo "--- root listing"
ls
echo "--- grow_vec call for columns"
grep -n -A3 stmt->columns, src/sql/sql_parse.c | head -8
