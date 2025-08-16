#pragma once
#include "Pass.h"
namespace optimization
{
    // 24.指令合并Pass
    class InstructionCombinePass : public Pass
    {
    public:
        InstructionCombinePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        string getName() const override { return "InstructionCombine"; }
    };
}