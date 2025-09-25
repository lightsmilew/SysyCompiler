#pragma once
#include "Pass.h"
namespace optimization
{
    // 27.循环融合
    class FusionPass : public Pass
    {
    public:
        FusionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "Fusion"; }
    };
}