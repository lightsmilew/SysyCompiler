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
            if (!outer.containsBlock(body))
            {
                body = br->getFalseBlock();
            }
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

    vector<BasicBlock *> orderPerIterationBlocks(const Loop &outer)
    {
        vector<BasicBlock *> order;
        if (!outer.header)
        {
            return order;
        }

        BasicBlock *body = nullptr;
        auto &headerInsts = outer.header->getInstructions();
        auto *br = dynamic_cast<BranchInst *>(headerInsts.back().get());
        if (br && br->isConditional())
        {
            body = br->getTrueBlock();
            if (!outer.containsBlock(body))
            {
                body = br->getFalseBlock();
            }
        }
        if (!body || !outer.containsBlock(body))
        {
            return order;
        }

        set<BasicBlock *> visited;
        vector<BasicBlock *> worklist = {body};
        visited.insert(body);
        while (!worklist.empty())
        {
            BasicBlock *bb = worklist.front();
            worklist.erase(worklist.begin());
            order.push_back(bb);
            for (auto *succ : bb->getSuccessors())
            {
                if (succ == outer.header || !outer.containsBlock(succ) || visited.count(succ))
                {
                    continue;
                }
                visited.insert(succ);
                worklist.push_back(succ);
            }
        }
        return order;
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

    string getArrayBaseKey(Value *ptr)
    {
        if (!ptr)
        {
            return "";
        }

        Value *current = ptr;
        while (auto *gep = dynamic_cast<GetElementPtrInst *>(current))
        {
            current = gep->getPointerOperand();
        }
        return current ? current->toRef() : "";
    }

    string buildMemoryAccessKey(Value *ptr)
    {
        if (!ptr)
        {
            return "";
        }

        vector<string> indexParts;
        Value *current = ptr;
        while (auto *gep = dynamic_cast<GetElementPtrInst *>(current))
        {
            const auto indices = gep->getIndices();
            const int usefulCount =
                static_cast<int>(indices.size()) - std::max(0, gep->num_addedzero);
            if (usefulCount <= 0)
            {
                return "";
            }

            vector<string> level;
            level.reserve(static_cast<size_t>(usefulCount));
            for (int i = 0; i < usefulCount; ++i)
            {
                if (!indices[static_cast<size_t>(i)])
                {
                    return "";
                }
                level.push_back(indices[static_cast<size_t>(i)]->toRef());
            }
            indexParts.insert(indexParts.begin(), level.begin(), level.end());
            current = gep->getPointerOperand();
        }

        string key = current->toRef();
        for (const auto &part : indexParts)
        {
            key += "#";
            key += part;
        }
        return key;
    }

    bool valueDependsOnLoadAtKeyImpl(Value *val,
                                     const string &key,
                                     unordered_set<Value *> &visited)
    {
        val = stripCopy(val);
        if (!val)
        {
            return false;
        }
        if (!visited.insert(val).second)
        {
            return false;
        }

        if (auto *load = dynamic_cast<LoadInst *>(val))
        {
            if (buildMemoryAccessKey(load->getPointer()) == key)
            {
                return true;
            }
        }

        if (auto *inst = dynamic_cast<Instruction *>(val))
        {
            for (auto *op : inst->getOperands())
            {
                if (valueDependsOnLoadAtKeyImpl(op, key, visited))
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool valueDependsOnLoadAtKey(Value *val, const string &key)
    {
        unordered_set<Value *> visited;
        return valueDependsOnLoadAtKeyImpl(val, key, visited);
    }

    bool isAllowedIvUse(Instruction *inst, Value *iv)
    {
        if (!inst || !iv)
        {
            return false;
        }
        if (sameLoopValue(inst, iv))
        {
            return true;
        }
        if (dynamic_cast<PhiInst *>(inst) && sameLoopValue(inst, iv))
        {
            return true;
        }
        if (dynamic_cast<CopyInst *>(inst) && sameLoopValue(inst, iv))
        {
            return true;
        }
        if (dynamic_cast<BranchInst *>(inst))
        {
            return true;
        }
        if (auto *icmp = dynamic_cast<ICmpInst *>(inst))
        {
            if (icmp->getPredicate() == ICmpInst::ICMP_SLT && sameLoopValue(icmp->getLHS(), iv))
            {
                return true;
            }
        }
        if (auto *addInst = dynamic_cast<BinaryOperator *>(inst))
        {
            if (addInst->getOpcode() == Opcode::Add)
            {
                auto *one = dynamic_cast<ConstantInt *>(stripCopy(addInst->getRHS()));
                if (one && one->Value == 1 && sameLoopValue(addInst->getLHS(), iv))
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool tryReadLoopControlFromBlock(BasicBlock *bb,
                                     ICmpInst *&cmp,
                                     Value *&iv,
                                     Value *&bound,
                                     int &constTripCount)
    {
        cmp = nullptr;
        iv = nullptr;
        bound = nullptr;
        constTripCount = -1;
        if (!bb || bb->getInstructions().size() < 2)
        {
            return false;
        }

        auto &insts = bb->getInstructions();
        auto *br = dynamic_cast<BranchInst *>(insts.back().get());
        if (!br || !br->isConditional())
        {
            return false;
        }

        cmp = dynamic_cast<ICmpInst *>(insts[insts.size() - 2].get());
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

        if (auto *boundConst = dynamic_cast<ConstantInt *>(stripCopy(bound)))
        {
            constTripCount = boundConst->Value;
        }
        return true;
    }
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

    struct LoopControlCandidate
    {
        ICmpInst *cmp = nullptr;
        Value *iv = nullptr;
        Value *bound = nullptr;
        int trip = -1;
        bool ivUnusedInBody = false;
    };

    vector<LoopControlCandidate> candidates;
    auto tryCollect = [&](BasicBlock *bb) {
        if (!bb)
        {
            return;
        }
        LoopControlCandidate cand;
        if (!tryReadLoopControlFromBlock(bb, cand.cmp, cand.iv, cand.bound, cand.trip))
        {
            return;
        }
        if (!hasZeroInitOutsideLoop(cand.iv, loop) || !hasUnitIncrementAtLatch(cand.iv, loop))
        {
            return;
        }
        cand.ivUnusedInBody = isOuterIvUnusedInBody(loop, cand.iv);
        candidates.push_back(cand);
    };

    tryCollect(loop.header);
    tryCollect(findLoopLatchBlock(loop));

    if (candidates.empty())
    {
        return false;
    }

    const LoopControlCandidate *best = nullptr;
    for (const auto &cand : candidates)
    {
        if (!cand.ivUnusedInBody)
        {
            continue;
        }
        if (!best || (best->trip <= 1 && cand.trip > 1) ||
            (cand.trip > 1 && best->trip > 1 && cand.trip < best->trip))
        {
            best = &cand;
        }
    }
    if (!best)
    {
        for (const auto &cand : candidates)
        {
            if (!best || cand.trip > best->trip)
            {
                best = &cand;
            }
        }
    }
    if (!best)
    {
        return false;
    }

    cmp = best->cmp;
    iv = best->iv;
    bound = best->bound;
    constTripCount = best->trip;
    return true;
}

bool LoopLinearIterationFoldPass::isOuterIvUnusedInBody(const Loop &outer, Value *iv) const
{
    if (!iv || !outer.header)
    {
        return false;
    }

    for (auto *bb : outer.blocks)
    {
        for (auto &instPtr : bb->getInstructions())
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
            if (!isAllowedIvUse(inst, iv))
            {
                return false;
            }
        }
    }
    return true;
}

bool LoopLinearIterationFoldPass::allLoopCarriedValuesIterationInvariant(const Loop &outer,
                                                                         Value *iv) const
{
    if (!outer.header)
    {
        return false;
    }

    BasicBlock *latch = findLoopLatchBlock(outer);
    if (!latch)
    {
        return false;
    }

    for (auto &instPtr : outer.header->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi || sameLoopValue(phi, iv))
        {
            continue;
        }

        Value *nextVal = nullptr;
        for (size_t i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            if (phi->getIncomingBlock(i) == latch)
            {
                nextVal = phi->getIncomingValue(i);
                break;
            }
        }
        if (!nextVal || !sameLoopValue(stripCopy(nextVal), stripCopy(phi)))
        {
            return false;
        }
    }
    return true;
}

bool LoopLinearIterationFoldPass::provePerElementFirstStoreFresh(const Loop &outer) const
{
    if (!outer.header || !outer.header->Parent)
    {
        return false;
    }

    auto perIterBlocks = collectPerIterationBlocks(outer);
    if (perIterBlocks.empty())
    {
        return false;
    }

    unordered_map<string, bool> seenCellKey;
    unordered_set<string> seenArrayBase;
    for (BasicBlock *bb : orderPerIterationBlocks(outer))
    {
        if (!bb)
        {
            continue;
        }

        for (auto &instPtr : bb->getInstructions())
        {
            auto *store = dynamic_cast<StoreInst *>(instPtr.get());
            if (!store)
            {
                continue;
            }

            const string cellKey = buildMemoryAccessKey(store->getPointer());
            const string baseKey = getArrayBaseKey(store->getPointer());
            if (cellKey.empty() || baseKey.empty())
            {
                return false;
            }
            if (seenCellKey.count(cellKey))
            {
                continue;
            }
            seenCellKey[cellKey] = true;
            if (seenArrayBase.count(baseKey))
            {
                continue;
            }
            seenArrayBase.insert(baseKey);
            if (valueDependsOnLoadAtKey(store->getValueToStore(), cellKey))
            {
                return false;
            }
        }
    }
    return true;
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

bool LoopLinearIterationFoldPass::tryFoldIterationInvariantOuterLoop(Function *func,
                                                                     const Loop &outer,
                                                                     ICmpInst *cmp,
                                                                     Value *iv,
                                                                     int constTripCount)
{
    if (!func || !cmp || !iv || constTripCount <= 1)
    {
        return false;
    }

    if (!isOuterIvUnusedInBody(outer, iv))
    {
        return false;
    }
    if (!allLoopCarriedValuesIterationInvariant(outer, iv))
    {
        return false;
    }
    if (!provePerElementFirstStoreFresh(outer))
    {
        return false;
    }

    auto *newBound = new ConstantInt(IntegerType::getInstance(), 1);
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        for (auto &instPtr : bb->getInstructions())
        {
            auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get());
            if (!icmp || icmp->getPredicate() != ICmpInst::ICMP_SLT)
            {
                continue;
            }
            if (!sameLoopValue(icmp->getLHS(), iv))
            {
                continue;
            }
            auto *boundConst = dynamic_cast<ConstantInt *>(stripCopy(icmp->getRHS()));
            if (!boundConst || boundConst->Value != constTripCount)
            {
                continue;
            }
            icmp->setOperandByIndex(1, newBound);
        }
    }

    if (verbose)
    {
        debugInfo << "LoopLinearIterationFold: folded iteration-invariant outer loop "
                  << outer.header->getName() << " (trip " << constTripCount << " -> 1)\n";
    }
    return true;
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
        ICmpInst *cmp = nullptr;
        Value *iv = nullptr;
        Value *bound = nullptr;
        int constTripCount = -1;
        if (!getCountableOuterLoopInfo(outerLoop, cmp, iv, bound, constTripCount) ||
            !isFoldableTripBound(bound, constTripCount))
        {
            continue;
        }

        if (constTripCount > 1 &&
            isOuterIvUnusedInBody(outerLoop, iv) &&
            tryFoldIterationInvariantOuterLoop(func, outerLoop, cmp, iv, constTripCount))
        {
            func->setLoops(ControlFlowAnalysis::findLoops(func));
            return true;
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
