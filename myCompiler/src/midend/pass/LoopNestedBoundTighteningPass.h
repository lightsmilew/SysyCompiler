#pragma once
#include "Pass.h"

namespace optimization
{
    // 嵌套循环中，当内层上界可表示为 min(常量上界, 外层归纳变量+1) 时，
    // 收紧内层 trip bound 并去掉循环体内的冗余守卫分支。
    class LoopNestedBoundTighteningPass : public Pass
    {
    public:
        LoopNestedBoundTighteningPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopNestedBoundTightening"; }

    private:
        bool tryTightenNestedBound(Function *func, const Loop &innerLoop, const Loop &outerLoop);
    };
}
