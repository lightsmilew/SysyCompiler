#pragma once
#include "MatrixStructureAnalysis.h"
#include "Pass.h"

namespace optimization
{
    /// 将 i-j-k 点积循环交换为 i-k-j，改善矩阵乘等访存模式。
    class LoopInterchangePass : public Pass
    {
    public:
        LoopInterchangePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopInterchange"; }

    private:
        static AllocaInst *getOrCreateAccBuffer(Function *func);
        bool applyInterchange(Function *func, MatMulDotProductNest &pat);
    };
}
