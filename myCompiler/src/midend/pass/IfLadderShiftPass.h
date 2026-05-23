#pragma once
#include "Pass.h"

namespace optimization
{
    // 将 rotrN/rotlN 类 if 链（n==1..8 时 x/2^n 或 x*2^n，否则 x）折叠为变量移位
    class IfLadderShiftPass : public Pass
    {
    public:
        IfLadderShiftPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "IfLadderShift"; }

    private:
        enum class LadderKind
        {
            MulPow2,  // x * 2^n
            SDivPow2  // x / 2^n (truncate toward zero)
        };

        bool matchIfLadderShiftFunction(Function *func, LadderKind &kind);
        void rewriteFunction(Function *func, LadderKind kind);
    };
}
