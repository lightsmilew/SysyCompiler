# RISC-V Vector Extension (RVV) 指令集参考

本文档包含 **RVV 1.0 程序员模型与常用指令**（§1–§8），以及本编译器 **`-rvv` 向量化扩展的实现说明**（§12）。§12.2 按**向量化模式**列出该模式生成汇编里会出现的向量指令集合，便于对照 `.s` 定位 bug。架构与流水线位置见 [设计文档.md](./设计文档.md) §4.5；Pass 细节见 [IRPass.md](./IRPass.md)「LoopVectorizePass」。

> **说明**：RVV 是可变长向量扩展（非定长 Packed-SIMD）。定长 P 扩展不在本文范围。

---

## 1. 程序员模型速览

### 1.1 寄存器与 CSR

| 资源 | 说明 |
|------|------|
| `v0`–`v31` | 32 个向量寄存器；可按 LMUL 组成寄存器组 |
| `vl` | 当前有效向量长度（元素个数） |
| `vtype` | 向量类型：SEW / LMUL / 尾/掩码策略等 |
| `vlenb` | VLEN/8（实现相关，只读） |
| `vstart` | 向量指令起始元素索引（异常恢复用） |
| `vxrm` / `vxsat` / `vcsr` | 定点舍入模式、饱和标志、合并控制 |

### 1.2 关键参数

| 参数 | 含义 |
|------|------|
| **VLEN** | 单向量寄存器位宽（实现相关，≥128） |
| **SEW** | Selected Element Width：8 / 16 / 32 / 64 |
| **LMUL** | 寄存器组倍数：`mf8`/`mf4`/`mf2`/`m1`/`m2`/`m4`/`m8` |
| **VLMAX** | 当前配置下最大元素数 ≈ `LMUL * VLEN / SEW` |
| **AVL** | Application Vector Length，传给 `vset*` 的期望长度 |

### 1.3 掩码与尾策略

- 多数运算支持 mask：`vm` 位；`v0` 常作掩码源。
- `vta` / `vma`：tail / mask agnostic（未写元素可保留或扰动，利于优化）。
- 比较类指令写 mask 向量（每位对应一个元素）。

### 1.4 操作数形式后缀

| 后缀 | 含义 | 示例 |
|------|------|------|
| `.vv` | 向量–向量 | `vadd.vv vd, vs2, vs1` |
| `.vx` | 向量–标量（x 寄存器） | `vadd.vx vd, vs2, rs1` |
| `.vi` | 向量–立即数 | `vadd.vi vd, vs2, imm` |
| `.vf` | 向量–浮点标量（f 寄存器） | `vfadd.vf vd, vs2, fs1` |
| `.vs` | 向量–标量（归约等） | `vredsum.vs vd, vs2, vs1` |
| `.v` | 单操作数 / 访存 | `vle32.v vd, (rs1)` |

汇编中常写作 `vadd.vv vd, vs2, vs1, v0.t`（带掩码）。

---

## 2. 配置指令（必用）

| 指令 | 格式 | 作用 |
|------|------|------|
| `vsetvli` | `rd, rs1, vtypei` | 按 AVL=`rs1` 与立即数 `vtype` 设置 `vl`/`vtype`，`rd←vl` |
| `vsetivli` | `rd, uimm, vtypei` | AVL 为 5 位立即数 |
| `vsetvl` | `rd, rs1, rs2` | `vtype` 来自 `rs2` |

`vtypei` 常用字段编码示例（汇编助记）：

```text
e8 / e16 / e32 / e64     # SEW
mf8 / mf4 / mf2 / m1 / m2 / m4 / m8   # LMUL
ta / tu                 # tail agnostic / undisturbed
ma / mu                 # mask agnostic / undisturbed
```

示例：

```asm
vsetvli t0, a0, e32, m1, ta, ma   # 32-bit 元素，LMUL=1
vle32.v v8, (a1)
vadd.vv v8, v8, v9
vse32.v v8, (a2)
```

编译器侧：每次改变 SEW/LMUL（或跨调用约定边界）前通常需重新 `vset*`。

---

## 3. 向量访存

宽度编码：`8` / `16` / `32` / `64`（与 SEW 对应）。

### 3.1 单位步长（Unit-Stride）

