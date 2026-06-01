#pragma once
#include "../irbuild/IRDataStructure.h"
#include "LengauerTarjan.h"
namespace optimization
{

    // 基本块支配关系分析
    class ControlFlowAnalysis
    {
    public:
        // 分析函数的支配关系(Lengauer-Tarjan算法)
        static unordered_map<BasicBlock *, BasicBlock *> analyze(Function *func);
        static bool dominates(unordered_map<BasicBlock *, BasicBlock *> idom, BasicBlock *dom, BasicBlock *node);
        // 查找循环
        static void dfs(BasicBlock *bb, std::unordered_map<BasicBlock *, int> &dfn,
                        vector<BasicBlock *> &order, int &idx,
                        std::unordered_map<BasicBlock *, int> &inStack,
                        std::vector<std::pair<BasicBlock *, BasicBlock *>> &backedges);
        static vector<Loop> findLoops(Function *func);
        // 在 findLoops 基础上识别 GCC 形态（preheader/body/latch/exit），供后端使用
        static vector<Loop> findGccLoops(Function *func);
        static bool hasStoreOnPath(BasicBlock *startBB,BasicBlock *endBB,Value *arr);
        static bool hasStoreOnPath(BasicBlock *startBB, BasicBlock *endBB, Value *arr,Value *inst1,Value *inst2);
        static bool hasPhiInputOnPath(BasicBlock *startBB, BasicBlock *endBB, Value *phi,Value *inst1,Value *inst2);
    };
}