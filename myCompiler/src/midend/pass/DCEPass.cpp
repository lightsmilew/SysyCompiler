#include "DCEPass.h"
#include <stack>
using namespace std;
using namespace optimization;
// ========== 死代码消除 ==========
bool DeadCodeEliminationPass::runOnFunction(Function *func)
{
    // 先删除不可达基本块（前驱为空且不是入口块）
    auto &bbs = func->getBasicBlocks();

    if (!bbs.empty())
    {
        BasicBlock *entry = bbs[0].get();
        // 先收集所有将要删除的不可达基本块
        std::vector<BasicBlock *> toDelete;
        for (auto &bbPtr : bbs)
        {
            BasicBlock *bb = bbPtr.get();
            if (bb != entry && bb->getPredecessors().empty())
            {
                toDelete.push_back(bb);
                debugInfo<<"delete block:"<<bb->getName()<<std::endl;
            }
        }
        // 对所有 phi 指令，移除对将要删除块的引用
        for (auto &bbPtr : bbs)
        {
            BasicBlock *bb = bbPtr.get();
            auto &insts = bb->getInstructions();
            for (auto &instPtr : insts)
            {
                if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                {
                    for (BasicBlock *delBB : toDelete)
                    {
                        unsigned index = phi->getIndexByBasicBlock(delBB);
                        if (index == -1)
                            continue; // 如果没有这个前驱块，跳过
                        // 删除对应的前驱块和值
                        phi->removeIncoming(index);
                    }
                    //  如果只剩一个incoming，直接替换
                    if (phi->getNumIncomingValues() == 1)
                    {
                        Value *incomingValue = phi->getIncomingValue(0);
                        phi->replaceAllUsesWith(incomingValue);
                    }
                }
            }
        }
        for (auto it = bbs.begin(); it != bbs.end();)
        {
            BasicBlock *bb = it->get();
            if (bb != entry && bb->getPredecessors().empty())
            {
                // 从后继中删除自身
                for (auto *succ : bb->getSuccessors())
                {
                    succ->removePredecessor(bb);
                }
                // 这里不能直接删除，把它放到needToDelete中,否则内存空间释放了
                needToDelete.push_back(it->release());
                // 从基本块列表中删除
                it = bbs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::unordered_set<Instruction *> liveInsts;
    markLiveInstructions(func, liveInsts);

    bool changed = false;
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            if (liveInsts.count(inst) == 0)
            {
                inst->removeThisFromOperands(); // 从操作数中移除自己
                needToDelete.push_back(it->release());
                it = insts.erase(it);
                changed = true;
                if (verbose)
                {
                    debugInfo << "Dead instruction: " << inst->toString() << "\n";
                }
            }
            else
            {
                ++it;
            }
        }
    }
    return changed;
}

// 标记所有关键指令和其依赖为活跃
void DeadCodeEliminationPass::markLiveInstructions(Function *func, std::unordered_set<Instruction *> &liveInsts)
{
    std::stack<Instruction *> worklist;
    // 1. 标记终结指令、store、call等有副作用的指令为活跃
    for (auto &bb : func->getBasicBlocks())
    {
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            // if(!inst)continue;
            //  如果是关键指令
            if (isInstructionCritical(inst))
            {
                liveInsts.insert(inst);
                worklist.push(inst);
            }
        }
    }
    // 2. 反向传播依赖
    while (!worklist.empty())
    {
        Instruction *inst = worklist.top();
        worklist.pop();
        for (auto *op : inst->getOperands())
        {
            if (auto *def = dynamic_cast<Instruction *>(op))
            {
                if (liveInsts.insert(def).second)
                {
                    worklist.push(def);
                }
            }
        }
    }
}

// 判断指令是否为关键指令
bool DeadCodeEliminationPass::isInstructionCritical(Instruction *inst)
{
    return inst->mayHaveSideEffects() || inst->getOpcode() == Opcode::Copy;
}
bool RemoveRedundantStorePass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        for (size_t i = 0; i < insts.size(); ++i)
        {
            Instruction *inst = insts[i].get();
            // 判断是否为store指令
            if (inst && inst->getOpcode() == Opcode::Store)
            {
                auto now_store = dynamic_cast<StoreInst *>(inst);
                Value *addr = now_store->getPointer();
                Value *val = now_store->getValueToStore();
                // 检查最近一次对该地址的load
                Instruction *lastLoad = nullptr;
                for (int j = (int)i - 1; j >= 0; --j)
                {
                    Instruction *prev = insts[j].get();
                    if (prev == nullptr)
                        continue;
                    if (auto loadInst = dynamic_cast<LoadInst *>(prev))
                    {
                        if (loadInst->getPointer() == addr)
                        {
                            lastLoad = prev;
                            break;
                        }
                    }
                    // 如果遇到对该地址的store则停止
                    else if (auto storeInst = dynamic_cast<StoreInst *>(prev))
                    {
                        if (storeInst->getPointer() == addr)
                        {
                            break;
                        }
                    }
                }
                // 如果最近一次load的值和store的值相同，则该store无用
                if (lastLoad && val == lastLoad)
                {
                    if (verbose)
                    {
                        debugInfo << "Removing redundant store: " << inst->toString() << "\n";
                    }
                    inst->removeThisFromOperands();
                    needToDelete.push_back(insts[i].release());
                    insts.erase(insts.begin() + i);
                    --i;
                    changed = true;
                }
            }
        }
    }
    return changed;
}