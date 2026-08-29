#!/bin/bash

# 删除所有 .c 和 .h 文件中的尾部空白
# 用法: ./remove_trailing_whitespace.sh

echo "========================================="
echo "Remove Trailing Whitespace"
echo "========================================="

# 查找所有 .c 和 .h 文件
find . -type f \( -name "*.c" -o -name "*.h" \) | while read -r file; do
    echo "Processing: $file"
    # 删除尾部空白（空格和制表符）
    sed -i 's/[[:space:]]*$//' "$file"
done

echo "========================================="
echo "Done!"
echo "========================================="
