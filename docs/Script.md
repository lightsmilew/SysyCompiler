### 运行脚本

本项目提供了 `run.sh` 脚本，支持一键编译、运行、测试、调试和结果对比等多种常用操作。常用参数及用法如下：

```bash
# 编译项目（进入 build 目录并 make）
./run.sh -build

# 清理并重新 cmake + make（全量重构编译）
./run.sh -rebuild

# 对 INPUT_DIR 下所有 .sy 文件生成 IR 中间代码，输出到 OUTPUT_DIR
./run.sh -ir

# 生成优化后的 IR（可加 -O0/-O1/-O17 指定优化等级；可与 -rvv 组合）
./run.sh -ir -O1
./run.sh -ir -O17 -rvv

# 生成 RISC-V 汇编代码（可与 -O17 -rvv 组合启用 RVV 向量化）
./run.sh -riscv -O17 -rvv

# gdb 调试所有 .sy 文件，遇到崩溃自动进入 gdb 并打印回溯
./run.sh -gdb

# 启动 qemu-riscv64 虚拟机环境
./run.sh -qemu

# 将 OUTPUT_DIR 下的 .s/.in/.out 文件通过 scp 传输到 qemu 虚拟机
./run.sh -transfer

# RVV 全量正确性：编译 + 增量 QEMU 验证（functional / h_functional / performance2026）
./run.sh -qemu-test-rvv
FORCE_QEMU=1 ./run.sh -qemu-test-rvv   # 忽略汇编 hash 缓存，强制全量重测

# 对比不同优化等级下生成的 IR 文件行数，便于分析优化效果
./run.sh -diff
```

**脚本参数可组合使用，具体用法详见脚本注释或直接运行 `./run.sh` 查看帮助。**

- `INPUT_DIR` 和 `OUTPUT_DIR` 可在脚本顶部灵活配置，支持多套测试用例和输出目录切换。
- 支持自动创建输出目录、超时检测、彩色输出、详细进度提示等功能，便于批量测试和调试。

### RVV 测试脚本

| 脚本 | 作用 |
|------|------|
| `tools/qemu_verify_rvv.sh` | 编译并 QEMU 验证 RVV 测例；functional/h_functional 用 **O0 + `-rvv`**，performance2026 用 **`-O17 -rvv`** |
| `tools/rvv_patterns_verify.sh` | 专项模式样例 `case/rvv_patterns/`（norvv / rvv 对照） |
| `tools/qemu_asm_cache.sh` | 汇编 SHA256 缓存（`.cache/qemu_asm/asm_sha256.tsv`）；上次 PASS 且 hash 不变则跳过传输与 guest 运行 |

环境变量：

| 变量 | 含义 |
|------|------|
| `FORCE_QEMU=1` | 禁用增量缓存，全部重新 scp 并在 QEMU 上运行 |
| `CASE_LIST` | `scripts/test.sh` 中过滤待测用例 basename 列表 |
| `QEMU_HOST` / `QEMU_PORT` / `QEMU_USER` / `QEMU_PASS` | SSH 连接 QEMU 虚拟机（默认 localhost:2222） |

QEMU 运行 RVV 程序时需向量扩展，例如：

```bash
qemu-riscv64 -cpu rv64,v=true,vlen=128 -L /usr/riscv64-linux-gnu ./a.out
```
