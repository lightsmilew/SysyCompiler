#pragma once
#include "MatrixStructureAnalysis.h"

namespace optimization
{
    /// 消除可证明冗余的拷贝链：在 init → 拷贝 → 纯归约场景下，
    /// 将使用改写到起源缓冲并删除中间拷贝。
    class CopyChainEliminationPass : public Pass
    {
    public:
        CopyChainEliminationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "CopyChainElimination"; }

    private:
        bool applyRewrite(Function *func, const InPlaceCopyOriginChain &chain);
    };
}
