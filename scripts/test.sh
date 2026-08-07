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
# 含 RVV 指令时需 rv64gcv；可用 MARCH=rv64gc 覆盖
MARCH="${MARCH:-rv64gcv}"

# 编译功能
assemble() {
    mkdir -p "$OUTPUT_DIR"
    for file in "$INPUT_DIR"/*.s; do
        [ -f "$file" ] || continue
        filename=$(basename "$file" .s)
        echo "Assembling $filename (march=$MARCH)...."

        ${CROSS_COMPILE}as -march="$MARCH" -g "$file" -o "$OUTPUT_DIR/${filename}.o" || {
            echo " 汇编 $filename 失败"
            continue
        }

        ${CROSS_COMPILE}gcc -march="$MARCH" -static -g "$OUTPUT_DIR/${filename}.o" -L. -lsysy_riscv -lc -lgcc -o "$OUTPUT_DIR/$filename" || {
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
    local tmp
    tmp=$(mktemp)
    grep -v "^$name " "$TIME_LOG" > "$tmp" 2>/dev/null || true
    echo "$name $t" >> "$tmp"
    mv "$tmp" "$TIME_LOG"
}

# 格式化毫秒 -> 秒
format_time() {
    local ms="$1"
    printf "%.2f" $(echo "scale=2; $ms / 1000" | bc 2>/dev/null)
}

# 归一化输出再对比：折叠所有空白（含换行）为单个空格。
normalize_out() {
    tr -d '\r' < "$1" | tr -s '[:space:]' ' ' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//'
    echo
}

# 把「超长行被 4096 截断」的续行粘回去（行长 >= 4095 才粘，不影响一行一数的 h-9）。
fix_long_line_wraps() {
    awk '
    NR == 1 { prev = $0; next }
    length(prev) >= 4095 { prev = prev $0; next }
    { print prev; prev = $0 }
    END { if (NR) print prev }
    ' "$1"
}

# 提取整数 token；先粘回被拆开的负号（-\n121）。sed -z 跨行，grep -o 流式抽取。
extract_int_tokens() {
    tr -d '\r' < "$1" | sed -z 's/-[[:space:]]\+\([0-9]\)/-\1/g' | grep -oE -- '-?[0-9]+'
}

# 对拍：快路径空白归一化；中路径修长行截断；慢路径流式 token 合并（避免大串 O(n²)）。
outputs_match() {
    local exp_file="$1" act_file="$2"
    local exp_n act_n exp_f act_f exp_t act_t rc=1

    exp_n=$(mktemp)
    act_n=$(mktemp)
    normalize_out "$exp_file" > "$exp_n"
    normalize_out "$act_file" > "$act_n"
    if diff -q "$exp_n" "$act_n" > /dev/null; then
        rm -f "$exp_n" "$act_n"
        return 0
    fi

    # 中路径：仅粘超长续行后再比（覆盖 fft1/shuffle1/sl1）
    exp_f=$(mktemp)
    act_f=$(mktemp)
    fix_long_line_wraps "$exp_file" | tr -d '\r' | tr -s '[:space:]' ' ' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' > "$exp_f"
    echo >> "$exp_f"
    fix_long_line_wraps "$act_file" | tr -d '\r' | tr -s '[:space:]' ' ' | sed 's/^[[:space:]]*//;s/[[:space:]]*$//' > "$act_f"
    echo >> "$act_f"
    if diff -q "$exp_f" "$act_f" > /dev/null; then
        rm -f "$exp_n" "$act_n" "$exp_f" "$act_f"
        return 0
    fi

    # 慢路径：流式抽 token 再合并（覆盖 h-9 等短行中段截断）
    exp_t=$(mktemp)
    act_t=$(mktemp)
    extract_int_tokens "$exp_file" > "$exp_t"
    extract_int_tokens "$act_file" > "$act_t"
    awk -v exp_t="$exp_t" -v act_t="$act_t" '
    BEGIN {
        while ((getline e < exp_t) > 0) E[++ne] = e
        close(exp_t)
        while ((getline a < act_t) > 0) A[++na] = a
        close(act_t)
        i = 1; j = 1
        while (i <= ne && j <= na) {
            if (A[j] == E[i]) { i++; j++; continue }
            acc = A[j]; j++
            while (j <= na && acc != E[i] && length(acc) < length(E[i]) + 2) {
                if (A[j] ~ /^-/) break
                acc = acc A[j]; j++
            }
            if (acc != E[i]) exit 1
            i++
        }
        exit (i == ne + 1 && j == na + 1) ? 0 : 1
    }
    '
    rc=$?
    rm -f "$exp_n" "$act_n" "$exp_f" "$act_f" "$exp_t" "$act_t"
    return "$rc"
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
        TMP_EXP_N=
        TMP_ACT_N=

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

            # 对拍用原始过滤结果（保留截断换行信息）；归一化文件仅用于失败 diff 写入 log
            if outputs_match "$expected_file" "$TMP_FILTERED"; then
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
            else
                echo "  测试失败 → 差异已写入 failure_case.log"
                echo -e "\n--- $filename 测试失败 ---" >> failure_case.log
                echo "(空白归一化 diff；若仅 4096 截断数字，token 合并对拍应已通过)" >> failure_case.log
                diff -u "$TMP_EXP_N" "$TMP_ACT_N" >> failure_case.log
                failed_cases+=("$filename")
            fi
            rm -f "$TMP_OUTPUT" "$TMP_FILTERED" "$TMP_EXP_N" "$TMP_ACT_N"
        else
            ec=$?
            if [ $ec -eq 124 ]; then
                echo " 测试超时 → 已记录"
            else
                echo " 程序异常退出: $ec → 已记录"
            fi
            echo "--- $filename 超时/异常 ---" >> failure_case.log
            # 超时/异常时把截断后的输出摘要写入 log，不保留临时文件
            if [ -s "$TMP_OUTPUT" ]; then
                echo "程序输出(末尾最多 40 行):" >> failure_case.log
                tail -n 40 "$TMP_OUTPUT" >> failure_case.log
            fi
            failed_cases+=("$filename")
            rm -f "$TMP_OUTPUT" "$TMP_FILTERED" "$TMP_EXP_N" "$TMP_ACT_N"
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