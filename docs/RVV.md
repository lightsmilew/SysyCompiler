# RISC-V Vector Extension (RVV) 指令集参考

本文档面向本编译器后续 **RVV 向量化扩展**，整理 RVV 1.0 程序员模型与常用指令。完整规范见 [RISC-V V Extension 1.0](https://docs.riscv.org/reference/isa/v20250508/unpriv/v-st-ext.html)。

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

## 9. 编译器后端落地清单（建议）

实现 RVV 扩展时可按下列顺序接入：

1. **MIR / 指令枚举**：配置、`vle/vse`、整数 `.vv/.vx`、`vmv`、归约。
2. **`vset*` 插入**：按 SEW/LMUL/`vl` 需求放置；避免冗余配置。
3. **寄存器分配**：向量寄存器 `v0`–`v31`；注意 LMUL>1 时的组约束与 `v0` 掩码占用。
4. **调用约定**：调用者保存向量状态（或按平台 ABI / 保守全保存）；SysY 无向量 ABI 时可先禁止跨调用存活。
5. **循环向量化**：识别可向量化循环 → 生成 strip-mining（`vsetvli` + body + 指针推进）。
6. **浮点 / mask / gather**：按收益逐步打开。

最小向量加循环模板：

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

## 修订记录

| 日期 | 说明 |
|------|------|
| 2026-08-07 | 初版：RVV 1.0 模型与指令分类，供后端向量扩展使用 |
