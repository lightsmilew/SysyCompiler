#include "LoopLinearIterationFoldPass.h"
#include <algorithm>
#include <unordered_set>
using namespace std;
using namespace optimization;

namespace
{
    Value *stripCopy(Value *v)
    {
        while (auto *cpy = dynamic_cast<CopyInst *>(v))
        {
            v = cpy->getSource();
        }
        return v;
    }

    bool sameLoopValue(Value *a, Value *b)
    {
        if (!a || !b)
        {
            return false;
        }
        if (stripCopy(a) == stripCopy(b))
        {
            return true;
        }
        if (!a->getName().empty() && a->getName() == b->getName())
        {
            return true;
        }
        return false;
    }

    bool valueDependsOnImpl(Value *val, Value *target, unordered_set<Value *> &visited)
    {
        if (!val || !target)
        {
            return false;
        }
        if (sameLoopValue(val, target))
        {
            return true;
        }
        if (!visited.insert(val).second)
        {
            return false;
        }
        if (auto *inst = dynamic_cast<Instruction *>(val))
        {
            for (auto *op : inst->getOperands())
            {
                if (valueDependsOnImpl(op, target, visited))
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool valueDependsOn(Value *val, Value *target)
    {
        unordered_set<Value *> visited;
        return valueDependsOnImpl(val, target, visited);
    }

    BasicBlock *findLoopLatchBlock(const Loop &loop)
    {
        for (auto *bb : loop.blocks)
        {
            if (bb == loop.header)
            {
                continue;
            }
            for (auto *succ : bb->getSuccessors())
            {
                if (succ == loop.header)
                {
                    return bb;
                }
            }
        }
        return nullptr;
    }

    bool hasZeroInitOutsideLoop(Value *tracked, const Loop &loop)
    {
        if (!tracked || !loop.header)
        {
            return false;
        }

        if (auto *trackedPhi = dynamic_cast<PhiInst *>(stripCopy(tracked)))
        {
            for (size_t i = 0; i < trackedPhi->getNumIncomingValues(); ++i)
            {
                if (loop.containsBlock(trackedPhi->getIncomingBlock(i)))
                {
                    continue;
                }
                auto *initConst = dynamic_cast<ConstantInt *>(stripCopy(trackedPhi->getIncomingValue(i)));
                if (initConst && initConst->Value == 0)
                {
                    return true;
                }
            }
            return false;
        }

        for (auto *pred : loop.header->getPredecessors())
        {
            if (loop.containsBlock(pred))
            {
                continue;
            }
            for (auto &instPtr : pred->getInstructions())
            {
                auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
                if (!cpy || !sameLoopValue(cpy, tracked))
                {
                    continue;
                }
                auto *initConst = dynamic_cast<ConstantInt *>(stripCopy(cpy->getSource()));
                if (initConst && initConst->Value == 0)
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool hasUnitIncrementAtLatch(Value *iv, const Loop &loop)
    {
        BasicBlock *latch = findLoopLatchBlock(loop);
        if (!latch)
        {
            return false;
        }

        for (auto &instPtr : latch->getInstructions())
        {
            auto *addInst = dynamic_cast<BinaryOperator *>(instPtr.get());
            if (!addInst || addInst->getOpcode() != Opcode::Add)
            {
                continue;
            }
            auto *one = dynamic_cast<ConstantInt *>(stripCopy(addInst->getRHS()));
            if (!one || one->Value != 1)
            {
                continue;
            }
            if (sameLoopValue(addInst->getLHS(), iv))
            {
                return true;
            }
        }
        return false;
    }

    bool hasNestedLoopInside(const Loop &outer, const vector<Loop> &allLoops)
    {
        for (const auto &inner : allLoops)
        {
            if (inner.header == outer.header)
            {
                continue;
            }
            if (outer.containsBlock(inner.header))
            {
                return true;
            }
        }
        return false;
    }

    set<BasicBlock *> collectPerIterationBlocks(const Loop &outer)
    {
        set<BasicBlock *> perIter;
        if (!outer.header)
        {
            return perIter;
        }

        BasicBlock *body = nullptr;
        auto &headerInsts = outer.header->getInstructions();
        auto *br = dynamic_cast<BranchInst *>(headerInsts.back().get());
        if (br && br->isConditional())
        {
            body = br->getTrueBlock();
        }
        if (!body || !outer.containsBlock(body))
        {
            return perIter;
        }

        vector<BasicBlock *> worklist = {body};
        perIter.insert(body);
        while (!worklist.empty())
        {
            BasicBlock *bb = worklist.back();
            worklist.pop_back();
            for (auto *succ : bb->getSuccessors())
            {
                if (succ == outer.header || !outer.containsBlock(succ) || perIter.count(succ))
                {
                    continue;
                }
                perIter.insert(succ);
                worklist.push_back(succ);
            }
        }
        return perIter;
    }

    bool sameValueForAccess(Value *lhs, Value *rhs)
    {
        if (lhs == rhs)
        {
            return true;
        }

        auto *lhsConstInt = dynamic_cast<ConstantInt *>(lhs);
        auto *rhsConstInt = dynamic_cast<ConstantInt *>(rhs);
        if (lhsConstInt && rhsConstInt)
        {
            return lhsConstInt->Value == rhsConstInt->Value;
        }

        if (!lhs || !rhs)
        {
            return false;
        }

        return isSameAddr(lhs, rhs);
    }

    bool collectAccessPattern(Value *value, Value *&baseValue, vector<Value *> &indices)
    {
        if (!value)
        {
            return false;
        }

        if (auto *castInst = dynamic_cast<CastInst *>(value))
        {
            return collectAccessPattern(castInst->getOperand(), baseValue, indices);
        }

        if (auto *gepInst = dynamic_cast<GetElementPtrInst *>(value))
        {
            if (!collectAccessPattern(gepInst->getPointerOperand(), baseValue, indices))
            {
                return false;
            }

            auto gepIndices = gepInst->getIndices();
            indices.insert(indices.end(), gepIndices.begin(), gepIndices.end());
            return true;
        }

        if (!baseValue)
        {
            baseValue = value;
            return true;
        }

        return sameValueForAccess(baseValue, value);
    }

    bool sameAccessPattern(const vector<Value *> &lhs, const vector<Value *> &rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }

        for (size_t i = 0; i < lhs.size(); ++i)
        {
            if (!sameValueForAccess(lhs[i], rhs[i]))
            {
                return false;
            }
        }

        return true;
    }

    bool isFoldableTripBound(Value *bound, int constTripCount)
    {
        if (constTripCount >= 0)
        {
            return constTripCount > 1;
        }
        if (auto *boundConst = dynamic_cast<ConstantInt *>(stripCopy(bound)))
        {
            return boundConst->Value > 1;
        }
        return bound != nullptr;
    }

    bool combineAccDelta(Value *base, Value *extra, Value *&delta)
    {
        auto *zero = dynamic_cast<ConstantInt *>(stripCopy(base));
        if (zero && zero->Value == 0)
        {
            delta = extra;
            return true;
        }
        delta = new BinaryOperator(Opcode::Add, base, extra, "loop_linear_fold_delta");
        return true;
    }

    // 从 val 中剥离 acc，余下 delta 不依赖 acc（允许经 copy 链传递 acc）。
    bool peelAccAdds(Value *val, Value *acc, Value *&delta, unordered_set<Value *> &visiting)
    {
        val = stripCopy(val);
        if (!val || !acc)
        {
            return false;
        }
        if (sameLoopValue(val, acc))
        {
            delta = new ConstantInt(IntegerType::getInstance(), 0);
            return true;
        }
        if (!visiting.insert(val).second)
        {
            return false;
        }

        if (auto *add = dynamic_cast<BinaryOperator *>(val))
        {
            if (add->getOpcode() == Opcode::Add)
            {
                Value *partial = nullptr;
                if (!valueDependsOn(add->getRHS(), acc) &&
                    peelAccAdds(add->getLHS(), acc, partial, visiting))
                {
                    combineAccDelta(partial, add->getRHS(), delta);
                    visiting.erase(val);
                    return true;
                }
                if (!valueDependsOn(add->getLHS(), acc) &&
                    peelAccAdds(add->getRHS(), acc, partial, visiting))
                {
                    combineAccDelta(partial, add->getLHS(), delta);
                    visiting.erase(val);
                    return true;
                }
            }
        }

        visiting.erase(val);
        return false;
    }
}

bool LoopLinearIterationFoldPass::getFixedTripCountLoopInfo(const Loop &loop,
                                                            ICmpInst *&cmp,
                                                            ConstantInt *&boundConst,
                                                            int &tripCount) const
{
    cmp = nullptr;
    boundConst = nullptr;
    tripCount = -1;

    BasicBlock *header = loop.header;
    if (!header)
    {
        return false;
    }

    auto &headerInsts = header->getInstructions();
    if (headerInsts.size() < 2)
    {
        return false;
    }

    auto *br = dynamic_cast<BranchInst *>(headerInsts.back().get());
    if (!br || !br->isConditional())
    {
        return false;
    }

    cmp = dynamic_cast<ICmpInst *>(headerInsts[headerInsts.size() - 2].get());
    if (!cmp)
    {
        return false;
    }

    if (cmp->getPredicate() != ICmpInst::ICMP_SLT && cmp->getPredicate() != ICmpInst::ICMP_SLE)
    {
        return false;
    }

    auto *lhsPhi = dynamic_cast<PhiInst *>(cmp->getLHS());
    auto *rhsPhi = dynamic_cast<PhiInst *>(cmp->getRHS());
    if (!lhsPhi && !rhsPhi)
    {
        return false;
    }

    auto *lhsConst = dynamic_cast<ConstantInt *>(cmp->getLHS());
    auto *rhsConst = dynamic_cast<ConstantInt *>(cmp->getRHS());

    if (lhsPhi && rhsConst)
    {
        boundConst = rhsConst;
        for (size_t i = 0; i < lhsPhi->getNumIncomingValues(); ++i)
        {
            if (find(loop.blocks.begin(), loop.blocks.end(), lhsPhi->getIncomingBlock(i)) == loop.blocks.end())
            {
                auto *initConst = dynamic_cast<ConstantInt *>(lhsPhi->getIncomingValue(i));
                if (!initConst || initConst->Value != 0)
                {
                    return false;
                }
                tripCount = (cmp->getPredicate() == ICmpInst::ICMP_SLT) ? boundConst->Value : boundConst->Value + 1;
                return tripCount > 0;
            }
        }
    }
    else if (rhsPhi && lhsConst)
    {
        return false;
    }

    return false;
}

bool LoopLinearIterationFoldPass::getCountableOuterLoopInfo(const Loop &loop,
                                                            ICmpInst *&cmp,
                                                            Value *&iv,
                                                            Value *&bound,
                                                            int &constTripCount) const
{
    cmp = nullptr;
    iv = nullptr;
    bound = nullptr;
    constTripCount = -1;

    BasicBlock *header = loop.header;
    if (!header)
    {
        return false;
    }

    auto &headerInsts = header->getInstructions();
    if (headerInsts.size() < 2)
    {
        return false;
    }

    auto *br = dynamic_cast<BranchInst *>(headerInsts.back().get());
    if (!br || !br->isConditional())
    {
        return false;
    }

    cmp = dynamic_cast<ICmpInst *>(headerInsts[headerInsts.size() - 2].get());
    if (!cmp || cmp->getPredicate() != ICmpInst::ICMP_SLT)
    {
        return false;
    }

    iv = cmp->getLHS();
    bound = cmp->getRHS();
    if (!iv || !bound)
    {
        return false;
    }

    if (!hasZeroInitOutsideLoop(iv, loop) || !hasUnitIncrementAtLatch(iv, loop))
    {
        return false;
    }

    if (auto *boundConst = dynamic_cast<ConstantInt *>(stripCopy(bound)))
    {
        constTripCount = boundConst->Value;
    }
    return true;
}

bool LoopLinearIterationFoldPass::isPureCopyLoop(const Loop &loop, Value *&srcArray, Value *&dstArray) const
{
    srcArray = nullptr;
    dstArray = nullptr;

    int loadCount = 0;
    int storeCount = 0;
    vector<Value *> loadIndices;
    vector<Value *> storeIndices;

    for (auto *bb : loop.blocks)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (dynamic_cast<CallInst *>(inst))
            {
                return false;
            }

            if (auto *load = dynamic_cast<LoadInst *>(inst))
            {
                if (++loadCount > 1)
                {
                    return false;
                }
                Value *origin = nullptr;
                vector<Value *> indices;
                if (!collectAccessPattern(load->getPointer(), origin, indices) || !origin)
                {
                    return false;
                }
                if (!srcArray)
                {
                    srcArray = origin;
                    loadIndices = std::move(indices);
                }
                else if (!isSameAddr(srcArray, origin) || !sameAccessPattern(loadIndices, indices))
                {
                    return false;
                }
                continue;
            }

            if (auto *store = dynamic_cast<StoreInst *>(inst))
            {
                if (++storeCount > 1)
                {
                    return false;
                }
                Value *origin = nullptr;
                vector<Value *> indices;
                if (!collectAccessPattern(store->getPointer(), origin, indices) || !origin)
                {
                    return false;
                }
                if (!dstArray)
                {
                    dstArray = origin;
                    storeIndices = std::move(indices);
                }
                else if (!isSameAddr(dstArray, origin) || !sameAccessPattern(storeIndices, indices))
                {
                    return false;
                }
                continue;
            }

            if (dynamic_cast<BranchInst *>(inst) ||
                dynamic_cast<ICmpInst *>(inst) ||
                dynamic_cast<PhiInst *>(inst) ||
                dynamic_cast<BinaryOperator *>(inst) ||
                dynamic_cast<CastInst *>(inst) ||
                dynamic_cast<GetElementPtrInst *>(inst) ||
                dynamic_cast<CopyInst *>(inst))
            {
                continue;
            }

            return false;
        }
    }

    if (loadCount != 1 || storeCount != 1 || !srcArray || !dstArray || isSameAddr(srcArray, dstArray))
    {
        return false;
    }

    return sameAccessPattern(loadIndices, storeIndices);
}

Instruction *LoopLinearIterationFoldPass::buildLinearCompensation(Value *acc,
                                                                 Value *tripScale,
                                                                 const LinearIterationMap &map) const
{
    if (!acc || !tripScale || !map.valid)
    {
        return nullptr;
    }
    if (!map.weakIncrement && !map.addend)
    {
        return nullptr;
    }

    return new BinaryOperator(Opcode::Mul, acc, tripScale, "loop_linear_fold_scale");
}

void LoopLinearIterationFoldPass::replaceValueInFunction(Function *func,
                                                         Value *oldValue,
                                                         Value *newValue,
                                                         const set<BasicBlock *> &skipBlocks,
                                                         const set<Instruction *> &skipInsts) const
{
    if (!func || !oldValue || !newValue)
    {
        return;
    }

    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        if (skipBlocks.count(bb))
        {
            continue;
        }

        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (skipInsts.count(inst))
            {
                continue;
            }
            for (size_t i = 0; i < inst->getOperands().size(); ++i)
            {
                Value *op = inst->getOperandByIndex(i);
                if (op == oldValue || sameLoopValue(op, oldValue))
                {
                    inst->setOperandByIndex(i, newValue);
                }
            }
        }
    }
}

bool LoopLinearIterationFoldPass::findLoopAccumulator(const Loop &outer, Value *iv, Value *&acc) const
{
    acc = nullptr;
    BasicBlock *header = outer.header;
    if (!header)
    {
        return false;
    }

    PhiInst *ivPhi = dynamic_cast<PhiInst *>(stripCopy(iv));
    BasicBlock *latch = findLoopLatchBlock(outer);

    for (auto &instPtr : header->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi || phi == ivPhi || sameLoopValue(phi, iv))
        {
            continue;
        }
        bool hasZeroInit = false;
        bool hasLatchIncoming = false;
        for (size_t i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            if (!outer.containsBlock(phi->getIncomingBlock(i)))
            {
                auto *initConst = dynamic_cast<ConstantInt *>(stripCopy(phi->getIncomingValue(i)));
                if (initConst && initConst->Value == 0)
                {
                    hasZeroInit = true;
                }
            }
            else if (latch && phi->getIncomingBlock(i) == latch)
            {
                hasLatchIncoming = true;
            }
        }
        if (hasZeroInit && hasLatchIncoming)
        {
            acc = phi;
            return true;
        }
    }