| 指令 | 含义 |
|------|------|
| `vle{8,16,32,64}.v` | 连续加载 |
| `vse{8,16,32,64}.v` | 连续存储 |
| `vlm.v` / `vsm.v` | 加载/存储 mask（按字节） |
| `vl{1,2,4,8}re{8,16,32,64}.v` | 整寄存器组加载（不受 `vl` 限制） |
| `vs{1,2,4,8}r.v` | 整寄存器组存储 |

### 3.2 跨步（Strided）

| 指令 | 含义 |
|------|------|
| `vlse{8,16,32,64}.v` | `base + i * stride` 加载 |
| `vsse{8,16,32,64}.v` | 跨步存储 |

### 3.3 索引 / Gather-Scatter

| 指令 | 含义 |
|------|------|
| `vluxei{8,16,32,64}.v` | 无序索引加载 |
| `vloxei{8,16,32,64}.v` | 有序索引加载 |
| `vsuxei{8,16,32,64}.v` | 无序索引存储 |
| `vsoxei{8,16,32,64}.v` | 有序索引存储 |

索引宽度由助记符中的 `ei*` 指定；数据宽度由当前 SEW 决定。

### 3.4 Fault-Only-First

| 指令 | 含义 |
|------|------|
| `vle{8,16,32,64}ff.v` | 单位步长；首元素故障则 trap，后续故障可截断 `vl` |

常用于 strlen / 扫描类循环。

### 3.5 Segment（结构体数组）

`vlseg` / `vsseg` / `vlsseg` / `vssseg` / `vluxseg` / `vloxseg` 等：一次加载/存储多个字段到连续向量寄存器组。编译器向量化初期可后置。

---

## 4. 整数运算

### 4.1 加减

| 指令 | 含义 |
|------|------|
| `vadd` | 加 |
| `vsub` | 减 |
| `vrsub` | 反向减（`rs1/imm - vs2`） |
| `vadc` | 带进位加 |
| `vsbc` | 带借位减 |
| `vmadc` / `vmsbc` | 产生进位/借位 mask |

形式：`.vv` / `.vx` / `.vi`（部分指令无 `.vi`）。

### 4.2 逻辑与移位

| 指令 | 含义 |
|------|------|
| `vand` / `vor` / `vxor` | 按位与/或/异或 |
| `vnot` | 伪指令（`vxor` 取反） |
| `vsll` | 逻辑左移 |
| `vsrl` | 逻辑右移 |
| `vsra` | 算术右移 |
| `vnsrl` / `vnsra` | 缩窄移位（源为 2×SEW） |

### 4.3 比较（结果写入 mask）

| 指令 | 含义 |
|------|------|
| `vmseq` | `==` |
| `vmsne` | `!=` |
| `vmsltu` / `vmslt` | `<`（无符号/有符号） |
| `vmsleu` / `vmsle` | `<=` |
| `vmsgtu` / `vmsgt` | `>`（常见为 `.vx`/`.vi`） |
| `vmsgeu` / `vmsge` | `>=`（伪指令，可用交换操作数实现） |

### 4.4 最值、符号扩展

| 指令 | 含义 |
|------|------|
| `vminu` / `vmin` | 最小 |
| `vmaxu` / `vmax` | 最大 |
| `vzext.vf{2,4,8}` | 零扩展 |
| `vsext.vf{2,4,8}` | 符号扩展 |

### 4.5 乘除与乘加

| 指令 | 含义 |
|------|------|
| `vmul` | 乘（取低 SEW 位） |
| `vmulh` / `vmulhu` / `vmulhsu` | 乘高位 |
| `vdiv` / `vdivu` | 除 |
| `vrem` / `vremu` | 余数 |
| `vmacc` | `vd += vs1 * vs2` |
| `vnmsac` | `vd -= vs1 * vs2` |
| `vmadd` | `vd = vs1 * vd + vs2` |
| `vnmsub` | `vd = -vs1 * vd + vs2` |

### 4.6 加宽 / 缩窄整数

| 指令 | 含义 |
|------|------|
| `vwaddu` / `vwadd` | 加宽加（结果 2×SEW） |
| `vwsubu` / `vwsub` | 加宽减 |
| `vwmulu` / `vwmul` / `vwmulsu` | 加宽乘 |
| `vwmaccu` / `vwmacc` / `vwmaccsu` / `vwmaccus` | 加宽乘加 |
| `vnsrl` / `vnsra` | 缩窄移位 |
| `vnclipu` / `vnclip` | 缩窄饱和裁剪 |

