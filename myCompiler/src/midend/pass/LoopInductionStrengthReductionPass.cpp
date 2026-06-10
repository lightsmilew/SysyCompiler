#include "LoopInductionStrengthReductionPass.h"
#include <algorithm>
using namespace std;
using namespace optimization;

Value *LoopInductionStrengthReductionPass::stripCopy(Value *v)
{
    while (auto *cpy = dynamic_cast<CopyInst *>(v))
        v = cpy->getSource();
    return v;
}

bool LoopInductionStrengthReductionPass::sameIV(Value *a, Value *b)
{
    if (!a || !b)
        return false;
    return a->getName() == b->getName();
}

bool LoopInductionStrengthReductionPass::isLoopInvariant(Value *val, const Loop &loop) const
{
    if (!val)
        return false;
    auto *def = dynamic_cast<Instruction *>(val);
    if (!def)
        return true;
    return !loop.containsInst(def);
}

bool LoopInductionStrengthReductionPass::isInCurrentLoopBodyOnly(const Loop &loop, BasicBlock *bb,
                                                                 Function *func) const
{
    if (!bb || bb == loop.header || !loop.containsBlock(bb))
        return false;
    for (const auto &sub : func->getLoops())
    {
        if (sub.header == bb && sub.header != loop.header && loop.containsBlock(bb))
            return false;
    }
    return true;
}

BinaryOperator *LoopInductionStrengthReductionPass::findIVIncrement(BasicBlock *latch, Value *iv,
                                                                    int64_t &step) const
{
    if (!latch || !iv)
        return nullptr;

    auto tryAdd = [&](BinaryOperator *addInst) -> BinaryOperator * {
        if (!addInst || addInst->getOpcode() != Opcode::Add)
            return nullptr;
        if (sameIV(addInst->getLHS(), iv))
        {
            if (auto *stepC = dynamic_cast<ConstantInt *>(stripCopy(addInst->getRHS())))
            {
                step = stepC->Value;
                return addInst;
            }
        }
        if (sameIV(addInst->getRHS(), iv))
        {
            if (auto *stepC = dynamic_cast<ConstantInt *>(stripCopy(addInst->getLHS())))
            {
                step = stepC->Value;
                return addInst;
            }
        }
        return nullptr;
    };

    // Prefer the add that actually updates iv: %iv = copy %next where %next = add %iv, step
    for (auto &instPtr : latch->getInstructions())
    {
        auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
        if (!cpy || !sameIV(cpy, iv))
            continue;
        if (auto *inc = tryAdd(dynamic_cast<BinaryOperator *>(stripCopy(cpy->getSource()))))
            return inc;
    }

    if (auto *phi = dynamic_cast<PhiInst *>(iv))
    {
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            if (phi->getIncomingBlock(i) != latch)
                continue;
            if (auto *inc = tryAdd(dynamic_cast<BinaryOperator *>(phi->getIncomingValue(i))))
                return inc;
        }
    }

    return nullptr;
}

bool LoopInductionStrengthReductionPass::feedsIVUpdate(Value *val, Value *iv,
                                                       BasicBlock *latch) const
{
    if (!val || !iv || !latch)
        return false;
    for (auto &instPtr : latch->getInstructions())
    {
        auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
        if (!cpy || !sameIV(cpy, iv))
            continue;
        if (stripCopy(cpy->getSource()) == val)
            return true;
    }
    if (auto *phi = dynamic_cast<PhiInst *>(iv))
    {
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            if (phi->getIncomingBlock(i) == latch && phi->getIncomingValue(i) == val)
                return true;
        }
    }
    return false;
}

bool LoopInductionStrengthReductionPass::findBasicIV(const Loop &loop,
                                                       InductionVarInfo &info) const
{
    BasicBlock *header = loop.header;
    if (!header)
        return false;

    BasicBlock *preheader = loop.getPreheader();
    if (!preheader)
        return false;

    auto *br = dynamic_cast<BranchInst *>(header->getTerminator());
    if (!br || !br->isConditional())
        return false;
    auto *cmp = dynamic_cast<ICmpInst *>(br->getCondition());
    if (!cmp || cmp->getPredicate() != ICmpInst::ICMP_SLT)
        return false;

    info.preheader = preheader;

    BasicBlock *latch = nullptr;
    for (auto *pred : header->getPredecessors())
    {
        if (loop.containsBlock(pred) && pred != header)
        {
            if (latch)
                return false;
            latch = pred;
        }
    }
    if (!latch)
        return false;
    info.latch = latch;

    PhiInst *ivPhi = nullptr;
    for (auto &instPtr : header->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi)
            continue;
        if (phi == cmp->getLHS() || sameIV(phi, cmp->getLHS()))
        {
            ivPhi = phi;
            break;
        }
    }

    if (ivPhi)
    {
        info.iv = ivPhi;
        info.phi = ivPhi;
        for (unsigned i = 0; i < ivPhi->getNumIncomingValues(); ++i)
        {
            if (ivPhi->getIncomingBlock(i) == preheader)
                info.init = ivPhi->getIncomingValue(i);
        }
        info.inc = findIVIncrement(latch, ivPhi, info.step);
        return info.init && info.inc;
    }

    info.iv = cmp->getLHS();
    info.inc = findIVIncrement(latch, info.iv, info.step);
    if (!info.inc)
        return false;

    for (auto &instPtr : preheader->getInstructions())
    {
        if (auto *cpy = dynamic_cast<CopyInst *>(instPtr.get()))
        {
            if (sameIV(cpy, info.iv))
                info.init = stripCopy(cpy->getSource());
        }
    }
    return info.init != nullptr;
}

