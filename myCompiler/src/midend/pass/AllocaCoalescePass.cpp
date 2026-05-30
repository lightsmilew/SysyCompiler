#include "AllocaCoalescePass.h"
#include <algorithm>
#include <map>
#include <queue>
#include <unordered_set>
#include <vector>

using namespace std;
using namespace optimization;

namespace
{
    struct AllocaSite
    {
        AllocaInst *inst;
        BasicBlock *block;
        size_t blockIndex;
        size_t instIndex;
    };

    BasicBlock *findBlockContaining(Function *func, Instruction *inst)
    {
        for (auto &bbPtr : func->getBasicBlocks())
        {
            auto &insts = bbPtr->getInstructions();
            for (size_t i = 0; i < insts.size(); ++i)
            {
                if (insts[i].get() == inst)
                    return bbPtr.get();
            }
        }
        return nullptr;
    }

    size_t blockIndexInFunction(Function *func, BasicBlock *bb)
    {
        auto &bbs = func->getBasicBlocks();
        for (size_t i = 0; i < bbs.size(); ++i)
        {
            if (bbs[i].get() == bb)
                return i;
        }
        return bbs.size();
    }

    size_t getAllocaTotalElements(AllocaInst *alloca)
    {
        auto *arrayTy = dynamic_cast<ArrayType *>(alloca->AllocatedType);
        if (!arrayTy)
            return 0;
        return arrayTy->getArrayLength();
    }

    BasicBlock *findDominatorMeet(const unordered_map<BasicBlock *, BasicBlock *> &idom,
                                  const vector<BasicBlock *> &blocks)
    {
        if (blocks.empty())
            return nullptr;
        BasicBlock *meet = blocks[0];
        for (size_t i = 1; i < blocks.size() && meet; ++i)
        {
            BasicBlock *b = blocks[i];
            while (meet && !ControlFlowAnalysis::dominates(idom, meet, b))
            {
                auto it = idom.find(meet);
                meet = (it != idom.end()) ? it->second : nullptr;
            }
        }
        return meet;
    }

    bool allocaUsesDominatedBy(AllocaInst *alloca, BasicBlock *target,
                               const unordered_map<BasicBlock *, BasicBlock *> &idom,
                               Function *func)
    {
        for (auto *user : alloca->getUsers())
        {
            auto *useInst = dynamic_cast<Instruction *>(user);
            if (!useInst)
                continue;
            BasicBlock *useBB = findBlockContaining(func, useInst);
            if (!useBB)
                continue;
            if (!ControlFlowAnalysis::dominates(idom, target, useBB))
                return false;
        }
        return true;
    }

    size_t leadingAllocaEnd(BasicBlock *bb)
    {
        size_t i = 0;
        auto &insts = bb->getInstructions();
        while (i < insts.size() && dynamic_cast<AllocaInst *>(insts[i].get()))
            ++i;
        return i;
    }

    void moveAllocaToBlock(AllocaSite &site, BasicBlock *dest, size_t insertIndex)
    {
        auto &srcInsts = site.block->getInstructions();
        unique_ptr<Instruction> moved = std::move(srcInsts[site.instIndex]);
        srcInsts.erase(srcInsts.begin() + static_cast<long>(site.instIndex));

        auto &destInsts = dest->getInstructions();
        destInsts.insert(destInsts.begin() + static_cast<long>(insertIndex), std::move(moved));
    }

    bool isUnconditionalBranchOnly(BasicBlock *bb)
    {
        if (!bb || !bb->hasTerminator())
            return false;
        auto &insts = bb->getInstructions();
        for (size_t i = 0; i + 1 < insts.size(); ++i)
        {
            if (dynamic_cast<AllocaInst *>(insts[i].get()))
                continue;
            return false;
        }
        auto *br = dynamic_cast<BranchInst *>(bb->getTerminator());
        return br && !br->isConditional();
    }

