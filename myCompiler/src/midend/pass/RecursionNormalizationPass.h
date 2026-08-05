#pragma once
#include "Pass.h"

namespace optimization
{
    /// 递归形态规范化：将可证明的「主参数 + 深度累加器」二元递归
    /// 规范化为一元步数函数，便于后续记忆化等通用优化。
    class RecursionNormalizationPass : public Pass
    {
    public:
        RecursionNormalizationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "RecursionNormalization"; }
    };
}
