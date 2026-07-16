#pragma once
#include "MatrixStructureAnalysis.h"

namespace optimization
{
    /// 将「三角原地拷贝链 + 前缀归约」改写为 O(len×T) 的起源追踪。
    class TriangularCopyOriginPass : public Pass
    {
    public:
        TriangularCopyOriginPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "TriangularCopyOrigin"; }

    private:
        bool applyRewrite(Function *func, const TriangularInPlaceCopyChain &chain);
    };
}
