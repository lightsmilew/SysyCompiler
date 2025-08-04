### 运行脚本

本项目提供了 `run.sh` 脚本，支持一键编译、运行、测试、调试和结果对比等多种常用操作。常用参数及用法如下：

```bash
# 编译项目（进入 build 目录并 make）
./run.sh -build

# 清理并重新 cmake + make（全量重构编译）
./run.sh -rebuild

# 对 INPUT_DIR 下所有 .sy 文件生成 IR 中间代码，输出到 OUTPUT_DIR
./run.sh -ir

# 生成优化后的 IR（可加 -O0/-O1/-O2 指定优化等级）
./run.sh -ir -O1

# 生成 RISC-V 汇编代码（可加 -O0/-O1/-O2 指定优化等级）
./run.sh -riscv -O2

# gdb 调试所有 .sy 文件，遇到崩溃自动进入 gdb 并打印回溯
./run.sh -gdb

# 启动 qemu-riscv64 虚拟机环境
./run.sh -qemu

# 将 OUTPUT_DIR 下的 .s/.in/.out 文件通过 scp 传输到 qemu 虚拟机
./run.sh -transfer

# 对比不同优化等级下生成的 IR 文件行数，便于分析优化效果
./run.sh -diff
```

**脚本参数可组合使用，具体用法详见脚本注释或直接运行 `./run.sh` 查看帮助。**

- `INPUT_DIR` 和 `OUTPUT_DIR` 可在脚本顶部灵活配置，支持多套测试用例和输出目录切换。
- 支持自动创建输出目录、超时检测、彩色输出、详细进度提示等功能，便于批量测试和调试。