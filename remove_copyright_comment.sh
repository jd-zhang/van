#!/bin/bash

# 删除所有包含 "Copyright" 的块注释（/* ... */），不涉及其他注释块
# 用法：./remove_copyright_comments.sh 文件名
# 或者：./remove_copyright_comments.sh *.c *.h

process_file() {
    local file="$1"
    local temp_file=$(mktemp)
    local in_block=0           # 0=不在块注释中, 1=在块注释中
    local block_has_copyright=0
    local block_start_line=0
    local line_num=0
    local output_lines=()

    # 读取文件逐行处理
    while IFS= read -r line; do
        ((line_num++))

        # 检测是否进入块注释
        if [[ $line =~ /\* ]]; then
            in_block=1
            block_start_line=$line_num
            block_has_copyright=0
            # 检查当前行是否包含 Copyright
            if [[ $line =~ [Cc]opyright ]]; then
                block_has_copyright=1
            fi
            # 检查当前行是否在同一行结束块注释（/* ... */）
            if [[ $line =~ \*/ ]]; then
                in_block=0
                if [[ $block_has_copyright -eq 0 ]]; then
                    # 没有 Copyright，保留该行
                    output_lines+=("$line")
                fi
                # 有 Copyright 则丢弃该行（不添加到 output）
            fi
            continue
        fi

        # 如果已在块注释中
        if [[ $in_block -eq 1 ]]; then
            # 检查当前行是否包含 Copyright
            if [[ $line =~ [Cc]opyright ]]; then
                block_has_copyright=1
            fi
            # 检查是否结束块注释
            if [[ $line =~ \*/ ]]; then
                in_block=0
                # 如果块注释中没有 Copyright，则保留这个块的所有行
                if [[ $block_has_copyright -eq 0 ]]; then
                    # 需要回退重新输出这个块的每一行
                    # 但由于我们不知道块内具体行内容，这里用另一种方式处理
                    # 在后面的逻辑中会重新处理
		    :
                fi
                # 有 Copyright 则丢弃整个块，不添加任何行
                continue
            fi
            # 在块注释内部，且未结束，暂时不输出
            continue
        fi

        # 不在块注释中，正常输出
        output_lines+=("$line")
    done < "$file"

    # 由于上面逻辑无法处理块内行保留的问题，改用更精确的 awk 方案（见下文）
    # 这里实际上用 awk 更合适
}

# 其实用 awk 更简洁高效，直接上 awk 方案：
remove_copyright_blocks() {
    local file="$1"
    local temp_file=$(mktemp)

    awk '
    BEGIN { in_block = 0; block_has_copyright = 0; block_lines = "" }
    {
        # 检查当前行是否有块注释开始
        if (index($0, "/*") && !in_block) {
            in_block = 1
            block_has_copyright = 0
            block_lines = ""
        }

        if (in_block) {
            # 累积块注释内容
            block_lines = block_lines $0 "\n"
            # 检查是否包含 Copyright（忽略大小写）
            if (tolower($0) ~ /copyright/) {
                block_has_copyright = 1
            }
            # 检查是否块注释结束
            if (index($0, "*/")) {
                in_block = 0
                if (!block_has_copyright) {
                    # 没有 Copyright，输出该块
                    printf "%s", block_lines
                }
                # 有 Copyright 则丢弃
                block_lines = ""
            }
            next
        }

        # 不在块注释中，正常输出
        print
    }' "$file" > "$temp_file"

    mv "$temp_file" "$file"
    echo "✓ 处理完成: $file"
}

# 批量处理
for file in "$@"; do
    if [[ -f "$file" ]]; then
        remove_copyright_blocks "$file"
    else
        echo "跳过不存在的文件: $file"
    fi
done
