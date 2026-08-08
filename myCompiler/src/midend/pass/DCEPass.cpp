#include "DCEPass.h"
#include <algorithm>
#include <stack>
#include <unordered_set>
#include <vector>
using namespace std;
using namespace optimization;

namespace
{
StoreInst *findPendingStore(const vector<StoreInst *> &pending, Value *addr)
{
    for (StoreInst *store : pending)
    {
        if (isSameAddr(store->getPointer(), addr))
            return store;
    }
    return nullptr;
}

void clearPendingForAddr(vector<StoreInst *> &pending, Value *addr)
{
    pending.erase(remove_if(pending.begin(), pending.end(),
                            [&](StoreInst *store) {
                                return isSameAddr(store->getPointer(), addr);
                            }),
                  pending.end());
}

bool removeShadowedStoresInBlock(BasicBlock *bb, bool verbose, stringstream &debugInfo,
                                 vector<Value *> &needToDelete)
{
    auto &insts = bb->getInstructions();
    vector<StoreInst *> pendingStores;
    unordered_set<StoreInst *> shadowedStores;

    for (auto &instPtr : insts)
    {
        Instruction *inst = instPtr.get();
        // Any call may read memory; do not treat stores before it as shadowed by later stores.
        if (dynamic_cast<CallInst *>(inst))
        {
            pendingStores.clear();
            continue;
        }
        // 任何内存读（标量/向量/strided load）都会读取该地址：
        // 读之后，此前的标量 store 不能被后续 store 影子删除
        if (inst->isMemoryLoad())
        {
            clearPendingForAddr(pendingStores, inst->getPointerOperand());
            continue;
        }
        if (auto *store = dynamic_cast<StoreInst *>(inst))
        {
            Value *addr = store->getPointer();
            if (StoreInst *prev = findPendingStore(pendingStores, addr))
                shadowedStores.insert(prev);
            clearPendingForAddr(pendingStores, addr);
            pendingStores.push_back(store);
        }
        // 向量 store（含 strided）是内存写：会覆盖同地址的标量 store，但自身不可被删除，故不加入 pending
        if (inst->isMemoryStore() && !dynamic_cast<StoreInst *>(inst))
        {
            Value *addr = inst->getPointerOperand();
            if (StoreInst *prev = findPendingStore(pendingStores, addr))
                shadowedStores.insert(prev);
            clearPendingForAddr(pendingStores, addr);
        }
    }

    if (shadowedStores.empty())
        return false;

    bool changed = false;
    for (size_t i = 0; i < insts.size();)
    {
        StoreInst *store = dynamic_cast<StoreInst *>(insts[i].get());
        if (store && shadowedStores.count(store))
        {
            if (verbose)
            {
                debugInfo << "Removing shadowed store: " << store->toString() << "\n";
            }
            store->removeThisFromOperands();
            needToDelete.push_back(insts[i].release());
            insts.erase(insts.begin() + static_cast<long>(i));
            changed = true;
            continue;
        }
        ++i;
    }
    return changed;
}
bool replaceBranchTarget(BasicBlock *from, BasicBlock *oldSucc, BasicBlock *newSucc)
{
    if (!from || !oldSucc || !newSucc || oldSucc == newSucc)
        return false;
    auto *br = dynamic_cast<BranchInst *>(from->getTerminator());
    if (!br)
        return false;

    bool changed = false;
    if (br->isConditional())
    {
        if (br->getTrueBlock() == oldSucc)
        {
            br->setTrueBlock(newSucc);
            changed = true;
        }
        if (br->getFalseBlock() == oldSucc)
        {
            br->setFalseBlock(newSucc);
            changed = true;
        }
    }
    else if (br->getTrueBlock() == oldSucc)
    {
        br->setTrueBlock(newSucc);
        changed = true;
    }

    if (!changed)
        return false;

    from->removeSuccessor(oldSucc);
    oldSucc->removePredecessor(from);
    from->addSuccessor(newSucc);
    newSucc->addPredecessor(from);
    return true;
}

BasicBlock *forwardThroughDeleted(BasicBlock *bb, const std::unordered_set<BasicBlock *> &deleted)
{
    std::unordered_set<BasicBlock *> visited;
    while (bb && deleted.count(bb))
    {
        if (!visited.insert(bb).second)
            return nullptr;
        auto *br = dynamic_cast<BranchInst *>(bb->getTerminator());
        if (!br || br->isConditional())
            return nullptr;
        bb = br->getTrueBlock();
    }
    return bb;
}

void retargetBranchesAwayFromDeleted(Function *func, const std::vector<BasicBlock *> &deleted)
{
    if (!func || deleted.empty())
        return;

    std::unordered_set<BasicBlock *> deletedSet(deleted.begin(), deleted.end());
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        if (!bb || deletedSet.count(bb))
            continue;
        auto *br = dynamic_cast<BranchInst *>(bb->getTerminator());
        if (!br)
            continue;

        auto fixTarget = [&](BasicBlock *target) {
            if (!target || !deletedSet.count(target))
                return;
            BasicBlock *fwd = forwardThroughDeleted(target, deletedSet);
            if (fwd && fwd != target)
                (void)replaceBranchTarget(bb, target, fwd);
        };

        if (br->isConditional())
        {
            fixTarget(br->getTrueBlock());
            fixTarget(br->getFalseBlock());
        }
        else
        {
            fixTarget(br->getTrueBlock());
        }
    }
}
} // namespace

