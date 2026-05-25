#include "LoopSkipContinueElimPass.h"
#include <algorithm>
using namespace std;
using namespace optimization;

namespace
{
    static Value *stripCopy(Value *v)
    {
        while (auto *cpy = dynamic_cast<CopyInst *>(v))
        {
            v = cpy->getSource();
        }
        return v;
    }

    static bool sameValue(Value *a, Value *b)
    {
        return stripCopy(a) == stripCopy(b);
    }

    static const Loop *findParentLoop(const Loop &inner, const vector<Loop> &loops)
    {
        const Loop *best = nullptr;
        size_t bestSize = 0;
        for (const auto &cand : loops)
        {
            if (&cand == &inner || cand.header == inner.header)
            {
                continue;
            }
            if (!cand.containsBlock(inner.header))
            {
                continue;
            }
            if (!best || cand.blocks.size() < bestSize)
            {
                best = &cand;
                bestSize = cand.blocks.size();
            }
        }
        return best;
    }

    static ICmpInst *getHeaderBoundCmp(BasicBlock *header, Value *&innerIV, Value *&bound)
    {
        ICmpInst *cmp = nullptr;
        for (auto &instPtr : header->getInstructions())
        {
            auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get());
            if (!icmp || icmp->getPredicate() != ICmpInst::ICMP_SLT)
            {
                continue;
            }
            cmp = icmp;
            innerIV = icmp->getLHS();
            bound = icmp->getRHS();
        }
        return cmp;
    }

    static BranchInst *getConditionalBranch(BasicBlock *bb)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            if (auto *br = dynamic_cast<BranchInst *>(instPtr.get()))
            {
                if (br->isConditional())
                {
                    return br;
                }
            }
        }
        return nullptr;
    }

    static Value *getInnerIncrementSource(BasicBlock *bb, Value *innerIV)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            auto *addInst = dynamic_cast<BinaryOperator *>(instPtr.get());
            if (!addInst || addInst->getOpcode() != Opcode::Add)
            {
                continue;
            }
            auto *one = dynamic_cast<ConstantInt *>(addInst->getRHS());
            if (!one || one->Value != 1)
            {
                continue;
            }
            if (sameValue(addInst->getLHS(), innerIV))
            {
                return addInst;
            }
        }
        return nullptr;
    }

    static bool isSkipContinueBlock(BasicBlock *skipBB, BasicBlock *innerHeader, Value *innerIV)
    {
        if (!skipBB || !innerHeader)
        {
            return false;
        }
        if (skipBB->getSuccessors().size() != 1 || skipBB->getSuccessors()[0] != innerHeader)
        {
            return false;
        }
        return getInnerIncrementSource(skipBB, innerIV) != nullptr;
    }

    static PhiInst *findInnerPhiAtHeader(BasicBlock *header, Value *innerIV)
    {
        for (auto &instPtr : header->getInstructions())
        {
            auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
            if (phi && (phi == innerIV || sameValue(phi, innerIV)))
            {
                return phi;
            }
        }
        return nullptr;
    }

    static void removeSkipPhiIncoming(PhiInst *innerPhi, BasicBlock *skipBB)
    {
        if (!innerPhi)
        {
            return;
        }
        for (int i = static_cast<int>(innerPhi->getNumIncomingValues()) - 1; i >= 0; --i)
        {
            if (innerPhi->getIncomingBlock(static_cast<unsigned>(i)) == skipBB)
            {
                innerPhi->removeIncoming(static_cast<unsigned>(i));
            }
        }
    }

    static unsigned instructionIndex(BasicBlock *bb, Instruction *target)
    {
        auto &insts = bb->getInstructions();
        for (unsigned i = 0; i < insts.size(); ++i)
        {
            if (insts[i].get() == target)
            {
                return i;
            }
        }
        return insts.size();
    }

    // if (outer < inner) continue  =>  仅执行 j<=outer，即 j < min(rowsize, outer+1)
    static void tightenInnerLoopBound(BasicBlock *header, ICmpInst *headerCmp, Value *outerIV, Value *rowsizeBound)
    {
        auto *one = new ConstantInt(IntegerType::getInstance(), 1);
        auto *outerPlus1 = new BinaryOperator(Opcode::Add, outerIV, one, "outer_iv_plus_1_lsc");
        auto *useTightBound = new ICmpInst(ICmpInst::ICMP_SLT, outerPlus1, rowsizeBound, "inner_tight_sel_lsc");
        auto *tripBound = new SelectInst(useTightBound, outerPlus1, rowsizeBound, "inner_trip_bound_lsc");

        unsigned idx = instructionIndex(header, headerCmp);
        header->insert(unique_ptr<Instruction>(outerPlus1), idx);
        header->insert(unique_ptr<Instruction>(useTightBound), idx + 1);
        header->insert(unique_ptr<Instruction>(tripBound), idx + 2);
        headerCmp->replaceOperand(rowsizeBound, tripBound);
    }
}

