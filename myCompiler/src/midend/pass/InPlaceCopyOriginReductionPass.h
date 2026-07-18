#pragma once
#include "MatrixStructureAnalysis.h"

namespace optimization
{
    /// 变换：纯 init + 原地拷贝链 + 前缀纯归约 → 起源追踪消去拷贝。
    class InPlaceCopyOriginReductionPass : public Pass
    {
    public:
        InPlaceCopyOriginReductionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "InPlaceCopyOriginReduction"; }

    private:
        bool applyRewrite(Function *func, const InPlaceCopyOriginChain &chain);
    };
}
