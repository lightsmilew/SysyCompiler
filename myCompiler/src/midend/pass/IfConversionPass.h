#pragma once
#include "Pass.h"
namespace optimization
{
    // 22.if转换Pass
    class IfConversionPass : public Pass
    {
    public:
        IfConversionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        string getName() const override { return "IfConversion"; }
    private:
        static bool isSideEffectFree(BasicBlock *bb);
    };
}