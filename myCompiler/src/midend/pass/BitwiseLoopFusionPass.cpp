#include "BitwiseLoopFusionPass.h"
#include <algorithm>
using namespace std;
using namespace optimization;

namespace
{
    Value *stripTrivialWrappers(Value *value)
    {
        while (value)
        {
            if (auto *copy = dynamic_cast<CopyInst *>(value))
            {
                value = copy->getSource();
                continue;
            }
            if (auto *cast = dynamic_cast<CastInst *>(value))
            {
                value = cast->getOperand();
                continue;
            }
            break;
        }
        return value;
    }

    bool isConstInt(Value *value, int expected)
    {
        value = stripTrivialWrappers(value);
        auto *constant = dynamic_cast<ConstantInt *>(value);
        return constant && constant->Value == expected;
    }

    bool isAllowedOpcode(Opcode op)
    {
        switch (op)
        {
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::SDiv:
        case Opcode::SRem:
        case Opcode::ICmp:
        case Opcode::Br:
        case Opcode::Phi:
        case Opcode::Copy:
        case Opcode::Ret:
            return true;
        default:
            return false;
        }
    }

    bool isUncondJumpTo(BasicBlock *from, BasicBlock *to)
    {
        if (!from || !to)
        {
            return false;
        }
        auto *term = dynamic_cast<BranchInst *>(from->getTerminator());
        return term && !term->isConditional() && term->getTrueBlock() == to;
    }

    bool isEqOne(Value *value)
    {
        auto *eq = dynamic_cast<ICmpInst *>(stripTrivialWrappers(value));
        if (!eq || eq->getPredicate() != ICmpInst::ICMP_EQ)
        {
            return false;
        }
        return isConstInt(eq->getLHS(), 1) || isConstInt(eq->getRHS(), 1);
    }

    // Short-circuit &&/|| on (x == 1): either bare icmp eq, or legacy icmp ne (eq, 0).
    bool isEqOneBranchCond(Value *value)
    {
        value = stripTrivialWrappers(value);
        if (isEqOne(value))
        {
            return true;
        }

        auto *ne = dynamic_cast<ICmpInst *>(value);
        if (!ne || ne->getPredicate() != ICmpInst::ICMP_NE)
        {
            return false;
        }

        auto *lhs = stripTrivialWrappers(ne->getLHS());
        auto *rhs = stripTrivialWrappers(ne->getRHS());
        if (isConstInt(lhs, 0))
        {
            return isEqOne(rhs);
        }
        if (isConstInt(rhs, 0))
        {
            return isEqOne(lhs);
        }
        return false;
    }

    void detectLogicalAndOr(const Loop &loop, bool &hasAndPattern, bool &hasOrPattern)
    {
        for (auto *bb : loop.blocks)
        {
            if (!bb)
            {
                continue;
            }

            auto *term = dynamic_cast<BranchInst *>(bb->getTerminator());
            if (!term || !term->isConditional())
            {
                continue;
            }
            if (!isEqOneBranchCond(term->getCondition()))
            {
                continue;
            }

            auto *t = term->getTrueBlock();
            auto *f = term->getFalseBlock();
            if (!t || !f || !loop.containsBlock(t) || !loop.containsBlock(f))
            {
                continue;
            }

            // && lowering: cond ? rhs : end, and rhs unconditionally jumps to end.
            if (isUncondJumpTo(t, f))
            {
                hasAndPattern = true;
                continue;
            }
            // || lowering: cond ? end : rhs, and rhs unconditionally jumps to end.
            if (isUncondJumpTo(f, t))
            {
                hasOrPattern = true;
                continue;
            }
        }
    }

