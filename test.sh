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
    failed_cases=()
    > failure_case.log  # 清空或新建日志文件
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

        TMP_OUTPUT=$(mktemp)
        TMP_FILTERED=$(mktemp)

        echo -e "\n--- 测试 $filename ---"
        if timeout ${TIMEOUT_SECONDS} bash -c "$exe $input_redir > $TMP_OUTPUT 2>&1; echo \$? >> $TMP_OUTPUT"; then
            total_time=$(grep '^TOTAL:' "$TMP_OUTPUT" || echo "")
            grep -v '^TOTAL:' "$TMP_OUTPUT" | grep -v '+Timer@' > "$TMP_FILTERED"
            diff -u "$expected_file" "$TMP_FILTERED" > /dev/null
            if [ $? -eq 0 ]; then
                echo "✅ 测试通过"
                [ -n "$total_time" ] && echo "  $total_time"
            else
                echo "❌ 测试失败（差异如下）："
                diff -u "$expected_file" "$TMP_FILTERED"
                echo -e "\n--- 测试 $filename ---" >> failure_case.log
                diff -u "$expected_file" "$TMP_FILTERED" >> failure_case.log
                failed_cases+=("$filename")
            fi
        else
            if [ $? -eq 124 ]; then
                echo "⏰ 测试超时（超过${TIMEOUT_SECONDS}秒），已中断"
                echo -e "\n--- 测试 $filename ---" >> failure_case.log
                echo "⏰ 测试超时（超过${TIMEOUT_SECONDS}秒），已中断" >> failure_case.log
            else
                echo "❌ 程序异常退出（非超时）"
                echo -e "\n--- 测试 $filename ---" >> failure_case.log
                echo "❌ 程序异常退出（非超时）" >> failure_case.log
            fi
            failed_cases+=("$filename")
        fi

        rm -f "$TMP_OUTPUT" "$TMP_FILTERED"
    done

    echo -e "\n===== 测试结束 ====="
    if [ ${#failed_cases[@]} -eq 0 ]; then
        echo "🎉 所有样例测试通过！"
    else
        echo "❌ 测试失败样例数量：${#failed_cases[@]}"
        echo "失败编号：${failed_cases[@]}"
        echo "详细失败信息已写入 failure_case.log"
    fi
}

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