// ========== 死代码消除 ==========
bool DeadCodeEliminationPass::runOnFunction(Function *func)
{
    //std::cout << func->toString() << "\n";
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
                // for(auto *succ:bb->getSuccessors())
                // {
                //     succ->removePredecessor(bb);
                //     bb->removeSuccessor(succ);
                // }
                toDelete.push_back(bb);
                bb->removeSelfBasicBlock();
                debugInfo<<"delete block:"<<bb->getName()<<std::endl;
            }
            else if(bb!=entry&&bb->getPredecessors().size()==1&&bb->getPredecessors()[0]==bb)
            {
                // 自己是自己的前驱，死循环块
                toDelete.push_back(bb);
                // bb->removePredecessor(bb);
                // bb->removeSuccessor(bb);
                // for(auto *succ:bb->getSuccessors())
                // {
                //     succ->removePredecessor(bb);
                //     bb->removeSuccessor(succ);
                // }
                bb->removeSelfBasicBlock();
                debugInfo<<"delete self-loop block:"<<bb->getName()<<std::endl;
            }
        }
        vector<Loop>loops=ControlFlowAnalysis::findLoops(func);
        for(auto loop:loops)
        {
            if(loop.header->getPredecessors().empty())
            {
                // 删除循环块的cfg连接
                loop.breakCFG();
                for(auto *bb:loop.blocks)
                {
                    toDelete.push_back(bb);
                    debugInfo<<"delete loop block:"<<bb->getName()<<std::endl;
                }
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
                    if (phi->getNumIncomingValues() == 0)
                    {
                        phi->removeThisFromOperands();
                    }
                    else if (phi->getNumIncomingValues() == 1 &&
                             phi->getNumOperands() == phi->getNumIncomingValues())
                    {
                        Value *incomingValue = phi->getIncomingValue(0);
                        phi->replaceAllUsesWith(incomingValue);
                    }
                }
            }
        }
        retargetBranchesAwayFromDeleted(func, toDelete);
        for (auto it = bbs.begin(); it != bbs.end();)
        {
            BasicBlock *bb = it->get();
            if (bb != entry && bb->getPredecessors().empty())
            {
                // 从后继中删除自身
                // for (auto *succ : bb->getSuccessors())
                // {
                //     succ->removePredecessor(bb);
                //     bb->removeSuccessor(succ);
                // }
                bb->removeSelfBasicBlock();
                // 这里不能直接删除，把它放到needToDelete中,否则内存空间释放了
                needToDelete.push_back(it->release());
                // 从基本块列表中删除
                it = bbs.erase(it);
            }
            else if(bb->getPredecessors().size()==1&&bb->getPredecessors()[0]==bb)
            {
                // 自己是自己的前驱，死循环块
                // for (auto *succ : bb->getSuccessors())
                // {
                //     succ->removePredecessor(bb);
                //     bb->removeSuccessor(succ);
                // }
                bb->removeSelfBasicBlock();
                needToDelete.push_back(it->release());
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
        changed |= removeShadowedStoresInBlock(bb.get(), verbose, debugInfo, needToDelete);

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
                    if (dynamic_cast<CallInst *>(prev))
                    {
                        break;
                    }
                    if (auto storeInst = dynamic_cast<StoreInst *>(prev))
                    {
                        // 仅同地址 store 会截断；不同地址 store 不影响当前地址的 load-store 冗余判断
                        if (isSameAddr(storeInst->getPointer(), addr))
                        {
                            break;
                        }
                        continue;
                    }
                    if (prev->mayHaveSideEffects() && prev->getOpcode() != Opcode::Load)
                    {
                        break;
                    }
                    if (auto loadInst = dynamic_cast<LoadInst *>(prev))
                    {
                        if (isSameAddr(loadInst->getPointer(), addr))
                        {
                            lastLoad = prev;
                            break;
                        }
                    }
                }
                // 如果最近一次load的值和store的值相同，则该store无用
                if (lastLoad && val == static_cast<Value *>(lastLoad))
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

namespace
{
void collectReachableFunctions(Function *root, std::unordered_set<Function *> &reachable)
{
    if (!root || root->isLibraryFunction() || root->isDeletedFunction())
        return;

    std::stack<Function *> worklist;
    reachable.insert(root);
    worklist.push(root);

    while (!worklist.empty())
    {
        Function *func = worklist.top();
        worklist.pop();
        for (auto &bbPtr : func->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *call = dynamic_cast<CallInst *>(instPtr.get());
                if (!call)
                    continue;
                Function *callee = call->getCalledFunction();
                if (!callee || callee->isLibraryFunction() || callee->isDeletedFunction())
                    continue;
                if (reachable.insert(callee).second)
                    worklist.push(callee);
            }
        }
    }
}
} // namespace

