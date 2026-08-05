# IRPass 设计与实现一览

本文档描述 `/midend/pass` 目录下的 IR 优化 Pass，以及 `OptimizationPasses.cpp` 中各优化等级的流水线配置。

---

## PassManager 与优化等级

- **PassManager**：按顺序对每个 Pass 作用于模块内所有非库函数；`FunctionInliningPass` / `RemoveUnusedGlobalAndFunctionPass` 之后清理已无引用的函数。
- **调试**：`-debug -info` 时，各 Pass 的 `toString()` 输出追加到 `.ir.optO*` 文件末尾。
- **优化等级**（`createOptimizationPipeline`）：

| 等级 | 用途 |
|------|------|
| **O0** | 完整中端流水线（含 `RecursionNormalization` 等；**不含** O1 专属 `LoopVersioning`） |
| **O1** | 完整流水线 + **`LoopVersioning`**（日常评测主等级） |
| **O17** | 完整流水线变体：无 `RecursionNormalization` / `LoopVersioning`；内联后等多一轮 DCE |
| **O2** | 仅 DCE + Phi 消除（调试/最小优化） |
| **O15** | 部分 Pass 实验配置（大量 Pass 被注释） |
| **O16** | 完整流水线至循环展开附近，**跳过后段** GEP 展开、尾递归、Phi 消除、LICM 等（供后端调试） |

### O1 流水线阶段概览

**阶段 A — 预处理与内联前**

`CFGSimplification` → `RecursionNormalization` → `MemoizationV2` → `CSE(块内)` → `RemoveRedundantStore` → `Normalization` → `GlobalScalarPromotion` → `PowDivLoopReduction` → `CompareChainFold` → `HelperReturnAnalysis` → `FunctionInlining` → `LoopNestInteriorSplit`

**阶段 B — 数组与循环结构**

`ModLoopReduction` → `AllocaCoalesce` → `ArrayElimination` → `RemoveOnlyWriteArray` → `DCE` → **`LoopLinearIterationFold`** → **`ArrayCopyPropagation`** → `LoopSumReduction` → `BasicBlockMerge` → `ConstantFolding`

**阶段 C — 循环变换与展开**

`LoopIfGuardHoist` → `LoopNestedBoundTightening` → `BBMerge` → `LoopInductionStrengthReduction` → `CondGuardedAccumulate` → `MatrixStructureAnalysis` → `SkewSymmetricLoopRestrict` → `LoopInterchange` → **`LoopVersioning`(O1)** → `LoopUnrolling` → `CopyChainElimination` → `InstructionCombine` → `ArrayStoreLoadForward`

**阶段 D — GEP 与算术归约**

`DCE` → `BBMerge` → `ConstantFolding` → `GEPExpansion` → `ArrayStoreLoadForward` → `DCE` → `CSE` → `AddChainReduction` → `GEPChainFold` → `InstructionCombine` → `DCE`

**阶段 E — 收尾与后端准备**

`TailRecursionElimination` → `FunctionInlining(二次)` → `GEPToBitCast` → `PhiElimination` → `AddChainReduction` → **`LoopInvariantCodeMotion`(IR)** → `ConstantFolding` → `CSE` → `SRFixed` → `ConstantFolding` → `RemoveRedundantStore` → `BasicBlockReorder` → `DCE` → `RemoveUnusedGlobalAndFunction` → **`LoopGccStyleTransform`**


---

## 一、控制流与基本块

### CFGSimplificationPass（控制流图简化）

- 合并空基本块、删除无用跳转、简化条件分支。
- 支持 if-else 链转循环等结构化简。

### BasicBlockMergePass（基本块合并）

- 合并「唯一后继且后继唯一前驱」的基本块对，修正 phi 与 CFG。

### BasicBlockReorderPass（基本块重排）

- 按支配树 DFS 重排基本块，真分支优先，提升代码布局。

### NormalizationPass（归一化）

- 将 `>=`/`>` 转为 `<=`/`<`；常数项归一化到右操作数，便于后续循环/比较优化。

### LoopGccStyleTransformPass（GCC 风格循环变换）

- 将 LLVM 风格（cond 在 header、body 回跳 cond）变为 GCC 风格（入口判断 + body 末尾回边）。
- 在流水线**末尾**运行，便于后端按 GCC 循环结构生成代码。

---

## 二、死代码消除与清理

