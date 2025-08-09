
#pragma once
#include "Pass.h"
namespace optimization
{
    // 4. 函数内联 Pass（将函数调用替换为函数体）
    class FunctionInliningPass : public Pass
    {
    public:
        FunctionInliningPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        string getsuffix(string funcname)
        {
            int count = inlineCountMap[funcname]++;
            return "_inl_" + funcname + "_" + to_string(count);
        }
        std::string getName() const override { return "FunctionInlining"; }

    private:
        unordered_map<string, int> inlineCountMap; // 记录每个函数的内联次数
        bool shouldInline(Function *callee);
        int inlineAt(CallInst *call, Function *caller, BasicBlock *bb, size_t insertPos);
        // debug
        void verifyCFG(Function *func);
    };

    // 17.尾递归消除
    class TailRecursionEliminationPass : public Pass
    {
    public:
        TailRecursionEliminationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "TailRecursionElimination"; }
    };
}