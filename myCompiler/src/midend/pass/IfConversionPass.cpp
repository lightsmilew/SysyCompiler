#include "IfConversionPass.h"
using namespace std;
using namespace optimization;

namespace
{
    bool isIntegerValue(Value *value)
    {
        return value && !value->getType()->isFloatTy();
    }

    bool mergePhisAreIntegerConvertible(BasicBlock *mergeBB, BasicBlock *thenBB, BasicBlock *elseBB)
    {
        bool found = false;
        for (const auto &inst : mergeBB->getInstructions())
        {
            auto *phi = dynamic_cast<PhiInst *>(inst.get());
            if (!phi || phi->getNumIncomingValues() != 2)
            {
                continue;
            }
            BasicBlock *bb1 = phi->getIncomingBlock(0);
            BasicBlock *bb2 = phi->getIncomingBlock(1);
            if (!((bb1 == thenBB && bb2 == elseBB) || (bb1 == elseBB && bb2 == thenBB)))
            {
                continue;
            }
            if (phi->getType()->isFloatTy())
            {
                return false;
            }
            found = true;
        }
        return found;
    }
}

bool IfConversionPass::isSideEffectFree(BasicBlock *bb)
{
    for (const auto &inst : bb->getInstructions())
    {
        if (auto *br = dynamic_cast<BranchInst *>(inst.get()))
        {
            // 条件分支可能有副作用
            if (br->isConditional())
                return false;
            // 无条件跳转直接跳过
            else
            {
                continue;
            }
        }
        // 其他可能有副作用的指令
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
        if (thenBB->getInstructions().size() == 1 && elseBB->getInstructions().size() == 1)
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
                condInsts.pop_back();
                condInsts.push_back(std::make_unique<ReturnInst>(select));
                // 断开CFG连接
                thenBB->removeSelfBasicBlock();
                elseBB->removeSelfBasicBlock();
                thenBB->getInstructions().clear();
                elseBB->getInstructions().clear();
                changed = true;
                continue;
            }
        }

        // 情况2：if-else后接merge块且merge块有phi
        BasicBlock *mergeBB = (thenBB->getSuccessors().size() == 1 && elseBB->getSuccessors().size() == 1 && thenBB->getSuccessors()[0] == elseBB->getSuccessors()[0])
                                  ? thenBB->getSuccessors()[0]
                                  : nullptr;
        if (mergeBB && isSideEffectFree(thenBB) && isSideEffectFree(elseBB) &&
            mergePhisAreIntegerConvertible(mergeBB, thenBB, elseBB))
        {
            // 把then/else所有非终结指令移动到ifcond块br前
            auto &condInsts = bb->getInstructions();
            for (auto &inst : thenBB->getInstructions())
            {
                if (inst->getOpcode() == Opcode::Br || inst->getOpcode() == Opcode::Ret)
                {
                    continue; // 跳过分支和返回指令
                }
                bb->insertBeforeTerminator(std::move(inst));
            }
            thenBB->clearInstructions();
            // 移动elseBB指令
            for (auto &inst : elseBB->getInstructions())
            {
                if (inst->getOpcode() == Opcode::Br || inst->getOpcode() == Opcode::Ret)
                {
                    continue; // 跳过分支和返回指令
                }
                bb->insertBeforeTerminator(std::move(inst));
            }
            elseBB->clearInstructions();
            // 处理phi
            auto &mergeInsts = mergeBB->getInstructions();
            for (auto it = mergeInsts.begin(); it != mergeInsts.end();)
            {
                auto *phi = dynamic_cast<PhiInst *>(it->get());
                if (!phi)
                {
                    ++it;
                    continue;
                }
                if (phi->getNumIncomingValues() != 2 || phi->getType()->isFloatTy())
                {
                    ++it;
                    continue;
                }
                Value *v1 = phi->getIncomingValue(0);
                BasicBlock *bb1 = phi->getIncomingBlock(0);
                Value *v2 = phi->getIncomingValue(1);
                BasicBlock *bb2 = phi->getIncomingBlock(1);
                // 判断是否来自前面两个基本块的合流
                if (!((bb1 == thenBB && bb2 == elseBB) || (bb1 == elseBB && bb2 == thenBB)))
                {
                    ++it;
                    continue;
                }
                // 构造select
                Value *trueVal = (bb1 == thenBB) ? v1 : v2;
                Value *falseVal = (bb1 == thenBB) ? v2 : v1;
                auto *select = new SelectInst(cond, trueVal, falseVal, phi->getName() + "_sel");
                bb->insertBeforeTerminator(std::unique_ptr<Instruction>(select));
                phi->replaceAllUsesWith(select);
                phi->removeThisFromOperands();
                needToDelete.push_back(it->release());
                it = mergeInsts.erase(it);
                changed = true;
                if(verbose)
                {
                    debugInfo << "If Conversion: Replaced phi " << phi->getName()
                              << " in " << mergeBB->getName() << " with select " << select->getName() << "\n";
                }
            }

            // 修改bb末尾br为无条件跳转mergeBB
            condInsts.pop_back();
            condInsts.push_back(std::make_unique<BranchInst>(mergeBB));
            // 修正CFG连接
            bb->addSuccessor(mergeBB);
            mergeBB->addPredecessor(bb.get());
            // 断开原来的连接
            thenBB->removeSelfBasicBlock();
            elseBB->removeSelfBasicBlock();
            changed = true;
        }
    }
    return changed;
}
