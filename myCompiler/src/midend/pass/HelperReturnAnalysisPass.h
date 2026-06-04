#pragma once
#include "Pass.h"
class Module;
namespace optimization
{
    // 内联前：分析单基本块 helper 的返回值仿射表达式，消除 call 并重建最简 add/sub 形式
    class HelperReturnAnalysisPass : public Pass
    {
    public:
        HelperReturnAnalysisPass(bool verbose = false) : Pass(verbose) {}
        bool runOnModule(Module *module);
        bool runOnFunction(Function *func) override { (void)func; return false; }
        std::string getName() const override { return "HelperReturnAnalysis"; }
    };
}
