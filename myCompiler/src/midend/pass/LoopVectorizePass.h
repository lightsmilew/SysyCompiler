#pragma once
#include "Pass.h"
#include "MatrixStructureAnalysis.h"
#include <map>
#include <memory>
#include <set>
#include <string>
#include <tuple>
#include <vector>

namespace optimization
{
    // ===== 通用逐元素表达式循环的模式结构 =====
    enum class ElemKind { CONST, INVARIANT, LOAD, ADD, SUB, MUL, SLL, SRL, SRA };

    struct ElemExpr
    {
        ElemKind kind = ElemKind::CONST;
        int64_t cval = 0;             // CONST
        Value *scalar = nullptr;      // INVARIANT：循环不变标量
        Value *base = nullptr;        // LOAD：数组基址
        Value *row = nullptr;         // LOAD：行索引（1D 为 nullptr）
        Value *offset = nullptr;      // LOAD：列偏移（jIV 之外的不变部分）
        Instruction *ir = nullptr;    // 源 IR 指令（LOAD/Binary），用于覆盖检查
        std::unique_ptr<ElemExpr> lhs, rhs;
    };

    struct ElemStore
    {
        Value *base = nullptr;
        Value *row = nullptr;
        Value *offset = nullptr;
        std::unique_ptr<ElemExpr> expr;
    };

    struct ElemLoopPattern
    {
        BasicBlock *entry = nullptr;
        BasicBlock *jHeader = nullptr;
        BasicBlock *jBody = nullptr;
        BasicBlock *jExit = nullptr;
        Value *jIV = nullptr;
        Value *bound = nullptr;
        std::vector<ElemStore> stores;
    };

    // ===== 纯标量链循环（x += d 归纳 + f(x) 计算 + sum 归约，无数组访存）的模式结构 =====
    struct ScalarChainPattern
    {
        BasicBlock *entry = nullptr;
        BasicBlock *header = nullptr;
        BasicBlock *latch = nullptr;
        BasicBlock *exitBlock = nullptr;
        Loop loop;
        Value *x = nullptr;
        Value *xInit = nullptr;
        Value *step = nullptr;
        Value *bound = nullptr;
        Value *sum = nullptr;
        Value *sumInit = nullptr;
        Value *fx = nullptr;
        Value *mod = nullptr;
        std::vector<BasicBlock *> bodyBlocks;
        // max/min 结构：phi → {左操作数, 右操作数, 是否为 max}；min 对应 VecMin
        std::map<Instruction *, std::tuple<Value *, Value *, bool>> maxMinPhis;
        std::set<Instruction *> laneInsts;
        std::set<Instruction *> sumChain;
    };

    // 将矩阵循环的规整模式（scaled-row 更新、通用逐元素表达式、纯标量链）向量化：
    // 生成 strip-mining 向量循环（vecsetvl / vecload / vecsplat / vecbinary / vecstore）。
    // 仅在 CompilerConfig::enableRVV 为 true 时生效。
    class LoopVectorizePass : public Pass
    {
    public:
        LoopVectorizePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopVectorize"; }

    private:
        // C[i][j] = C[i][j] * A[i][k] + B[k][j] 的 j 内层循环 → 向量 strip-mining 循环
        bool vectorizeScaledRowUpdate(Function *func, const ScaledRowUpdateNest &nest);

        // 通用逐元素表达式循环（含零填充、常量/循环不变填充、逐元素表达式、拷贝）
        bool findElementwiseLoop(const Loop &jLoop, ElemLoopPattern &pat);
        bool vectorizeElementwiseLoop(Function *func, const ElemLoopPattern &pat);

        // 纯标量链循环（x += d 归纳 + f(x) 计算 + sum 归约）
        bool findScalarChainLoop(const Loop &loop, ScalarChainPattern &pat);
        bool vectorizeScalarChainLoop(Function *func, const Loop &loop);

        // 判断循环是否可被 LoopUnrollingPass 完全展开：归纳变量的 init/step/bound
        // 均为常量，且静态 trip count ∈ (0, kFullUnrollMaxTripCount]。可完全展开的
        // 小循环优先交给循环展开（inline 化）而非向量化，避免引入 vsetvli 开销。
        bool isFullyUnrollableLoop(const Loop &loop, Value *jIV, Value *bound);

        // max/min 优化：识别循环内 icmp(slt/sgt) + br + phi 构成的 max/min 结构，
        // 记录到 pat.maxMinPhis（供 translateToVec 翻译为 VecMax/VecMin）
        void collectMaxMinPhis(const Loop &loop, Value *x,
                               const std::set<Instruction *> &laneInsts,
                               ScalarChainPattern &pat);

        // 将标量计算链递归翻译为向量指令
        Value *translateToVec(Value *v, BasicBlock *body, Value *vl, Value *xVec,
                              ScalarChainPattern &pat, std::map<Value *, Value *> &cache);
    };
}
