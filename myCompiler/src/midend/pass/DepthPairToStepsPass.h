#pragma once
#include "Pass.h"

namespace optimization
{
    /// 将「第二参数为深度累加器」的二元递归改为一元 steps：
    ///   f(n,d) 且递归为 f(g(n), d+c)、基例 return d / return const
    /// → steps(n) 返回步数或失败哨兵；原 f 变为对 steps 的薄包装。
    /// 便于后续对一元 steps 做稠密表记忆化。
    class DepthPairToStepsPass : public Pass
    {
    public:
        DepthPairToStepsPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "DepthPairToSteps"; }
    };
}