    if (!latch)
    {
        return false;
    }

    for (auto &instPtr : latch->getInstructions())
    {
        auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
        if (!cpy || sameLoopValue(cpy, iv) || valueDependsOn(cpy->getSource(), iv))
        {
            continue;
        }
        if (!hasZeroInitOutsideLoop(cpy, outer))
        {
            continue;
        }
        acc = cpy;
        return true;
    }

    return false;
}

bool LoopLinearIterationFoldPass::proveLinearIterationMap(const Loop &outer,
                                                          Value *acc,
                                                          Value *iv,
                                                          LinearIterationMap &map)
{
    map = {};
    if (!acc || !outer.header)
    {
        return false;
    }

    PhiInst *accPhi = dynamic_cast<PhiInst *>(stripCopy(acc));
    if (!accPhi)
    {
        return false;
    }

    BasicBlock *latch = findLoopLatchBlock(outer);
    if (!latch)
    {
        return false;
    }

    Value *nextAcc = nullptr;
    for (size_t i = 0; i < accPhi->getNumIncomingValues(); ++i)
    {
        if (accPhi->getIncomingBlock(i) == latch)
        {
            nextAcc = accPhi->getIncomingValue(i);
            break;
        }
    }
    if (!nextAcc)
    {
        return false;
    }

    Value *accVal = stripCopy(acc);
    nextAcc = stripCopy(nextAcc);

    if (sameLoopValue(nextAcc, accVal))
    {
        map.valid = true;
        map.addend = nullptr;
        return true;
    }

    if (!valueDependsOn(nextAcc, accVal))
    {
        return false;
    }

    Value *addend = nullptr;
    unordered_set<Value *> visiting;
    if (peelAccAdds(nextAcc, accVal, addend, visiting))
    {
        if (addend && valueDependsOn(addend, accVal))
        {
            return false;
        }
        auto *zero = dynamic_cast<ConstantInt *>(stripCopy(addend));
        if (zero && zero->Value == 0)
        {
            return false;
        }
        map.valid = true;
        map.addend = addend;
        return true;
    }

    // 嵌套循环经 copy/内层 phi 回传时，peel 可能失败；若 nextAcc 依赖 acc 但不依赖外层 IV，
    // 仍满足 acc' = acc + f() 且 f 与 r 无关。
    if (iv && valueDependsOn(nextAcc, accVal) && !valueDependsOn(nextAcc, iv))
    {
        map.valid = true;
        map.weakIncrement = true;
        return true;
    }

    return false;
}