### DeadCodeEliminationPass（死代码消除）

- 删除不可达块；标记有副作用或被使用的指令为活跃，递归删除无用指令；可多轮迭代。

### RemoveRedundantStorePass（冗余 Store 删除）

- 同一基本块内，若 store 的值与最近一次同地址 load 相同且中间无其它 store，则删除该 store。
- **须在内联前**运行，避免跨函数别名分析。

### RemoveUnusedGlobalAndFunctionPass（无用全局量/函数删除）

- 删除无引用的全局变量与函数定义。

---

## 三、常量折叠与算术

### ConstantFoldingPass（常量折叠）

- 对算术、比较、类型转换等常量表达式求值；恒等消除（`x+0`、`x*1` 等）；条件跳转常量化。

### AddChainReductionPass（加法链归约）

- 将连续同用户加法链归约为乘法等形式（安全时）。

### SRFixedPass（强度削弱）

- 含魔数法常数除法/取模、2 的幂移位、条件减法取模等；替代旧版通用 IR 强度削弱。

### PowDivLoopReductionPass（幂除循环归约）

- 改写为 `(num >> (pos<<log2(R))) & (R-1)`（`pos >= 32/log2(R)` 时为 0）；并记录已改写 callee，供调用点内联替换。


### CompareChainFoldPass（比较链折叠 / 强度削弱）

- 将可归约为移位的比较/条件链折叠为 `shl`/`sra`（通用强度削弱）。


---

## 四、公共子表达式与内存

### CommonSubexpressionEliminationPass（CSE）

- 基于表达式 key 的支配树 CSE；load 需同块且无 intervening store；支持路径敏感分析。
- 流水线中多次出现：内联前 `CSE(1)`、GEP 展开后、Phi 消除后各一轮。

### RemoveRedundantStorePass

见第二节。

### ArrayStoreLoadForwardPass（数组 store-load 转发）

- 同一数组元素先 store 后 load 时，load 直接使用前序 store 的值。

---

## 五、函数与调用

### FunctionInliningPass（函数内联）

- 按体积、递归、副作用等条件内联；修正参数、返回值与控制流。
- 流水线末段有**二次内联**（`aggressive=true`），配合尾递归消除。

### TailRecursionEliminationPass（尾递归消除）

- 尾调用改循环 + 参数更新，消除递归栈开销。

### HelperReturnAnalysisPass（辅助返回值分析）

- 模块级分析 helper 函数返回值模式，供内联与后续优化使用。

### MemoizationV2Pass（记忆化 V2）

- 对满足条件的递归函数插入统一缓存表（4096 槽），入口校验参数后查表/写表。

### RecursionNormalizationPass（递归形态规范化）

- 将可证明的「主参数 + 深度累加器」二元递归规范化为一元形式，便于后续记忆化。


### GlobalScalarPromotionPass（全局标量提升）

- 将仅局部使用的全局标量提升为函数内局部变量。

---

## 六、循环 — 通用

### LoopInvariantCodeMotionPass（IR 层 LICM）

- 将循环不变指令外提到 preheader；检查 store 副作用与地址依赖。
- **在 Phi 消除之后**运行（Phi 会阻碍外提判断）。

### LoopUnrollingPass（循环展开）

- 常量 trip 且 ≤100 时尝试完全展开；否则四路部分展开（体过大则跳过）。
- 同一函数内完全展开最多连续 2 层；支持 copy 归纳变量与 `_unroll_exit` 标记。

### LoopSumReductionPass（循环求和归约）

- `sum = sum + f(i)` 等高斯公式归约。

### ModLoopReductionPass（模循环归约）

- `(sum + x) % c` 形式循环公式化。


### LoopIfGuardHoistPass（循环 if 守卫外提）

- 将循环内不变条件/守卫外提到更外层或 preheader。

### LoopNestedBoundTighteningPass（嵌套循环界收紧）

- 利用外层界收紧内层循环上界。

### LoopInductionStrengthReductionPass（归纳变量强度削弱）

- 将 `i * stride` 等转为递推指针/增量形式。

### CondGuardedAccumulatePass（条件 guarded 累加）

- 识别带条件的累加模式并化简。

### LoopVersioningPass（循环版本化）

- 对含循环不变除数的 nest 做版本化，特化快路径供后续常数除法强度削弱。

---

