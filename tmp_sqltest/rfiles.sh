#!/bin/bash
# Inspect store files after a run.
cd /tmp/sqlpersist || exit 1
ls -la
echo "=== catalog odlist"
find . -name '*catalog*' | head
for f in $(find . -name '*.od*' | head -10); do
	echo "--- $f"
	head -c 400 "$f" | od -c | head -20
done
