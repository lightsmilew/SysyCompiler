#include "IfLadderShiftPass.h"
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

    bool valueIs(Value *v, Value *expected)
    {
        return stripTrivialWrappers(v) == expected;
    }

    bool isPow2(int v, int &log2)
    {
        if (v <= 0 || (v & (v - 1)) != 0)
        {
            return false;
        }
        log2 = 0;
        while (v > 1)
        {
            v >>= 1;
            ++log2;
        }
        return true;
    }

    bool icmpEqN(Value *cond, Value *n, int expected)
    {
        auto *icmp = dynamic_cast<ICmpInst *>(stripTrivialWrappers(cond));
        if (!icmp || icmp->getPredicate() != ICmpInst::ICMP_EQ)
        {
            return false;
        }
        auto *lhs = stripTrivialWrappers(icmp->getLHS());
        auto *rhs = stripTrivialWrappers(icmp->getRHS());
        if (lhs == n)
        {
            auto *c = dynamic_cast<ConstantInt *>(rhs);
            return c && c->Value == expected;
        }
        if (rhs == n)
        {
            auto *c = dynamic_cast<ConstantInt *>(lhs);
            return c && c->Value == expected;
        }
        return false;
    }

    bool parseThenReturnPow2(BasicBlock *thenBB, Value *x, Opcode expectedOp, int expectedShift)
    {
        auto &insts = thenBB->getInstructions();
        if (insts.size() != 2)
        {
            return false;
        }
        auto *bin = dynamic_cast<BinaryOperator *>(insts[0].get());
        auto *ret = dynamic_cast<ReturnInst *>(insts[1].get());
        if (!bin || !ret || ret->getReturnValue() != bin || bin->getOpcode() != expectedOp)
        {
            return false;
        }
        if (!valueIs(bin->getLHS(), x))
        {
            return false;
        }
        auto *rhsConst = dynamic_cast<ConstantInt *>(stripTrivialWrappers(bin->getRHS()));
        if (!rhsConst)
        {
            return false;
        }
        int log2 = 0;
        return isPow2(rhsConst->Value, log2) && log2 == expectedShift;
    }

    bool parseDefaultRetX(BasicBlock *bb, Value *x)
    {
        auto &insts = bb->getInstructions();
        if (insts.size() != 1)
        {
            return false;
        }
        auto *ret = dynamic_cast<ReturnInst *>(insts[0].get());
        return ret && valueIs(ret->getReturnValue(), x);
    }
}

bool IfLadderShiftPass::matchIfLadderShiftFunction(Function *func, LadderKind &kind)
{
    if (func->isLibraryFunction() || func->getArguments().size() != 2)
    {
        return false;
    }
    auto *x = func->getArgumentByIndex(0);
    auto *n = func->getArgumentByIndex(1);
    if (!x || !n || !x->getType()->isIntegerTy() || !n->getType()->isIntegerTy())
    {
        return false;
    }

    BasicBlock *bb = func->getEntryBlock();
    if (!bb)
    {
        return false;
    }

    Opcode binOp = Opcode::Mul;
    bool opSet = false;

    for (int k = 1; k <= 8; ++k)
    {
        auto *br = dynamic_cast<BranchInst *>(bb->getTerminator());
        if (!br || !br->isConditional() || !icmpEqN(br->getCondition(), n, k))
        {
            return false;
        }
        BasicBlock *thenBB = br->getTrueBlock();
        BasicBlock *elseBB = br->getFalseBlock();
        if (!thenBB || !elseBB)
        {
            return false;
        }

        if (!opSet)
        {
            auto *bin = dynamic_cast<BinaryOperator *>(thenBB->getInstructions()[0].get());
            if (!bin)
            {
                return false;
            }
            if (bin->getOpcode() == Opcode::Mul)
            {
                binOp = Opcode::Mul;
                kind = LadderKind::MulPow2;
            }
            else if (bin->getOpcode() == Opcode::SDiv)
            {
                binOp = Opcode::SDiv;
                kind = LadderKind::SDivPow2;
            }
            else
            {
                return false;
            }
            opSet = true;
        }

        if (!parseThenReturnPow2(thenBB, x, binOp, k))
        {
            return false;
        }

        bb = elseBB;
    }

    return parseDefaultRetX(bb, x);
}

