#pragma once
#include "Pass.h"
namespace optimization
{
    // 12.CFG优化
    class CFGSimplificationPass : public Pass
    {
    public:
        CFGSimplificationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "CFGSimplification"; }
    };

    // 18.基本块合并
    class BasicBlockMergePass : public Pass
    {
    public:
        BasicBlockMergePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "BasicBlockMerge"; }
    };

    // 20.基本块重排
    class BasicBlockReorderPass : public Pass
    {
    public:
        BasicBlockReorderPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "BasicBlockReorder"; }
    };
}