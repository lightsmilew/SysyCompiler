#!/bin/bash
INPUT_DIR="test_cases/official_cases"
OUTPUT_DIR="test_cases/official_output"
#INPUT_DIR="test_cases/semantic_cases"
#OUTPUT_DIR="test_cases/output"

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
        # 执行编译器并生成IR代码
        #./myCompiler/build/my_compiler "$file" -ast > "$OUTPUT_DIR/${filename%.sy}.ir"
        ./myCompiler/build/my_compiler "$file" -ir > "$OUTPUT_DIR/${filename%.sy}.ir"
    done
    #优化
elif [ "$1" == "-ir_opt" ]; then
    for file in $INPUT_DIR/*.sy; do
        filename=$(basename "$file")
        echo "Processing $filename..."
        # 执行编译器并生成优化后的IR代码
        ./myCompiler/build/my_compiler "$file" -ir -opt "$2"> "$OUTPUT_DIR/${filename%.sy}.ir.opt"
    done

elif [ "$1" == "-riscv" ]; then
    for file in $INPUT_DIR/*.sy; do
        filename=$(basename "$file")
        echo "Processing $filename..."
        # 执行编译器并生成RISC-V代码
        ./myCompiler/build/my_compiler "$file" -riscv > "$OUTPUT_DIR/${filename%.sy}.riscv"
    done
fi