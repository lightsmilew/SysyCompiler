#!/bin/bash

if [ $# -ge 2 ]; then
    INPUT_DIR="$1"
else
    INPUT_DIR="riscv"
fi
OUTPUT_DIR="assembles"
CROSS_COMPILE="riscv64-linux-gnu-"
TMP_OUTPUT=$(mktemp)      # 存储程序原始输出（含时间信息）
TMP_FILTERED=$(mktemp)    # 过滤后的输出（不含时间信息）
TIMEOUT_SECONDS=5         # 超时阈值（可根据需要调整，单位：秒）



# 编译功能（保持不变）
assemble() {
    mkdir -p "$OUTPUT_DIR"
    for file in "$INPUT_DIR"/*.s; do
        [ -f "$file" ] || continue
        filename=$(basename "$file" .s)
        echo "Assembling $filename...."

        ${CROSS_COMPILE}as -g "$file" -o "$OUTPUT_DIR/${filename}.o" || {
            echo "❌ 汇编 $filename 失败"
            continue
        }

        ${CROSS_COMPILE}gcc -static -g "$OUTPUT_DIR/${filename}.o" -L. -lsysy_riscv -lc -lgcc -o "$OUTPUT_DIR/${filename}" || {
            echo "❌ 链接 $filename 失败"
            continue
        }

        chmod +x "$OUTPUT_DIR/${filename}"
        echo "✅ $filename 编译完成"
    done
    rm -f "$OUTPUT_DIR"/*.o
}

# 测试功能（新增超时中断机制）
# 测试功能（支持超时和输出过滤）
test_programs() {
    [ -d "$OUTPUT_DIR" ] || {
        echo "❌ 未找到编译目录 $OUTPUT_DIR，请先执行 ./test.sh $INPUT_DIR -assembles"
        return 1
    }

    echo -e "\n===== 开始测试（超时阈值：${TIMEOUT_SECONDS}秒） ====="
    for file in "$INPUT_DIR"/*.s; do
        [ -f "$file" ] || continue
        filename=$(basename "$file" .s)
        exe="$OUTPUT_DIR/$filename"

        [ -x "$exe" ] || {
            echo "❌ 跳过 $filename：未找到可执行文件"
            continue
        }

        input_file="$INPUT_DIR/$filename.in"
        [ -f "$input_file" ] && input_redir="< $input_file" || input_redir=""

        expected_file="$INPUT_DIR/$filename.out"
        [ -f "$expected_file" ] || {
            echo "⚠️ 跳过 $filename：未找到预期输出文件"
            continue
        }

        # 为每个用例生成独立的临时文件
        TMP_OUTPUT=$(mktemp)
        TMP_FILTERED=$(mktemp)

        echo -e "\n--- 测试 $filename ---"
        # 运行程序并设置超时，把返回值直接追加到输出文件最后一行
        if timeout ${TIMEOUT_SECONDS} bash -c "$exe $input_redir > $TMP_OUTPUT 2>&1; echo \$? >> $TMP_OUTPUT"; then
            total_time=$(grep '^TOTAL:' "$TMP_OUTPUT" || echo "")
            # 过滤 TOTAL 和 +Timer@ 行
            grep -v '^TOTAL:' "$TMP_OUTPUT" | grep -v '+Timer@' > "$TMP_FILTERED"
            # 不需要再追加返回值，因为已经在最后一行
            diff -u "$expected_file" "$TMP_FILTERED" > /dev/null
            if [ $? -eq 0 ]; then
                echo "✅ 测试通过"
                [ -n "$total_time" ] && echo "  $total_time"
            else
                echo "❌ 测试失败（差异如下）："
                diff -u "$expected_file" "$TMP_FILTERED"
            fi
        else
            if [ $? -eq 124 ]; then
                echo "⏰ 测试超时（超过${TIMEOUT_SECONDS}秒），已中断"
            else
                echo "❌ 程序异常退出（非超时）"
            fi
        fi

        rm -f "$TMP_OUTPUT" "$TMP_FILTERED"
    done

    echo -e "\n===== 测试结束 ====="
}

# 脚本入口
# 脚本入口
if [[ "$2" == "-assembles" || "$2" == "-test" ]]; then
    case "$2" in
        -assembles)
            assemble
            ;;
        -test)
            test_programs
            ;;
    esac
else
    case "$1" in
        -assembles)
            assemble
            ;;
        -test)
            test_programs
            ;;
        *)
            echo "用法："
            echo "  编译程序：./test.sh [输入目录] -assembles"
            echo "  测试程序：./test.sh [输入目录] -test"
            exit 1
            ;;
    esac
fi