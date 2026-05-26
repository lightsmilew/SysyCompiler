#pragma once
#include "Pass.h"

namespace optimization
{
    // 循环体仅含 if（then 有副作用、else 仅递增/空）时，根据 if 条件收紧 while 上界或提高初值，并去掉循环内分支。
    class LoopIfGuardHoistPass : public Pass
    {
    public:
        LoopIfGuardHoistPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopIfGuardHoist"; }

    private:
        bool tryHoistIfGuard(Function *func, const Loop &loop);
    };
}
