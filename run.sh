#!/bin/bash
#INPUT_DIR="../compiler2023/公开样例与运行时库/functional"
#OUTPUT_DIR="../compiler2023/公开样例与运行时库/functional"
INPUT_DIR="test_cases/semantic_cases"
OUTPUT_DIR="test_cases/output"
#INPUT_DIR="test_cases/official_cases"
#OUTPUT_DIR="test_cases/official_output"
#INPUT_DIR="case/final_performance"
#OUTPUT_DIR="case/final_performance/output"


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
    for file in $INPUT_DIR/*.sy; do
        filename=$(basename "$file")
        echo "Processing $filename..."
        # 支持带调试参数
        if [ "$2" == "-debug" ]; then
            ./myCompiler/build/my_compiler "$file" -ir -X -X -debug > "$OUTPUT_DIR/${filename%.sy}.ir"
        else
            ./myCompiler/build/my_compiler "$file" -ir > "$OUTPUT_DIR/${filename%.sy}.ir"
        fi
    done
    #优化
elif [ "$1" == "-ir_opt" ]; then
    for file in $INPUT_DIR/*.sy; do
        filename=$(basename "$file")
        echo "Processing $filename..."
        # 执行编译器并生成优化后的IR代码
        # 支持带调试参数
        if [ "$3" == "-debug" ]; then
            ./myCompiler/build/my_compiler "$file" -ir -opt $2 -debug > "$OUTPUT_DIR/${filename%.sy}.ir.opt"
        else
            ./myCompiler/build/my_compiler "$file" -ir -opt $2 > "$OUTPUT_DIR/${filename%.sy}.ir.opt"
        fi
    done
elif [ "$1" == "-riscv" ]; then
    for file in $INPUT_DIR/*.sy; do
        filename=$(basename "$file")
        echo "Processing $filename..."
        # 执行编译器并生成RISC-V代码
        ./myCompiler/build/my_compiler "$file" -riscv > "$OUTPUT_DIR/${filename%.sy}.s"
    done
elif [ "$1" == "-gdb" ]; then
    for file in $INPUT_DIR/*.sy; do
        filename=$(basename "$file")
        echo -e "\n\033[1;34m==========================================\033[0m"
        echo -e "\033[1;34m🔍 Processing: $filename\033[0m"
        echo -e "\033[1;34m==========================================\033[0m"
        # 先尝试正常运行，成功时丢弃输出，失败时显示错误
        if ! ./myCompiler/build/my_compiler "$file" -riscv > /dev/null; then
            echo -e "\n\033[1;31m❌ ERROR OCCURRED WITH: $filename\033[0m"
            echo -e "\033[1;31m==========================================\033[0m"
            ./myCompiler/build/my_compiler "$file" -riscv
            echo -e "\033[1;31m==========================================\033[0m"
            echo -e "\033[1;33m🔧 Running with GDB for debugging...\033[0m"
            gdb --batch --ex run --ex bt --ex quit --args ./myCompiler/build/my_compiler "$file" -riscv
        else
            echo -e "\033[1;32m✅ Successfully processed: $filename\033[0m"
        fi
    done
elif [ "$1" == "-transfer" ]; then
        scp -P 2222 $INPUT_DIR/*.s $INPUT_DIR/*.in $INPUT_DIR/*.out $INPUT_DIR/*.ir.opt ubuntu@localhost:/home/ubuntu/riscv
fi