Value *LoopInductionStrengthReductionPass::materializeAffineInit(BasicBlock *preheader,
                                                                 Value *ivInit, Value *base,
                                                                 Value *coeff,
                                                                 const string &namePrefix)
{
    auto *i32 = IntegerType::getInstance();
    Value *ivPart = nullptr;
    if (!coeff)
    {
        ivPart = ivInit;
    }
    else if (auto *coeffC = dynamic_cast<ConstantInt *>(stripCopy(coeff)))
    {
        if (coeffC->Value == 0)
            ivPart = new ConstantInt(i32, 0);
        else if (auto *initC = dynamic_cast<ConstantInt *>(stripCopy(ivInit)))
            ivPart = new ConstantInt(i32, static_cast<int>(initC->Value * coeffC->Value));
        else
        {
            auto *mul = new BinaryOperator(Opcode::Mul, ivInit, coeff, namePrefix + "_init_mul");
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(mul));
            ivPart = mul;
        }
    }
    else
    {
        auto *mul = new BinaryOperator(Opcode::Mul, ivInit, coeff, namePrefix + "_init_mul");
        preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(mul));
        ivPart = mul;
    }

    if (!base || (dynamic_cast<ConstantInt *>(stripCopy(base)) &&
                  dynamic_cast<ConstantInt *>(stripCopy(base))->Value == 0))
        return ivPart;

    if (auto *ivC = dynamic_cast<ConstantInt *>(stripCopy(ivPart)))
    {
        if (ivC->Value == 0)
            return base;
    }

    auto *add = new BinaryOperator(Opcode::Add, base, ivPart, namePrefix + "_init");
    preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(add));
    return add;
}

Value *LoopInductionStrengthReductionPass::materializeAffineStep(BasicBlock *bb, Value *coeff,
                                                                 int64_t ivStep,
                                                                 const string &namePrefix)
{
    auto *i32 = IntegerType::getInstance();
    if (ivStep == 1)
        return coeff;
    if (auto *coeffC = dynamic_cast<ConstantInt *>(stripCopy(coeff)))
    {
        return new ConstantInt(i32, static_cast<int>(coeffC->Value * ivStep));
    }
    auto *stepC = new ConstantInt(i32, static_cast<int>(ivStep));
    auto *mul = new BinaryOperator(Opcode::Mul, coeff, stepC, namePrefix + "_step_mul");
    bb->insertBeforeTerminator(std::unique_ptr<Instruction>(mul));
    return mul;
}

PhiInst *LoopInductionStrengthReductionPass::insertAffinePhi(const Loop &loop,
                                                             const InductionVarInfo &iv,
                                                             Value *initVal, Value *stepVal,
                                                             const string &name)
{
    auto *i32 = IntegerType::getInstance();
    auto *phi = new PhiInst(i32, name);
    loop.header->insert(std::unique_ptr<Instruction>(phi), 0);

    auto *next = new BinaryOperator(Opcode::Add, phi, stepVal, name + "_next");
    iv.latch->insertBeforeTerminator(std::unique_ptr<Instruction>(next));

    phi->addIncoming(initVal, iv.preheader);
    phi->addIncoming(next, iv.latch);
    return phi;
}