## 七、循环 — 迭代折叠与拷贝传播

### LoopLinearIterationFoldPass（外层线性/迭代不变折叠）

两类互补优化，**不使用** copy-nest 模式匹配：

1. **迭代不变外层循环**  
   - 条件：外层 trip > 1；归纳变量在循环体中仅用于比较/自增；所有 loop-carried phi 迭代不变；**读写全局数组在首次 store 前不得 load**（防止跨轮依赖）；每轮按执行顺序的首写 store 不依赖同单元旧值。  
   - 动作：将所有 `icmp slt iv, N` 改为 `icmp slt iv, 1`。  

2. **线性累加器折叠**  
   - 条件：`acc' = acc + β`（β 不依赖 acc），外层体可线性折叠。  
   - 动作：trip 压 1，在出口用 `acc * tripBound` 补偿。

- 循环控制可从 **header 或 latch** 读取 `icmp slt`（GCC 风格循环的界在 latch 上）。

### ArrayCopyPropagationPass（数组拷贝传播）

- 识别 **纯拷贝循环**：体内仅 1 load + 1 store，**且 store 的值就是该 load**（排除 `a2[i]=f(a1[i])` 等误匹配）。
- 支持下标模式：
  - **同下标**：`dst[i] = src[i]`
  - **常量偏移**：`dst[i] = src[off + i]`（`off` 为 loop-invariant）
- 安全条件：无 enclosing 区域约束时直接传播；若 dst 在 copy 外还有 store，需证明区域内按执行顺序的首写 freshness。
- 动作：删除 copy 循环；同下标时函数内 `dst` 基址替换为 `src`；偏移时将 `dst[...]` 的 GEP 改写为 `src[off + ...]`。

### CopyChainEliminationPass（拷贝链消除）

- 在可证明冗余的拷贝链上，将使用改写到起源并删除中间拷贝。

### LoopNestInteriorSplitPass（循环 nest interior/border 拆分）

- 基于循环嵌套与内存访问结构，将 guarded kernel（拆为 **interior**（无边界检查）与 **border**（保留 guard）。

---

## 八、循环 — 矩阵/多面体

### MatrixStructureAnalysisPass（矩阵结构分析）

- 分析嵌套循环的矩阵访问模式，为 interchange / packing 等提供元数据。

### SkewSymmetricLoopRestrictPass（斜对称循环约束）

- 利用斜对称矩阵访问特性收紧循环或简化下标。

### LoopInterchangePass（循环交换）

- 交换嵌套循环顺序以改善局部性（依赖矩阵结构分析）。



---

## 九、数组

### ArrayEliminationPass（数组消除 / SRA）

- 顺序访问、可标量化的数组 alloca 替换为标量表达式。

### AllocaCoalescePass（Alloca 合并）

- 合并相邻或等价的栈分配，减小栈帧。

### RemoveOnlyWriteArrayPass（只写数组消除）

- 删除从未 load 的数组 alloca 及相关 store/GEP。

### ArrayCopyPropagationPass / ArrayStoreLoadForwardPass / CopyChainEliminationPass

见第七、四节。

---

## 十、GEP 与地址

### GEPExpansionPass（GEP 展开）

- 多维 GEP 展开为一维链式 GEP。

### GEPChainFoldPass（GEP 链折叠）

- 合并连续 GEP/偏移为单条地址计算（须在 `AddChainReduction` 之后）。

### GEPToBitCastPass（GEP 转 BitCast）

- 全零索引 GEP 转为 BitCast。

---

## 十一、SSA 与 lowering 准备

### PhiEliminationPass（Phi 消除）

- SSA phi 转为前驱块 copy；为后端与 IR LICM 准备非 SSA 形式。

---

## 设计亮点

- **高度模块化**：每个 Pass 独立，便于 `-O15`/`-O16`/`-O1` 分段调试。
- **GCC 风格协同**：`LoopGccStyleTransform` + 后端 LICM/CSE 针对 latch 上的界与 li/la 模式优化。
- **迭代不变 + 拷贝传播解耦**：`LoopLinearIterationFold` 负责 trip 折叠；`ArrayCopyPropagation` 负责纯 copy 循环删除，语义分别证明。
- **调试友好**：所有 Pass 支持 `-info` 详细输出。

---

如需详细实现，请查阅各 Pass 源码及 `OptimizationPasses.cpp`。
