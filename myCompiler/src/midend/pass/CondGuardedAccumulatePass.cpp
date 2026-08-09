#include "CondGuardedAccumulatePass.h"
using namespace std;
using namespace optimization;

namespace
{
    static Value *stripCopy(Value *v)
    {
        while (auto *cpy = dynamic_cast<CopyInst *>(v))
            v = cpy->getSource();
        return v;
    }

    static unique_ptr<Instruction> own(Instruction *inst)
    {
        return unique_ptr<Instruction>(inst);
    }

    static BranchInst *getTerminator(BasicBlock *bb)
    {
        if (!bb || bb->getInstructions().empty())
            return nullptr;
        return dynamic_cast<BranchInst *>(bb->getInstructions().back().get());
    }

    static BinaryOperator *findThenAddInst(BasicBlock *thenBB, Value *&acc, Value *&addend)
    {
        acc = nullptr;
        addend = nullptr;
        BinaryOperator *addInst = nullptr;
        for (auto it = thenBB->getInstructions().rbegin(); it != thenBB->getInstructions().rend(); ++it)
        {
            if (dynamic_cast<BranchInst *>(it->get()))
                continue;
            if (auto *cpy = dynamic_cast<CopyInst *>(it->get()))
            {
                addInst = dynamic_cast<BinaryOperator *>(stripCopy(cpy->getSource()));
                if (addInst && addInst->getOpcode() == Opcode::Add)
                    break;
                continue;
            }
            addInst = dynamic_cast<BinaryOperator *>(it->get());
            if (addInst && addInst->getOpcode() == Opcode::Add)
                break;
            return nullptr;
        }
        if (!addInst)
            return nullptr;
        acc = stripCopy(addInst->getLHS());
        addend = stripCopy(addInst->getRHS());
        if (acc == addend)
            return nullptr;
        return addInst;
    }

    static void hoistThenCompute(BasicBlock *bb, BasicBlock *thenBB, BinaryOperator *addInst)
    {
        for (auto it = thenBB->getInstructions().begin(); it != thenBB->getInstructions().end();)
        {
            if ((*it)->Op == Opcode::Br || dynamic_cast<CopyInst *>(it->get()) ||
                it->get() == addInst)
            {
                ++it;
                continue;
            }
            bb->insertBeforeTerminator(unique_ptr<Instruction>(it->release()));
            it = thenBB->getInstructions().erase(it);
        }
    }

    static bool isLoopLatchMerge(BasicBlock *mergeBB, BasicBlock *&loopHeader)
    {
        if (!mergeBB || mergeBB->getSuccessors().size() != 1)
            return false;
        loopHeader = mergeBB->getSuccessors()[0];
        if (!loopHeader)
            return false;

        bool mergeIsPred = false;
        for (auto *pred : loopHeader->getPredecessors())
        {
            if (pred == mergeBB)
                mergeIsPred = true;
        }
        if (!mergeIsPred)
            return false;

        for (auto &instPtr : mergeBB->getInstructions())
        {
            if (dynamic_cast<BranchInst *>(instPtr.get()))
                continue;
            if (dynamic_cast<PhiInst *>(instPtr.get()))
                continue;
            if (auto *add = dynamic_cast<BinaryOperator *>(instPtr.get()))
            {
                if (add->getOpcode() == Opcode::Add)
                    continue;
            }
            if (dynamic_cast<CopyInst *>(instPtr.get()))
                continue;
            return false;
        }
        return true;
    }

    static void finalizeConversion(BasicBlock *bb, BasicBlock *thenBB, BasicBlock *mergeBB,
                                   BasicBlock *loopHeader,
                                   Value *cond, Value *acc, Value *addend, BinaryOperator *addInst,
                                   PhiInst *accPhi, Value *accPhiValue, CopyInst *mergeAccCopy)
    {
        auto *br = getTerminator(bb);
        br->removeThisFromOperands();
        bb->getInstructions().pop_back();

        hoistThenCompute(bb, thenBB, addInst);

        auto *scaled = new BinaryOperator(Opcode::Mul, cond, addend, "cga_scaled");
        bb->insertBeforeTerminator(own(scaled));
        auto *newAcc = new BinaryOperator(Opcode::Add, acc, scaled, "cga_new_acc");
        bb->insertBeforeTerminator(own(newAcc));

        for (auto it = mergeBB->getInstructions().begin(); it != mergeBB->getInstructions().end();)
        {
            if (dynamic_cast<BranchInst *>(it->get()))
            {
                ++it;
                continue;
            }
            if (accPhi && it->get() == accPhi)
            {
                ++it;
                continue;
            }
            if (mergeAccCopy && it->get() == mergeAccCopy)
            {
                bb->addInstruction(own(new CopyInst(newAcc, mergeAccCopy->getName() + "_cga")));
                ++it;
                continue;
            }
            bb->addInstruction(unique_ptr<Instruction>(it->release()));
            it = mergeBB->getInstructions().erase(it);
        }

        bb->addInstruction(make_unique<BranchInst>(loopHeader));
        bb->addSuccessor(loopHeader);
        loopHeader->addPredecessor(bb);

        if (accPhi)
            accPhi->replaceAllUsesWith(newAcc);

        for (auto &instPtr : loopHeader->getInstructions())
        {
            auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
            if (!phi)
                continue;
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                if (phi->getIncomingBlock(i) != mergeBB)
                    continue;
                phi->setIncomingBlock(i, bb);
                if (accPhi && phi->getIncomingValue(i) == accPhi)
                    phi->setIncomingValue(i, newAcc);
                else if (accPhiValue && phi->getIncomingValue(i) == accPhiValue)
                    phi->setIncomingValue(i, newAcc);
            }
        }