bool LoopLinearIterationFoldPass::isLinearFoldableOuterBody(const Loop &outer,
                                                            Value *iv,
                                                            Value *acc,
                                                            const vector<Loop> &allLoops) const
{
    if (!iv || !acc || !hasNestedLoopInside(outer, allLoops))
    {
        return false;
    }

    ICmpInst *headerCmp = nullptr;
    if (outer.header && outer.header->getInstructions().size() >= 2)
    {
        headerCmp = dynamic_cast<ICmpInst *>(
            outer.header->getInstructions()[outer.header->getInstructions().size() - 2].get());
    }

    auto perIterBlocks = collectPerIterationBlocks(outer);
    if (perIterBlocks.empty())
    {
        return false;
    }

    for (auto *bb : perIterBlocks)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (dynamic_cast<StoreInst *>(inst) || dynamic_cast<CallInst *>(inst))
            {
                return false;
            }

            bool usesIv = false;
            for (auto *op : inst->getOperands())
            {
                if (valueDependsOn(op, iv))
                {
                    usesIv = true;
                    break;
                }
            }
            if (!usesIv)
            {
                continue;
            }

            if (sameLoopValue(inst, iv) || inst == headerCmp)
            {
                continue;
            }
            if (auto *addInst = dynamic_cast<BinaryOperator *>(inst))
            {
                if (addInst->getOpcode() == Opcode::Add)
                {
                    auto *one = dynamic_cast<ConstantInt *>(stripCopy(addInst->getRHS()));
                    if (one && one->Value == 1 && sameLoopValue(addInst->getLHS(), iv))
                    {
                        continue;
                    }
                }
            }
            if (dynamic_cast<CopyInst *>(inst) && sameLoopValue(inst, iv))
            {
                continue;
            }
            if (dynamic_cast<PhiInst *>(inst) && sameLoopValue(inst, iv))
            {
                continue;
            }
            if (dynamic_cast<BranchInst *>(inst))
            {
                continue;
            }
            return false;
        }
    }

    if (outer.header)
    {
        for (auto &instPtr : outer.header->getInstructions())
        {
            Instruction *inst = instPtr.get();
            bool usesIv = false;
            for (auto *op : inst->getOperands())
            {
                if (valueDependsOn(op, iv))
                {
                    usesIv = true;
                    break;
                }
            }
            if (!usesIv)
            {
                continue;
            }
            if (inst == headerCmp || sameLoopValue(inst, iv))
            {
                continue;
            }
            if (dynamic_cast<PhiInst *>(inst) && sameLoopValue(inst, iv))
            {
                continue;
            }
            if (dynamic_cast<CopyInst *>(inst) && sameLoopValue(inst, iv))
            {
                continue;
            }
            if (dynamic_cast<BranchInst *>(inst))
            {
                continue;
            }
            return false;
        }
    }

    return true;
}

