#pragma once
#include "Pass.h"
namespace optimization
{
    // 5. 常量折叠 Pass（将常量表达式计算为常量值）函数内联时会产生常量二元表达式
    class ConstantFoldingPass : public Pass
    {
    public:
        ConstantFoldingPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "ConstantFoldingPass"; }
    };
    // 9.链式加法转乘法
    class AddChainReductionPass : public Pass
    {
    public:
        AddChainReductionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "AddChainReduction"; }
    };
}