    bool isInitStubBlock(BasicBlock *bb)
    {
        if (!bb)
            return false;
        if (bb->getInstructions().empty())
            return bb->getSuccessors().size() == 1;
        return isUnconditionalBranchOnly(bb);
    }

    BasicBlock *skipInitStubChain(BasicBlock *start)
    {
        BasicBlock *bb = start;
        unordered_set<BasicBlock *> visited;
        while (bb && isInitStubBlock(bb) && bb->getSuccessors().size() == 1)
        {
            if (!visited.insert(bb).second)
                break;
            BasicBlock *next = bb->getSuccessors()[0];
            if (next == bb)
                break;
            bb = next;
        }
        return bb;
    }

    BasicBlock *findSinkThroughEarlyExit(Function *func, BasicBlock *earlyRet,
                                         const vector<AllocaInst *> &allocas,
                                         const unordered_map<BasicBlock *, BasicBlock *> &idom)
    {
        BasicBlock *entry = func->getEntryBlock();
        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            auto *br = dynamic_cast<BranchInst *>(bb->getTerminator());
            if (!br || !br->isConditional())
                continue;

            BasicBlock *candidate = nullptr;
            if (br->TrueBlock == earlyRet)
                candidate = br->FalseBlock;
            else if (br->FalseBlock == earlyRet)
                candidate = br->TrueBlock;
            else
                continue;

            if (!candidate || candidate == entry)
                continue;

            bool ok = true;
            for (AllocaInst *a : allocas)
            {
                if (!allocaUsesDominatedBy(a, candidate, idom, func))
                {
                    ok = false;
                    break;
                }
            }
            if (ok)
                return candidate;
        }
        return nullptr;
    }

    vector<BasicBlock *> findGatePredsToSink(BasicBlock *sinkBB, BasicBlock *earlyRet)
    {
        vector<BasicBlock *> gates;
        Function *func = sinkBB->Parent;
        if (!func)
            return gates;
        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            auto *br = dynamic_cast<BranchInst *>(bb->getTerminator());
            if (!br || !br->isConditional())
                continue;
            if ((br->TrueBlock == earlyRet && br->FalseBlock == sinkBB) ||
                (br->FalseBlock == earlyRet && br->TrueBlock == sinkBB))
                gates.push_back(bb);
        }
        return gates;
    }

    bool blockUsesAnyAlloca(BasicBlock *bb, const vector<AllocaInst *> &allocas)
    {
        if (!bb)
            return false;
        unordered_set<AllocaInst *> set(allocas.begin(), allocas.end());
        for (auto &instPtr : bb->getInstructions())
        {
            for (Value *op : instPtr->getOperands())
            {
                if (set.count(dynamic_cast<AllocaInst *>(op)))
                    return true;
            }
        }
        return false;
    }

    bool reachableWithoutBlock(BasicBlock *from, BasicBlock *avoid, BasicBlock *target)
    {
        if (!from || !target)
            return false;
        queue<BasicBlock *> q;
        unordered_set<BasicBlock *> visited;
        q.push(from);
        visited.insert(from);
        while (!q.empty())
        {
            BasicBlock *bb = q.front();
            q.pop();
            if (bb == target)
                return true;
            for (BasicBlock *succ : bb->getSuccessors())
            {
                if (succ == avoid || visited.count(succ))
                    continue;
                visited.insert(succ);
                q.push(succ);
            }
        }
        return false;
    }

    bool reachableWithoutUses(BasicBlock *from, BasicBlock *avoid,
                              BasicBlock *target, const vector<AllocaInst *> &allocas)
    {
        if (!from || !target)
            return false;
        queue<BasicBlock *> q;
        unordered_set<BasicBlock *> visited;
        q.push(from);
        visited.insert(from);
        while (!q.empty())
        {
            BasicBlock *bb = q.front();
            q.pop();
            if (bb == target)
                return true;
            for (BasicBlock *succ : bb->getSuccessors())
            {
                if (succ == avoid || visited.count(succ) || blockUsesAnyAlloca(succ, allocas))
                    continue;
                visited.insert(succ);
                q.push(succ);
            }
        }
        return false;
    }

    void detachBranchFromBlock(BasicBlock *from, BranchInst *br)
    {
        if (!from || !br)
            return;
        if (BasicBlock *t = br->getTrueBlock())
        {
            from->removeSuccessor(t);
            t->removePredecessor(from);
        }
        if (br->isConditional())
        {
            if (BasicBlock *f = br->getFalseBlock())
            {
                from->removeSuccessor(f);
                f->removePredecessor(from);
            }
        }
    }

    void wireBranchFromBlock(BasicBlock *from, BranchInst *br)
    {
        if (!from || !br)
            return;
        if (BasicBlock *t = br->getTrueBlock())
        {
            from->addSuccessor(t);
            t->addPredecessor(from);
        }
        if (br->isConditional())
        {
            if (BasicBlock *f = br->getFalseBlock())
            {
                from->addSuccessor(f);
                f->addPredecessor(from);
            }
        }
    }

    void eraseBasicBlockFromFunction(Function *func, BasicBlock *bb)
    {
        if (!func || !bb)
            return;
        vector<BasicBlock *> preds = bb->getPredecessors();
        vector<BasicBlock *> succs = bb->getSuccessors();
        for (BasicBlock *pred : preds)
        {
            pred->removeSuccessor(bb);
            bb->removePredecessor(pred);
        }
        for (BasicBlock *succ : succs)
        {
            bb->removeSuccessor(succ);
            succ->removePredecessor(bb);
        }
        auto &bbs = func->getBasicBlocks();
        for (auto it = bbs.begin(); it != bbs.end(); ++it)
        {
            if (it->get() != bb)
                continue;
            for (auto &instPtr : bb->getInstructions())
                instPtr->removeThisFromOperands();
            bb->clearInstructions();
            bbs.erase(it);
            return;
        }
    }

    void replaceBranchTarget(BasicBlock *pred, BasicBlock *oldSucc, BasicBlock *newSucc)
    {
        auto *br = dynamic_cast<BranchInst *>(pred->getTerminator());
        if (!br)
            return;
        bool changed = false;
        if (br->TrueBlock == oldSucc)
        {
            br->TrueBlock = newSucc;
            changed = true;
        }
        if (br->FalseBlock == oldSucc)
        {
            br->FalseBlock = newSucc;
            changed = true;
        }
        if (!changed)
            return;
        pred->removeSuccessor(oldSucc);
        pred->addSuccessor(newSucc);
        oldSucc->removePredecessor(pred);
        newSucc->addPredecessor(pred);
        for (auto &instPtr : oldSucc->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                phi->replaceIncomingBasicBlock(pred, newSucc);
        }
        for (auto &instPtr : newSucc->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                phi->replaceIncomingBasicBlock(oldSucc, newSucc);
        }
    }

    void insertInstructionsBeforeTerminator(BasicBlock *bb, vector<unique_ptr<Instruction>> &insts)
    {
        auto &dst = bb->getInstructions();
        if (!bb->hasTerminator())
        {
            for (auto &inst : insts)
                dst.push_back(std::move(inst));
            insts.clear();
            return;
        }
        size_t pos = dst.size() - 1;
        for (auto &inst : insts)
            dst.insert(dst.begin() + static_cast<long>(pos++), std::move(inst));
        insts.clear();
    }

    vector<unique_ptr<Instruction>> takeInstructionsExceptTerminator(BasicBlock *bb)
    {
        vector<unique_ptr<Instruction>> out;
        if (!bb)
            return out;
        auto &insts = bb->getInstructions();
        size_t end = bb->hasTerminator() ? insts.size() - 1 : insts.size();
        for (size_t i = 0; i < end; ++i)
            out.push_back(std::move(insts[i]));
        if (end < insts.size())
            insts.erase(insts.begin(), insts.begin() + static_cast<long>(end));
        else
            insts.clear();
        return out;
    }

    unique_ptr<Instruction> takeTerminator(BasicBlock *bb)
    {
        if (!bb || !bb->hasTerminator())
            return nullptr;
        auto &insts = bb->getInstructions();
        unique_ptr<Instruction> term = std::move(insts.back());
        insts.pop_back();
        return term;
    }

    void attachInitStubAfterAllocas(Function *func, BasicBlock *setupBB, BasicBlock *destBB,
                                    const string &suffix)
    {
        auto *initHead = func->addBasicBlock(setupBB->getName() + ".ac_init" + suffix);
        auto *initEnd = func->addBasicBlock(setupBB->getName() + ".ac_init_end" + suffix);

        initHead->addInstruction(make_unique<BranchInst>(initEnd));
        initHead->addSuccessor(initEnd);
        initEnd->addPredecessor(initHead);

        initEnd->addInstruction(make_unique<BranchInst>(destBB));
        initEnd->addSuccessor(destBB);
        destBB->addPredecessor(initEnd);

        setupBB->addInstruction(make_unique<BranchInst>(initHead));
        setupBB->addSuccessor(initHead);
        initHead->addPredecessor(setupBB);
    }

    vector<BasicBlock *> resolveGatePredsAfterHoist(const vector<BasicBlock *> &gatePreds,
                                                    BasicBlock *postInit, BasicBlock *entry)
    {
        vector<BasicBlock *> resolved;
        unordered_set<BasicBlock *> seen;
        for (BasicBlock *pred : gatePreds)
        {
            BasicBlock *effective = (pred == postInit) ? entry : pred;
            if (!effective || !seen.insert(effective).second)
                continue;
            resolved.push_back(effective);
        }
        return resolved;
    }

    bool gatePredBranchesTo(BasicBlock *pred, BasicBlock *target)
    {
        auto *br = dynamic_cast<BranchInst *>(pred->getTerminator());
        if (!br)
            return false;
        return br->getTrueBlock() == target || br->getFalseBlock() == target;
    }

    bool coalesceSameShapeAllocas(Function *func, bool verbose, stringstream &log)
    {
        if (func->isLibraryFunction() || func->getBasicBlocks().empty())
            return false;

        auto idom = ControlFlowAnalysis::analyze(func);
        vector<AllocaSite> sites;

        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            size_t bbIdx = blockIndexInFunction(func, bb);
            auto &insts = bb->getInstructions();
            for (size_t i = 0; i < insts.size(); ++i)
            {
                auto *alloca = dynamic_cast<AllocaInst *>(insts[i].get());
                if (!alloca || !alloca->getIsInitialized())
                    continue;
                if (getAllocaTotalElements(alloca) == 0)
                    continue;
                sites.push_back({alloca, bb, bbIdx, i});
            }
        }

        if (sites.size() < 2)
            return false;

        using GroupKey = pair<size_t, int>;
        map<GroupKey, vector<AllocaSite *>> groups;
        for (auto &site : sites)
        {
            GroupKey key{getAllocaTotalElements(site.inst), site.inst->getAllocatedSize()};
            groups[key].push_back(&site);
        }

        bool changed = false;
        for (auto &[key, group] : groups)
        {
            if (group.size() < 2)
                continue;

            vector<BasicBlock *> blocks;
            blocks.reserve(group.size());
            for (auto *site : group)
                blocks.push_back(site->block);

            BasicBlock *target = findDominatorMeet(idom, blocks);
            if (!target)
                continue;

            bool safe = true;
            for (auto *site : group)
            {
                if (!allocaUsesDominatedBy(site->inst, target, idom, func))
                {
                    safe = false;
                    break;
                }
            }
            if (!safe)
                continue;

            vector<AllocaSite *> toMove;
            for (auto *site : group)
            {
                if (site->block != target)
                    toMove.push_back(site);
            }
            if (toMove.empty())
                continue;

            sort(toMove.begin(), toMove.end(), [](const AllocaSite *a, const AllocaSite *b) {
                if (a->blockIndex != b->blockIndex)
                    return a->blockIndex < b->blockIndex;
                return a->instIndex > b->instIndex;
            });

            size_t insertPos = leadingAllocaEnd(target);
            for (auto *site : toMove)
            {
                BasicBlock *srcBB = site->block;
                auto &srcInsts = srcBB->getInstructions();
                size_t idx = srcInsts.size();
                for (size_t i = 0; i < srcInsts.size(); ++i)
                {
                    if (srcInsts[i].get() == site->inst)
                    {
                        idx = i;
                        break;
                    }
                }
                if (idx >= srcInsts.size())
                    continue;

                AllocaSite moving{site->inst, srcBB, site->blockIndex, idx};
                moveAllocaToBlock(moving, target, insertPos);
                ++insertPos;
                changed = true;

                if (verbose)
                {
                    log << "AllocaCoalescePass: moved " << site->inst->getName() << " ("
                        << key.first << " elems) to " << target->getName() << "\n";
                }
            }
        }

        return changed;
    }

    bool sinkInitializedAllocasPastEarlyExit(Function *func, bool verbose, stringstream &log)
    {
        BasicBlock *entry = func->getEntryBlock();
        if (!entry || !entry->hasTerminator())
            return false;

        auto *entryBr = dynamic_cast<BranchInst *>(entry->getTerminator());
        if (!entryBr || entryBr->isConditional())
            return false;

        vector<AllocaInst *> initAllocas;
        for (auto &instPtr : entry->getInstructions())
        {
            auto *alloca = dynamic_cast<AllocaInst *>(instPtr.get());
            if (alloca && alloca->getIsInitialized() && getAllocaTotalElements(alloca) > 0)
                initAllocas.push_back(alloca);
        }
        if (initAllocas.empty())
            return false;

        BasicBlock *initHead = entryBr->TrueBlock;
        if (!initHead)
            return false;

        BasicBlock *postInit = skipInitStubChain(initHead);
        if (!postInit || postInit == entry || !postInit->hasTerminator())
            return false;

        vector<BasicBlock *> stubBlocks;
        for (BasicBlock *stub = initHead; stub && stub != postInit;)
        {
            stubBlocks.push_back(stub);
            if (stub->getSuccessors().size() != 1)
                break;
            stub = stub->getSuccessors()[0];
        }

        auto idom = ControlFlowAnalysis::analyze(func);

        BasicBlock *earlyRetBB = nullptr;
        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            if (bb == postInit)
                continue;
            bool hasRet = false;
            for (auto &instPtr : bb->getInstructions())
            {
                if (dynamic_cast<ReturnInst *>(instPtr.get()))
                    hasRet = true;
            }
            if (!hasRet || blockUsesAnyAlloca(bb, initAllocas))
                continue;
            if (reachableWithoutUses(postInit, nullptr, bb, initAllocas))
            {
                earlyRetBB = bb;
                break;
            }
        }
        if (!earlyRetBB)
            return false;

        BasicBlock *sinkBB = findSinkThroughEarlyExit(func, earlyRetBB, initAllocas, idom);
        if (!sinkBB || sinkBB == entry || sinkBB == postInit)
            return false;

        if (!reachableWithoutBlock(entry, sinkBB, earlyRetBB))
            return false;

        if (!reachableWithoutUses(postInit, sinkBB, earlyRetBB, initAllocas))
            return false;

        // Real early-exit blocks are not reachable from the sink region (e.g. loop body).
        if (reachableWithoutBlock(sinkBB, nullptr, earlyRetBB))
            return false;

        vector<BasicBlock *> gatePreds = findGatePredsToSink(sinkBB, earlyRetBB);
        if (gatePreds.empty())
            return false;

        for (BasicBlock *pred : gatePreds)
        {
            if (!gatePredBranchesTo(pred, sinkBB))
                return false;
        }

        BasicBlock *allocaSetup = func->addBasicBlock(entry->getName() + ".ac_setup");

        vector<unique_ptr<Instruction>> movedAllocas;
        auto &entryInsts = entry->getInstructions();
        for (size_t i = 0; i < entryInsts.size();)
        {
            if (auto *alloca = dynamic_cast<AllocaInst *>(entryInsts[i].get()))
            {
                if (find(initAllocas.begin(), initAllocas.end(), alloca) != initAllocas.end())
                {
                    movedAllocas.push_back(std::move(entryInsts[i]));
                    entryInsts.erase(entryInsts.begin() + static_cast<long>(i));
                    continue;
                }
            }
            ++i;
        }

        vector<unique_ptr<Instruction>> postInitBody = takeInstructionsExceptTerminator(postInit);
        unique_ptr<Instruction> postInitTerm = takeTerminator(postInit);
        vector<BasicBlock *> postInitSuccs = postInit->getSuccessors();

        detachBranchFromBlock(entry, entryBr);
        entryBr->removeThisFromOperands();
        entryInsts.pop_back();

        insertInstructionsBeforeTerminator(entry, postInitBody);
        if (postInitTerm)
            entry->addInstruction(std::move(postInitTerm));

        for (BasicBlock *succ : postInitSuccs)
        {
            postInit->removeSuccessor(succ);
            succ->removePredecessor(postInit);
            for (auto &instPtr : succ->getInstructions())
            {
                if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                    phi->replaceIncomingBasicBlock(postInit, entry);
            }
        }
        if (auto *newEntryTerm = dynamic_cast<BranchInst *>(entry->getTerminator()))
            wireBranchFromBlock(entry, newEntryTerm);

        for (auto &inst : movedAllocas)
            allocaSetup->addInstruction(std::move(inst));

        attachInitStubAfterAllocas(func, allocaSetup, sinkBB, "");

        vector<BasicBlock *> effectiveGatePreds =
            resolveGatePredsAfterHoist(gatePreds, postInit, entry);
        for (BasicBlock *pred : effectiveGatePreds)
            replaceBranchTarget(pred, sinkBB, allocaSetup);

        BasicBlock *initEnd = nullptr;
        if (!allocaSetup->getSuccessors().empty())
        {
            BasicBlock *initHead = allocaSetup->getSuccessors()[0];
            if (!initHead->getSuccessors().empty())
                initEnd = initHead->getSuccessors()[0];
        }
        if (initEnd)
        {
            for (auto &instPtr : sinkBB->getInstructions())
            {
                if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                {
                    for (BasicBlock *gate : effectiveGatePreds)
                        phi->replaceIncomingBasicBlock(gate, initEnd);
                }
            }
        }

        vector<BasicBlock *> blocksToErase;
        blocksToErase.push_back(postInit);
        for (BasicBlock *stub : stubBlocks)
        {
            if (stub != postInit)
                blocksToErase.push_back(stub);
        }
        for (BasicBlock *bb : blocksToErase)
            eraseBasicBlockFromFunction(func, bb);

        if (verbose)
        {
            log << "AllocaCoalescePass: sank " << initAllocas.size()
                << " initialized alloca(s) to " << allocaSetup->getName()
                << " with init stub before " << sinkBB->getName()
                << " in " << func->getName() << "\n";
        }
        return true;
    }
}

bool AllocaCoalescePass::runOnFunction(Function *func)
{
    if (func->isLibraryFunction() || func->getBasicBlocks().empty())
        return false;

    bool changed = coalesceSameShapeAllocas(func, verbose, debugInfo);
    changed = sinkInitializedAllocasPastEarlyExit(func, verbose, debugInfo) || changed;
    return changed;
}