        mergeBB->removePredecessor(bb);
        mergeBB->removePredecessor(thenBB);
        bb->removeSuccessor(mergeBB);
        bb->removeSuccessor(thenBB);
        loopHeader->removePredecessor(mergeBB);

        thenBB->removeSelfBasicBlock();
        mergeBB->removeSelfBasicBlock();
        // clear 会析构指令，但析构不自动 removeThisFromOperands，
        // 残留指令的 operand（如循环 phi）的 users 列表会留下悬垂指针。
        for (auto &instPtr : thenBB->getInstructions())
            instPtr->removeThisFromOperands();
        for (auto &instPtr : mergeBB->getInstructions())
            instPtr->removeThisFromOperands();
        thenBB->getInstructions().clear();
        mergeBB->getInstructions().clear();
    }
}

Value *CondGuardedAccumulatePass::stripCopy(Value *v)
{
    while (auto *cpy = dynamic_cast<CopyInst *>(v))
        v = cpy->getSource();
    return v;
}

bool CondGuardedAccumulatePass::isSideEffectFree(BasicBlock *bb)
{
    for (auto &instPtr : bb->getInstructions())
    {
        if (dynamic_cast<BranchInst *>(instPtr.get()))
            continue;
        if (instPtr->mayHaveSideEffects())
            return false;
    }
    return true;
}

bool CondGuardedAccumulatePass::tryConvert(Function *func, BasicBlock *bb)
{
    (void)func;
    auto *br = getTerminator(bb);
    if (!br || !br->isConditional())
        return false;

    BasicBlock *thenBB = br->getTrueBlock();
    BasicBlock *mergeBB = br->getFalseBlock();
    if (!thenBB || !mergeBB)
        return false;
    if (thenBB->getSuccessors().size() != 1 || thenBB->getSuccessors()[0] != mergeBB)
        return false;
    if (!isSideEffectFree(thenBB))
        return false;

    Value *cond = br->getCondition();
    Value *acc = nullptr;
    Value *addend = nullptr;
    BinaryOperator *addInst = findThenAddInst(thenBB, acc, addend);
    if (!addInst)
        return false;
    Value *addResult = stripCopy(addInst);

    BasicBlock *loopHeader = nullptr;
    if (!isLoopLatchMerge(mergeBB, loopHeader))
        return false;

    for (auto &instPtr : mergeBB->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi || phi->getNumIncomingValues() != 2)
            continue;
        Value *fromBBVal = nullptr;
        Value *fromThenVal = nullptr;
        for (unsigned i = 0; i < 2; ++i)
        {
            if (phi->getIncomingBlock(i) == bb)
                fromBBVal = phi->getIncomingValue(i);
            else if (phi->getIncomingBlock(i) == thenBB)
                fromThenVal = phi->getIncomingValue(i);
        }
        if (!fromBBVal || !fromThenVal)
            continue;
        if (stripCopy(fromBBVal) != acc)
            continue;
        if (stripCopy(fromThenVal) != addResult)
            continue;

        finalizeConversion(bb, thenBB, mergeBB, loopHeader, cond, acc, addend, addInst, phi, phi,
                           nullptr);
        if (verbose)
            debugInfo << "CondGuardedAccumulate: converted guarded add (phi) in "
                        << bb->getName() << "\n";
        return true;
    }

    CopyInst *mergeAccCopy = nullptr;
    for (auto &instPtr : mergeBB->getInstructions())
    {
        if (auto *cpy = dynamic_cast<CopyInst *>(instPtr.get()))
        {
            mergeAccCopy = cpy;
            break;
        }
    }
    if (!mergeAccCopy)
        return false;

    CopyInst *thenBridgeCopy = nullptr;
    for (auto &instPtr : thenBB->getInstructions())
    {
        if (auto *cpy = dynamic_cast<CopyInst *>(instPtr.get()))
        {
            if (stripCopy(cpy->getSource()) == addResult)
            {
                thenBridgeCopy = cpy;
                break;
            }
        }
    }
    if (!thenBridgeCopy)
        return false;
    if (stripCopy(mergeAccCopy->getSource()) != stripCopy(thenBridgeCopy))
        return false;

    finalizeConversion(bb, thenBB, mergeBB, loopHeader, cond, acc, addend, addInst, nullptr,
                       mergeAccCopy->getSource(), mergeAccCopy);
    if (verbose)
        debugInfo << "CondGuardedAccumulate: converted guarded add (copy) in " << bb->getName()
                  << "\n";
    return true;
}

bool CondGuardedAccumulatePass::runOnFunction(Function *func)
{
    bool changed = false;
    bool local = true;
    while (local)
    {
        local = false;
        for (auto &bbPtr : func->getBasicBlocks())
        {
            if (tryConvert(func, bbPtr.get()))
            {
                changed = true;
                local = true;
                break;
            }
        }
    }
    return changed;
}