bool LoopInductionStrengthReductionPass::tryReduceMulIV(Function *func, const Loop &loop,
                                                      const InductionVarInfo &iv)
{
    bool changed = false;
    vector<pair<BinaryOperator *, BasicBlock *>> candidates;

    for (auto *bb : loop.blocks)
    {
        if (!isInCurrentLoopBodyOnly(loop, bb, func))
            continue;
        for (auto &instPtr : bb->getInstructions())
        {
            auto *mul = dynamic_cast<BinaryOperator *>(instPtr.get());
            if (!mul || mul->getOpcode() != Opcode::Mul)
                continue;
            if (mul == iv.inc)
                continue;
            Value *coeff = nullptr;
            if (sameIV(mul->getLHS(), iv.iv))
                coeff = mul->getRHS();
            else if (sameIV(mul->getRHS(), iv.iv))
                coeff = mul->getLHS();
            else
                continue;
            if (!isLoopInvariant(coeff, loop))
                continue;
            candidates.emplace_back(mul, bb);
        }
    }

    for (auto &[mul, bb] : candidates)
    {
        Value *coeff = sameIV(mul->getLHS(), iv.iv) ? mul->getRHS() : mul->getLHS();
        Value *initVal = materializeAffineInit(iv.preheader, iv.init, nullptr, coeff, mul->getName());
        Value *stepVal = materializeAffineStep(iv.latch, coeff, iv.step, mul->getName());
        auto *phi = insertAffinePhi(loop, iv, initVal, stepVal, mul->getName() + "_sr");

        mul->replaceAllUsesWith(phi);
        mul->removeThisFromOperands();
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end(); ++it)
        {
            if (it->get() == mul)
            {
                needToDelete.push_back(it->release());
                insts.erase(it);
                break;
            }
        }
        changed = true;
        if (verbose)
        {
            debugInfo << "LoopInductionStrengthReduction: mul iv*coeff -> phi in "
                      << loop.header->getName() << "\n";
        }
    }
    return changed;
}

bool LoopInductionStrengthReductionPass::tryReduceAffineAddIV(Function *func, const Loop &loop,
                                                            const InductionVarInfo &iv)
{
    bool changed = false;

    for (auto *bb : loop.blocks)
    {
        if (!isInCurrentLoopBodyOnly(loop, bb, func))
            continue;
        for (auto it = bb->getInstructions().begin(); it != bb->getInstructions().end();)
        {
            auto *add = dynamic_cast<BinaryOperator *>(it->get());
            if (!add || add->getOpcode() != Opcode::Add)
            {
                ++it;
                continue;
            }
            if (add == iv.inc || feedsIVUpdate(add, iv.iv, iv.latch))
            {
                ++it;
                continue;
            }

            Value *base = nullptr;
            Value *coeff = nullptr;

            if (auto *mul = dynamic_cast<BinaryOperator *>(add->getLHS()))
            {
                if (mul->getOpcode() == Opcode::Mul)
                {
                    if (sameIV(mul->getLHS(), iv.iv) && isLoopInvariant(mul->getRHS(), loop))
                        coeff = mul->getRHS();
                    else if (sameIV(mul->getRHS(), iv.iv) && isLoopInvariant(mul->getLHS(), loop))
                        coeff = mul->getLHS();
                    if (coeff && isLoopInvariant(add->getRHS(), loop))
                        base = add->getRHS();
                }
            }
            if (!coeff)
            {
                if (auto *mul = dynamic_cast<BinaryOperator *>(add->getRHS()))
                {
                    if (mul->getOpcode() == Opcode::Mul)
                    {
                        if (sameIV(mul->getLHS(), iv.iv) && isLoopInvariant(mul->getRHS(), loop))
                            coeff = mul->getRHS();
                        else if (sameIV(mul->getRHS(), iv.iv) &&
                                 isLoopInvariant(mul->getLHS(), loop))
                            coeff = mul->getLHS();
                        if (coeff && isLoopInvariant(add->getLHS(), loop))
                            base = add->getLHS();
                    }
                }
            }

            if (!coeff)
            {
                ++it;
                continue;
            }

            Value *initVal =
                materializeAffineInit(iv.preheader, iv.init, base, coeff, add->getName());
            Value *stepVal = materializeAffineStep(iv.latch, coeff, iv.step, add->getName());
            auto *phi = insertAffinePhi(loop, iv, initVal, stepVal, add->getName() + "_sr");

            add->replaceAllUsesWith(phi);
            add->removeThisFromOperands();
            needToDelete.push_back(it->release());
            it = bb->getInstructions().erase(it);
            changed = true;
            if (verbose)
            {
                debugInfo << "LoopInductionStrengthReduction: base+iv*coeff -> phi in "
                          << loop.header->getName() << "\n";
            }
        }
    }
    return changed;
}

bool LoopInductionStrengthReductionPass::runOnFunction(Function *func)
{
    bool changed = false;
    func->setLoops(ControlFlowAnalysis::findLoops(func));

    vector<Loop *> order;
    for (auto &loop : func->getLoops())
        order.push_back(&loop);
    sort(order.begin(), order.end(), [](const Loop *a, const Loop *b) {
        return a->blocks.size() < b->blocks.size();
    });

    for (Loop *loopPtr : order)
    {
        Loop &loop = *loopPtr;
        loop.computePreheader();

        InductionVarInfo iv;
        if (!findBasicIV(loop, iv))
            continue;

        changed |= tryReduceMulIV(func, loop, iv);
        changed |= tryReduceAffineAddIV(func, loop, iv);
    }
    return changed;
}