bool LoopLinearIterationFoldPass::tryFoldIdentityCopyNest(Function *func,
                                                          const Loop &outer,
                                                          ICmpInst *cmp,
                                                          int tripCount,
                                                          const vector<Loop> &loops)
{
    if (!func || !cmp || tripCount <= 1)
    {
        return false;
    }

    for (const auto &innerLoop : loops)
    {
        if (innerLoop.header == outer.header)
        {
            continue;
        }
        if (find(outer.blocks.begin(), outer.blocks.end(), innerLoop.header) == outer.blocks.end())
        {
            continue;
        }

        Value *srcArray = nullptr;
        Value *dstArray = nullptr;
        if (!isPureCopyLoop(innerLoop, srcArray, dstArray))
        {
            continue;
        }

        replaceValueInFunction(func, dstArray, srcArray, {});
        cmp->setOperandByIndex(1, new ConstantInt(IntegerType::getInstance(), 1));
        redirectAndRemoveLoop(func, innerLoop);

        if (verbose)
        {
            debugInfo << "LoopLinearIterationFold: folded identity copy nest at "
                      << outer.header->getName() << " (identity), removed inner copy loop "
                      << innerLoop.header->getName() << " (" << dstArray->getName() << " -> "
                      << srcArray->getName() << ")\n";
        }
        return true;
    }

    return false;
}