加宽指令要求目的寄存器组不与源重叠（规范有约束）。

---

## 5. 定点数（可选，DSP 类）

依赖定点舍入 CSR `vxrm`（`rnu`/`rne`/`rdn`/`rod`）与饱和标志 `vxsat`。

| 指令 | 含义 |
|------|------|
| `vsaddu` / `vsadd` | 饱和加 |
| `vssubu` / `vssub` | 饱和减 |
| `vaaddu` / `vaadd` | 平均加（带舍入） |
| `vasubu` / `vasub` | 平均减 |
| `vsmul` | 饱和定点数乘 |
| `vssrl` / `vssra` | 舍入右移 |
| `vnclipu` / `vnclip` | 缩窄饱和 |

SysY 竞赛路径若以 `i32`/`float` 为主，可后置实现。

---

## 6. 浮点运算

需 `F`/`D` 与对应向量子集（如 `Zve32f` / `Zve64d` / 完整 `V`）。

### 6.1 基础算术

| 指令 | 含义 |
|------|------|
| `vfadd` / `vfsub` / `vfrsub` | 加 / 减 / 反向减 |
| `vfmul` / `vfdiv` / `vfrdiv` | 乘 / 除 / 反向除 |
| `vfsqrt` | 平方根 |
| `vfmin` / `vfmax` | 最小 / 最大 |
| `vfsgnj` / `vfsgnjn` / `vfsgnjx` | 符号注入 |
| `vfclass` | 浮点分类 → 整数 mask 类结果 |

形式：`.vv` / `.vf`。

### 6.2 融合乘加（FMA）

| 指令 | 含义 |
|------|------|
| `vfmadd` | `vd = vs1 * vd + vs2` |
| `vfnmadd` | `vd = -(vs1 * vd) - vs2` |
| `vfmsub` | `vd = vs1 * vd - vs2` |
| `vfnmsub` | `vd = -(vs1 * vd) + vs2` |
| `vfmacc` | `vd = vs1 * vs2 + vd` |
| `vfnmacc` / `vfmsac` / `vfnmsac` | 对应变体 |

### 6.3 比较

| 指令 | 含义 |
|------|------|
| `vmfeq` / `vmfne` | `==` / `!=` |
| `vmflt` / `vmfle` | `<` / `<=` |
| `vmfgt` / `vmfge` | `>` / `>=`（多为 `.vf`） |

### 6.4 转换

| 指令 | 含义 |
|------|------|
| `vfcvt.xu.f.v` / `vfcvt.x.f.v` | float → 无符号/有符号整数 |
| `vfcvt.rtz.xu.f.v` / `vfcvt.rtz.x.f.v` | 向零舍入转换 |
| `vfcvt.f.xu.v` / `vfcvt.f.x.v` | 整数 → float |
| `vfwcvt.*` | 加宽转换（如 `f32→f64`，`i32→f64`） |
| `vfncvt.*` | 缩窄转换 |

### 6.5 加宽浮点

| 指令 | 含义 |
|------|------|
| `vfwadd` / `vfwsub` / `vfwmul` | 加宽加减乘 |
| `vfwmacc` / `vfwnmacc` / `vfwmsac` / `vfwnmsac` | 加宽 FMA |

---

## 7. Mask、归约、搬移与排列

### 7.1 Mask 逻辑

| 指令 | 含义 |
|------|------|
| `vmand` / `vmnand` | AND / NAND |
| `vmandn` | `vs2 & ~vs1` |
| `vmor` / `vmnor` | OR / NOR |
| `vmorn` | `vs2 \| ~vs1` |
| `vmxor` / `vmxnor` | XOR / XNOR |
| `vmmv` / `vmclr` / `vmset` / `vmnot` | 伪指令 |

### 7.2 Mask 与标量交互

| 指令 | 含义 |
|------|------|
| `vcpop.m` | 统计 mask 中 1 的个数 |
| `vfirst.m` | 第一个置位元素下标（无则 -1） |
| `vmsbf.m` / `vmsif.m` / `vmsof.m` | before / including / only first set |
| `viota.m` | 按 mask 生成 iota 索引 |
| `vid.v` | 元素下标向量 `0..vl-1` |

### 7.3 整数 / 浮点归约

