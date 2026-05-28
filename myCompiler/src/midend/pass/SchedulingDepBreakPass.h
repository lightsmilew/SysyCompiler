#pragma once
#include "Pass.h"

namespace optimization
{
    /// 将 dependent 递推 d'=d+(a+b) 中“用新 a”的写法改为 d'=d+a_old+b_old，便于 ILP。
    /// 改写指令设置 Instruction::NoCSE，避免后续 CSE 合并掉刻意保留的计算链。
    class SchedulingDepBreakPass : public Pass
    {
    public:
        SchedulingDepBreakPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "SchedulingDepBreak"; }

    private:
        bool tryBreakLoop(Function *func, const Loop &loop);
    };
}
