#include "LoopGccStyleTransformPass.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
using namespace std;
using namespace optimization;

namespace
{
    BasicBlock *findLoopLatchBlock(const Loop &loop)
    {
        BasicBlock *latch = nullptr;
        for (auto *bb : loop.blocks)
        {
            if (bb == loop.header)
            {
                continue;
            }
            for (auto *succ : bb->getSuccessors())
            {
                if (succ == loop.header)
                {
                    if (latch)
                    {
                        return nullptr;
                    }
                    latch = bb;
                }
            }
        }
        return latch;
    }

    BranchInst *getUnconditionalBranchTo(BasicBlock *bb, BasicBlock *target)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            auto *br = dynamic_cast<BranchInst *>(instPtr.get());
            if (br && !br->isConditional() && br->getTrueBlock() == target)
            {
                return br;
            }
        }
        return nullptr;
    }

    void disconnectEdge(BasicBlock *from, BasicBlock *to)
    {
        if (!from || !to)
        {
            return;
        }
        from->removeSuccessor(to);
        to->removePredecessor(from);
    }

    void connectEdge(BasicBlock *from, BasicBlock *to)
    {
        if (!from || !to)
        {
            return;
        }
        from->addSuccessor(to);
        to->addPredecessor(from);
    }

    bool headerHasPhi(BasicBlock *header)
    {
        for (auto &instPtr : header->getInstructions())
        {
            if (dynamic_cast<PhiInst *>(instPtr.get()))
            {
                return true;
            }
        }
        return false;
    }

    vector<Instruction *> collectHeaderPrefix(BasicBlock *header, BranchInst *headerBr)
    {
        vector<Instruction *> prefix;
        for (auto &instPtr : header->getInstructions())
        {
            if (instPtr.get() == headerBr)
            {
                break;
            }
            prefix.push_back(instPtr.get());
        }
        return prefix;
    }

    Value *remapValue(Value *value, const unordered_map<Value *, Value *> &valueMap)
    {
        auto it = valueMap.find(value);
        if (it != valueMap.end())
        {
            return it->second;
        }
        return value;
    }

    bool nameEndsWith(const string &name, const string &suffix)
    {
        return name.size() >= suffix.size() &&
               name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    vector<unique_ptr<Instruction>> cloneInstructionsForPreheader(const vector<Instruction *> &prefix,
                                                                  unordered_map<Value *, Value *> &cloneMap,
                                                                  bool unrollLoop)
    {
        vector<unique_ptr<Instruction>> clones;
        clones.reserve(prefix.size());
        for (auto *inst : prefix)
        {
            Instruction *cloned = inst->clone();
            const string &origName = inst->getName();
            const bool keepUnrollName = unrollLoop && nameEndsWith(origName, "_unroll_step");
            cloned->setName(keepUnrollName ? origName : origName + "_gcc_entry");
            for (size_t i = 0; i < cloned->getOperands().size(); ++i)
            {
                cloned->setOperandByIndex(i, remapValue(cloned->getOperandByIndex(i), cloneMap));
            }
            cloneMap[inst] = cloned;
            clones.push_back(unique_ptr<Instruction>(cloned));
        }
        return clones;
    }

    bool isUnrollLoopHeader(BasicBlock *header)
    {
        return header && header->getName().find("_unroll_header") != string::npos;
    }

    unordered_set<Value *> collectHeaderDefinedValues(const vector<Instruction *> &headerPrefix)
    {
        unordered_set<Value *> defined;
        for (auto *inst : headerPrefix)
        {
            defined.insert(static_cast<Value *>(inst));
        }
        return defined;
    }

    Value *findHeaderStepValue(const vector<Instruction *> &headerPrefix,
                               const unordered_set<Value *> &headerDefined, bool unrollLoop)
    {
        for (auto *inst : headerPrefix)
        {
            auto *icmp = dynamic_cast<ICmpInst *>(inst);
            if (!icmp)
            {
                continue;
            }
            Value *lhs = icmp->getLHS();
            Value *rhs = icmp->getRHS();
            if (lhs && headerDefined.count(lhs))
            {
                return lhs;
            }
            if (rhs && headerDefined.count(rhs))
            {
                return rhs;
            }
            if (unrollLoop)
            {
                if (lhs && nameEndsWith(lhs->getName(), "_unroll_step"))
                {
                    return lhs;
                }
                if (rhs && nameEndsWith(rhs->getName(), "_unroll_step"))
                {
                    return rhs;
                }
            }
        }
        return nullptr;
    }

    bool copySourcesStepValue(CopyInst *copy, const unordered_set<Value *> &headerDefined, Value *stepValue,
                              bool unrollLoop)
    {
        if (!copy)
        {
            return false;
        }
        Value *src = copy->getSource();
        if (!src)
        {
            return false;
        }
        if (headerDefined.count(src) || (stepValue && src == stepValue))
        {
            return true;
        }
        if (unrollLoop && nameEndsWith(src->getName(), "_unroll_step"))
        {
            return true;
        }
        if (stepValue && nameEndsWith(stepValue->getName(), "_unroll_step") &&
            nameEndsWith(src->getName(), "_unroll_step"))
        {
            return true;
        }
        return false;
    }

    // 普通循环：header 非 icmp 部分插在 phi copy 之前；仅 icmp 时追加到末尾。
    // 展开循环：header 的 add+icmp 插在 phi copy 之后（先 copy 再加 4 比较）。
    size_t findLatchHeaderInsertIndex(const vector<unique_ptr<Instruction>> &latchInsts,
                                      const vector<Instruction *> &headerPrefix, bool unrollLoop)
    {
        const unordered_set<Value *> headerDefined = collectHeaderDefinedValues(headerPrefix);
        Value *stepValue = findHeaderStepValue(headerPrefix, headerDefined, unrollLoop);

        bool hasNonIcmp = false;
        for (auto *inst : headerPrefix)
        {
            if (!dynamic_cast<ICmpInst *>(inst))
            {
                hasNonIcmp = true;
                break;
            }
        }
        if (hasNonIcmp)
        {
            for (size_t i = 0; i < latchInsts.size(); ++i)
            {
                auto *copy = dynamic_cast<CopyInst *>(latchInsts[i].get());
                if (copySourcesStepValue(copy, headerDefined, stepValue, unrollLoop))
                {
                    return unrollLoop ? i + 1 : i;
                }
            }
        }

        return latchInsts.size();
    }

    // 展开循环 latch：add 的 lhs 改为 copy 结果（先 copy 再 phi+4）
    void remapUnrollLatchAddAfterCopy(vector<unique_ptr<Instruction>> &latchInsts, size_t insertIdx)
    {
        if (insertIdx == 0)
        {
            return;
        }
        auto *copy = dynamic_cast<CopyInst *>(latchInsts[insertIdx - 1].get());
        if (!copy)
        {
            return;
        }
        for (size_t i = insertIdx; i < latchInsts.size(); ++i)
        {
            auto *add = dynamic_cast<BinaryOperator *>(latchInsts[i].get());
            if (add && add->getOpcode() == Opcode::Add)
            {
                add->setOperandByIndex(0, copy);
                return;
            }
        }
    }
}

