#include "LoopLinearIterationFoldPass.h"
#include <algorithm>
#include <unordered_map>
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

    bool hasUnitDecrementAtLatch(Value *iv, const Loop &loop)
    {
        BasicBlock *latch = findLoopLatchBlock(loop);
        if (!latch)
        {
            return false;
        }

        for (auto &instPtr : latch->getInstructions())
        {
            auto *subInst = dynamic_cast<BinaryOperator *>(instPtr.get());
            if (!subInst || subInst->getOpcode() != Opcode::Sub)
            {
                continue;
            }
            auto *one = dynamic_cast<ConstantInt *>(stripCopy(subInst->getRHS()));
            if (!one || one->Value != 1)
            {
                continue;
            }
            if (sameLoopValue(subInst->getLHS(), iv))
            {
                return true;
            }
        }
        return false;
    }

    // 在循环外找到将 iv 初始化为常量的 copy（倒计数 n=N）
    Instruction *findConstInitOutsideLoop(Value *iv, const Loop &loop, int &initVal)
    {
        initVal = 0;
        if (!iv || !loop.header)
        {
            return nullptr;
        }

        if (auto *trackedPhi = dynamic_cast<PhiInst *>(stripCopy(iv)))
        {
            for (size_t i = 0; i < trackedPhi->getNumIncomingValues(); ++i)
            {
                if (loop.containsBlock(trackedPhi->getIncomingBlock(i)))
                {
                    continue;
                }
                auto *initConst = dynamic_cast<ConstantInt *>(stripCopy(trackedPhi->getIncomingValue(i)));
                if (initConst && initConst->Value > 1)
                {
                    initVal = initConst->Value;
                    return trackedPhi;
                }
            }
            return nullptr;
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
                if (!cpy || !sameLoopValue(cpy, iv))
                {
                    continue;
                }
                auto *initConst = dynamic_cast<ConstantInt *>(stripCopy(cpy->getSource()));
                if (initConst && initConst->Value > 1)
                {
                    initVal = initConst->Value;
                    return cpy;
                }
            }
        }

        // while 形态：入口块直接落到 body，初始化可能在更外层 entry
        Function *func = loop.header->Parent;
        if (!func)
        {
            return nullptr;
        }
        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            if (!bb || loop.containsBlock(bb))
            {
                continue;
            }
            for (auto &instPtr : bb->getInstructions())
            {
                auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
                if (!cpy || !sameLoopValue(cpy, iv))
                {
                    continue;
                }
                auto *initConst = dynamic_cast<ConstantInt *>(stripCopy(cpy->getSource()));
                if (initConst && initConst->Value > 1)
                {
                    initVal = initConst->Value;
                    return cpy;
                }
            }
        }
        return nullptr;
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
        else if (br && !br->isConditional())
        {
            // latch 处判断的 while：header 即 body 入口
            body = outer.header;
        }
        if (!body || !outer.containsBlock(body))
        {
            // 回退：整圈循环块都算每轮执行
            for (auto *bb : outer.blocks)
            {
                if (bb)
                {
                    perIter.insert(bb);
                }
            }
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
                // header 自身作为 body 起点时，允许沿后继扩展到 header 以外的环内块
                perIter.insert(succ);
                worklist.push_back(succ);
            }
        }
        // header==body 时，上面 succ==header 被跳过是对的；但 header 已在 perIter
        if (body == outer.header)
        {
            for (auto *bb : outer.blocks)
            {
                if (bb)
                {
                    perIter.insert(bb);
                }
            }
        }
        return perIter;
    }

    vector<BasicBlock *> orderPerIterationBlocks(const Loop &outer)
    {
        vector<BasicBlock *> order;
        auto perIter = collectPerIterationBlocks(outer);
        if (perIter.empty() || !outer.header)
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
        else
        {
            body = outer.header;
        }
        if (!body || !perIter.count(body))
        {
            for (auto *bb : outer.blocks)
            {
                if (bb && perIter.count(bb))
                {
                    order.push_back(bb);
                }
            }
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
                if (!perIter.count(succ) || visited.count(succ))
                {
                    continue;
                }
                // 避免仅因回边立刻再次进入 header 造成重复；header 已作为起点时跳过回边
                if (succ == outer.header && body == outer.header)
                {
                    continue;
                }
                if (succ == outer.header && body != outer.header)
                {
                    continue;
                }
                visited.insert(succ);
                worklist.push_back(succ);
            }
        }
        for (auto *bb : perIter)
        {
            if (!visited.count(bb))
            {
                order.push_back(bb);
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

    Value *getArrayBaseValue(Value *ptr)
    {
        if (!ptr)
        {
            return nullptr;
        }

        Value *current = ptr;
        while (auto *gep = dynamic_cast<GetElementPtrInst *>(current))
        {
            current = gep->getPointerOperand();
        }
        return current;
    }

    string getArrayBaseKey(Value *ptr)
    {
        Value *base = getArrayBaseValue(ptr);
        return base ? base->toRef() : "";
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
            if (icmp->getPredicate() == ICmpInst::ICMP_SLT)
            {
                // count-up: iv < bound
                if (sameLoopValue(icmp->getLHS(), iv))
                {
                    return true;
                }
                // count-down: 0 < iv
                if (sameLoopValue(icmp->getRHS(), iv))
                {
                    auto *zero = dynamic_cast<ConstantInt *>(stripCopy(icmp->getLHS()));
                    if (zero && zero->Value == 0)
                    {
                        return true;
                    }
                }
            }
        }
        if (auto *bin = dynamic_cast<BinaryOperator *>(inst))
        {
            auto *one = dynamic_cast<ConstantInt *>(stripCopy(bin->getRHS()));
            if (one && one->Value == 1 && sameLoopValue(bin->getLHS(), iv))
            {
                if (bin->getOpcode() == Opcode::Add || bin->getOpcode() == Opcode::Sub)
                {
                    return true;
                }
            }
        }
        return false;
    }

    // count-up: icmp slt iv, bound
    bool tryReadCountUpControl(BasicBlock *bb,
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
        // 排除 countdown 形态 0 < n
        if (dynamic_cast<ConstantInt *>(stripCopy(iv)))
        {
            return false;
        }

        if (auto *boundConst = dynamic_cast<ConstantInt *>(stripCopy(bound)))
        {
            constTripCount = boundConst->Value;
        }
        return true;
    }

    // count-down: icmp slt 0, iv 或 icmp sgt iv, 0  （while (n > 0)）
    bool tryReadCountDownControl(BasicBlock *bb, ICmpInst *&cmp, Value *&iv)
    {
        cmp = nullptr;
        iv = nullptr;
        if (!bb || bb->getInstructions().empty())
        {
            return false;
        }

        auto &insts = bb->getInstructions();
        auto *br = dynamic_cast<BranchInst *>(insts.back().get());
        if (!br || !br->isConditional())
        {
            return false;
        }

        // 优先看 br 前一条，再扫描整块（中间可能夹杂 copy）
        vector<ICmpInst *> icmps;
        if (insts.size() >= 2)
        {
            if (auto *c = dynamic_cast<ICmpInst *>(insts[insts.size() - 2].get()))
            {
                icmps.push_back(c);
            }
        }
        for (auto &instPtr : insts)
        {
            if (auto *c = dynamic_cast<ICmpInst *>(instPtr.get()))
            {
                if (icmps.empty() || icmps.front() != c)
                {
                    icmps.push_back(c);
                }
            }
        }

        for (auto *cand : icmps)
        {
            if (cand->getPredicate() == ICmpInst::ICMP_SLT)
            {
                auto *zero = dynamic_cast<ConstantInt *>(stripCopy(cand->getLHS()));
                if (zero && zero->Value == 0 && cand->getRHS() &&
                    !dynamic_cast<ConstantInt *>(stripCopy(cand->getRHS())))
                {
                    cmp = cand;
                    iv = cand->getRHS();
                    return true;
                }
            }
            if (cand->getPredicate() == ICmpInst::ICMP_SGT)
            {
                auto *zero = dynamic_cast<ConstantInt *>(stripCopy(cand->getRHS()));
                if (zero && zero->Value == 0 && cand->getLHS() &&
                    !dynamic_cast<ConstantInt *>(stripCopy(cand->getLHS())))
                {
                    cmp = cand;
                    iv = cand->getLHS();
                    return true;
                }
            }
        }
        return false;
    }
}

