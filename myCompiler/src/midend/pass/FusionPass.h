#pragma once
#include "Pass.h"
namespace optimization
{
    // 27.结构化模式规约：识别位运算模拟循环并直接降成原始位运算
    class FusionPass : public Pass
    {
    public:
        FusionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "BitwiseLoopFusion"; }
    };
}