bool LoopLinearIterationFoldPass::tryFoldLinearAccumulator(Function *func,
                                                           const Loop &outer,
                                                           ICmpInst *cmp,
                                                           Value *iv,
                                                           Value *bound,
                                                           Value *acc,
                                                           Value *tripScale,
                                                           const LinearIterationMap &map)
{
    if (!func || !cmp || !iv || !bound || !acc || !tripScale || !map.valid)
    {
        return false;
    }
    if (!map.weakIncrement && !map.addend)
    {
        return false;
    }

    if (!hasZeroInitOutsideLoop(acc, outer))
    {
        return false;
    }

    if (!map.weakIncrement)
    {
        if (valueDependsOn(map.addend, iv) || valueDependsOn(map.addend, acc))
        {
            return false;
        }
    }

    set<BasicBlock *> loopBlocks(outer.blocks.begin(), outer.blocks.end());
    BasicBlock *exitBlock = nullptr;
    if (!outer.exits.empty())
    {
        exitBlock = outer.exits[0];
    }
    if (!exitBlock)
    {
        auto &headerInsts = outer.header->getInstructions();
        auto *br = dynamic_cast<BranchInst *>(headerInsts.back().get());
        if (br && br->isConditional())
        {
            exitBlock = br->getFalseBlock();
        }
    }
    if (!exitBlock || loopBlocks.count(exitBlock))
    {
        return false;
    }

    Instruction *compensated = this->buildLinearCompensation(acc, tripScale, map);
    if (!compensated)
    {
        return false;
    }

    cmp->setOperandByIndex(1, new ConstantInt(IntegerType::getInstance(), 1));

    unsigned insertIdx = 0;
    for (auto &instPtr : exitBlock->getInstructions())
    {
        if (dynamic_cast<PhiInst *>(instPtr.get()))
        {
            ++insertIdx;
        }
        else
        {
            break;
        }
    }
    exitBlock->insert(unique_ptr<Instruction>(compensated), insertIdx);
    set<Instruction *> skipInsts = {compensated};
    replaceValueInFunction(func, acc, compensated, loopBlocks, skipInsts);

    if (verbose)
    {
        debugInfo << "LoopLinearIterationFold: folded outer loop " << outer.header->getName()
                  << " (acc += invariant addend, scale by trip bound -> 1)\n";
    }
    return true;
}

