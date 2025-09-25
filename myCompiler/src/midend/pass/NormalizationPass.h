#pragma once
#include "Pass.h"
namespace optimization
{
    //26. 归一化 Pass
    class NormalizationPass : public Pass
    {
    public:
        NormalizationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "Normalization"; }
    };
}