bool RemoveUnusedGlobalAndFunctionPass::removeUnusedGlobals(Module *module)
{
    bool changed = false;
    for (auto &gvPtr : module->GlobalVariables)
    {
        GlobalVariable *gv = gvPtr.get();
        if (gv->isEliminated || !gv->getUsers().empty())
            continue;
        gv->isEliminated = true;
        changed = true;
        if (verbose)
        {
            debugInfo << "RemoveUnusedGlobalAndFunctionPass: Eliminated unused global @"
                      << gv->getName() << "\n";
        }
    }
    return changed;
}

bool RemoveUnusedGlobalAndFunctionPass::removeUnusedFunctions(Module *module)
{
    Function *mainFunc = module->getFunction("main");
    if (!mainFunc)
        return false;

    std::unordered_set<Function *> reachable;
    collectReachableFunctions(mainFunc, reachable);

    bool changed = false;
    for (auto &funcPtr : module->Functions)
    {
        Function *func = funcPtr.get();
        if (func->isLibraryFunction() || func->getName() == "main" || func->isDeletedFunction())
            continue;
        if (reachable.count(func))
            continue;
        func->setDeleted(true);
        changed = true;
        if (verbose)
        {
            debugInfo << "RemoveUnusedGlobalAndFunctionPass: Eliminated unused function @"
                      << func->getName() << "\n";
        }
    }
    return changed;
}

bool RemoveUnusedGlobalAndFunctionPass::runOnFunction(Function *func)
{
    Module *module = func->getParent();
    if (!module || moduleProcessed)
        return false;

    moduleProcessed = true;
    bool changed = false;
    bool localChanged = false;
    do
    {
        localChanged = false;
        localChanged |= removeUnusedFunctions(module);
        localChanged |= removeUnusedGlobals(module);
        changed |= localChanged;
    } while (localChanged);

    return changed;
}