| 指令 | 含义 |
|------|------|
| `vredsum` / `vredand` / `vredor` / `vredxor` | 和 / 与 / 或 / 异或 |
| `vredminu` / `vredmin` / `vredmaxu` / `vredmax` | 最值归约 |
| `vwredsumu` / `vwredsum` | 加宽求和 |
| `vfredusum` / `vfredosum` | 浮点和（无序/有序） |
| `vfredmin` / `vfredmax` | 浮点最值 |
| `vfwredusum` / `vfwredosum` | 加宽浮点和 |

形式多为 `.vs`：结果写入 `vd[0]`。

### 7.4 标量 ↔ 向量搬移

| 指令 | 含义 |
|------|------|
| `vmv.v.v` / `vmv.v.x` / `vmv.v.i` | 整向量填充/拷贝 |
| `vmv.s.x` | 标量 → `vd[0]` |
| `vmv.x.s` | `vs2[0]` → 标量 |
| `vfmv.v.f` / `vfmv.s.f` / `vfmv.f.s` | 浮点对应搬移 |
| `vmv{1,2,4,8}r.v` | 整寄存器组拷贝 |

### 7.5 合并、压缩、滑动、收集

| 指令 | 含义 |
|------|------|
| `vmerge` | 按 mask 在两源间选择 |
| `vcompress` | 按 mask 压缩元素到低端 |
| `vslideup` / `vslidedown` | 元素上/下滑 |
| `vslide1up` / `vslide1down` | 滑入一个标量 |
| `vfslide1up` / `vfslide1down` | 浮点版 |
| `vrgather` / `vrgatherei16` | 按索引收集 |

---

## 8. 标准子集与目标选型

| 扩展 | 内容概要 |
|------|----------|
| `Zve32x` | 嵌入式向量：整数，SEW≤32，不含浮点 |
| `Zve32f` | `Zve32x` + 单精度向量浮点 |
| `Zve64x` | 整数，SEW≤64 |
| `Zve64f` | `Zve64x` + 单精度 |
| `Zve64d` | `Zve64f` + 双精度 |
| `V` | 完整应用处理器向量扩展（含上述能力与更大 VLEN 要求） |
| `Zvfh` / `Zve32f` 等 | 半精度等可选扩展 |

**对本项目建议（SysY → RISC-V）：**

| 阶段 | 目标 | 优先指令 |
|------|------|----------|
| MVP | `Zve32x` 或 qemu `rv64gcv` | `vsetvli`、`vle32`/`vse32`、`vadd`/`vmul`、`vmv`、简单归约 |
| 浮点 | `Zve32f` / `V` | `vfadd`/`vfmul`/`vfmadd`、`vfcvt`、`vle32`/`vse32` |
| 进阶 | 完整 `V` | strided/indexed、mask 循环、`vrgather`、segment |

QEMU / 交叉工具链常见 march：`rv64gcv`、`rv64gc_zve32f` 等。

---

## 9. 编译器后端落地清单

| 项 | 状态 | 说明 |
|----|------|------|
| MIR / 指令枚举 | **已实现** | `RISCVOpcode`：`VSETVLI`、`VLE32_V`/`VSE32_V`、`VLSE32_V`/`VSSE32_V`、整数/浮点 `.vv`、归约 |
| `vset*` 插入 | **已实现** | `VecSetVlInst` → `vsetvli`；默认 e32、m1、ta/ma |
| 寄存器分配 | **已实现** | 向量寄存器独立着色；LMUL 固定 m1 |
| 调用约定 | **保守** | SysY 无向量 ABI；向量值不跨函数调用存活 |
| 循环向量化 | **已实现** | `LoopVectorizePass`：strip-mining + 多种循环模式（见 §12） |
| mask / gather | **未实现** | 暂无条件向量循环、`vrgather` 等 |

最小向量加循环模板（与后端生成形态一致）：

```asm
# a0=n, a1=dst, a2=src0, a3=src1
1:
    vsetvli t0, a0, e32, m1, ta, ma
    vle32.v v0, (a2)
    vle32.v v1, (a3)
    vadd.vv v2, v0, v1
    vse32.v v2, (a1)
    slli    t1, t0, 2      # bytes = vl * 4
    add     a1, a1, t1
    add     a2, a2, t1
    add     a3, a3, t1
    sub     a0, a0, t0
    bnez    a0, 1b
```

---

## 10. 指令速查总表（按功能）

