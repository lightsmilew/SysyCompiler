#!/bin/bash

if [ $# -ge 2 ]; then
    INPUT_DIR="$1"
else
    INPUT_DIR="riscv"
fi

OUTPUT_DIR="assembles"
CROSS_COMPILE="riscv64-linux-gnu-"
TIMEOUT_SECONDS=150
TIME_LOG="time_history.log"

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

        ${CROSS_COMPILE}gcc -march=rv64gc -static -g "$OUTPUT_DIR/${filename}.o" -L. -lsysy_riscv -lc -lgcc -o "$OUTPUT_DIR/$filename" || {
            echo " 链接 $filename 失败"
            continue
        }

        chmod +x "$OUTPUT_DIR/$filename"
        echo " $filename 编译完成"
    done
    rm -f "$OUTPUT_DIR"/*.o
}

# 读取上次时间 (ms)
get_last_time() {
    local name="$1"
    awk -v n="$name" '$1 == n {print $2}' "$TIME_LOG" 2>/dev/null || echo 0
}

# 保存时间 (ms)
save_time() {
    local name="$1"
    local t="$2"
    grep -v "^$name " "$TIME_LOG" > tmp.log 2>/dev/null
    echo "$name $t" >> tmp.log
    mv tmp.log "$TIME_LOG"
}

# 格式化毫秒 -> 秒
format_time() {
    local ms="$1"
    printf "%.2f" $(echo "scale=2; $ms / 1000" | bc 2>/dev/null)
}

# 归一化输出再对比：折叠所有空白（含换行）为单个空格。
# 避免 qemu/管道在 ~4096 字节边界插入换行导致超长单行误判 WA。
normalize_out() {
    tr -d '\r' < "$1" | tr -s '[:space:]' ' ' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//'
    echo
}

# 测试功能（带耗时对比）
test_programs() {
    [ -d "$OUTPUT_DIR" ] || {
        echo " 未找到编译目录 $OUTPUT_DIR，请先执行 ./test.sh $INPUT_DIR -assembles"
        return 1
    }

    echo -e "\n===== 开始测试（超时：${TIMEOUT_SECONDS}s）====="
    failed_cases=()
    > failure_case.log

    for file in "$INPUT_DIR"/*.s; do
        [ -f "$file" ] || continue
        filename=$(basename "$file" .s)
        exe="$OUTPUT_DIR/$filename"

        [ -x "$exe" ] || { echo " 跳过 $filename：无执行文件"; continue; }
        input_file="$INPUT_DIR/$filename.in"
        input_redir=""
        [ -f "$input_file" ] && input_redir="< $input_file"

        expected_file="$INPUT_DIR/$filename.out"
        [ -f "$expected_file" ] || { echo " 跳过 $filename：无.out"; continue; }

        TMP_OUTPUT=$(mktemp)
        TMP_FILTERED=$(mktemp)

        echo -e "\n--- 测试 $filename ---"

        # 计时运行
        start=$(date +%s%3N)
        # Ensure a newline before appending exit code: putint does not emit '\n',
        # so bare `echo $? >>` would glue digits onto the answer (e.g. 39219810).
        if timeout ${TIMEOUT_SECONDS} bash -c "$exe $input_redir > $TMP_OUTPUT 2>&1; echo >> $TMP_OUTPUT; echo \$? >> $TMP_OUTPUT"; then
            end=$(date +%s%3N)
            cost_ms=$((end - start))
            last_ms=$(get_last_time "$filename")
            save_time "$filename" "$cost_ms"

            # 精准过滤时间格式：Timer@0065-0084: 0H-0M-0S-60306us
            sed -E '
            s/^[[:space:]]*$/MARKER_EMPTY/;
            s/Timer@[0-9]+-[0-9]+: [0-9]+H-[0-9]+M-[0-9]+S-[0-9]+us//g;
            s/^TOTAL:.*//g;
            /^[[:space:]]*$/d;
            s/MARKER_EMPTY//g;
            ' "$TMP_OUTPUT" > "$TMP_FILTERED"

            TMP_EXP_N=$(mktemp)
            TMP_ACT_N=$(mktemp)
            normalize_out "$expected_file" > "$TMP_EXP_N"
            normalize_out "$TMP_FILTERED" > "$TMP_ACT_N"

            if diff -q "$TMP_EXP_N" "$TMP_ACT_N" > /dev/null; then
                echo "  测试通过"

                printf "  耗时: %s" "$(format_time $cost_ms)s"
                if [ "$last_ms" -gt 0 ] 2>/dev/null; then
                    delta=$(( (cost_ms - last_ms) * 100 / last_ms ))
                    printf " | 上次: %s | 变化: %d%%" "$(format_time $last_ms)" "$delta"
                    
                    if [ "$delta" -lt -1 ]; then
                        echo -e " \033[32m↑ 更快\033[0m"
                    elif [ "$delta" -gt 1 ]; then
                        echo -e " \033[31m↓ 更慢\033[0m"
                    else
                        echo " → 基本不变"
                    fi
                else
                    echo " (首次运行，已记录时间)"
                fi

                rm -f "$TMP_OUTPUT" "$TMP_FILTERED" "$TMP_EXP_N" "$TMP_ACT_N"
            else
                echo "  测试失败 → 差异已写入 failure_case.log"
                echo "  原始输出文件: $TMP_OUTPUT"
                echo "  过滤对比文件: $TMP_FILTERED"
                echo "  预期输出文件: $expected_file"
                
                echo -e "\n--- $filename 测试失败 ---" >> failure_case.log
                diff -u "$TMP_EXP_N" "$TMP_ACT_N" >> failure_case.log
                failed_cases+=("$filename")
                rm -f "$TMP_EXP_N" "$TMP_ACT_N"
            fi
        else
            ec=$?
            if [ $ec -eq 124 ]; then
                echo " 测试超时 → 已记录"
            else
                echo " 程序异常退出: $ec → 已记录"
            fi
            echo "  原始输出文件: $TMP_OUTPUT"
            echo "--- $filename 超时/异常 ---" >> failure_case.log
            failed_cases+=("$filename")
            # 超时/异常：保留临时文件
        fi

    done

    echo -e "\n===== 测试结束 ====="
    if [ ${#failed_cases[@]} -eq 0 ]; then
        echo " 全部通过！"
    else
        echo " 失败数：${#failed_cases[@]}"
        echo " 失败列表：${failed_cases[@]}"
        echo " 详情见 failure_case.log"
    fi
}

# 入口
if [ "$2" = "-assembles" ] || [ "$2" = "-test" ]; then
    case "$2" in
        -assembles) assemble ;;
        -test) test_programs ;;
    esac
else
    case "$1" in
        -assembles) assemble ;;
        -test) test_programs ;;
        *)
            echo "用法："
            echo "  编译：./test.sh [目录] -assembles"
            echo "  测试：./test.sh [目录] -test"
            exit 1
            ;;
    esac
fi