bool LoopSkipContinueElimPass::tryEliminate(Function *func, const Loop &innerLoop, const Loop &outerLoop)
{
    (void)func;
    (void)outerLoop;
    BasicBlock *header = innerLoop.header;
    if (!header)
    {
        return false;
    }

    Value *innerIV = nullptr;
    Value *bound = nullptr;
    ICmpInst *headerCmp = getHeaderBoundCmp(header, innerIV, bound);
    if (!headerCmp || !innerIV || !bound)
    {
        return false;
    }

    auto *headerBr = dynamic_cast<BranchInst *>(header->getTerminator());
    if (!headerBr || !headerBr->isConditional())
    {
        return false;
    }
    BasicBlock *bodyBB = headerBr->getTrueBlock();
    if (!bodyBB || !innerLoop.containsBlock(bodyBB))
    {
        return false;
    }

    BranchInst *guardBr = getConditionalBranch(bodyBB);
    if (!guardBr)
    {
        return false;
    }
    auto *guardCmp = dynamic_cast<ICmpInst *>(guardBr->getCondition());
    if (!guardCmp || guardCmp->getPredicate() != ICmpInst::ICMP_SLT)
    {
        return false;
    }

    bool skipOnTrue = false;
    Value *outerIV = nullptr;
    if (sameValue(guardCmp->getLHS(), innerIV))
    {
        outerIV = guardCmp->getRHS();
        skipOnTrue = false;
    }
    else if (sameValue(guardCmp->getRHS(), innerIV))
    {
        outerIV = guardCmp->getLHS();
        skipOnTrue = true;
    }
    else if (sameValue(stripCopy(guardCmp->getLHS()), stripCopy(innerIV)))
    {
        outerIV = guardCmp->getRHS();
        innerIV = guardCmp->getLHS();
        skipOnTrue = false;
    }
    else if (sameValue(stripCopy(guardCmp->getRHS()), stripCopy(innerIV)))
    {
        outerIV = guardCmp->getLHS();
        innerIV = guardCmp->getRHS();
        skipOnTrue = true;
    }
    else
    {
        return false;
    }

    BasicBlock *skipBB = skipOnTrue ? guardBr->getTrueBlock() : guardBr->getFalseBlock();
    BasicBlock *mergeBB = skipOnTrue ? guardBr->getFalseBlock() : guardBr->getTrueBlock();
    if (!skipBB || !mergeBB || skipBB == mergeBB)
    {
        return false;
    }
    if (!isSkipContinueBlock(skipBB, header, innerIV) || !innerLoop.containsBlock(mergeBB))
    {
        return false;
    }

    tightenInnerLoopBound(header, headerCmp, outerIV, bound);
    removeSkipPhiIncoming(findInnerPhiAtHeader(header, innerIV), skipBB);

    guardBr->removeThisFromOperands();
    auto &bodyInsts = bodyBB->getInstructions();
    for (auto it = bodyInsts.begin(); it != bodyInsts.end(); ++it)
    {
        if (it->get() == guardBr)
        {
            bodyInsts.erase(it);
            break;
        }
    }
    bodyBB->insertBeforeTerminator(unique_ptr<Instruction>(new BranchInst(mergeBB)));

    removePhiIncomingFromPredecessor(header, skipBB);
    skipBB->removeSelfBasicBlock();
    skipBB->clearInstructions();

    return true;
}

bool LoopSkipContinueElimPass::runOnFunction(Function *func)
{
    if (!func)
    {
        return false;
    }

    func->setLoops(ControlFlowAnalysis::findLoops(func));
    const vector<Loop> &loops = func->getLoops();
    bool changed = false;

    for (const auto &inner : loops)
    {
        const Loop *outer = findParentLoop(inner, loops);
        if (!outer)
        {
            continue;
        }
        if (this->tryEliminate(func, inner, *outer))
        {
            changed = true;
            if (verbose)
            {
                debugInfo << "LoopSkipContinueElim: inner loop " << inner.header->getName()
                          << " trip bound := min(rowsize, outer+1), continue removed\n";
            }
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