| 类别 | 代表助记符 |
|------|------------|
| 配置 | `vsetvli` `vsetivli` `vsetvl` |
| 连续访存 | `vle*.v` `vse*.v` `vlm.v` `vsm.v` `vl*re*.v` `vs*r.v` |
| 跨步/索引 | `vlse*` `vsse*` `vluxei*` `vloxei*` `vsuxei*` `vsoxei*` |
| 整数算术 | `vadd` `vsub` `vrsub` `vand` `vor` `vxor` `vsll` `vsrl` `vsra` |
| 比较/最值 | `vmseq` `vmsne` `vmslt*` `vmsle*` `vmin*` `vmax*` |
| 乘除/FMA整 | `vmul*` `vdiv*` `vrem*` `vmacc` `vmadd` … |
| 加宽/缩窄 | `vwadd*` `vwmul*` `vwmacc*` `vnsrl` `vnclip*` `vzext` `vsext` |
| 定点 | `vsadd*` `vssub*` `vaadd*` `vsmul` `vssr*` |
| 浮点 | `vfadd` `vfmul` `vfdiv` `vfsqrt` `vfmadd*` `vfmacc*` `vfmin` `vfmax` |
| 浮点比较/转换 | `vmfeq` `vmflt` `vfcvt.*` `vfwcvt.*` `vfncvt.*` |
| Mask | `vmand*` `vmor*` `vmxor*` `vcpop.m` `vfirst.m` `viota.m` `vid.v` |
| 归约 | `vredsum` `vredmin*` `vfredusum` `vfwred*` |
| 搬移/排列 | `vmv.*` `vfmv.*` `vmerge` `vcompress` `vslide*` `vrgather*` |

---

## 11. 参考链接

