#! /bin/bash
find . -name "*.h" -o -name "*.c" -o -name "*.sh" | while read f;do dos2unix $f; done