bool LoopLinearIterationFoldPass::getCountableOuterLoopInfo(const Loop &loop,
                                                            CountableLoopInfo &info) const
{
    info = CountableLoopInfo{};

    struct LoopControlCandidate
    {
        CountableLoopInfo info;
        bool ivUnusedInBody = false;
    };

    vector<LoopControlCandidate> candidates;
    auto tryCollectCountUp = [&](BasicBlock *bb) {
        if (!bb)
        {
            return;
        }
        LoopControlCandidate cand;
        if (!tryReadCountUpControl(bb, cand.info.cmp, cand.info.iv, cand.info.bound, cand.info.constTripCount))
        {
            return;
        }
        if (!hasZeroInitOutsideLoop(cand.info.iv, loop) || !hasUnitIncrementAtLatch(cand.info.iv, loop))
        {
            return;
        }
        cand.info.countDown = false;
        cand.ivUnusedInBody = isOuterIvUnusedInBody(loop, cand.info.iv);
        candidates.push_back(cand);
    };
    auto tryCollectCountDown = [&](BasicBlock *bb) {
        if (!bb)
        {
            return;
        }
        LoopControlCandidate cand;
        if (!tryReadCountDownControl(bb, cand.info.cmp, cand.info.iv))
        {
            return;
        }
        if (!hasUnitDecrementAtLatch(cand.info.iv, loop))
        {
            return;
        }
        int initVal = 0;
        Instruction *initInst = findConstInitOutsideLoop(cand.info.iv, loop, initVal);
        if (!initInst || initVal <= 1)
        {
            return;
        }
        cand.info.countDown = true;
        cand.info.constTripCount = initVal;
        cand.info.bound = new ConstantInt(IntegerType::getInstance(), initVal);
        cand.info.initInst = initInst;
        cand.ivUnusedInBody = isOuterIvUnusedInBody(loop, cand.info.iv);
        candidates.push_back(cand);
    };

    tryCollectCountUp(loop.header);
    tryCollectCountUp(findLoopLatchBlock(loop));
    tryCollectCountDown(loop.header);
    tryCollectCountDown(findLoopLatchBlock(loop));

    // countdown 的 icmp 可能在 latch 末尾，而 header 是 body（无 cmp）
    if (candidates.empty())
    {
        for (auto *bb : loop.blocks)
        {
            tryCollectCountDown(bb);
            tryCollectCountUp(bb);
        }
    }

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
        if (!best || (best->info.constTripCount <= 1 && cand.info.constTripCount > 1) ||
            (cand.info.constTripCount > 1 && best->info.constTripCount > 1 &&
             cand.info.constTripCount < best->info.constTripCount))
        {
            best = &cand;
        }
    }
    if (!best)
    {
        for (const auto &cand : candidates)
        {
            if (!best || cand.info.constTripCount > best->info.constTripCount)
            {
                best = &cand;
            }
        }
    }
    if (!best)
    {
        return false;
    }

    info = best->info;
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