void LoopGccStyleTransformPass::replacePhiIncomingBlock(BasicBlock *succBlock, BasicBlock *oldPred,
                                                        BasicBlock *newPred)
{
    if (!succBlock || !oldPred || !newPred)
    {
        return;
    }
    for (auto &instPtr : succBlock->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi)
        {
            continue;
        }
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            if (phi->getIncomingBlock(i) == oldPred)
            {
                phi->setIncomingBlock(i, newPred);
            }
        }
    }
}

bool LoopGccStyleTransformPass::tryTransform(Function *func, const Loop &loop)
{
    (void)func;
    BasicBlock *header = loop.header;
    if (!header || headerHasPhi(header))
    {
        return false;
    }

    auto *headerBr = dynamic_cast<BranchInst *>(header->getTerminator());
    if (!headerBr || !headerBr->isConditional())
    {
        return false;
    }

    Loop loopCopy = loop;
    loopCopy.computePreheader();
    BasicBlock *preheader = loopCopy.getPreheader();
    if (!preheader)
    {
        return false;
    }

    if (!getUnconditionalBranchTo(preheader, header))
    {
        return false;
    }

    BasicBlock *bodyBB = nullptr;
    BasicBlock *exitBB = nullptr;
    if (loop.containsBlock(headerBr->getTrueBlock()))
    {
        bodyBB = headerBr->getTrueBlock();
        exitBB = headerBr->getFalseBlock();
    }
    else if (loop.containsBlock(headerBr->getFalseBlock()))
    {
        bodyBB = headerBr->getFalseBlock();
        exitBB = headerBr->getTrueBlock();
    }
    if (!bodyBB || !exitBB || loop.containsBlock(exitBB))
    {
        return false;
    }

    BasicBlock *latch = findLoopLatchBlock(loop);
    if (!latch || !loop.containsBlock(latch))
    {
        return false;
    }

    auto *backBr = getUnconditionalBranchTo(latch, header);
    if (!backBr)
    {
        return false;
    }

    for (auto *pred : header->getPredecessors())
    {
        if (pred != preheader && pred != latch)
        {
            return false;
        }
    }

    vector<Instruction *> headerPrefix = collectHeaderPrefix(header, headerBr);
    for (auto *inst : headerPrefix)
    {
        if (inst->hasExternalUse(loop))
        {
            return false;
        }
    }

    const bool unrollLoop = isUnrollLoopHeader(header);

    unordered_map<Value *, Value *> cloneMap;
    vector<unique_ptr<Instruction>> entryInsts =
        cloneInstructionsForPreheader(headerPrefix, cloneMap, unrollLoop);
    Value *entryCond = remapValue(headerBr->getCondition(), cloneMap);

    // preheader: 入库判断 body / exit
    auto *preBr = getUnconditionalBranchTo(preheader, header);
    preBr->removeThisFromOperands();
    auto &preInsts = preheader->getInstructions();
    for (auto it = preInsts.begin(); it != preInsts.end(); ++it)
    {
        if (it->get() == preBr)
        {
            preInsts.erase(it);
            break;
        }
    }
    disconnectEdge(preheader, header);
    connectEdge(preheader, bodyBB);
    connectEdge(preheader, exitBB);
    replacePhiIncomingBlock(exitBB, header, preheader);
    for (auto &inst : entryInsts)
    {
        preheader->addInstruction(std::move(inst));
    }
    preheader->addInstruction(unique_ptr<Instruction>(new BranchInst(entryCond, bodyBB, exitBB)));

    // latch：删除回 cond 的无条件跳
    backBr->removeThisFromOperands();
    auto &latchInsts = latch->getInstructions();
    for (auto it = latchInsts.begin(); it != latchInsts.end(); ++it)
    {
        if (it->get() == backBr)
        {
            latchInsts.erase(it);
            break;
        }
    }

    const string removedHeaderName = header->getName();

    disconnectEdge(latch, header);
    header->removePredecessor(latch);

    auto &headerInsts = header->getInstructions();
    vector<unique_ptr<Instruction>> movedInsts;
    movedInsts.reserve(headerPrefix.size() + 1);
    for (auto it = headerInsts.begin(); it != headerInsts.end();)
    {
        if (it->get() == headerBr)
        {
            ++it;
            continue;
        }
        movedInsts.push_back(std::move(*it));
        it = headerInsts.erase(it);
    }

    const size_t insertIdx = findLatchHeaderInsertIndex(latchInsts, headerPrefix, unrollLoop);
    auto insertIt = latchInsts.begin() + static_cast<ptrdiff_t>(insertIdx);
    for (auto &inst : movedInsts)
    {
        insertIt = latchInsts.insert(insertIt, std::move(inst));
        ++insertIt;
    }

    if (unrollLoop)
    {
        remapUnrollLatchAddAfterCopy(latchInsts, insertIdx);
    }

    Value *loopCond = headerBr->getCondition();
    latchInsts.push_back(unique_ptr<Instruction>(new BranchInst(loopCond, bodyBB, exitBB)));
    connectEdge(latch, bodyBB);
    connectEdge(latch, exitBB);
    replacePhiIncomingBlock(exitBB, header, latch);

    removePhiIncomingFromPredecessor(exitBB, header);
    for (auto &instPtr : header->getInstructions())
    {
        instPtr->removeThisFromOperands();
        needToDelete.push_back(instPtr.release());
    }
    header->clearInstructions();
    header->removeSelfBasicBlock();

    if (verbose)
    {
        debugInfo << "LoopGccStyleTransform: " << removedHeaderName
                  << (unrollLoop ? " (unroll)" : "") << " -> body " << bodyBB->getName() << ", latch "
                  << latch->getName() << ", entry at " << preheader->getName() << "\n";
    }

    for (auto &instPtr : exitBB->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi)
        {
            continue;
        }
        bool hasBodyIncoming = false;
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            if (phi->getIncomingBlock(i) == bodyBB)
            {
                hasBodyIncoming = true;
                break;
            }
        }
        if (!hasBodyIncoming)
        {
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                if (phi->getIncomingBlock(i) == preheader)
                {
                    phi->addIncoming(phi->getIncomingValue(i), bodyBB);
                    break;
                }
            }
        }
    }

    return true;
}