void LoopLinearIterationFoldPass::redirectAndRemoveLoop(Function *func, const Loop &loop)
{
    if (!func || !loop.header)
    {
        return;
    }

    set<BasicBlock *> loopBlocks(loop.blocks.begin(), loop.blocks.end());

    BasicBlock *preheader = nullptr;
    int externalPreds = 0;
    for (auto *pred : loop.header->getPredecessors())
    {
        if (!loopBlocks.count(pred))
        {
            preheader = pred;
            ++externalPreds;
        }
    }

    BasicBlock *exitBlock = nullptr;
    int externalSuccs = 0;
    for (auto *succ : loop.header->getSuccessors())
    {
        if (!loopBlocks.count(succ))
        {
            exitBlock = succ;
            ++externalSuccs;
        }
    }

    if (externalPreds != 1 || externalSuccs != 1 || !preheader || !exitBlock)
    {
        return;
    }

    for (auto &instPtr : preheader->getInstructions())
    {
        if (auto *br = dynamic_cast<BranchInst *>(instPtr.get()))
        {
            if (br->getTrueBlock() == loop.header)
            {
                preheader->removeSuccessor(loop.header);
                loop.header->removePredecessor(preheader);
                preheader->addSuccessor(exitBlock);
                exitBlock->addPredecessor(preheader);
                br->setTrueBlock(exitBlock);
            }
            if (br->getFalseBlock() == loop.header)
            {
                preheader->removeSuccessor(loop.header);
                loop.header->removePredecessor(preheader);
                preheader->addSuccessor(exitBlock);
                exitBlock->addPredecessor(preheader);
                br->setFalseBlock(exitBlock);
            }
        }
    }

    for (auto *bb : loop.blocks)
    {
        for (auto *succ : bb->getSuccessors())
        {
            if (!loopBlocks.count(succ))
            {
                removePhiIncomingFromPredecessor(succ, bb);
            }
        }
    }

    for (auto *bb : loop.blocks)
    {
        bb->removeSelfBasicBlock();
    }

    auto &bbs = func->getBasicBlocks();
    for (auto it = bbs.begin(); it != bbs.end();)
    {
        if (loopBlocks.count(it->get()))
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

bool LoopLinearIterationFoldPass::runOnFunction(Function *func)
{
    bool changed = false;
    if (!func || func->isLibraryFunction())
    {
        return false;
    }

    func->setLoops(ControlFlowAnalysis::findLoops(func));
    auto loops = func->getLoops();

    for (const auto &outerLoop : loops)
    {
        int tripCount = -1;
        ICmpInst *cmp = nullptr;
        ConstantInt *boundConst = nullptr;
        if (!getFixedTripCountLoopInfo(outerLoop, cmp, boundConst, tripCount) || tripCount <= 1)
        {
            continue;
        }

        if (tryFoldIdentityCopyNest(func, outerLoop, cmp, tripCount, loops))
        {
            func->setLoops(ControlFlowAnalysis::findLoops(func));
            return true;
        }
    }

    func->setLoops(ControlFlowAnalysis::findLoops(func));
    loops = func->getLoops();
    for (const auto &outerLoop : loops)
    {
        ICmpInst *cmp = nullptr;
        Value *iv = nullptr;
        Value *bound = nullptr;
        int constTripCount = -1;
        if (!getCountableOuterLoopInfo(outerLoop, cmp, iv, bound, constTripCount) ||
            !isFoldableTripBound(bound, constTripCount))
        {
            continue;
        }

        Value *acc = nullptr;
        if (!findLoopAccumulator(outerLoop, iv, acc))
        {
            continue;
        }

        LinearIterationMap map;
        if (!proveLinearIterationMap(outerLoop, acc, iv, map))
        {
            continue;
        }
        if (!isLinearFoldableOuterBody(outerLoop, iv, acc, loops))
        {
            continue;
        }

        if (tryFoldLinearAccumulator(func, outerLoop, cmp, iv, bound, acc, bound, map))
        {
            func->setLoops(ControlFlowAnalysis::findLoops(func));
            return true;
        }
    }

    return changed;
}
