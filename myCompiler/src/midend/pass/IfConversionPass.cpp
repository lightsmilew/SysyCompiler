#include "IfConversionPass.h"
using namespace std;
using namespace optimization;

namespace
{
    bool isIntegerValue(Value *value)
    {
        return value && !value->getType()->isFloatTy();
    }

    bool isBranchOnlyBlock(BasicBlock *bb)
    {
        for (const auto &inst : bb->getInstructions())
        {
            if (inst->getOpcode() == Opcode::Br || inst->getOpcode() == Opcode::Ret)
            {
                continue;
            }
            return false;
        }
        return true;
    }

    // merge 块中的每个 phi 都必须且仅能来自 then/else
    bool mergePhisAreIntegerConvertible(BasicBlock *mergeBB, BasicBlock *thenBB, BasicBlock *elseBB)
    {
        bool found = false;
        for (const auto &inst : mergeBB->getInstructions())
        {
            auto *phi = dynamic_cast<PhiInst *>(inst.get());
            if (!phi)
            {
                continue;
            }
            if (phi->getNumIncomingValues() != 2)
            {
                return false;
            }
            BasicBlock *bb1 = phi->getIncomingBlock(0);
            BasicBlock *bb2 = phi->getIncomingBlock(1);
            if (!((bb1 == thenBB && bb2 == elseBB) || (bb1 == elseBB && bb2 == thenBB)))
            {
                return false;
            }
            if (phi->getType()->isFloatTy())
            {
                return false;
            }
            found = true;
        }
        return found;
    }

    bool noExternalPhiUsesThenElse(Function *func, BasicBlock *mergeBB, BasicBlock *thenBB,
                                     BasicBlock *elseBB)
    {
        for (auto &bbPtr : func->getBasicBlocks())
        {
            if (bbPtr.get() == mergeBB)
            {
                continue;
            }
            for (auto &inst : bbPtr->getInstructions())
            {
                auto *phi = dynamic_cast<PhiInst *>(inst.get());
                if (!phi)
                {
                    continue;
                }
                for (BasicBlock *in : phi->getIncomingBlocks())
                {
                    if (in == thenBB || in == elseBB)
                    {
                        return false;
                    }
                }
            }
        }
        return true;
    }
}

bool IfConversionPass::isSideEffectFree(BasicBlock *bb)
{
    for (const auto &inst : bb->getInstructions())
    {
        if (auto *br = dynamic_cast<BranchInst *>(inst.get()))
        {
            if (br->isConditional())
                return false;
            continue;
        }
        if (inst->mayHaveSideEffects())
        {
            return false;
        }
    }
    return true;
}

bool IfConversionPass::runOnFunction(Function *func)
{
    bool changed = false;

    for (auto &bb : func->getBasicBlocks())
    {
        if (bb->getInstructions().empty())
            continue;
        auto *br = dynamic_cast<BranchInst *>(bb->getInstructions().back().get());
        if (!br || !br->isConditional())
            continue;

        Value *cond = br->getCondition();
        BasicBlock *thenBB = br->TrueBlock;
        BasicBlock *elseBB = br->FalseBlock;
        if (!thenBB || !elseBB)
            continue;

        // 情况1：if-else分支均为return
        if (false && thenBB->getInstructions().size() == 1 && elseBB->getInstructions().size() == 1)
        {
            auto *thenRet = dynamic_cast<ReturnInst *>(thenBB->getInstructions().front().get());
            auto *elseRet = dynamic_cast<ReturnInst *>(elseBB->getInstructions().front().get());
            if (thenRet && elseRet && isIntegerValue(thenRet->getReturnValue()) &&
                isIntegerValue(elseRet->getReturnValue()))
            {
                Value *thenVal = thenRet->getReturnValue();
                Value *elseVal = elseRet->getReturnValue();
                auto &condInsts = bb->getInstructions();
                auto *select = new SelectInst(cond, thenVal, elseVal, "ifc_ret");
                condInsts.insert(condInsts.end() - 1, std::unique_ptr<Instruction>(select));
                br->removeThisFromOperands();
                condInsts.pop_back();
                condInsts.push_back(std::make_unique<ReturnInst>(select));
                thenBB->removeSelfBasicBlock();
                elseBB->removeSelfBasicBlock();
                thenBB->getInstructions().clear();
                elseBB->getInstructions().clear();
                changed = true;
                continue;
            }
        }

        // 情况2：then/else 仅含跳转，merge 块 phi 可安全转为 select
        BasicBlock *mergeBB = (thenBB->getSuccessors().size() == 1 && elseBB->getSuccessors().size() == 1 &&
                               thenBB->getSuccessors()[0] == elseBB->getSuccessors()[0])
                                  ? thenBB->getSuccessors()[0]
                                  : nullptr;
        if (!mergeBB || !isBranchOnlyBlock(thenBB) || !isBranchOnlyBlock(elseBB) ||
            !mergePhisAreIntegerConvertible(mergeBB, thenBB, elseBB) ||
            !noExternalPhiUsesThenElse(func, mergeBB, thenBB, elseBB))
        {
            continue;
        }

        auto &mergeInsts = mergeBB->getInstructions();
        for (auto it = mergeInsts.begin(); it != mergeInsts.end();)
        {
            auto *phi = dynamic_cast<PhiInst *>(it->get());
            if (!phi)
            {
                ++it;
                continue;
            }
            Value *v1 = phi->getIncomingValue(0);
            BasicBlock *bb1 = phi->getIncomingBlock(0);
            Value *v2 = phi->getIncomingValue(1);
            BasicBlock *bb2 = phi->getIncomingBlock(1);
            Value *trueVal = (bb1 == thenBB) ? v1 : v2;
            Value *falseVal = (bb1 == thenBB) ? v2 : v1;
            auto *select = new SelectInst(cond, trueVal, falseVal, phi->getName() + "_sel");
            bb->insertBeforeTerminator(std::unique_ptr<Instruction>(select));
            phi->replaceAllUsesWith(select);
            phi->removeThisFromOperands();
            needToDelete.push_back(it->release());
            it = mergeInsts.erase(it);
            changed = true;
            if (verbose)
            {
                debugInfo << "If Conversion: Replaced phi " << phi->getName()
                          << " in " << mergeBB->getName() << " with select " << select->getName() << "\n";
            }
        }

        auto &condInsts = bb->getInstructions();
        br->removeThisFromOperands();
        condInsts.pop_back();
        condInsts.push_back(std::make_unique<BranchInst>(mergeBB));
        bb->addSuccessor(mergeBB);
        mergeBB->addPredecessor(bb.get());
        thenBB->removeSelfBasicBlock();
        elseBB->removeSelfBasicBlock();
        changed = true;
    }
    return changed;
}
