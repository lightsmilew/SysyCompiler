#!/bin/bash
INPUT_DIR="../compiler2023/公开样例与运行时库/functional"
OUTPUT_DIR="../compiler2023/公开样例与运行时库/functional"
#PERFORMANCE_DIR="../compiler2023/公开样例与运行时库/performance"
#INPUT_DIR="debug_case"
#OUTPUT_DIR="debug_case"

#INPUT_DIR="case/final_performance"
#OUTPUT_DIR="case/final_performance/output"
# INPUT_DIR="case/functional"
# OUTPUT_DIR="case/functional"


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
            timeout 180s ./myCompiler/build/my_compiler -S -o "$OUTPUT_DIR/${filename%.sy}.s" "$file" "-${opt_level}"
            status=$?
            if [ $status -eq 124 ]; then
                echo "⏰ $filename 编译超时（180秒）"
            fi
        done
    else
        for file in $INPUT_DIR/*.sy; do
            filename=$(basename "$file")
            echo "Processing $filename (RISC-V mode, no opt_level)..."
            timeout 180s ./myCompiler/build/my_compiler -S -o "$OUTPUT_DIR/${filename%.sy}.s" "$file"
            status=$?
            if [ $status -eq 124 ]; then
                echo "⏰ $filename 编译超时（180秒）"
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