bool LoopGccStyleTransformPass::runOnFunction(Function *func)
{
    if (!func)
    {
        return false;
    }

    func->setLoops(ControlFlowAnalysis::findLoops(func));
    vector<Loop> loops = func->getLoops();
    std::sort(loops.begin(), loops.end(), [](const Loop &a, const Loop &b)
              { return a.blocks.size() < b.blocks.size(); });

    bool changed = false;
    for (const auto &loop : loops)
    {
        if (tryTransform(func, loop))
        {
            changed = true;
            func->setLoops(ControlFlowAnalysis::findLoops(func));
            loops = func->getLoops();
            std::sort(loops.begin(), loops.end(), [](const Loop &a, const Loop &b)
                      { return a.blocks.size() < b.blocks.size(); });
        }
    }

    if (changed)
    {
        auto &bbs = func->getBasicBlocks();
        for (auto it = bbs.begin(); it != bbs.end();)
        {
            BasicBlock *bb = it->get();
            if (bb != func->getEntryBlock() && bb->getPredecessors().empty())
            {
                for (auto &instPtr : bb->getInstructions())
                {
                    instPtr->removeThisFromOperands();
                    needToDelete.push_back(instPtr.release());
                }
                bb->clearInstructions();
                needToDelete.push_back(it->release());
                it = bbs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    return changed;
}