// 每轮覆盖写：回边带来的新值不依赖旧 phi（最后一轮决定 live-out），或保持不变
bool LoopLinearIterationFoldPass::allLoopCarriedValuesOverwrittenOrInvariant(const Loop &outer,
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
        if (!nextVal)
        {
            return false;
        }
        if (sameLoopValue(stripCopy(nextVal), stripCopy(phi)))
        {
            continue; // invariant
        }
        if (valueDependsOn(nextVal, phi))
        {
            return false; // 累加/依赖旧值，非纯覆盖
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

            Value *base = getArrayBaseValue(store->getPointer());
            auto *gv = dynamic_cast<GlobalVariable *>(base);
            // 标量 RMW（如 buf = buf | x）不要求“首存不读旧值”；只约束数组元素
            if (gv && !gv->isArray())
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

bool LoopLinearIterationFoldPass::proveNoReadWriteGlobalLoadBeforeFirstStore(const Loop &outer) const
{
    if (!outer.header || !outer.header->Parent)
    {
        return false;
    }

    auto ordered = orderPerIterationBlocks(outer);
    if (ordered.empty())
    {
        return false;
    }

    unordered_set<string> readWriteGlobalBases;
    for (BasicBlock *bb : ordered)
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
            Value *base = getArrayBaseValue(store->getPointer());
            if (!base || !base->isGlobal())
            {
                continue;
            }
            const string baseKey = getArrayBaseKey(store->getPointer());
            if (!baseKey.empty())
            {
                readWriteGlobalBases.insert(baseKey);
            }
        }
    }

    if (readWriteGlobalBases.empty())
    {
        return true;
    }

    unordered_set<string> storedGlobalBases;
    for (BasicBlock *bb : ordered)
    {
        if (!bb)
        {
            continue;
        }
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (auto *load = dynamic_cast<LoadInst *>(inst))
            {
                const string baseKey = getArrayBaseKey(load->getPointer());
                if (baseKey.empty() || !readWriteGlobalBases.count(baseKey))
                {
                    continue;
                }
                if (!storedGlobalBases.count(baseKey))
                {
                    return false;
                }
                continue;
            }

            auto *store = dynamic_cast<StoreInst *>(inst);
            if (!store)
            {
                continue;
            }
            const string baseKey = getArrayBaseKey(store->getPointer());
            if (!baseKey.empty() && readWriteGlobalBases.count(baseKey))
            {
                storedGlobalBases.insert(baseKey);
            }
        }
    }
    return true;
}

