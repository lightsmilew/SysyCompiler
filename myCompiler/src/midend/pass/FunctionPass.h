
#pragma once
#include "Pass.h"
namespace optimization
{
    class FunctionInliningPass : public Pass
    {
    public:
        FunctionInliningPass(bool verbose = false, bool inlineMainCalleeLayer = false)
            : Pass(verbose), inlineMainCalleeLayer(inlineMainCalleeLayer) {}
        bool runOnFunction(Function *func) override;
        string getsuffix(string funcname)
        {
            int count = inlineCountMap[funcname]++;
            return "_inl_" + funcname + "_" + to_string(count);
        }
        std::string getName() const override { return "FunctionInlining"; }

    private:
        bool inlineMainCalleeLayer; // true: 仅 main 内联一层（TRE 后的原递归函数，替换全部 call）
        unordered_map<string, int> inlineCountMap;
        bool shouldInline(Function *callee, Function *caller);
        int inlineAt(CallInst *call, Function *caller, BasicBlock *bb, size_t insertPos);
        void verifyCFG(Function *func);
    };

    class TailRecursionEliminationPass : public Pass
    {
    public:
        TailRecursionEliminationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "TailRecursionElimination"; }
    };
}
