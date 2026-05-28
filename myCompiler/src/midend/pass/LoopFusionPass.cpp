#include "LoopFusionPass.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
using namespace std;
using namespace optimization;

namespace
{
    Value *stripCopy(Value *v)
    {
        while (auto *cpy = dynamic_cast<CopyInst *>(v))
            v = cpy->getSource();
        return v;
    }

    bool sameValue(Value *a, Value *b)
    {
        if (!a || !b)
            return false;
        if (stripCopy(a) == stripCopy(b))
            return true;
        if (!a->getName().empty() && a->getName() == b->getName())
            return true;
        return false;
    }

    BranchInst *getTerminatorBranch(BasicBlock *bb)
    {
        if (!bb)
            return nullptr;
        for (auto &instPtr : bb->getInstructions())
        {
            if (auto *br = dynamic_cast<BranchInst *>(instPtr.get()))
                return br;
        }
        return nullptr;
    }

    ICmpInst *getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound)
    {
        ICmpInst *cmp = nullptr;
        iv = nullptr;
        bound = nullptr;
        if (!header)
            return nullptr;
        for (auto &instPtr : header->getInstructions())
        {
            auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get());
            if (!icmp || icmp->getPredicate() != ICmpInst::ICMP_SLT)
                continue;
            cmp = icmp;
            iv = icmp->getLHS();
            bound = icmp->getRHS();
        }
        return cmp;
    }

    PhiInst *findIvPhi(BasicBlock *header, Value *iv)
    {
        if (!header || !iv)
            return nullptr;
        for (auto &instPtr : header->getInstructions())
        {
            auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
            if (phi && (phi == iv || sameValue(phi, iv)))
                return phi;
        }
        return nullptr;
    }

    BinaryOperator *findIVIncrement(BasicBlock *bb, Value *iv)
    {
        if (!bb || !iv)
            return nullptr;
        for (auto &instPtr : bb->getInstructions())
        {
            auto *addInst = dynamic_cast<BinaryOperator *>(instPtr.get());
            if (!addInst || addInst->getOpcode() != Opcode::Add)
                continue;
            auto *one = dynamic_cast<ConstantInt *>(stripCopy(addInst->getRHS()));
            if (!one || one->Value != 1)
                continue;
            if (sameValue(addInst->getLHS(), iv))
                return addInst;
        }
        return nullptr;
    }

    bool blockNameHasUnroll(BasicBlock *bb)
    {
        return bb && bb->getName().find("_unroll_") != string::npos;
    }

    bool isOnlyUnconditionalBranch(BasicBlock *bb)
    {
        if (!bb)
            return false;
        for (auto &instPtr : bb->getInstructions())
        {
            if (dynamic_cast<BranchInst *>(instPtr.get()))
                continue;
            return false;
        }
        auto *br = getTerminatorBranch(bb);
        return br && !br->isConditional();
    }

    struct LoopNestInfo
    {
        BasicBlock *outerHeader = nullptr;
        BasicBlock *outerBody = nullptr;
        BasicBlock *outerExit = nullptr;
        BasicBlock *innerHeader = nullptr;
        BasicBlock *innerBody = nullptr;
        BasicBlock *innerExit = nullptr;
        Value *outerIV = nullptr;
        Value *outerBound = nullptr;
        Value *innerIV = nullptr;
        Value *innerBound = nullptr;
        PhiInst *outerPhi = nullptr;
        PhiInst *innerPhi = nullptr;
        BinaryOperator *innerInc = nullptr;
        BinaryOperator *outerInc = nullptr;
    };

    bool parsePerfectNest(BasicBlock *outerHeader, LoopNestInfo &info)
    {
        if (!outerHeader || blockNameHasUnroll(outerHeader))
            return false;

        Value *outerIV = nullptr;
        Value *outerBound = nullptr;
        auto *outerCmp = getHeaderBoundCmp(outerHeader, outerIV, outerBound);
        auto *outerBr = getTerminatorBranch(outerHeader);
        if (!outerCmp || !outerBr || !outerBr->isConditional() || !outerIV || !outerBound)
            return false;

        BasicBlock *outerBody = outerBr->getTrueBlock();
        BasicBlock *outerExit = outerBr->getFalseBlock();
        if (!outerBody || !outerExit || !isOnlyUnconditionalBranch(outerBody))
            return false;

        auto *toInnerBr = getTerminatorBranch(outerBody);
        BasicBlock *innerHeader = toInnerBr ? toInnerBr->getTrueBlock() : nullptr;
        if (!innerHeader || blockNameHasUnroll(innerHeader))
            return false;

        Value *innerIV = nullptr;
        Value *innerBound = nullptr;
        auto *innerCmp = getHeaderBoundCmp(innerHeader, innerIV, innerBound);
        auto *innerBr = getTerminatorBranch(innerHeader);
        if (!innerCmp || !innerBr || !innerBr->isConditional() || !innerIV || !innerBound)
            return false;

        BasicBlock *innerBody = innerBr->getTrueBlock();
        BasicBlock *innerExit = innerBr->getFalseBlock();
        if (!innerBody || !innerExit)
            return false;

        BinaryOperator *innerInc = findIVIncrement(innerBody, innerIV);
        if (!innerInc)
            return false;

        BinaryOperator *outerInc = findIVIncrement(innerExit, outerIV);
        if (!outerInc)
            return false;

        auto *backBr = getTerminatorBranch(innerExit);
        if (!backBr || backBr->isConditional() || backBr->getTrueBlock() != outerHeader)
            return false;

        info.outerHeader = outerHeader;
        info.outerBody = outerBody;
        info.outerExit = outerExit;
        info.innerHeader = innerHeader;
        info.innerBody = innerBody;
        info.innerExit = innerExit;
        info.outerIV = outerIV;
        info.outerBound = outerBound;
        info.innerIV = innerIV;
        info.innerBound = innerBound;
        info.outerPhi = findIvPhi(outerHeader, outerIV);
        info.innerPhi = findIvPhi(innerHeader, innerIV);
        info.innerInc = innerInc;
        info.outerInc = outerInc;
        return true;
    }

    bool outerInitIsZeroFromBlock(PhiInst *outerPhi, BasicBlock *fromBB)
    {
        if (!outerPhi || !fromBB)
            return false;
        for (unsigned i = 0; i < outerPhi->getNumIncomingValues(); ++i)
        {
            if (outerPhi->getIncomingBlock(i) != fromBB)
                continue;
            auto *c = dynamic_cast<ConstantInt *>(stripCopy(outerPhi->getIncomingValue(i)));
            return c && c->Value == 0;
        }
        return false;
    }

    struct MemAccess
    {
        Value *ptr = nullptr;
        Value *idx0 = nullptr;
        Value *idx1 = nullptr;
        bool isStore = false;
    };

    void collectMemAccesses(BasicBlock *bb, vector<MemAccess> &out)
    {
        if (!bb)
            return;
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (auto *load = dynamic_cast<LoadInst *>(inst))
            {
                Value *ptr = stripCopy(load->getPointer());
                auto *gep = dynamic_cast<GetElementPtrInst *>(ptr);
                MemAccess acc;
                acc.isStore = false;
                acc.ptr = gep ? getOriginalPointerFromAddress(gep) : ptr;
                if (gep && gep->getIndices().size() >= 2)
                {
                    acc.idx0 = gep->getIndices()[0];
                    acc.idx1 = gep->getIndices()[1];
                }
                else if (gep && gep->getIndices().size() == 1)
                    acc.idx0 = gep->getIndices()[0];
                out.push_back(acc);
            }
            else if (auto *store = dynamic_cast<StoreInst *>(inst))
            {
                Value *ptr = stripCopy(store->getPointer());
                auto *gep = dynamic_cast<GetElementPtrInst *>(ptr);
                MemAccess acc;
                acc.isStore = true;
                acc.ptr = gep ? getOriginalPointerFromAddress(gep) : ptr;
                if (gep && gep->getIndices().size() >= 2)
                {
                    acc.idx0 = gep->getIndices()[0];
                    acc.idx1 = gep->getIndices()[1];
                }
                else if (gep && gep->getIndices().size() == 1)
                    acc.idx0 = gep->getIndices()[0];
                out.push_back(acc);
            }
            else if (dynamic_cast<CallInst *>(inst))
            {
                out.push_back({}); // sentinel: call blocks fusion
            }
        }
    }

    bool indicesMatch(Value *a0, Value *a1, Value *b0, Value *b1, Value *outerA, Value *innerA,
                      Value *outerB, Value *innerB)
    {
        auto matchPair = [&](Value *x0, Value *x1) {
            if (x0 && x1)
                return sameValue(x0, outerA) && sameValue(x1, innerA);
            if (x0)
                return sameValue(x0, outerA) || sameValue(x0, innerA);
            return true;
        };
        if (!matchPair(a0, a1))
            return false;
        if (a0 && a1)
            return true;
        if (b0 && b1)
            return matchPair(b0, b1);
        return true;
    }

    bool fusionDependenceOk(const LoopNestInfo &a, const LoopNestInfo &b)
    {
        vector<MemAccess> storesA, loadsB;
        collectMemAccesses(a.innerBody, storesA);
        collectMemAccesses(b.innerBody, loadsB);

        for (const auto &x : storesA)
        {
            if (!x.ptr)
                return false;
        }
        for (const auto &x : loadsB)
        {
            if (!x.ptr)
                return false;
        }

        for (const auto &ld : loadsB)
        {
            for (const auto &st : storesA)
            {
                if (!st.isStore || st.ptr != ld.ptr)
                    continue;
                if (!indicesMatch(st.idx0, st.idx1, ld.idx0, ld.idx1, a.outerIV, a.innerIV,
                                  b.outerIV, b.innerIV))
                    return false;
            }
        }
        return true;
    }

    void remapOperands(Instruction *inst, const unordered_map<Value *, Value *> &vmap)
    {
        for (unsigned i = 0; i < inst->getNumOperands(); ++i)
        {
            Value *op = inst->getOperandByIndex(i);
            auto it = vmap.find(stripCopy(op));
            if (it != vmap.end())
                inst->replaceOperand(op, it->second);
        }
    }

    size_t instructionIndex(BasicBlock *bb, Instruction *target)
    {
        auto &insts = bb->getInstructions();
        for (size_t i = 0; i < insts.size(); ++i)
        {
            if (insts[i].get() == target)
                return i;
        }
        return insts.size();
    }

    void spliceInnerBody(const LoopNestInfo &first, const LoopNestInfo &second,
                         const unordered_map<Value *, Value *> &vmap)
    {
        BasicBlock *dst = first.innerBody;
        BasicBlock *src = second.innerBody;
        size_t insertAt = instructionIndex(dst, first.innerInc);

        vector<unique_ptr<Instruction>> moved;
        auto &srcInsts = src->getInstructions();
        for (auto it = srcInsts.begin(); it != srcInsts.end();)
        {
            Instruction *inst = it->get();
            if (inst == second.innerInc || dynamic_cast<BranchInst *>(inst))
            {
                ++it;
                continue;
            }
            moved.push_back(std::move(*it));
            it = srcInsts.erase(it);
        }

        for (size_t i = moved.size(); i > 0; --i)
        {
            remapOperands(moved[i - 1].get(), vmap);
            dst->insert(std::move(moved[i - 1]), static_cast<unsigned>(insertAt));
        }
    }

    void replacePhiIncomingBlock(Function *func, BasicBlock *oldBB, BasicBlock *newBB)
    {
        for (auto &bbPtr : func->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
                if (!phi)
                    continue;
                for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
                {
                    if (phi->getIncomingBlock(i) == oldBB)
                    {
                        phi->replaceIncomingBasicBlock(oldBB, newBB);
                        break;
                    }
                }
            }
        }
    }

    void spliceOuterBodySetup(const LoopNestInfo &first, const LoopNestInfo &second,
                              const unordered_map<Value *, Value *> &vmap)
    {
        BasicBlock *dst = first.outerBody;
        BasicBlock *src = second.outerBody;
        if (!dst || !src)
            return;

        auto *term = getTerminatorBranch(dst);
        if (!term)
            return;
        size_t insertAt = instructionIndex(dst, term);

        vector<unique_ptr<Instruction>> moved;
        auto &srcInsts = src->getInstructions();
        for (auto it = srcInsts.begin(); it != srcInsts.end();)
        {
            if (dynamic_cast<BranchInst *>(it->get()))
            {
                ++it;
                continue;
            }
            moved.push_back(std::move(*it));
            it = srcInsts.erase(it);
        }

        for (size_t i = moved.size(); i > 0; --i)
        {
            remapOperands(moved[i - 1].get(), vmap);
            dst->insert(std::move(moved[i - 1]), static_cast<unsigned>(insertAt));
        }
    }

    void eraseBlocksFromFunction(Function *func, const vector<BasicBlock *> &blocks,
                                 vector<Value *> &needToDelete)
    {
        unordered_set<BasicBlock *> eraseSet(blocks.begin(), blocks.end());
        BasicBlock *entry = func->getEntryBlock();

        for (auto *bb : blocks)
        {
            if (!bb)
                continue;
            bb->removeSelfBasicBlock();
        }

        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            for (auto &instPtr : bb->getInstructions())
            {
                auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
                if (!phi)
                    continue;
                for (int i = static_cast<int>(phi->getNumIncomingValues()) - 1; i >= 0; --i)
                {
                    if (eraseSet.count(phi->getIncomingBlock(static_cast<unsigned>(i))))
                        phi->removeIncoming(static_cast<unsigned>(i));
                }
                if (phi->getNumIncomingValues() == 1)
                {
                    Value *v = phi->getIncomingValue(0);
                    phi->replaceAllUsesWith(v);
                    phi->removeThisFromOperands();
                }
            }
        }

        auto &bbs = func->getBasicBlocks();
        for (auto it = bbs.begin(); it != bbs.end();)
        {
            BasicBlock *bb = it->get();
            if (bb != entry && eraseSet.count(bb))
            {
                needToDelete.push_back(it->release());
                it = bbs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}

bool LoopFusionPass::tryFuseAdjacentNests(Function *func)
{
    for (auto &bbPtr : func->getBasicBlocks())
    {
        LoopNestInfo first;
        if (!parsePerfectNest(bbPtr.get(), first))
            continue;

        auto *exitBr = getTerminatorBranch(first.outerExit);
        if (!exitBr || exitBr->isConditional())
            continue;
        BasicBlock *nextBB = exitBr->getTrueBlock();
        if (!nextBB)
            continue;
        bool adjacent = false;
        for (BasicBlock *pred : nextBB->getPredecessors())
        {
            if (pred == first.outerExit)
            {
                adjacent = true;
                break;
            }
        }
        if (!adjacent)
            continue;

        LoopNestInfo second;
        if (!parsePerfectNest(nextBB, second))
            continue;

        if (!sameValue(first.outerBound, second.outerBound) ||
            !sameValue(first.innerBound, second.innerBound))
            continue;

        if (!outerInitIsZeroFromBlock(second.outerPhi, first.outerExit))
            continue;

        if (!fusionDependenceOk(first, second))
            continue;

        unordered_map<Value *, Value *> vmap;
        vmap[stripCopy(second.outerIV)] = stripCopy(first.outerIV);
        vmap[stripCopy(second.innerIV)] = stripCopy(first.innerIV);
        if (second.outerPhi && first.outerPhi)
            vmap[second.outerPhi] = first.outerPhi;
        if (second.innerPhi && first.innerPhi)
            vmap[second.innerPhi] = first.innerPhi;

        spliceInnerBody(first, second, vmap);
        spliceOuterBodySetup(first, second, vmap);

        auto *secondOuterExitBr = getTerminatorBranch(second.outerExit);
        if (!secondOuterExitBr)
            continue;
        BasicBlock *afterFused = secondOuterExitBr->getTrueBlock();

        replacePhiIncomingBlock(func, second.outerExit, first.outerExit);
        removePhiIncomingFromPredecessor(second.outerHeader, first.outerExit);

        exitBr->setTrueBlock(afterFused);
        first.outerExit->removeSuccessor(second.outerHeader);
        second.outerHeader->removePredecessor(first.outerExit);
        afterFused->removePredecessor(second.outerExit);
        afterFused->addPredecessor(first.outerExit);

        vector<BasicBlock *> fusedAway = {second.outerHeader, second.outerBody, second.outerExit,
                                          second.innerHeader, second.innerBody, second.innerExit};
        eraseBlocksFromFunction(func, fusedAway, needToDelete);

        if (verbose)
        {
            debugInfo << "LoopFusion: fused " << first.outerHeader->getName() << " with "
                      << second.outerHeader->getName() << "\n";
        }
        return true;
    }
    return false;
}

bool LoopFusionPass::runOnFunction(Function *func)
{
    bool changed = false;
    while (tryFuseAdjacentNests(func))
        changed = true;
    if (changed)
        func->setLoops(ControlFlowAnalysis::findLoops(func));
    return changed;
}
