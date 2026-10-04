#! /bin/bash
find . -name "*.h" -o -name "*.c" -o -name "*.sh" -o -name "*.md" -o  -name "*.MD" -o -name "*.i" -o -name "*.py" | while read f;do dos2unix $f; done
