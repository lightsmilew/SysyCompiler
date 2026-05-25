#pragma once
#include "Pass.h"

namespace optimization
{
    // 消除 if (outer < inner) { inner++; continue; } 模式：
    // 保留 inner 初值 0，将内层上界收紧为 min(rowsize, outer+1)，并删除 continue 分支。
    class LoopSkipContinueElimPass : public Pass
    {
    public:
        LoopSkipContinueElimPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopSkipContinueElim"; }

    private:
        bool tryEliminate(Function *func, const Loop &innerLoop, const Loop &outerLoop);
    };
}
