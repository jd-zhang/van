#!/bin/bash

# 查找所有 .c 和 .h 文件，如果头部没有 "Copyright" 则插入 c-license 内容

LICENSE_FILE="c-license"
TEMP_FILE=$(mktemp)

# 检查 license 文件是否存在
if [ ! -f "$LICENSE_FILE" ]; then
    echo "错误: $LICENSE_FILE 文件不存在"
    exit 1
fi

# 遍历所有 .c 和 .h 文件
find . -type f \( -name "*.c" -o -name "*.h" \) | while read -r file; do
    # 检查文件头部是否包含 "Copyright"（忽略大小写）
    if grep -qi "Copyright" "$file" 2>/dev/null; then
        echo "✓ 已有 license: $file"
    else
        echo "✗ 添加 license: $file"
        # 将 license 内容加到文件头部
        cat "$LICENSE_FILE" "$file" > "$TEMP_FILE"
        mv "$TEMP_FILE" "$file"
    fi
done

rm -f "$TEMP_FILE"
echo "完成！"
