#pragma once
#include "Pass.h"

namespace optimization
{
    /// 对含循环不变 sdiv x,d 的外层 nest 做一次版本化：
    ///   if (d==3) 克隆 nest（sdiv 改为 sdiv x,3，供 SRFixed magic）
    ///   else 原 nest
    class InvariantDivisorNestVersionPass : public Pass
    {
    public:
        InvariantDivisorNestVersionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "InvariantDivisorNestVersion"; }
    };
}
