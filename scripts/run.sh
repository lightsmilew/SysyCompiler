#!/bin/bash
# INPUT_DIR="compiler2023/公开样例与运行时库/functional"
# OUTPUT_DIR="compiler2023/公开样例与运行时库/functional"
INPUT_DIR="compiler2023/公开样例与运行时库/performance"
OUTPUT_DIR="compiler2023/公开样例与运行时库/performance"
#INPUT_DIR="../debug_case"
#OUTPUT_DIR="../debug_case"


# INPUT_DIR="../case/h_functional"
# OUTPUT_DIR="../case/h_functional"


if [ "$1" == "-rebuild" ]; then
    rm -rf myCompiler/build
    mkdir -p myCompiler/build
    cd myCompiler/build
    cmake ..
    make
    cd ../..
elif [ "$1" == "-build" ]; then
    cd myCompiler/build
    make
    cd ../..
elif [ "$1" == "-ir" ]; then
    mkdir -p "$OUTPUT_DIR"
    info_flag=""
    # 检查是否有 -info 参数（支持 -ir -info 或 -ir -O1 -info）
    if [[ "$2" == "-info" ]]; then
        info_flag="-info"
        shift
    fi
    if [[ "$2" =~ ^-O[0-9]+$ ]]; then
        opt_level="${2#-}"
        if [[ "$3" == "-info" ]]; then
            info_flag="-info"
        fi
        for file in $INPUT_DIR/*.sy; do
            filename=$(basename "$file")
            output_prefix="$OUTPUT_DIR/${filename%.sy}"
            echo "Processing $filename (IR debug mode, $opt_level $info_flag)..."
            ./myCompiler/build/my_compiler -debug "$file" "$output_prefix" -${opt_level} $info_flag
        done
    else
        for file in $INPUT_DIR/*.sy; do
            filename=$(basename "$file")
            output_prefix="$OUTPUT_DIR/${filename%.sy}"
            echo "Processing $filename (IR debug mode, no opt_level $info_flag)..."
            ./myCompiler/build/my_compiler -debug "$file" "$output_prefix" $info_flag
        done
    fi
elif [ "$1" == "-riscv" ]; then
    mkdir -p "$OUTPUT_DIR"
    if [[ "$2" =~ ^-O[0-9]+$ ]]; then
        opt_level="${2#-}"
        for file in $INPUT_DIR/*.sy; do
            filename=$(basename "$file")
            echo "Processing $filename (RISC-V mode, $opt_level)..."
            timeout 300s ./myCompiler/build/my_compiler -S -o "$OUTPUT_DIR/${filename%.sy}.s" "$file" "-${opt_level}"
            status=$?
            if [ $status -eq 124 ]; then
                echo "⏰ $filename 编译超时（300秒）"
            fi
        done
    else
        for file in $INPUT_DIR/*.sy; do
            filename=$(basename "$file")
            echo "Processing $filename (RISC-V mode, no opt_level)..."
            timeout 300s ./myCompiler/build/my_compiler -S -o "$OUTPUT_DIR/${filename%.sy}.s" "$file"
            status=$?
            if [ $status -eq 124 ]; then
                echo "⏰ $filename 编译超时（300秒）"
            fi
        done
    fi
elif [ "$1" == "-transfer" ]; then
        scp -P 2222 $INPUT_DIR/*.s $INPUT_DIR/*.in $INPUT_DIR/*.out ubuntu@localhost:/home/ubuntu/riscv
elif [ "$1" == "-qemu" ]; then
    qemu-system-riscv64 \
      -machine virt \
      -nographic \
      -m 2048 \
      -smp 4 \
      -kernel /usr/lib/u-boot/qemu-riscv64_smode/uboot.elf \
      -device virtio-net-device,netdev=eth0 \
      -netdev user,id=eth0,hostfwd=tcp::2222-:22 \
      -device virtio-rng-pci \
      -drive file=ubuntu-24.04.2-preinstalled-server-riscv64.img,format=raw,if=virtio
#优化使用，比较不同级别优化效果
elif [ "$1" == "-diff" ]; then
    for file in $INPUT_DIR/*.sy; do
        filename=$(basename "$file" .sy)
        file1="$OUTPUT_DIR/${filename}.ir.optO1"
        file2="$OUTPUT_DIR/${filename}.ir.optO11"
        if [ -f "$file1" ] && [ -f "$file2" ]; then
            line1=$(wc -l < "$file1")
            line2=$(wc -l < "$file2")
            if [ "$line1" -eq "$line2" ]; then
                echo "$filename: 行数相同 ($line1 行)"
            else
                echo "$filename: 行数不同 ($line1 vs $line2)"
            fi
        else
            echo "$filename: 缺少 $file1 或 $file2"
        fi
    done     
elif [ "$1" = "-rvdiff" ]; then
    if [ "$2" == "-detail" ]; then
        # 详细逐行比较模式
        for file in $INPUT_DIR/*.sy; do
            filename=$(basename "$file" .sy)
            file1="$OUTPUT_DIR/${filename}.s"
            file2="$OUTPUT_DIR/temp/${filename}.s"
            
            echo -e "\n\033[1;34m==========================================\033[0m"
            echo -e "\033[1;34m📊 比较汇编文件: $filename\033[0m"
            echo -e "\033[1;34m==========================================\033[0m"
            
            if [ -f "$file1" ] && [ -f "$file2" ]; then
                line1=$(wc -l < "$file1")
                line2=$(wc -l < "$file2")
                size1=$(stat -c%s "$file1")
                size2=$(stat -c%s "$file2")
                
                echo "📁 文件大小: $size1 bytes vs $size2 bytes"
                echo "📄 行数: $line1 vs $line2"
                
                # 使用 diff 进行逐行比较
                if diff -q "$file1" "$file2" > /dev/null; then
                    echo -e "\033[1;32m✅ 汇编代码完全相同\033[0m"
                else
                    echo -e "\033[1;33m⚠️  汇编代码有差异:\033[0m"
                    echo ""
                    
                    # 显示详细差异，使用统一格式
                    diff -u --label "原始版本($file1)" --label "对比版本($file2)" "$file1" "$file2" | head -50
                    
                    # 统计差异信息
                    added_lines=$(diff "$file1" "$file2" | grep '^>' | wc -l)
                    deleted_lines=$(diff "$file1" "$file2" | grep '^<' | wc -l)
                    changed_lines=$(diff "$file1" "$file2" | grep '^[0-9]' | wc -l)
                    
                    echo ""
                    echo "📈 差异统计:"
                    echo "  - 新增行数: $added_lines"
                    echo "  - 删除行数: $deleted_lines"
                    echo "  - 变更块数: $changed_lines"
                    
                    # 如果差异太多，提示查看完整差异
                    total_diff_lines=$(diff "$file1" "$file2" | wc -l)
                    if [ $total_diff_lines -gt 100 ]; then
                        echo "  ⚠️  差异较多，仅显示前50行。查看完整差异:"
                        echo "     diff -u $file1 $file2"
                    fi
                fi
            else
                echo -e "\033[1;31m❌ 缺少文件: $file1 或 $file2\033[0m"
            fi
        done
        
    elif [ "$2" == "-summary" ]; then
        # 汇总比较模式
        echo -e "\033[1;34m📊 汇编代码比较汇总\033[0m"
        echo "文件名 | 原始行数 | 对比行数 | 变化 | 状态"
        echo "-------|----------|----------|------|------"
        
        total_orig=0
        total_comp=0
        same_count=0
        diff_count=0
        
        for file in $INPUT_DIR/*.sy; do
            filename=$(basename "$file" .sy)
            file1="$OUTPUT_DIR/${filename}.s"
            file2="$OUTPUT_DIR/temp/${filename}.s"
            
            if [ -f "$file1" ] && [ -f "$file2" ]; then
                line1=$(wc -l < "$file1")
                line2=$(wc -l < "$file2")
                total_orig=$((total_orig + line1))
                total_comp=$((total_comp + line2))
                
                if diff -q "$file1" "$file2" > /dev/null; then
                    status="相同"
                    change="0"
                    same_count=$((same_count + 1))
                else
                    change=$((line2 - line1))
                    if [ $change -gt 0 ]; then
                        status="增加"
                        change="+$change"
                    else
                        status="减少"
                    fi
                    diff_count=$((diff_count + 1))
                fi
                
                printf "%-15s | %-8d | %-8d | %-6s | %s\n" \
                    "$filename" "$line1" "$line2" "$change" "✅"
            else
                printf "%-15s | %-8s | %-8s | %-6s | %s\n" \
                    "$filename" "N/A" "N/A" "N/A" "❌"
            fi
        done
        
        echo "-------|----------|----------|------|------"
        total_change=$((total_comp - total_orig))
        if [ $total_change -gt 0 ]; then
            change_str="+$total_change"
        else
            change_str="$total_change"
        fi
        printf "%-15s | %-8d | %-8d | %-6s | %s\n" \
            "总计" "$total_orig" "$total_comp" "$change_str" "📊"
        
        echo ""
        echo "📈 统计信息:"
        echo "  - 相同文件数: $same_count"
        echo "  - 有差异文件数: $diff_count"
        echo "  - 总行数变化: $change_str"
        
    elif [ "$2" == "-instructions" ]; then
        # 指令级别比较
        echo -e "\033[1;34m🔧 RISC-V指令差异分析\033[0m"
        echo ""
        
        for file in $INPUT_DIR/*.sy; do
            filename=$(basename "$file" .sy)
            file1="$OUTPUT_DIR/${filename}.s"
            file2="$OUTPUT_DIR/temp/${filename}.s"
            
            if [ -f "$file1" ] && [ -f "$file2" ]; then
                echo "📄 文件: $filename"
                
                # 提取并比较指令
                grep -E '^\s*[a-z]' "$file1" > /tmp/inst1.tmp 2>/dev/null || true
                grep -E '^\s*[a-z]' "$file2" > /tmp/inst2.tmp 2>/dev/null || true
                
                inst1_count=$(wc -l < /tmp/inst1.tmp)
                inst2_count=$(wc -l < /tmp/inst2.tmp)
                
                echo "  指令数量: $inst1_count vs $inst2_count"
                
                if ! diff -q /tmp/inst1.tmp /tmp/inst2.tmp > /dev/null; then
                    echo "  🔍 指令差异:"
                    diff -u /tmp/inst1.tmp /tmp/inst2.tmp | grep -E '^[+-]' | head -10
                fi
                
                # 清理临时文件
                rm -f /tmp/inst1.tmp /tmp/inst2.tmp
                echo ""
            fi
        done
        
    else
        # 默认简单比较（保持原有功能）
        for file in $INPUT_DIR/*.sy; do
            filename=$(basename "$file" .sy)
            file1="$OUTPUT_DIR/${filename}.s"
            file2="$OUTPUT_DIR/temp/${filename}.s"
            if [ -f "$file1" ] && [ -f "$file2" ]; then
                line1=$(wc -l < "$file1")
                line2=$(wc -l < "$file2")
                if [ "$line1" -eq "$line2" ]; then
                    echo "$filename: 行数相同 ($line1 行)"
                else
                    echo "$filename: 行数不同 ($line1 vs $line2)"
                    # 快速显示是否有实质差异
                    if diff -q "$file1" "$file2" > /dev/null; then
                        echo "  ✅ 但内容相同"
                    else
                        echo "  ⚠️  内容也有差异"
                    fi
                fi
            else
                echo "$filename: 缺少 $file1 或 $file2"
            fi
        done
    fi
elif [ "$1" == "-gdb" ]; then
    for file in $INPUT_DIR/*.sy; do
        filename=$(basename "$file")
        echo -e "\n\033[1;34m==========================================\033[0m"
        echo -e "\033[1;34m🔍 Processing: $filename\033[0m"
        echo -e "\033[1;34m==========================================\033[0m"
        # 直接显示编译器输出和错误信息
        ./myCompiler/build/my_compiler -S -o "$OUTPUT_DIR/${filename%.sy}.s" "$file"
        status=$?
        if [ $status -ne 0 ]; then
            echo -e "\n\033[1;31m❌ ERROR OCCURRED WITH: $filename\033[0m"
            echo -e "\033[1;31m==========================================\033[0m"
            echo -e "\033[1;33m🔧 Running with GDB for debugging...\033[0m"
            gdb --batch --ex run --ex bt --ex quit --args ./myCompiler/build/my_compiler -S -o "$OUTPUT_DIR/${filename%.sy}.s" "$file"
        else
            echo -e "\033[1;32m✅ Successfully processed: $filename\033[0m"
        fi
    done
fi