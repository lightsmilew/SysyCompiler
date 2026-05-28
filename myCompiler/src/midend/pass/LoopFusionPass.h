#pragma once
#include "Pass.h"

namespace optimization
{
    // 融合相邻、同界的二重 while 循环（将两个内层 j 循环合并为一个）
    class LoopFusionPass : public Pass
    {
    public:
        LoopFusionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopFusion"; }

    private:
        bool tryFuseAdjacentNests(Function *func);
    };
}
