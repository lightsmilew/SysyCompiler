#pragma once
#include "Pass.h"
namespace optimization
{
    // 6. phi 消除 Pass（SSA转回普通IR，消除phi指令）
    class PhiEliminationPass : public Pass
    {
    public:
        PhiEliminationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "PhiElimination"; }
    };
}