void IfLadderShiftPass::rewriteFunction(Function *func, LadderKind kind)
{
    auto &bbs = func->getBasicBlocks();
    BasicBlock *entry = func->getEntryBlock();
    Value *x = func->getArgumentByIndex(0);
    Value *n = func->getArgumentByIndex(1);
    const string prefix = func->getName() + ".shift";

    vector<BasicBlock *> allBlocks;
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
            needToDelete.push_back(instPtr.release());
        }
        insts.clear();
        bb->removeSelfBasicBlock();
    }

    bbs.erase(std::remove_if(bbs.begin(), bbs.end(),
                             [&](const unique_ptr<BasicBlock> &bbPtr)
                             { return bbPtr.get() != entry; }),
              bbs.end());

    auto *i32 = IntegerType::getInstance();
    auto *c0 = new ConstantInt(i32, 0);
    auto *c1 = new ConstantInt(i32, 1);
    auto *c8 = new ConstantInt(i32, 8);
    auto *c31 = new ConstantInt(i32, 31);

    auto *ge = new ICmpInst(ICmpInst::ICMP_SGE, n, c1, prefix + "_ge");
    auto *le = new ICmpInst(ICmpInst::ICMP_SLE, n, c8, prefix + "_le");
    // icmp 结果为 0/1；valid=1 当 n∈[1,8]。amt = n & (0-valid)：valid=1 时为 n，valid=0 时为 0
    auto *valid = new BinaryOperator(Opcode::And, ge, le, prefix + "_valid");
    auto *validMask = new BinaryOperator(Opcode::Sub, c0, valid, prefix + "_vmask");
    auto *amt = new BinaryOperator(Opcode::And, n, validMask, prefix + "_amt");

    entry->clearInstructions();
    entry->addInstruction(unique_ptr<Instruction>(ge));
    entry->addInstruction(unique_ptr<Instruction>(le));
    entry->addInstruction(unique_ptr<Instruction>(valid));
    entry->addInstruction(unique_ptr<Instruction>(validMask));
    entry->addInstruction(unique_ptr<Instruction>(amt));

    Instruction *result = nullptr;
    if (kind == LadderKind::MulPow2)
    {
        result = new BinaryOperator(Opcode::Sll, x, amt, prefix + "_shl");
        entry->addInstruction(unique_ptr<Instruction>(result));
    }
    else
    {
        auto *sign = new BinaryOperator(Opcode::Sra, x, c31, prefix + "_sign");
        auto *pow2 = new BinaryOperator(Opcode::Sll, c1, amt, prefix + "_pow2");
        auto *mask = new BinaryOperator(Opcode::Sub, pow2, c1, prefix + "_mask");
        auto *bias = new BinaryOperator(Opcode::And, sign, mask, prefix + "_bias");
        auto *adj = new BinaryOperator(Opcode::Add, x, bias, prefix + "_adj");
        result = new BinaryOperator(Opcode::Sra, adj, amt, prefix + "_sra");
        entry->addInstruction(unique_ptr<Instruction>(sign));
        entry->addInstruction(unique_ptr<Instruction>(pow2));
        entry->addInstruction(unique_ptr<Instruction>(mask));
        entry->addInstruction(unique_ptr<Instruction>(bias));
        entry->addInstruction(unique_ptr<Instruction>(adj));
        entry->addInstruction(unique_ptr<Instruction>(result));
    }

    entry->addInstruction(unique_ptr<Instruction>(new ReturnInst(result)));
    func->setLoops({});
}

bool IfLadderShiftPass::runOnFunction(Function *func)
{
    LadderKind kind = LadderKind::MulPow2;
    if (!matchIfLadderShiftFunction(func, kind))
    {
        return false;
    }

    rewriteFunction(func, kind);
    if (verbose)
    {
        debugInfo << "IfLadderShift: folded if-ladder in " << func->getName()
                  << (kind == LadderKind::MulPow2 ? " to variable shl\n"
                                                 : " to variable signed shift-div\n");
    }
    return true;
}
