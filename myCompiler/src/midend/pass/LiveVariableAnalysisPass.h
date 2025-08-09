#pragma once
#include "Pass.h"
namespace optimization
{
     // 7. 活跃变量分析 Pass（Live Variable Analysis）
    // liveIn
    // 含义：在进入该基本块时，哪些变量是“活跃”的。
    // 解释：这些变量在该基本块及其后继中会被使用，但在本基本块内还没有被重新定义。
    // 用途：进入基本块前，这些变量的值必须是有效的（不能被覆盖或丢弃）。
    // liveOut
    // 含义：在离开该基本块时，哪些变量是“活跃”的。
    // 解释：这些变量在该基本块的后继基本块中会被使用。
    // 用途：离开基本块时，这些变量的值必须被保留，以便后继块使用。
    class LiveVariableAnalysisPass : public Pass
    {
    public:
        // 每个基本块的liveIn/liveOut集合
        std::unordered_map<BasicBlock *, std::set<std::string>> liveIn, liveOut;

        LiveVariableAnalysisPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LiveVariableAnalysis"; }
    };
}