#include "LoopIfGuardHoistPass.h"
#include <algorithm>
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
        if (stripCopy(a) == stripCopy(b))
            return true;
        if (a && b && !a->getName().empty() && a->getName() == b->getName())
            return true;
        return false;
    }

    BranchInst *getConditionalBranch(BasicBlock *bb)
    {
        if (!bb)
            return nullptr;
        for (auto &instPtr : bb->getInstructions())
        {
            auto *br = dynamic_cast<BranchInst *>(instPtr.get());
            if (br && br->isConditional())
                return br;
        }
        return nullptr;
    }

    ICmpInst *getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound)
    {
        ICmpInst *cmp = nullptr;
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
        for (auto &instPtr : header->getInstructions())
        {
            auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
            if (phi && (phi == iv || sameValue(phi, iv)))
                return phi;
        }
        return nullptr;
    }

    Value *getInnerIncrementSource(BasicBlock *bb, Value *iv)
    {
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

    BasicBlock *findLatchBlock(BasicBlock *header, const Loop &loop, Value *iv)
    {
        for (auto *pred : header->getPredecessors())
        {
            if (!loop.containsBlock(pred))
                continue;
            if (getInnerIncrementSource(pred, iv))
                return pred;
        }
        return nullptr;
    }

    bool isLoopInvariant(Value *val, const Loop &loop)
    {
        if (!val)
            return false;
        auto *def = dynamic_cast<Instruction *>(val);
        if (!def)
            return true;
        return !loop.containsInst(def);
    }

    bool canHoistFromLoopBody(Instruction *inst, const Loop &loop)
    {
        if (!inst)
            return false;
        if (isLoopInvariant(inst, loop))
            return true;
        if (!loop.containsInst(inst))
            return false;
        for (Value *op : inst->getOperands())
        {
            if (!isLoopInvariant(op, loop))
                return false;
        }
        return true;
    }

    bool hasSideEffectInBlock(BasicBlock *bb, BasicBlock *latch, Value *iv)
    {
        if (!bb)
            return false;
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (dynamic_cast<BranchInst *>(inst))
                continue;
            if (inst == getInnerIncrementSource(bb, iv))
                continue;
            if (dynamic_cast<CallInst *>(inst) || dynamic_cast<StoreInst *>(inst) ||
                dynamic_cast<LoadInst *>(inst))
                return true;
            if (auto *br = dynamic_cast<BranchInst *>(inst))
                (void)br;
            if (inst->getOpcode() == Opcode::GetElementPtr)
                return true;
        }
        return false;
    }

    bool isSkipOnlyLatch(BasicBlock *bb, BasicBlock *latch, Value *iv)
    {
        if (!bb)
            return false;
        if (bb == latch)
            return !hasSideEffectInBlock(bb, latch, iv);
        if (bb->getSuccessors().size() != 1 || bb->getSuccessors()[0] != latch)
            return false;
        return !hasSideEffectInBlock(bb, latch, iv);
    }

    struct GuardPattern
    {
        ICmpInst::Predicate pred = ICmpInst::ICMP_SLT;
        Value *threshold = nullptr;
        bool workOnTrue = true;
    };

    bool parseGuardCmp(ICmpInst *cmp, Value *iv, GuardPattern &pat)
    {
        if (!cmp || !iv)
            return false;
        pat.pred = cmp->getPredicate();
        if (sameValue(cmp->getLHS(), iv))
        {
            pat.threshold = cmp->getRHS();
            pat.workOnTrue = (pat.pred == ICmpInst::ICMP_SLT || pat.pred == ICmpInst::ICMP_SLE);
            return true;
        }
        if (sameValue(cmp->getRHS(), iv))
        {
            pat.threshold = cmp->getLHS();
            pat.workOnTrue = (pat.pred == ICmpInst::ICMP_SGT || pat.pred == ICmpInst::ICMP_SGE ||
                              pat.pred == ICmpInst::ICMP_SLE);
            return true;
        }
        return false;
    }

    void moveInvariantInstsBeforeGuard(BasicBlock *body, BasicBlock *preheader, BranchInst *guardBr,
                                     const Loop &loop)
    {
        if (!body || !preheader || !guardBr)
            return;
        vector<unique_ptr<Instruction>> moved;
        auto &insts = body->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            if (it->get() == guardBr)
                break;
            Instruction *inst = it->get();
            if (!canHoistFromLoopBody(inst, loop))
            {
                ++it;
                continue;
            }
            moved.push_back(unique_ptr<Instruction>(it->release()));
            it = insts.erase(it);
        }
        for (auto &m : moved)
            preheader->insertBeforeTerminator(std::move(m));
    }

    static bool usesOnlyValue(Value *v, Value *root, unordered_set<Value *> &vis)
    {
        v = stripCopy(v);
        if (sameValue(v, root))
            return true;
        if (dynamic_cast<ConstantInt *>(v))
            return true;
        auto *def = dynamic_cast<Instruction *>(v);
        if (!def || vis.count(def))
            return false;
        vis.insert(def);
        for (Value *op : def->getOperands())
        {
            if (!usesOnlyValue(op, root, vis))
                return false;
        }
        return true;
    }

    // sra(add(bound, and(sra(bound,31),1)), 1) 等 strength-reduced T/2
    static bool isStrengthReducedDiv2Of(Value *threshold, Value *bound)
    {
        auto *sra1 = dynamic_cast<BinaryOperator *>(stripCopy(threshold));
        if (!sra1 || sra1->getOpcode() != Opcode::Sra)
            return false;
        auto *sh1 = dynamic_cast<ConstantInt *>(stripCopy(sra1->getRHS()));
        if (!sh1 || sh1->Value != 1)
            return false;

        auto *add = dynamic_cast<BinaryOperator *>(stripCopy(sra1->getLHS()));
        if (!add || add->getOpcode() != Opcode::Add)
            return false;

        unordered_set<Value *> vis;
        if (!usesOnlyValue(add, bound, vis))
            return false;
        return true;
    }

    // 在 i < T 且 guard 为 i < thresh 时，判断 thresh 是否恒有 thresh < T（可直接用 thresh 作上界）
    static bool thresholdStrictlyLessThanBound(Value *threshold, Value *bound)
    {
        threshold = stripCopy(threshold);
        bound = stripCopy(bound);
        if (sameValue(threshold, bound))
            return false;

        if (auto *cTh = dynamic_cast<ConstantInt *>(threshold))
        {
            if (auto *cBd = dynamic_cast<ConstantInt *>(bound))
                return cTh->Value < cBd->Value;
            return cTh->Value <= 0;
        }

        if (auto *sdiv = dynamic_cast<BinaryOperator *>(threshold))
        {
            if (sdiv->getOpcode() == Opcode::SDiv && sameValue(sdiv->getLHS(), bound))
            {
                if (auto *c = dynamic_cast<ConstantInt *>(stripCopy(sdiv->getRHS())))
                {
                    if (c->Value >= 2)
                        return true;
                    if (c->Value == 1)
                        return false;
                }
            }
        }

        if (isStrengthReducedDiv2Of(threshold, bound))
            return true;

        if (auto *sub = dynamic_cast<BinaryOperator *>(threshold))
        {
            if (sub->getOpcode() == Opcode::Sub && sameValue(sub->getLHS(), bound))
            {
                if (auto *c = dynamic_cast<ConstantInt *>(stripCopy(sub->getRHS())))
                    return c->Value > 0;
            }
        }

        return false;
    }

    void tightenUpperBound(BasicBlock *preheader, ICmpInst *headerCmp, Value *bound,
                           Value *threshold, bool &usedDirectThreshold)
    {
        usedDirectThreshold = false;
        if (thresholdStrictlyLessThanBound(threshold, bound))
        {
            headerCmp->replaceOperand(bound, threshold);
            usedDirectThreshold = true;
            return;
        }
        auto *useTight = new ICmpInst(ICmpInst::ICMP_SLT, threshold, bound, "ifguard_tight_sel");
        auto *tripBound = new SelectInst(useTight, threshold, bound, "ifguard_tight_bound");
        preheader->insertBeforeTerminator(unique_ptr<Instruction>(useTight));
        preheader->insertBeforeTerminator(unique_ptr<Instruction>(tripBound));
        headerCmp->replaceOperand(bound, tripBound);
    }

    void setPhiEntryInit(PhiInst *phi, BasicBlock *latch, Value *newInit)
    {
        if (!phi)
            return;
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            if (phi->getIncomingBlock(i) != latch)
                phi->setIncomingValue(i, newInit);
        }
    }

    void setLoopEntryInit(Value *loopIv, PhiInst *ivPhi, BasicBlock *latch, BasicBlock *preheader,
                          Value *newInit)
    {
        if (ivPhi)
        {
            setPhiEntryInit(ivPhi, latch, newInit);
            return;
        }
        for (auto &instPtr : preheader->getInstructions())
        {
            auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
            if (!cpy || !sameValue(cpy, loopIv))
                continue;
            cpy->replaceOperand(cpy->getSource(), newInit);
            return;
        }
    }

} // namespace