- [V Extension for Vector Operations, Version 1.0](https://docs.riscv.org/reference/isa/v20250508/unpriv/v-st-ext.html)
- [riscv-v-spec（归档仓库）](https://github.com/riscvarchive/riscv-v-spec)
- [指令编码表 inst-table](https://github.com/riscv/riscv-v-spec/blob/master/inst-table.adoc)

---

## 12. 本编译器 `-rvv` 向量化实现

### 12.1 启用方式

```bash
./myCompiler/build/my_compiler -S -o out.s prog.sy -rvv          # O0 + RVV
./myCompiler/build/my_compiler -S -o out.s prog.sy -O17 -rvv   # 性能测例推荐
./run.sh -riscv -O17 -rvv
```

`-rvv` 设置 `CompilerConfig::enableRVV`，在 O0/O1/O17 流水线中插入 `LoopVectorizePass`。**O0/O1/O17** 均在 `LoopInterchange` 后与 **`LoopGccStyleTransform` 末尾**各跑一轮（后者用于 copy/phi 归约形态稳定后的 array max 等）。

QEMU 运行需向量扩展：`qemu-riscv64 -cpu rv64,v=true,vlen=128 …`

### 12.2 各模式用到的向量指令

实现：`LoopVectorizePass.cpp` → `InstructionSelector.cpp`。统一配置：`vsetvli …, e32, m1, ta, ma`。

识别顺序：Scaled-row → Elementwise → Array reduce/max/min → Scalar chain；存在有效 `inPlaceCopyOriginChain` 时整函数跳过向量化。

下面「用到的指令」指该模式向量循环体里**会发出的 RISC-V 向量指令集合**（标量 `add/sub/mul/gep/branch` 省略）。浮点列在括号内。

---

#### A. Scaled-row 更新

- **形态**：`C[i][j] = C[i][j]*scale + B[k][j]`，或 TRSM 减法形 `C -= scale*B`
- **函数**：`vectorizeScaledRowUpdate`

| 用途 | 指令 |
|------|------|
| 设 vl | `vsetvli` |
| 广播 scale | `vmv.v.x`（`vfmv.v.f`） |
| 读 C / B 行 | `vle32.v` ×2 |
| 乘加形 | `vmul.vv` + `vadd.vv`（`vfmul.vv` + `vfadd.vv`） |
| 乘减形 | `vmul.vv` + `vsub.vv`（`vfmul.vv` + `vfsub.vv`） |
| 写回 C | `vse32.v` |

**不用**：`vid.v`、`vlse32`/`vsse32`、任何 `vred*` / `vfred*`。

典型序列（乘加）：

```text
vsetvli t0, count, e32, m1, ta, ma
vmv.v.x  vS, scale          # 或 vfmv.v.f
vle32.v  vC, (cPtr)
vle32.v  vB, (bPtr)
vmul.vv  vT, vC, vS         # 或 vfmul.vv
vadd.vv  vT, vT, vB         # 或 vfadd.vv；减法形则为 vsub
vse32.v  vT, (cPtr)
```

---

#### B. 逐元素循环（Elementwise）

- **形态**：填充、拷贝、`axpy`、逐元素算术/移位等写回数组
- **函数**：`vectorizeElementwiseLoop`
- **说明**：具体子集随表达式变化；下表为该模式**可能**出现的全集

| 用途 | 指令 |
|------|------|
| 设 vl | `vsetvli` |
| 常量/不变式广播 | `vmv.v.x`（`vfmv.v.f`） |
| 连续读/写 | `vle32.v` / `vse32.v` |
| 跨步读/写（列 stride） | `vlse32.v` / `vsse32.v`（stride=元素步长×4 字节） |
| 算术 | `vadd.vv` `vsub.vv` `vmul.vv` `vdiv.vv` `vrem.vv`（浮点：`vfadd` `vfsub` `vfmul` `vfdiv`，无 frem） |
| 移位 | `vsll.vv` `vsrl.vv` `vsra.vv`（仅 int） |

**不用**：`vid.v`、`vredsum`/`vredmax`/`vredmin`/`vfredosum`。

拷贝最小集：`vsetvli` + `vle32.v` + `vse32.v`  
axpy 常见集：`vsetvli` + `vmv.v.x`/`vfmv.v.f` + `vle32.v`×2 + `vmul`/`vfmul` + `vadd`/`vfadd` + `vse32.v`

---

#### C. 数组归约（sum / 平方和 / dot）

- **形态**：`sum+=A[i]`；`sum+=A[i]*A[i]`；`sum+=A[i]*B[i]`
- **函数**：`vectorizeArrayReduceLoop`（`Kind::Sum`）

| 用途 | 指令 |
|------|------|
| 设 vl | `vsetvli` |
| 读数组 | `vle32.v`；跨步则 `vlse32.v`（dot 可两路） |
| 平方 / 点积 | `vmul.vv`（`vfmul.vv`） |
| 块内求和 | 零种子 `vmv.v.x`/`vfmv.v.f` + **`vredsum.vs`** / **`vfredosum.vs`** + `vmv.x.s` / `vfmv.f.s` |

**不用**：`vid.v`、`vse32`/`vsse32`、`vredmax`/`vredmin`。

普通 sum：

```text
vsetvli t0, count, e32, m1, ta, ma
vle32.v   vA, (ptr)           # 或 vlse32.v
vmv.v.x   vZ, zero            # 浮点 vfmv.v.f
vredsum.vs vZ, vA, vZ         # 浮点 vfredosum.vs
vmv.x.s   t1, vZ              # 浮点 vfmv.f.s
# 再标量累加到 sumacc
```

平方和在归约前多一条 `vmul.vv vA, vA, vA`；dot 为两路 load + `vmul.vv` 再归约。

---

#### D. 数组 max / min

- **形态**：`m = max(m, A[i])` / `min`（含 copy-based if）
- **函数**：`vectorizeArrayReduceLoop`（`Kind::Max/Min`）或经 `findCopyBasedArrayMaxLoop`

| 用途 | 指令 |
|------|------|
| 设 vl | `vsetvli` |
| 读数组 | `vle32.v` 或 `vlse32.v` |
| 块内 max | `vmv.v.x`（seed=`INT_MIN`）+ **`vredmax.vs`** + `vmv.x.s` |
| 块内 min | `vmv.v.x`（seed=`INT_MAX`）+ **`vredmin.vs`** + `vmv.x.s` |

块结果与标量累加器用标量 `slt/sgt` + `select` 合并（非向量指令）。

**不用**：`vid.v`、store、`vredsum`/`vfredosum`、浮点 `vf*` 归约（当前按 int）。

```text
vsetvli t0, count, e32, m1, ta, ma
vle32.v    vA, (ptr)
vmv.v.x    vT, t_int_min      # min 则 t_int_max
vredmax.vs vT, vA, vT         # 或 vredmin.vs
vmv.x.s    t1, vT
```

---

#### E. 标量链（Scalar chain）

- **形态**：`while (x < t) { sum = (sum + f(x) + 1) % mod; x += d; }`（无数组）
- **函数**：`vectorizeScalarChainLoop`

| 用途 | 指令 |
|------|------|
| 设 vl | `vsetvli` |
| lane 下标 | **`vid.v`**（本模式标志指令） |
| 构造 `x` 向量 | `vmv.v.x`（step、xCur、1）+ `vmul.vv` + `vadd.vv` |
| `f(x)` 表达式 | 按需：`vadd/vsub/vmul/vdiv/vrem/vsll/vsra.vv`；max/min 选择：`vmax.vv`/`vmin.vv` |
| 横向求和 | 零种子 `vmv.v.x` + **`vredsum.vs`** + `vmv.x.s` |

`% mod` 与 `sum` 更新在**标量**侧（`rem`/`add`），不是向量指令。

**不用**：`vle32`/`vse32`/`vlse32`/`vsse32`（无访存）；当前无浮点向量。

```text
vsetvli t0, count, e32, m1, ta, ma
vid.v    vI
vmv.v.x  vD, step
vmul.vv  vK, vI, vD
vmv.v.x  vX, xCur
vadd.vv  vX, vX, vK           # 各 lane 的 x
# ... f(x)：若干 vadd/vmul/... ...
vmv.v.x  v1, 1
vadd.vv  vF, vF, v1
vmv.v.x  vZ, zero
vredsum.vs vZ, vF, vZ
vmv.x.s  t1, vZ
# 标量：(sum + t1 % mod) % mod；xCur += vl*step
```

---

#### 模式 × 指令速查

| 指令 | A Scaled-row | B Elementwise | C Sum/Dot | D Max/Min | E Scalar chain |
|------|:---:|:---:|:---:|:---:|:---:|
| `vsetvli` | ✓ | ✓ | ✓ | ✓ | ✓ |
| `vle32.v` | ✓ | ✓ | ✓ | ✓ | |
| `vse32.v` | ✓ | ✓ | | | |
| `vlse32.v` / `vsse32.v` | | 可选 | 可选 load | 可选 load | |
| `vmv.v.x` / `vfmv.v.f` | ✓ | ✓ | 归约种子 | 归约种子 | ✓ |
| `vid.v` | | | | | ✓ |
| `vadd/vsub/vmul…`（及 `vf*`） | mul+add/sub | 按表达式 | 可选 mul | | 按 `f(x)` |
| `vsll/vsrl/vsra` | | 可选 | | | 可选 |
| `vmax/vmin.vv` | | | | | 可选 |
| `vredsum` / `vfredosum` | | | ✓ | | ✓ |
| `vredmax` / `vredmin` | | | | ✓ | |
| `vmv.x.s` / `vfmv.f.s` | | | ✓ | ✓ | ✓ |

看汇编时：出现 **`vid.v`** → 模式 E；出现 **`vredmax`/`vredmin`** → 模式 D；出现 **`vredsum`/`vfredosum` 且有 `vle32`** → 模式 C；有 **`vse32` 且无归约** → A 或 B（A 固定两路 load + mul/add|sub；B 指令更杂、可能有 `vlse`/`vsse`）。

### 12.3 测试

| 命令 | 范围 |
|------|------|
| `./run.sh -qemu-test-rvv` | functional + h_functional（O0-rvv）+ performance2026（O17-rvv） |
| `FORCE_QEMU=1 ./run.sh -qemu-test-rvv` | 忽略汇编 hash 缓存，全量 QEMU |
| `./tools/rvv_patterns_verify.sh` | `case/rvv_patterns/` 专项样例（norvv/rvv 对照） |

汇编 SHA256 缓存在 `.cache/qemu_asm/asm_sha256.tsv`；与上次 PASS 且 hash 一致则跳过 scp 与 guest 运行。

### 12.4 尚未覆盖

- 条件向量循环（mask、`vmerge`）
- `vrgather` / indexed load（FFT 奇偶置换等）
- LMUL>1、SEW≠32 的通用支持
- 跨函数向量 ABI

---

## 修订记录

| 日期 | 说明 |
|------|------|
| 2026-08-07 | 初版：RVV 1.0 模型与指令分类，供后端向量扩展使用 |
| 2026-08-10 | §9 更新为已实现状态；新增 §12 编译器 `-rvv` 向量化说明 |
| 2026-08-17 | §12.2 改为按五种模式列出各自用到的 RISC-V 向量指令集合与典型序列 |
