#!/bin/bash

if [ $# -ge 2 ]; then
    INPUT_DIR="$1"
else
    INPUT_DIR="riscv"
fi

OUTPUT_DIR="assembles"
CROSS_COMPILE="riscv64-linux-gnu-"
TIMEOUT_SECONDS=150         # 超时阈值（可根据需要调整，单位：秒）

# 编译功能
assemble() {
    mkdir -p "$OUTPUT_DIR"
    for file in "$INPUT_DIR"/*.s; do
        [ -f "$file" ] || continue
        filename=$(basename "$file" .s)
        echo "Assembling $filename...."

        ${CROSS_COMPILE}as -g "$file" -o "$OUTPUT_DIR/${filename}.o" || {
            echo " 汇编 $filename 失败"
            continue
        }

        ${CROSS_COMPILE}gcc -march=rv64gc -static -g "$OUTPUT_DIR/${filename}.o" -L. -lsysy_riscv -lc -lgcc -o "$OUTPUT_DIR/${filename}" || {
            echo " 链接 $filename 失败"
            continue
        }

        chmod +x "$OUTPUT_DIR/${filename}"
        echo " $filename 编译完成"
    done
    rm -f "$OUTPUT_DIR"/*.o
}

# 测试功能（支持超时、显示时间、但比对时过滤时间信息）
test_programs() {
    [ -d "$OUTPUT_DIR" ] || {
        echo " 未找到编译目录 $OUTPUT_DIR，请先执行 ./test.sh $INPUT_DIR -assembles"
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
            echo " 跳过 $filename：未找到可执行文件"
            continue
        }

        input_file="$INPUT_DIR/$filename.in"
        [ -f "$input_file" ] && input_redir="< $input_file" || input_redir=""

        expected_file="$INPUT_DIR/$filename.out"
        [ -f "$expected_file" ] || {
            echo " 跳过 $filename：未找到预期输出文件"
            continue
        }

        # 为每个测试用例创建临时文件
        TMP_OUTPUT=$(mktemp)
        TMP_FILTERED=$(mktemp)

        echo -e "\n--- 测试 $filename ---"

        if timeout ${TIMEOUT_SECONDS} bash -c "$exe $input_redir > $TMP_OUTPUT 2>&1; echo \$? >> $TMP_OUTPUT"; then
            # 提取时间信息用于显示（不参与比对）
            #total_time=$(grep '^TOTAL:' "$TMP_OUTPUT" || true)
            timer_lines=$(grep '^Timer@' "$TMP_OUTPUT" || true)

            # 过滤掉调试信息，生成干净输出用于 diff 比对
            grep -Ev '^(TOTAL:|Timer@)' "$TMP_OUTPUT" > "$TMP_FILTERED"

            # 执行比对（只比对干净内容）
            diff -u -B -Z "$expected_file" "$TMP_FILTERED" > /dev/null
            if [ $? -eq 0 ]; then
                echo "  测试通过"
                # 显示时间信息
                [ -n "$total_time" ] && echo "  $total_time"
                [ -n "$timer_lines" ] && echo "  $timer_lines"
            else
                echo "  测试失败（差异如下）："
                diff -u -B -Z "$expected_file" "$TMP_FILTERED"
                # 记录失败信息
                echo -e "\n--- 测试 $filename ---" >> failure_case.log
                diff -u -B -Z "$expected_file" "$TMP_FILTERED" >> failure_case.log
                failed_cases+=("$filename")
            fi
        else
            exit_code=$?
            if [ $exit_code -eq 124 ]; then
                echo " 测试超时（超过${TIMEOUT_SECONDS}秒），已中断"
                echo -e "\n--- 测试 $filename ---" >> failure_case.log
                echo " 测试超时（超过${TIMEOUT_SECONDS}秒），已中断" >> failure_case.log
            else
                echo " 程序异常退出（退出码: $exit_code）"
                echo -e "\n--- 测试 $filename ---" >> failure_case.log
                echo " 程序异常退出（退出码: $exit_code）" >> failure_case.log
            fi
            failed_cases+=("$filename")
        fi

        # 清理临时文件
        rm -f "$TMP_OUTPUT" "$TMP_FILTERED"
    done

    echo -e "\n===== 测试结束 ====="
    if [ ${#failed_cases[@]} -eq 0 ]; then
        echo " 所有样例测试通过！"
    else
        echo " 测试失败样例数量：${#failed_cases[@]}"
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
        *)
            echo "用法："
            echo "  编译程序：./test.sh [输入目录] -assembles"
            echo "  测试程序：./test.sh [输入目录] -test"
            exit 1
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