#pragma once
#include "Pass.h"
namespace optimization
{
    // 8.展开getelementptr
    class GEPExpansionPass : public Pass
    {
    public:
        GEPExpansionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "GEPExpansion"; }
    };
    // 11.替换无用的gep指令为bitcast
    class GEPToBitCastPass : public Pass
    {
    public:
        GEPToBitCastPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "GEPToBitCast"; }
    };

}