bool LoopLinearIterationFoldPass::proveEarlyLoadsOnlyScalarRWGlobals(const Loop &outer) const
{
    if (!outer.header || !outer.header->Parent)
    {
        return false;
    }

    auto ordered = orderPerIterationBlocks(outer);
    if (ordered.empty())
    {
        return false;
    }

    unordered_map<string, Value *> rwGlobalBase;
    for (BasicBlock *bb : ordered)
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
            Value *base = getArrayBaseValue(store->getPointer());
            if (!base || !base->isGlobal())
            {
                continue;
            }
            const string baseKey = getArrayBaseKey(store->getPointer());
            if (!baseKey.empty())
            {
                rwGlobalBase[baseKey] = base;
            }
        }
    }

    if (rwGlobalBase.empty())
    {
        return true;
    }

    unordered_set<string> storedGlobalBases;
    for (BasicBlock *bb : ordered)
    {
        if (!bb)
        {
            continue;
        }
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (auto *load = dynamic_cast<LoadInst *>(inst))
            {
                const string baseKey = getArrayBaseKey(load->getPointer());
                if (baseKey.empty() || !rwGlobalBase.count(baseKey))
                {
                    continue;
                }
                if (storedGlobalBases.count(baseKey))
                {
                    continue;
                }
                // 首次 load 早于 store：仅允许标量全局（如 buf），禁止数组
                Value *base = rwGlobalBase[baseKey];
                auto *gv = dynamic_cast<GlobalVariable *>(base);
                if (!gv || gv->isArray())
                {
                    return false;
                }
                continue;
            }

            auto *store = dynamic_cast<StoreInst *>(inst);
            if (!store)
            {
                continue;
            }
            const string baseKey = getArrayBaseKey(store->getPointer());
            if (!baseKey.empty() && rwGlobalBase.count(baseKey))
            {
                storedGlobalBases.insert(baseKey);
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
                                                                     const CountableLoopInfo &info)
{
    if (!func || !info.cmp || !info.iv || info.constTripCount <= 1)
    {
        return false;
    }

    if (!isOuterIvUnusedInBody(outer, info.iv))
    {
        if (verbose)
        {
            debugInfo << "LoopLinearIterationFold: reject " << outer.header->getName()
                      << " (iv used in body)\n";
        }
        return false;
    }
    // 严格不变，或每轮覆盖写（最后一轮决定出口值）
    if (!allLoopCarriedValuesIterationInvariant(outer, info.iv) &&
        !allLoopCarriedValuesOverwrittenOrInvariant(outer, info.iv))
    {
        if (verbose)
        {
            debugInfo << "LoopLinearIterationFold: reject " << outer.header->getName()
                      << " (carried not overwrite/invariant)\n";
        }
        return false;
    }
    if (!provePerElementFirstStoreFresh(outer))
    {
        if (verbose)
        {
            debugInfo << "LoopLinearIterationFold: reject " << outer.header->getName()
                      << " (array first-store not fresh)\n";
        }
        return false;
    }
    // 完全自包含，或仅有标量全局的跨轮残留读（如 huffman buf）
    if (!proveNoReadWriteGlobalLoadBeforeFirstStore(outer) &&
        !proveEarlyLoadsOnlyScalarRWGlobals(outer))
    {
        if (verbose)
        {
            debugInfo << "LoopLinearIterationFold: reject " << outer.header->getName()
                      << " (early RW load)\n";
        }
        return false;
    }

    if (info.countDown)
    {
        // while (n > 0) { ...; n--; } 且 n 初值为 N → 把初值改为 1
        if (!info.initInst)
        {
            if (verbose)
            {
                debugInfo << "LoopLinearIterationFold: reject " << outer.header->getName()
                          << " (countdown missing init)\n";
            }
            return false;
        }
        if (auto *cpy = dynamic_cast<CopyInst *>(info.initInst))
        {
            cpy->setOperandByIndex(0, new ConstantInt(IntegerType::getInstance(), 1));
        }
        else if (auto *phi = dynamic_cast<PhiInst *>(info.initInst))
        {
            for (size_t i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                if (outer.containsBlock(phi->getIncomingBlock(i)))
                {
                    continue;
                }
                auto *initConst = dynamic_cast<ConstantInt *>(stripCopy(phi->getIncomingValue(i)));
                if (initConst && initConst->Value == info.constTripCount)
                {
                    phi->setIncomingValue(i, new ConstantInt(IntegerType::getInstance(), 1));
                }
            }
        }
        else
        {
            if (verbose)
            {
                debugInfo << "LoopLinearIterationFold: reject " << outer.header->getName()
                          << " (countdown init not copy/phi)\n";
            }
            return false;
        }
    }
    else
    {
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
                if (!sameLoopValue(icmp->getLHS(), info.iv))
                {
                    continue;
                }
                auto *boundConst = dynamic_cast<ConstantInt *>(stripCopy(icmp->getRHS()));
                if (!boundConst || boundConst->Value != info.constTripCount)
                {
                    continue;
                }
                icmp->setOperandByIndex(1, newBound);
            }
        }
    }

    if (verbose)
    {
        debugInfo << "LoopLinearIterationFold: folded iteration-invariant/overwrite outer loop "
                  << outer.header->getName() << " (trip " << info.constTripCount << " -> 1"
                  << (info.countDown ? ", countdown" : "") << ")\n";
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
        CountableLoopInfo info;
        if (!getCountableOuterLoopInfo(outerLoop, info) ||
            !isFoldableTripBound(info.bound, info.constTripCount))
        {
            continue;
        }

        if (verbose)
        {
            debugInfo << "LoopLinearIterationFold: candidate " << outerLoop.header->getName()
                      << " trip=" << info.constTripCount
                      << (info.countDown ? " countdown" : " countup") << "\n";
        }

        if (info.constTripCount > 1 &&
            isOuterIvUnusedInBody(outerLoop, info.iv) &&
            tryFoldIterationInvariantOuterLoop(func, outerLoop, info))
        {
            func->setLoops(ControlFlowAnalysis::findLoops(func));
            return true;
        }

        Value *acc = nullptr;
        if (!findLoopAccumulator(outerLoop, info.iv, acc))
        {
            continue;
        }

        LinearIterationMap map;
        if (!proveLinearIterationMap(outerLoop, acc, info.iv, map))
        {
            continue;
        }
        if (!isLinearFoldableOuterBody(outerLoop, info.iv, acc, loops))
        {
            continue;
        }

        if (tryFoldLinearAccumulator(func, outerLoop, info.cmp, info.iv, info.bound, acc, info.bound, map))
        {
            func->setLoops(ControlFlowAnalysis::findLoops(func));
            return true;
        }
    }

    return changed;
}