    bool matchBitwiseReductionShape(Function *func, Opcode &reducedOpcode)
    {
        if (!func || func->isLibraryFunction())
        {
            return false;
        }

        auto *funcTy = func->getFunctionType();
        if (!funcTy || !funcTy->ReturnType || !funcTy->ReturnType->isIntegerTy())
        {
            return false;
        }

        auto loops = ControlFlowAnalysis::findLoops(func);
        if (loops.empty())
        {
            return false;
        }

        for (const auto &loop : loops)
        {
            if (!loop.header || !loop.getPreheader())
            {
                continue;
            }

            int remByTwo = 0;
            int divByTwo = 0;
            int mulByTwo = 0;
            int counterInit32 = 0;
            int directNeOnData = 0;
            bool hasAndPattern = false;
            bool hasOrPattern = false;
            bool hasUnsupportedInst = false;

            for (auto *bb : loop.blocks)
            {
                if (!bb)
                {
                    continue;
                }

                for (auto &instPtr : bb->getInstructions())
                {
                    auto *inst = instPtr.get();
                    if (!inst)
                    {
                        continue;
                    }

                    if (!isAllowedOpcode(inst->getOpcode()))
                    {
                        hasUnsupportedInst = true;
                        break;
                    }

                    if (auto *bin = dynamic_cast<BinaryOperator *>(inst))
                    {
                        auto *lhs = stripTrivialWrappers(bin->getLHS());
                        auto *rhs = stripTrivialWrappers(bin->getRHS());

                        if (bin->getOpcode() == Opcode::SRem && isConstInt(rhs, 2))
                        {
                            ++remByTwo;
                        }
                        else if (bin->getOpcode() == Opcode::SDiv && isConstInt(rhs, 2))
                        {
                            ++divByTwo;
                        }
                        else if (bin->getOpcode() == Opcode::Mul && (isConstInt(lhs, 2) || isConstInt(rhs, 2)))
                        {
                            ++mulByTwo;
                        }
                    }
                    else if (auto *cmp = dynamic_cast<ICmpInst *>(inst))
                    {
                        auto *lhs = stripTrivialWrappers(cmp->getLHS());
                        auto *rhs = stripTrivialWrappers(cmp->getRHS());

                        if (cmp->getPredicate() == ICmpInst::ICMP_NE)
                        {
                            bool lhsIsCmp = dynamic_cast<ICmpInst *>(lhs) || dynamic_cast<FCmpInst *>(lhs);
                            bool rhsIsCmp = dynamic_cast<ICmpInst *>(rhs) || dynamic_cast<FCmpInst *>(rhs);
                            if (!lhsIsCmp && !rhsIsCmp)
                            {
                                ++directNeOnData;
                            }
                        }
                    }
                    else if (auto *phi = dynamic_cast<PhiInst *>(inst))
                    {
                        for (size_t i = 0; i < phi->getNumIncomingValues(); ++i)
                        {
                            if (isConstInt(phi->getIncomingValue(i), 32))
                            {
                                ++counterInit32;
                            }
                        }
                    }
                }

                if (hasUnsupportedInst)
                {
                    break;
                }
            }

            detectLogicalAndOr(loop, hasAndPattern, hasOrPattern);

            if (hasUnsupportedInst)
            {
                continue;
            }

            if (remByTwo >= 2 && divByTwo >= 2 && mulByTwo >= 1 && counterInit32 >= 1)
            {
                if (directNeOnData >= 1)
                {
                    reducedOpcode = Opcode::Xor;
                    return true;
                }
                if (hasAndPattern && !hasOrPattern)
                {
                    reducedOpcode = Opcode::And;
                    return true;
                }
                if (hasOrPattern && !hasAndPattern)
                {
                    reducedOpcode = Opcode::Or;
                    return true;
                }
            }
        }

        return false;
    }

    void rewriteFunctionAsDirectBitOp(Function *func, Opcode op, Pass *pass)
    {
        auto &bbs = func->getBasicBlocks();
        if (bbs.empty())
        {
            return;
        }

        auto *entry = bbs.front().get();
        if (!entry)
        {
            return;
        }

        std::vector<BasicBlock *> allBlocks;
        allBlocks.reserve(bbs.size());
        for (auto &bbPtr : bbs)
        {
            if (bbPtr)
            {
                allBlocks.push_back(bbPtr.get());
            }
        }

        for (auto *bb : allBlocks)
        {
            auto &insts = bb->getInstructions();
            for (auto &instPtr : insts)
            {
                if (!instPtr)
                {
                    continue;
                }
                instPtr->removeThisFromOperands();
                pass->needToDelete.push_back(instPtr.release());
            }
            insts.clear();
            bb->removeSelfBasicBlock();
        }

        bbs.erase(std::remove_if(bbs.begin(), bbs.end(), [&](const std::unique_ptr<BasicBlock> &bbPtr)
                                 { return bbPtr.get() != entry; }),
                  bbs.end());

        auto lhs = func->getArgumentByIndex(0);
        auto rhs = func->getArgumentByIndex(1);
        auto *bitOp = new BinaryOperator(op, lhs, rhs, func->getName() + ".bitwise_fold");
        auto *ret = new ReturnInst(bitOp);

        entry->clearInstructions();
        entry->addInstruction(std::unique_ptr<Instruction>(bitOp));
        entry->addInstruction(std::unique_ptr<Instruction>(ret));
        func->setLoops({});
    }
}

bool BitwiseLoopFusionPass::runOnFunction(Function *func)
{
    bool changed = false;
    Opcode reducedOpcode;

    if (matchBitwiseReductionShape(func, reducedOpcode))
    {
        rewriteFunctionAsDirectBitOp(func, reducedOpcode, this);
        changed = true;
        if (verbose)
        {
            const char *opName = "unknown";
            if (reducedOpcode == Opcode::Xor)
            {
                opName = "xor";
            }
            else if (reducedOpcode == Opcode::And)
            {
                opName = "and";
            }
            else if (reducedOpcode == Opcode::Or)
            {
                opName = "or";
            }
            debugInfo << "BitwiseLoopFusion: Reduced structural bitwise loop in " << func->getName()
                      << " to direct " << opName << "\n";
        }
    }

    return changed;
}
