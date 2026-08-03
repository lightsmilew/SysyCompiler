# 性能优化尝试记录（2026-08-02）

当前稳定基线：`46811bc`（partial unroll×8），线上约 **94.12s AC**。  
历史：`72c47b2`/`664fedd` NestVersion ≈94.47s；`abe591d` APMod ≈103.37s。  
流程：本地 qemu 验证目标性能样例 → push GitLab `test_16` → `educg_submit` → 涨分保留 / 否则回退。  
Session：`educg_session` 以浏览器最新 cookie 为准。

## 评分原则（决定优先级）

得分 ≈ **avg(best_time / my_time)**（越高越好）。  
因此：

- 相对最优差距大的用例（`my/best` 高）单点 upside 最大，但只有把 `my` 压到接近 `best` 量级才明显抬平均分；
- **绝对耗时长**且 ratio 仍差一截的用例（`sl*`、`many_mat*`）小幅提速也能抬总分与总时间；
- 比较基线只用**最后一次稳定 AC**，不要用已回退的错误提交。

当前最大 gap（`46811bc`）：

| 用例 | my | best | my/best | best/my |
|------|-----|------|---------|---------|
| 01_mm2/3/1 | 4.77 / 3.00 / 1.36 | 0.04 / 0.03 / 0.02 | ≈119 / 100 / 68 | ≈0.01 |
| many_mat* | ≈11 | ≈0.30–0.37 | ≈30–36 | ≈0.03 |
| h-1-03/01 | 3.75 / 0.64 | 0.39 / 0.08 | ≈9.6 / 8 | ≈0.11 |
| sl* | 7.22 / 3.71 / 0.45 | 0.94 / 0.48 / 0.06 | ≈7.5 | ≈0.13 |

输入规模：`01_mm*` 的 n≈100–150（非 1024）；`A==1` 约 0–0.5%（skip 几乎不触发）。

---

## 有效尝试

### A. InvariantDivisorNestVersion（整 nest 一次 d==3 版本化）— 有效

| 项 | 内容 |
|----|------|
| 提交 | `664fedd` |
| 目标 | `sl*`（原 ratio≈13，绝对时间大） |
| 做法 | 外层 nest **入口一次** `icmp eq d,3`：克隆 nest；快路径 `sdiv x,3`→SRFixed magic；慢路径原样；exit 处合并 live-out（i/j） |
| 挂载 | **仅 O1**（`LoopInterchange` 后、`LoopUnrolling` 前） |
| 线上 | **103.37s → 94.47s AC**（Δ≈−8.90s）；`sl1` 6.54→3.71；`sl2` 13.02→7.22；`sl3` 0.73→0.45 |
| 决策 | `IMPROVED` 保留 |

**为何有效**：消除热循环内 `divw`，且无 per-iter 分支（对比 §3 TLE）。  
**注意**：本地 qemu 对 `sl1` 长输出在第 4096 字符插空格会误报失败，基线编译器同样如此；以线上为准。

### B. Partial unroll 4→8 — 有效（小幅）

| 项 | 内容 |
|----|------|
| 提交 | `46811bc` |
| 目标 | 稠密内层循环（matmul / many_mat / mm） |
| 做法 | `LoopPass::kPartialUnrollFactor` 4→8 |
| 本地 | `many_mat_cal-1`、`01_mm*` qemu AC |
| 线上 | **94.47s → 94.12s AC**（Δ≈−0.35s）；many_mat/matmul 略降；`01_mm*`/`h-1-03` 略升 |
| 决策 | `IMPROVED` 保留 |

**为何有效**：降低内层分支密度；对已是内存瓶颈的 nest 收益有限。  
**评分**：`avg(best/my)` 略升（≈0.5425→0.5438）；`many_mat` best 已到 ~0.3s，ratio≈30× 仍是主战场。

---

## 无效 / 放弃尝试

**下列已回退或未提交；勿重复踩坑。**

### 1. LoopScaledRowInterchange（k-i-j → i-k-j）— 无效

| 项 | 内容 |
|----|------|
| 目标 | `01_mm1/2/3` |
| 做法 | 整 nest 重写为 i-k-j |
| 本地 | qemu 正确 |
| 线上 | **变慢**（≈103.4→105.1s） |
| 决策 | `SLOWER_HARD` |

**为何无效**：源码已是 k 外层，`B[k][*]` 可复用；改成 i 外层毁掉 B 行复用。  
**黑名单**：`01_mm*` 不要做 i-outer。

### 2. LoopInterchange scratch 寄存器分块（j-tile 外层）— 无效

| 项 | 内容 |
|----|------|
| 目标 | `many_mat_cal*` |
| 做法 | `j0` 步长 4 外层 + 寄存器累加 |
| 线上 | **大幅变慢**（≈103→151s） |
| 决策 | `SLOWER_HARD` |

**为何无效**：j-panel 外层使 `A[k][j]` 变列向，并放大 C 加载。  
**黑名单**：不要把 j-tile 提到 k 外面；保持 **k 外、j 内**。

### 3. InvariantDivisorSpecialize（per-iter d==3 版本化）— 无效

| 项 | 内容 |
|----|------|
| 目标 | `sl*`（f=3，热路径 sdiv） |
| 做法 | 每处 sdiv 拆 fast(`sdiv x,3`) / slow，**循环内每次分支** |
| 线上 | 功能 AC；**sl* TLE 300s** |
| 决策 | 丢弃 |

**为何无效**：最内层 per-iter 分支在板上极慢。  
**黑名单**：禁止 per-iter 版本化；若做必须 **整 nest 入口只分支一次**，且勿挂 O0。

### 4. ScaledRowToWeightedMatMul（W 权重 + GEMM）— 放弃未提交

| 项 | 内容 |
|----|------|
| 目标 | `01_mm*` |
| 做法 | 由 A 构造 W，再 `C += W*B` |
| 分析 | 数学正确；但在 **k 外层** 下与原 `C=C*A+B` **同构**，仅多 O(n²) W 构建与全局缓冲 |
| 现状 | IR 已有 gcc 式 j-unroll=4；再写一遍无预期提速 |
| 决策 | **未提交**；草稿已删 |

**后续**：`01_mm*` 需更强手段（更大 unroll / 指针归纳 / 后端），勿再做同构重写。

---

## 仍有效的背景事实

| 样例 | 事实 |
|------|------|
| `many_mat*` | MatMulDotProduct + interchange 已触发；j 已被 gcc unroll；下一步勿 j-panel-outer |
| `sl*` | `@y` 已删；`InvariantDivisorNestVersion` 已吃掉大部分 sdiv；仍约为 best 的 7–8× |
| `01_mm*` | 原生 k-i-j + unroll4；LSRI / 权重 GEMM 同构路已证伪或放弃 |
| 流水线 | 功能测 **O0**；性能测 **`-O1`** |
| 脚本 | `tools/opt_submit_loop.sh`、`tools/qemu_verify.sh`、`tools/educg_submit.py` |

---

## 决策对照（educg `--decide`）

| decision | 含义 |
|----------|------|
| `IMPROVED` / `SAME` | 保留 |
| `SLOWER_HARD` | `reset --hard` + 恢复远程 |
| `ERROR_SOFT` | `reset --soft` 留工作区 |

## 一句话黑名单

1. **不要**把 `01_mm*` 改成 i-outer。  
2. **不要**对 many_mat scratch 做 j-panel-outer 寄存器分块。  
3. **不要**在最内层对每个 sdiv 做 d==3 分支；整 nest 一次版本化且勿挂 O0。  
4. **不要**再做与原 k-i-j 同构的 `01_mm` 权重/GEMM 重写（无提速）。
