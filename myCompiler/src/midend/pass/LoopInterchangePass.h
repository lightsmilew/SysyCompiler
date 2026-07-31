#pragma once
#include "MatrixStructureAnalysis.h"
#include "Pass.h"

namespace optimization
{
    /// 将 i-j-k 点积循环交换为 i-k-j。
    /// out 与读写矩阵别名时用行缓冲；否则直接累加到 out[i][j]。
    class LoopInterchangePass : public Pass
    {
    public:
        LoopInterchangePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopInterchange"; }

    private:
        static AllocaInst *getOrCreateAccBuffer(Function *func);
        bool applyInterchange(Function *func, MatMulDotProductNest &pat);
        bool applyWithScratch(Function *func, MatMulDotProductNest &pat, Value *outBase);
        bool applyDirectAccumulate(Function *func, MatMulDotProductNest &pat, Value *outBase);
    };
}
