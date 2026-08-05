#pragma once
#include "Pass.h"

namespace optimization
{
    /// 将可归约为移位的比较链 / 条件幂次乘除折叠为移位指令。
    class CompareChainFoldPass : public Pass
    {
    public:
        CompareChainFoldPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "CompareChainFold"; }

    private:
        enum class FoldKind
        {
            MulPow2,  // x * 2^n
            SDivPow2  // x / 2^n (truncate toward zero)
        };

        bool matchCompareChain(Function *func, FoldKind &kind);
        void rewriteFunction(Function *func, FoldKind kind);
    };
}