bool LoopIfGuardHoistPass::tryHoistIfGuard(Function *func, const Loop &loop)
{
    (void)func;
    BasicBlock *header = loop.header;
    if (!header)
        return false;
    Value *iv = nullptr;
    Value *bound = nullptr;
    ICmpInst *headerCmp = getHeaderBoundCmp(header, iv, bound);
    if (!headerCmp || !iv || !bound)
        return false;

    auto *headerBr = dynamic_cast<BranchInst *>(header->getTerminator());
    if (!headerBr || !headerBr->isConditional())
        return false;

    BasicBlock *bodyBB = headerBr->getTrueBlock();
    if (!bodyBB || !loop.containsBlock(bodyBB) || bodyBB == header)
        return false;

    PhiInst *ivPhi = findIvPhi(header, iv);
    Value *loopIv = ivPhi ? static_cast<Value *>(ivPhi) : iv;

    BasicBlock *latch = findLatchBlock(header, loop, loopIv);
    if (!latch || !loop.containsBlock(latch))
        return false;

    BranchInst *guardBr = getConditionalBranch(bodyBB);
    if (!guardBr)
        return false;

    auto *guardCmp = dynamic_cast<ICmpInst *>(guardBr->getCondition());
    if (!guardCmp)
        return false;

    GuardPattern pat;
    if (!parseGuardCmp(guardCmp, loopIv, pat) || !pat.threshold)
        return false;

    BasicBlock *trueBB = guardBr->getTrueBlock();
    BasicBlock *falseBB = guardBr->getFalseBlock();
    if (!trueBB || !falseBB || trueBB == falseBB)
        return false;

    BasicBlock *workBB = pat.workOnTrue ? trueBB : falseBB;
    BasicBlock *skipBB = pat.workOnTrue ? falseBB : trueBB;

    if (!isSkipOnlyLatch(skipBB, latch, loopIv))
        return false;
    if (!loop.containsBlock(workBB) || workBB == header)
        return false;
    if (isSkipOnlyLatch(workBB, latch, loopIv))
        return false;

    Loop loopCopy = loop;
    loopCopy.computePreheader();
    BasicBlock *preheader = loopCopy.getPreheader();
    if (!preheader)
        return false;

    moveInvariantInstsBeforeGuard(bodyBB, preheader, guardBr, loop);
    if (!isLoopInvariant(pat.threshold, loop))
        return false;

    bool tightenUpper = false;
    bool raiseInit = false;

    if (pat.pred == ICmpInst::ICMP_SLT && pat.workOnTrue)
        tightenUpper = true;
    else if ((pat.pred == ICmpInst::ICMP_SGE || pat.pred == ICmpInst::ICMP_SLE) && pat.workOnTrue)
        raiseInit = true;
    else if (pat.pred == ICmpInst::ICMP_SGT && !pat.workOnTrue)
        tightenUpper = true;
    else if (pat.pred == ICmpInst::ICMP_SLE && !pat.workOnTrue)
        tightenUpper = true;
    else
        return false;

    bool usedDirectThreshold = false;
    if (tightenUpper)
        tightenUpperBound(preheader, headerCmp, bound, pat.threshold, usedDirectThreshold);
    if (raiseInit)
        setLoopEntryInit(loopIv, ivPhi, latch, preheader, pat.threshold);

    {
        BasicBlock *dropBB = (guardBr->getTrueBlock() == workBB) ? guardBr->getFalseBlock()
                                                                 : guardBr->getTrueBlock();
        bodyBB->removeSuccessor(dropBB);
        dropBB->removePredecessor(bodyBB);
        removePhiIncomingFromPredecessor(dropBB, bodyBB);

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
        bodyBB->insertBeforeTerminator(unique_ptr<Instruction>(new BranchInst(workBB)));
        if (std::find(bodyBB->getSuccessors().begin(), bodyBB->getSuccessors().end(), workBB) ==
            bodyBB->getSuccessors().end())
        {
            bodyBB->addSuccessor(workBB);
            workBB->addPredecessor(bodyBB);
        }
    }

    if (verbose)
    {
        debugInfo << "LoopIfGuardHoist: " << header->getName();
        if (tightenUpper)
            debugInfo << (usedDirectThreshold ? " bound := threshold" : " bound := min(T,threshold)");
        if (raiseInit)
            debugInfo << " raised iv init";
        debugInfo << ", guard -> work " << workBB->getName() << "\n";
    }
    return true;
}

bool LoopIfGuardHoistPass::runOnFunction(Function *func)
{
    if (!func || func->isLibraryFunction())
        return false;

    bool changed = false;
    func->setLoops(ControlFlowAnalysis::findLoops(func));

    vector<const Loop *> order;
    for (const auto &lp : func->getLoops())
        order.push_back(&lp);
    sort(order.begin(), order.end(), [](const Loop *a, const Loop *b) {
        return a->blocks.size() < b->blocks.size();
    });

    for (const Loop *lp : order)
    {
        if (tryHoistIfGuard(func, *lp))
            changed = true;
    }

    if (changed)
    {
        func->setLoops(ControlFlowAnalysis::findLoops(func));
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
