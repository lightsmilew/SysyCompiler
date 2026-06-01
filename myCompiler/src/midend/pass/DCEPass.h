#pragma once
#include "Pass.h"
#include <unordered_set>
namespace optimization
{
    // 1.死代码消除Pass
    class DeadCodeEliminationPass : public Pass
    {
    public:
        DeadCodeEliminationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        string getName() const override { return "DeadCodeElimination"; }

    private:
        void markLiveInstructions(Function *func, std::unordered_set<Instruction *> &liveInsts);
        bool isInstructionCritical(Instruction *inst);
    };
    // 23.无用store删除
    class RemoveRedundantStorePass : public Pass
    {
        public:
            RemoveRedundantStorePass(bool verbose = false) : Pass(verbose) {}
            bool runOnFunction(Function *func) override;
            string getName() const override { return "RemoveRedundantStore"; }
    };
    // 无用全局变量与无用函数删除
    class RemoveUnusedGlobalAndFunctionPass : public Pass
    {
    public:
        RemoveUnusedGlobalAndFunctionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        string getName() const override { return "RemoveUnusedGlobalAndFunction"; }

    private:
        bool moduleProcessed = false;
        bool removeUnusedGlobals(Module *module);
        bool removeUnusedFunctions(Module *module);
    };
}