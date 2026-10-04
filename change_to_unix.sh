#! /bin/bash
find . -name "*.h" -o -name "*.c" -o -name "*.sh" -o -name "*.md" -o  -name "*.MD" | while read f;do dos2unix $f; done
