#pragma once
#include "Pass.h"
namespace optimization
{
    // 28.循环条件交换
    class LoopConditionSwapPass : public Pass
    {
    public:
        LoopConditionSwapPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopConditionSwap"; }
    };
}