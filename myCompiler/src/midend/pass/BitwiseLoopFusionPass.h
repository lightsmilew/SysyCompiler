#pragma once
#include "Pass.h"

namespace optimization
{

    /// 识别位运算模拟循环并直接降成原始位运算。
    class BitwiseLoopFusionPass : public Pass
    {
    public:
        BitwiseLoopFusionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "BitwiseLoopFusion"; }
    };

} // namespace optimization
