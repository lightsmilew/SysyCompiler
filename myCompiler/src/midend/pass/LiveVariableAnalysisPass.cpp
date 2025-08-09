#include "LiveVariableAnalysisPass.h"
using namespace std;
using namespace optimization;
// 活跃变量分析 Pass 实现
bool LiveVariableAnalysisPass::runOnFunction(Function *func)
{
    liveIn.clear();
    liveOut.clear();

    auto &bbs = func->getBasicBlocks();
    for (auto &bbPtr : bbs)
    {
        BasicBlock *bb = bbPtr.get();
        liveIn[bb] = {};
        liveOut[bb] = {};
    }

    // 正确收集每个基本块的def和use集合
    std::unordered_map<BasicBlock *, std::set<std::string>> defMap, useMap;
    for (auto &bbPtr : bbs)
    {
        BasicBlock *bb = bbPtr.get();
        std::set<std::string> def, use;
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            // 处理操作数
            if (auto *call = dynamic_cast<CallInst *>(inst))
            {
                for (size_t i = 1; i < call->getNumOperands(); ++i)
                {
                    Value *op = call->getOperandByIndex(i);
                    if (dynamic_cast<Constant *>(op))
                        continue;
                    std::string name = op->getName();
                    if (def.count(name) == 0 && use.count(name) == 0)
                        use.insert(name);
                }
            }
            else
            {
                for (auto *op : inst->getOperands())
                {
                    if (dynamic_cast<Constant *>(op))
                        continue;
                    std::string name = op->getName();
                    if (def.count(name) == 0 && use.count(name) == 0)
                        use.insert(name);
                }
            }
            // 处理定义
            if (inst->hasResult())
                def.insert(inst->getName());
        }
        defMap[bb] = def;
        useMap[bb] = use;
    }

    if (verbose)
    {
        debugInfo << func->getName() << " Def/Use Sets:\n";
        for (auto &bbPtr : bbs)
        {
            BasicBlock *bb = bbPtr.get();
            debugInfo << "BB: " << bb->getName() << "\n";
            debugInfo << "  Def: ";
            for (auto &v : defMap[bb])
                debugInfo << v << " ";
            debugInfo << "\n  Use: ";
            for (auto &v : useMap[bb])
                debugInfo << v << " ";
            debugInfo << "\n";
        }
    }

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (auto it = bbs.rbegin(); it != bbs.rend(); ++it)
        {
            BasicBlock *bb = it->get();
            std::set<std::string> oldIn = liveIn[bb];
            std::set<std::string> oldOut = liveOut[bb];

            // liveOut[bb] = 并集(succ的liveIn)
            liveOut[bb].clear();
            for (auto *succ : bb->getSuccessors())
                liveOut[bb].insert(liveIn[succ].begin(), liveIn[succ].end());

            // liveIn[bb] = use[bb] ∪ (liveOut[bb] - def[bb])
            liveIn[bb] = useMap[bb];
            for (auto &v : liveOut[bb])
            {
                if (defMap[bb].count(v) == 0)
                    liveIn[bb].insert(v);
            }

            if (liveIn[bb] != oldIn || liveOut[bb] != oldOut)
                changed = true;
        }
    }
    for (auto &bbPtr : bbs)
    {
        BasicBlock *bb = bbPtr.get();
        bb->setLiveIn(liveIn[bb]);
        bb->setLiveOut(liveOut[bb]);
    }
    if (verbose)
    {
        debugInfo << func->getName() << " Live Variable Analysis:\n";
        debugInfo << "  Total Basic Blocks: " << bbs.size() << "\n";
        for (auto &bbPtr : bbs)
        {
            BasicBlock *bb = bbPtr.get();
            debugInfo << "BB: " << bb->getName() << "\n";
            debugInfo << "  liveIn: ";
            for (auto &v : liveIn[bb])
                debugInfo << v << " ";
            debugInfo << "\n  liveOut: ";
            for (auto &v : liveOut[bb])
                debugInfo << v << " ";
            debugInfo << "\n";
        }
    }
    return false;
}