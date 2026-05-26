#pragma once
#include "Pass.h"

namespace optimization
{
    /// 将 if (cond) acc = acc + val 转为 acc = acc + cond * val，消除分支。
    class CondGuardedAccumulatePass : public Pass
    {
    public:
        CondGuardedAccumulatePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "CondGuardedAccumulate"; }

    private:
        static Value *stripCopy(Value *v);
        static bool isSideEffectFree(BasicBlock *bb);
        bool tryConvert(Function *func, BasicBlock *bb);
    };
}
