#!/bin/bash
INPUT_DIR="../compiler2023/公开样例与运行时库/functional"
OUTPUT_DIR="../compiler2023/公开样例与运行时库/functional"
#INPUT_DIR="test_cases/semantic_cases"
#OUTPUT_DIR="test_cases/output"
#INPUT_DIR="test_cases/official_cases"
#OUTPUT_DIR="test_cases/official_output"
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
    # 支持 -O0/-O1/-O2... 作为第二参数
    opt_level="O0"
    if [[ "$2" =~ ^-O[0-9]+$ ]]; then
        opt_level="${2#-}"
    fi
    mkdir -p "$OUTPUT_DIR"
    for file in $INPUT_DIR/*.sy; do
        filename=$(basename "$file")
        output_prefix="$OUTPUT_DIR/${filename%.sy}"
        echo "Processing $filename (IR debug mode, $opt_level)..."
        ./myCompiler/build/my_compiler -debug "$file" "$OUTPUT_DIR/${filename%.sy}" -${opt_level}
    done
elif [ "$1" == "-riscv" ]; then
    for file in $INPUT_DIR/*.sy; do
        filename=$(basename "$file")
        echo "Processing $filename (RISC-V mode)..."
        # 判断是否带有 -opt 参数
        if [ "$2" == "-opt" ] && [ -n "$3" ]; then
            ./myCompiler/build/my_compiler -S -o "$OUTPUT_DIR/${filename%.sy}.s" "$file" "-O$3"
        else
            ./myCompiler/build/my_compiler -S -o "$OUTPUT_DIR/${filename%.sy}.s" "$file"
        fi
    done
elif [ "$1" == "-transfer" ]; then
        scp -P 2222 $INPUT_DIR/*.s $INPUT_DIR/*.in $INPUT_DIR/*.out  ubuntu@localhost:/home/ubuntu/riscv
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
fi