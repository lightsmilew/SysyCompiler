#include "BasicBlockPass.h"
#include <functional>
using namespace std;
using namespace optimization;

namespace
{
static void replaceBranchTargetInFunction(Function *func, BasicBlock *from, BasicBlock *to)
{
    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            auto *br = dynamic_cast<BranchInst *>(instPtr.get());
            if (!br)
                continue;
            if (br->getTrueBlock() == from)
                br->setTrueBlock(to);
            if (br->isConditional() && br->getFalseBlock() == from)
                br->setFalseBlock(to);
        }
    }
}

static void removeUnconditionalBranchesTo(BasicBlock *bb, BasicBlock *target)
{
    auto &insts = bb->getInstructions();
    for (auto it = insts.begin(); it != insts.end();)
    {
        auto *br = dynamic_cast<BranchInst *>(it->get());
        if (br && !br->isConditional() && br->getTrueBlock() == target)
        {
            br->removeThisFromOperands();
            it = insts.erase(it);
        }
        else
        {
            ++it;
        }
    }
}
} // namespace

bool CFGSimplificationPass::runOnFunction(Function *func)
{
    bool changed = false;
    auto &bbs = func->getBasicBlocks();
    vector<BasicBlock *> bbsToProcess;
    Value *arrayAddr = nullptr;
    for (auto &bbPtr : bbs)
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        //  1.优化 if (i > k) 嵌套结构
        //  这种情况下可以把嵌套if转换为循环，便于后续优化
        for (size_t i = 0; i < insts.size(); ++i)
        {
            auto *icmp = dynamic_cast<ICmpInst *>(insts[i].get());
            if (!icmp || icmp->getPredicate() != ICmpInst::ICMP_SGT)
                continue;
            auto *iVar = icmp->getLHS();
            auto *kConst = dynamic_cast<ConstantInt *>(icmp->getRHS());
            if (!kConst)
                continue;
            int k = kConst->Value;

            // 2. then 分支是否为 s[k] = k
            BasicBlock *thenBB = nullptr;
            BasicBlock *exitBB = nullptr;
            if (i + 1 < insts.size())
            {
                if (auto *br = dynamic_cast<BranchInst *>(insts[i + 1].get()))
                {
                    thenBB = br->TrueBlock;
                    exitBB = br->FalseBlock; // 该结构下无else分支，故假出口则为退出块
                }
            }
            if (!thenBB || !exitBB)
                continue;
            auto &thenInsts = thenBB->getInstructions();
            bool isIfElseSentence = false;
            for (auto &tinstPtr : thenInsts)
            {
                if (auto *store = dynamic_cast<StoreInst *>(tinstPtr.get()))
                {
                    // store的地址和数值是否都是循环规约量k
                    auto *gep = dynamic_cast<GetElementPtrInst *>(store->getPointer());
                    auto *storeVal = dynamic_cast<ConstantInt *>(store->getValueToStore());
                    if (gep && storeVal && storeVal->Value == k)
                    {
                        if (gep->getIndices().size() == 1)
                        {
                            auto *idx = dynamic_cast<ConstantInt *>(gep->getIndices()[0]);
                            if (idx && idx->Value == k)
                            {
                                arrayAddr = gep->getPointerOperand();
                                isIfElseSentence = true;
                                needToDelete.push_back(insts[i].release());     // 删除原有的 if (i > k) 结构
                                needToDelete.push_back(insts[i + 1].release()); // 删除分支指令
                                insts.erase(insts.begin() + i);                 // 删除if指令
                                insts.erase(insts.begin() + i);                 // 删除分支指令
                                break;
                            }
                        }
                    }
                }
            }
            if (!isIfElseSentence)
                continue;
            // 找到嵌套的 if (i > k) 结构
            // bb即是整个循环的前驱基本块
            bbsToProcess.push_back(thenBB);
            bbsToProcess.push_back(exitBB);
            // 3. 递归/循环收集连续的k
            std::vector<int> kList = {k};
            BasicBlock *curBB = thenBB;
            while (true)
            {
                // 查找下一个 if (i > k+1) 结构
                auto &curInsts = curBB->getInstructions();
                bool foundNext = false;
                for (size_t j = 0; j < curInsts.size(); ++j)
                {
                    auto *icmp2 = dynamic_cast<ICmpInst *>(curInsts[j].get());
                    if (icmp2 && icmp2->getPredicate() == ICmpInst::ICMP_SGT)
                    {
                        auto *k2Const = dynamic_cast<ConstantInt *>(icmp2->getRHS());
                        if (k2Const && k2Const->Value == kList.back() + 1)
                        {
                            kList.push_back(k2Const->Value);
                            // 跳到下一个then分支
                            if (j + 1 < curInsts.size())
                            {
                                if (auto *br2 = dynamic_cast<BranchInst *>(curInsts[j + 1].get()))
                                {
                                    curBB = br2->TrueBlock;
                                    bbsToProcess.push_back(curBB);
                                    bbsToProcess.push_back(br2->FalseBlock);
                                    foundNext = true;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (!foundNext)
                    break;
            }
            //
            // 4. 替换为循环
            if (kList.size() > 3)
            { // 只有连续if链足够长才替换
                int minK = kList.front();
                int maxK = kList.back();

                auto *loopCond = new BasicBlock("loop_cond", func);
                auto *loopBody = new BasicBlock("loop_body", func);
                auto *loopExit = new BasicBlock("loop_exit", func);
                // 比较指令min(i,maxK+1)
                auto *icmpMin = new ICmpInst(ICmpInst::ICMP_SLT, iVar, new ConstantInt(IntegerType::getInstance(), maxK + 1), "min_icmp");
                auto *bbBrToCond = new BranchInst(loopCond);
                // 构建合流指令
                auto *kPhi = new PhiInst(IntegerType::getInstance(), "tk_loop");
                auto *selectMin_i_maxK = new SelectInst(icmpMin, iVar, new ConstantInt(IntegerType::getInstance(), maxK + 1), "min_i_maxk_sel");
                // 设置循环条件
                auto *icmpLoop = new ICmpInst(ICmpInst::ICMP_SLT, kPhi, selectMin_i_maxK, "loop_cond_icmp");
                // 设置跳转指令
                auto *brLoop = new BranchInst(icmpLoop, loopBody, loopExit);
                // 设置循环体指令
                auto *gepLoop = new GetElementPtrInst(arrayAddr, {kPhi}, "loop_gep");
                auto *storeLoop = new StoreInst(kPhi, gepLoop);
                auto *incK = new BinaryOperator(Opcode::Add, kPhi, new ConstantInt(IntegerType::getInstance(), 1), "inc_k");
                // 无条件跳转到循环条件
                auto *brToCond = new BranchInst(loopCond);
                // 设置初始值
                kPhi->addIncoming(new ConstantInt(IntegerType::getInstance(), minK), bb);
                kPhi->addIncoming(incK, loopBody);
                // 将原来最外层if退出块指令复制到loopExit
                for (auto &instPtr : exitBB->getInstructions())
                {
                    std::unique_ptr<Instruction> instPtrCopy = move(instPtr);

                    loopExit->addInstruction(move(instPtrCopy));
                }
                // 清空原有退出块指令
                exitBB->clearInstructions();
                // 将原来phi输入从exitBB的指令改为loopExit
                for (auto *user : exitBB->getUsers())
                {
                    if (auto *phi = dynamic_cast<PhiInst *>(user))
                    {
                        phi->replaceIncomingBasicBlock(exitBB, loopExit);
                    }
                }
                bb->addInstruction(std::unique_ptr<Instruction>(bbBrToCond));
                // 将新构造的指令添加到基本块
                loopCond->addInstruction(std::unique_ptr<Instruction>(kPhi));
                loopCond->addInstruction(std::unique_ptr<Instruction>(icmpMin));
                loopCond->addInstruction(std::unique_ptr<Instruction>(selectMin_i_maxK));
                loopCond->addInstruction(std::unique_ptr<Instruction>(icmpLoop));
                loopCond->addInstruction(std::unique_ptr<Instruction>(brLoop));
                // 添加循环体指令
                loopBody->addInstruction(std::unique_ptr<Instruction>(gepLoop));
                loopBody->addInstruction(std::unique_ptr<Instruction>(storeLoop));
                loopBody->addInstruction(std::unique_ptr<Instruction>(incK));
                loopBody->addInstruction(std::unique_ptr<Instruction>(brToCond));
                // 更新CFG
                // 建立原来ifthen前驱与循环条件的连接
                bb->addSuccessor(loopCond);
                loopCond->addPredecessor(bb);
                // 将原来最外层的exit的后继块复制到loopExit
                for (auto *succ : exitBB->getSuccessors())
                {
                    exitBB->removeSuccessor(succ);   // 删除原有后继
                    succ->removePredecessor(exitBB); // 删除原有后继
                    succ->addPredecessor(loopExit);  // 添加到新循环退出块
                    loopExit->addSuccessor(succ);    // 添加后继到循环退出块
                }
                // 更新内部CFG连接
                loopCond->addSuccessor(loopBody);
                loopCond->addSuccessor(loopExit);
                loopExit->addPredecessor(loopCond);
                loopBody->addPredecessor(loopCond);
                loopBody->addSuccessor(loopCond);
                func->addBasicBlock(unique_ptr<BasicBlock>(loopCond));
                func->addBasicBlock(unique_ptr<BasicBlock>(loopBody));
                func->addBasicBlock(unique_ptr<BasicBlock>(loopExit));

                // 删除原来的if链相关基本块
                for (auto *bbToDel : bbsToProcess)
                {
                    bbToDel->removeSelfBasicBlock(); // 删除基本块的CFG连接，便于删除基本块
                }

                changed = true;
                if (verbose)
                    debugInfo << "CFG Simplification: Replace if-chain [" << minK << "," << maxK << "] with loop in " << bb->getName() << "\n";
            }
            bbsToProcess.clear(); // 清空待处理基本块
            break;
        }
        if (changed)
            break; // 如果已经进行了替换，退出函数
    }
    //  删除无用的基本块
    for (auto it = bbs.begin(); it != bbs.end();)
    {
        BasicBlock *bb = it->get();
        if (bb != func->getEntryBlock() && bb->getPredecessors().empty())
        {
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
    return changed;
}
bool BasicBlockMergePass::runOnFunction(Function *func)
{
    bool changed = false;
    // 如果删除不可达基本块后还能合并则继续合并
    bool localchanged = true;
    while (localchanged)
    {
        localchanged = false;
        auto &bbs = func->getBasicBlocks();
        for (auto it = bbs.begin(); it != bbs.end();)
        {
            BasicBlock *bb = it->get();
            if (!bb)
            {
                ++it;
                continue;
            }
            // 只合并只有一个后继且后继只有一个前驱的情况
            auto *succ = (bb->getSuccessors().size() == 1) ? bb->getSuccessors()[0] : nullptr;
            if (!succ || succ->getPredecessors().size() != 1 || succ == func->getEntryBlock())
            {
                ++it;
                continue; // 不是单一后继或后继不是单一前驱，跳过
            }
            // 判断succ是否为数组初始化块
            bool isArrayInit = false;
            // 1. 当前块倒数第二条是AllocaInst
            if (bb->getInstructions().size() >= 2 &&
                dynamic_cast<AllocaInst *>(bb->getInstructions()[bb->getInstructions().size() - 2].get()))
            {
                isArrayInit = true;
            }
            // 2. 或者当前块的前驱倒数第二条是AllocaInst(因为最后一条是branch指令)
            for (auto *pred : bb->getPredecessors())
            {
                if (pred->getInstructions().size() >= 2 &&
                    dynamic_cast<AllocaInst *>(pred->getInstructions()[pred->getInstructions().size() - 2].get()))
                {
                    isArrayInit = true;
                    break;
                }
            }
            if (isArrayInit)
            {
                ++it;
                continue;
            }
            // 合并succ到bb
            auto &bbInsts = bb->getInstructions();
            auto &succInsts = succ->getInstructions();
            // 移除 bb 中所有指向 succ 的无条件跳转（含末尾），合并后 succ 指令会接在后面
            removeUnconditionalBranchesTo(bb, succ);
            if (!bbInsts.empty() && bbInsts.back()->isTerminator())
            {
                auto *br = dynamic_cast<BranchInst *>(bbInsts.back().get());
                if (br && !br->isConditional() && br->getTrueBlock() == succ)
                {
                    br->removeThisFromOperands();
                    bbInsts.pop_back();
                }
            }
            // 把succ的所有指令移动到bb
            for (auto &inst : succInsts)
            {
                bbInsts.push_back(std::move(inst));
            }
            succInsts.clear();
            // 替换phi输入到bb
            for (auto &user : succ->getUsers())
            {
                if (auto *phi = dynamic_cast<PhiInst *>(user))
                {

                    // 如果有多个输入块，且存在以下情况:
                    // 一个输入来自bb，一个输入来自succ，则把原来bb的输入删除
                    int fromBBIndex = -1, fromSuccIndex = -1;
                    for (size_t i = 0; i < phi->getIncomingBlocks().size(); ++i)
                    {
                        if (phi->getIncomingBlock(i) == bb)
                        {
                            fromBBIndex = i;
                        }
                        else if (phi->getIncomingBlock(i) == succ)
                        {
                            fromSuccIndex = i;
                        }
                    }
                    if (fromBBIndex != -1 && fromSuccIndex != -1)
                    {
                        // 删除bb的输入
                        phi->removeIncoming(fromBBIndex);
                    }
                    // 正常处理：
                    // 替换phi的输入块
                    for (size_t i = 0; i < phi->getIncomingBlocks().size(); ++i)
                    {
                        if (phi->getIncomingBlock(i) == succ)
                        {
                            phi->setIncomingBlock(i, bb);
                        }
                    }
                }
            }
            // 更新CFG
            for (auto *succSucc : succ->getSuccessors())
            {
                bb->addSuccessor(succSucc);
                succSucc->removePredecessor(succ);
                succSucc->addPredecessor(bb);
            }
            bb->removeSuccessor(succ);
            replaceBranchTargetInFunction(func, succ, bb);
            // 移除succ
            for (auto succIt = bbs.begin(); succIt != bbs.end(); ++succIt)
            {
                if (succIt->get() == succ)
                {
                    needToDelete.push_back(succIt->release());
                    bbs.erase(succIt);
                    break;
                }
            }
            changed = true;
            localchanged = true;
            if (verbose)
            {
                debugInfo << "BasicBlockMergePass: Merged " << succ->getName() << " into " << bb->getName() << "\n";
            }
            // 不递增it，因为当前bb可能还能继续合并
        }
        // 复用死代码消除的不可达块删除代码
        // 删除不可达基本块
        auto &_bbs = func->getBasicBlocks();
        vector<Loop> loops = ControlFlowAnalysis::findLoops(func);
        unordered_map<BasicBlock *,vector<BasicBlock*>> loopMap;
        for(auto &loop:loops)
        {
            if(loopMap.count(loop.header)==0)loopMap[loop.header]=loop.blocks;
            // 如果是小循环则替换成大循环，否则不变
            else 
            {
                if(loopMap[loop.header].size()<loop.blocks.size())
                {
                    loopMap[loop.header]=loop.blocks;
                }
            }
        }
        if (!_bbs.empty())
        {
            BasicBlock *entry = _bbs[0].get();
            // 先收集所有将要删除的不可达基本块
            std::vector<BasicBlock *> toDelete;
            for (auto &bbPtr : _bbs)
            {
                BasicBlock *bb = bbPtr.get();
                if (bb != entry && bb->getPredecessors().empty())
                {
                    toDelete.push_back(bb);
                    bb->removeSelfBasicBlock();
                    debugInfo << "delete block:" << bb->getName() << std::endl;
                }
                else if (bb != entry && bb->getPredecessors().size() == 1 && bb->getPredecessors()[0] == bb)
                {
                    // 自己是自己的前驱，死循环块
                    toDelete.push_back(bb);
                    // bb->removePredecessor(bb);
                    // bb->removeSuccessor(bb);
                    bb->removeSelfBasicBlock();
                    debugInfo << "delete self-loop block:" << bb->getName() << std::endl;
                }
            }
            for(auto &[header,blocks]:loopMap)
            {
                if(header->getPredecessors().empty())
                {
                    for(auto *bb:blocks)
                    {
                        toDelete.push_back(bb);
                        bb->removeSelfBasicBlock();
                        debugInfo << "delete loop block:" << bb->getName() << std::endl;
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
                        //  如果只剩一个incoming，直接替换
                        if (phi->getNumIncomingValues() == 1)
                        {
                            Value *incomingValue = phi->getIncomingValue(0);
                            phi->replaceAllUsesWith(incomingValue);
                        }
                    }
                }
            }
            for (auto it = _bbs.begin(); it != _bbs.end();)
            {
                BasicBlock *bb = it->get();
                if (bb != entry && bb->getPredecessors().empty())
                {
                    // // 从后继中删除自身
                    // for (auto *succ : bb->getSuccessors())
                    // {
                    //     succ->removePredecessor(bb);
                    //     bb->removeSuccessor(succ);
                    // }
                    // 释放前清理 use 链，避免留下悬空 user（遍历 getUsers() 会崩溃）
                    for (auto &instPtr : bb->getInstructions())
                        instPtr->removeThisFromOperands();
                    bb->removeSelfBasicBlock();
                    // 这里不能直接删除，把它放到needToDelete中,否则内存空间释放了
                    needToDelete.push_back(it->release());
                    // 从基本块列表中删除
                    it = _bbs.erase(it);
                }
                else if(bb != entry && bb->getPredecessors().size() == 1 && bb->getPredecessors()[0] == bb)
                {
                    // 自己是自己的前驱，死循环块
                    // // 从后继中删除自身
                    // for (auto *succ : bb->getSuccessors())
                    // {
                    //     succ->removePredecessor(bb);
                    //     bb->removeSuccessor(succ);
                    // }
                    for (auto &instPtr : bb->getInstructions())
                        instPtr->removeThisFromOperands();
                    bb->removeSelfBasicBlock();
                    needToDelete.push_back(it->release());
                    it = _bbs.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
    }
    return changed;
}
bool BasicBlockReorderPass::runOnFunction(Function *func)
{
    bool changed = false;
    auto &bbs = func->getBasicBlocks();
    if (bbs.empty())
        return false;

    // 1. 计算直接支配者
    auto idom = optimization::computeIDom_LengauerTarjan(func);

    // 2. 构建支配树（父->子），不跳过入口块
    std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> domTree;
    for (auto &p : idom)
    {
        domTree[p.second].push_back(p.first); // p.second 允许为 nullptr
    }

    // 3. 建立 BasicBlock* 到 unique_ptr 的映射，方便重排
    std::unordered_map<BasicBlock *, std::unique_ptr<BasicBlock>> bbMap;
    BasicBlock *entry = func->getEntryBlock();
    for (auto &bbPtr : bbs)
    {
        bbMap[bbPtr.get()] = std::move(bbPtr);
    }

    std::vector<std::unique_ptr<BasicBlock>> newOrder;
    std::unordered_set<BasicBlock *> visited;
    // 4. 支配树DFS，真出口优先
    std::function<void(BasicBlock *)> dfs = [&](BasicBlock *bb)
    {
        if (!bb || visited.count(bb))
            return;
        visited.insert(bb);
        if (bbMap.count(bb))
            newOrder.push_back(std::move(bbMap[bb]));

        // 获取直接支配的子节点
        auto &children = domTree[bb];
        if (children.empty())
            return;

        // 真出口优先：如果是条件跳转，先真分支
        std::vector<BasicBlock *> sortedChildren;
        if (bb->getInstructions().size() > 0)
        {
            if (auto *br = dynamic_cast<BranchInst *>(bb->getInstructions().back().get()))
            {
                if (br->isConditional())
                {
                    auto *trueBB = br->getTrueBlock();
                    auto *falseBB = br->getFalseBlock();
                    if (std::find(children.begin(), children.end(), trueBB) != children.end())
                        sortedChildren.push_back(trueBB);
                    if (std::find(children.begin(), children.end(), falseBB) != children.end())
                        sortedChildren.push_back(falseBB);
                    for (auto *child : children)
                    {
                        if (child != trueBB && child != falseBB)
                            sortedChildren.push_back(child);
                    }
                }
                else
                {
                    sortedChildren = children;
                }
            }
            else
            {
                sortedChildren = children;
            }
        }
        else
        {
            sortedChildren = children;
        }
        for (auto *child : sortedChildren)
        {
            dfs(child);
        }
    };

    // 5. 从入口块开始DFS遍历
    dfs(entry);

    // 6. 补充未访问到的块（如不可达块）
    for (auto &kv : bbMap)
    {
        if (!visited.count(kv.first))
        {
            newOrder.push_back(std::move(kv.second));
        }
    }

    // 7. 替换原顺序
    if (bbs.size() != newOrder.size())
        return false;
    bbs = std::move(newOrder);
    changed = true;
    if (verbose)
    {
        debugInfo << "BlockReorderPass: Reordered basic blocks in function " << func->getName() << "\n";
    }
    return changed;
}