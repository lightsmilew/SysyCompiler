#include "LoopPointerInductionPass.h"
#include <algorithm>
#include <limits>
#include <unordered_map>

using namespace std;
using namespace optimization;

Value *LoopPointerInductionPass::stripCopy(Value *v)
{
    while (auto *cpy = dynamic_cast<CopyInst *>(v))
        v = cpy->getSource();
    return v;
}

bool LoopPointerInductionPass::sameLoopValue(Value *a, Value *b)
{
    if (!a || !b)
        return false;
    return stripCopy(a) == stripCopy(b);
}

bool LoopPointerInductionPass::isLoopInvariant(Value *val, const Loop &loop) const
{
    if (!val)
        return false;
    auto *def = dynamic_cast<Instruction *>(val);
    if (!def)
        return true;
    return !loop.containsInst(def);
}

int LoopPointerInductionPass::getElemSizeBytes(Type *ty)
{
    if (!ty)
        return 4;
    if (ty->isIntegerTy() || ty->isFloatTy())
        return 4;
    if (ty->isLongTy())
        return 8;
    return 4;
}

/// 递归计算类型字节大小（嵌套数组按元素展开）
static int64_t typeSizeBytes(Type *ty)
{
    if (!ty)
        return 4;
    if (auto *arr = dynamic_cast<ArrayType *>(ty))
        return static_cast<int64_t>(arr->NumElements) * typeSizeBytes(arr->ElementType);
    if (ty->isIntegerTy() || ty->isFloatTy())
        return 4;
    if (ty->isLongTy())
        return 8;
    return 4;
}

int64_t LoopPointerInductionPass::strideBytesForVaryingIndex(GetElementPtrInst *gep, int varyPos)
{
    // GEP：第 0 个下标按「指针所指类型」整块步进；其后下标深入聚合体。
    Type *ty = gep->getPointerOperand()->getType();
    if (auto *ptrTy = dynamic_cast<PointerType *>(ty))
        ty = ptrTy->ElementType;
    for (int i = 0; i < varyPos && ty; ++i)
    {
        if (auto *arr = dynamic_cast<ArrayType *>(ty))
            ty = arr->ElementType;
        else
            break;
    }
    return typeSizeBytes(ty);
}

bool LoopPointerInductionPass::matchIVIndex(Value *index, const InductionVarInfo &iv,
                                            int64_t &constOffset) const
{
    constOffset = 0;
    Value *v = stripCopy(index);
    if (!v)
        return false;

    // 展开 iv+c0+c1+...（循环展开 lane 常见）
    while (auto *add = dynamic_cast<BinaryOperator *>(v))
    {
        if (add->getOpcode() != Opcode::Add)
            break;
        Value *lhs = stripCopy(add->getLHS());
        Value *rhs = stripCopy(add->getRHS());
        if (auto *c = dynamic_cast<ConstantInt *>(rhs))
        {
            constOffset += c->Value;
            v = lhs;
            continue;
        }
        if (auto *c = dynamic_cast<ConstantInt *>(lhs))
        {
            constOffset += c->Value;
            v = rhs;
            continue;
        }
        break;
    }
    return sameLoopValue(v, iv.iv);
}

void LoopPointerInductionPass::appendIndexToKey(GepPromoKey &key, Value *idx)
{
    idx = stripCopy(idx);
    if (auto *c = dynamic_cast<ConstantInt *>(idx))
    {
        key.constSlots.emplace_back(true, c->Value);
        key.varSlots.push_back(nullptr);
    }
    else
    {
        key.constSlots.emplace_back(false, 0);
        key.varSlots.push_back(idx);
    }
}

bool LoopPointerInductionPass::flattenInvariantBase(Value *base, const Loop &loop,
                                                    GepPromoKey &key) const
{
    vector<GetElementPtrInst *> chain;
    Value *cursor = stripCopy(base);
    while (auto *g = dynamic_cast<GetElementPtrInst *>(cursor))
    {
        // 环外 GEP：整段作为根，不再拆
        if (!loop.containsInst(g))
            break;
        for (Value *idx : g->getIndices())
        {
            if (!isLoopInvariant(idx, loop))
                return false;
        }
        chain.push_back(g);
        cursor = stripCopy(g->getPointerOperand());
    }
    if (auto *inst = dynamic_cast<Instruction *>(cursor))
    {
        if (loop.containsInst(inst))
            return false;
    }

    key.base = cursor;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it)
    {
        for (Value *idx : (*it)->getIndices())
            appendIndexToKey(key, idx);
    }
    return true;
}

BinaryOperator *LoopPointerInductionPass::findIVIncrement(BasicBlock *latch, Value *iv,
                                                          int64_t &step) const
{
    if (!latch || !iv)
        return nullptr;

    auto tryAdd = [&](BinaryOperator *addInst) -> BinaryOperator * {
        if (!addInst || addInst->getOpcode() != Opcode::Add)
            return nullptr;
        if (sameLoopValue(addInst->getLHS(), iv))
        {
            if (auto *stepC = dynamic_cast<ConstantInt *>(stripCopy(addInst->getRHS())))
            {
                step = stepC->Value;
                return addInst;
            }
        }
        if (sameLoopValue(addInst->getRHS(), iv))
        {
            if (auto *stepC = dynamic_cast<ConstantInt *>(stripCopy(addInst->getLHS())))
            {
                step = stepC->Value;
                return addInst;
            }
        }
        return nullptr;
    };

    for (auto &instPtr : latch->getInstructions())
    {
        auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
        if (!cpy || !sameLoopValue(cpy, iv))
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

bool LoopPointerInductionPass::findBasicIV(const Loop &loop, InductionVarInfo &info) const
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

    // 唯一 latch：拒绝多回边（含 early-continue / 复杂 unroll 残留）
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
    Value *cmpLHS = stripCopy(cmp->getLHS());
    for (auto &instPtr : header->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi)
            continue;
        if (sameLoopValue(phi, cmpLHS))
        {
            ivPhi = phi;
            break;
        }
    }
    if (!ivPhi)
        return false;

    // 仅接受「preheader 初值 + latch 递推」两路 phi；展开余数环也是这种形态
    if (ivPhi->getNumIncomingValues() != 2)
        return false;

    info.iv = ivPhi;
    info.phi = ivPhi;
    info.init = nullptr;
    for (unsigned i = 0; i < ivPhi->getNumIncomingValues(); ++i)
    {
        BasicBlock *incoming = ivPhi->getIncomingBlock(i);
        if (incoming == preheader)
            info.init = ivPhi->getIncomingValue(i);
        else if (incoming != latch)
            return false;
    }
    if (!info.init)
        return false;

    info.inc = findIVIncrement(latch, ivPhi, info.step);
    if (!info.inc || info.step == 0)
        return false;
    // 步长过大通常意味着识别错误，拒绝以免错误扩地址
    if (info.step > 1024 || info.step < -1024)
        return false;
    return true;
}

Value *LoopPointerInductionPass::materializeInvariantInPreheader(Value *v, BasicBlock *preheader,
                                                                 const Loop &loop,
                                                                 const string &namePrefix)
{
    v = stripCopy(v);
    if (!v || !preheader)
        return nullptr;

    if (isLoopInvariant(v, loop))
        return v;

    auto *gep = dynamic_cast<GetElementPtrInst *>(v);
    if (!gep || !loop.containsInst(gep))
        return nullptr;

    Value *base = materializeInvariantInPreheader(gep->getPointerOperand(), preheader, loop,
                                                  namePrefix + "_b");
    if (!base)
        return nullptr;

    vector<Value *> indices;
    indices.reserve(gep->getIndices().size());
    for (Value *idx : gep->getIndices())
    {
        Value *m = materializeInvariantInPreheader(idx, preheader, loop, namePrefix + "_i");
        if (!m)
            return nullptr;
        indices.push_back(m);
    }

    auto *cloned = new GetElementPtrInst(base, indices, namePrefix + "_remat");
    preheader->insertBeforeTerminator(unique_ptr<Instruction>(cloned));
    return cloned;
}

BasicBlock *LoopPointerInductionPass::findInstructionBlock(const Loop &loop,
                                                           Instruction *inst) const
{
    if (!inst)
        return nullptr;
    for (auto *bb : loop.blocks)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            if (instPtr.get() == inst)
                return bb;
        }
    }
    return nullptr;
}

void LoopPointerInductionPass::eraseInstruction(BasicBlock *bb, Instruction *inst,
                                                vector<Value *> &needToDelete)
{
    if (!bb || !inst)
        return;
    auto &insts = bb->getInstructions();
    for (auto it = insts.begin(); it != insts.end(); ++it)
    {
        if (it->get() == inst)
        {
            inst->removeThisFromOperands();
            needToDelete.push_back(it->release());
            insts.erase(it);
            return;
        }
    }
}

bool LoopPointerInductionPass::tryPromoteGep(
    const Loop &loop, const InductionVarInfo &iv, GetElementPtrInst *gep,
    unordered_map<GepPromoKey, PhiInst *, GepPromoKeyHash> &cache)
{
    if (!gep || gep->getOpcode() != Opcode::GetElementPtr)
        return false;
    if (!gep->getType() || !gep->getType()->isPointerTy())
        return false;

    BasicBlock *gepBB = findInstructionBlock(loop, gep);
    if (!gepBB || gepBB == loop.header)
        return false;

    const auto &indices = gep->getIndices();
    if (indices.empty())
        return false;

    // 恰有一维仿射依赖于当前 IV；其余维必须对本环不变
    int varyPos = -1;
    int64_t indexOffset = 0;
    for (size_t i = 0; i < indices.size(); ++i)
    {
        int64_t off = 0;
        if (matchIVIndex(indices[i], iv, off))
        {
            if (varyPos >= 0)
                return false; // 多维同时依赖 IV，拒绝
            varyPos = static_cast<int>(i);
            indexOffset = off;
            continue;
        }
        if (!isLoopInvariant(indices[i], loop))
            return false;
    }
    if (varyPos < 0)
        return false;

    Value *rawBase = gep->getPointerOperand();
    GepPromoKey key;
    key.varyPos = varyPos;
    if (!flattenInvariantBase(rawBase, loop, key))
        return false;

    // 再追加本 GEP 下标（变化维占位）
    const size_t baseSlotCount = key.constSlots.size();
    for (size_t i = 0; i < indices.size(); ++i)
    {
        if (static_cast<int>(i) == varyPos)
        {
            key.constSlots.emplace_back(false, 0);
            key.varSlots.push_back(nullptr);
        }
        else
            appendIndexToKey(key, indices[i]);
    }
    // varyPos 是相对本 GEP 的；键里再记下「基址链 slot 数」以免不同链长冲突——
    // 已通过 baseSlotCount + 本 GEP 下标序列区分，varyPos 仅标记本 GEP 内位置。
    (void)baseSlotCount;

    const int64_t byteStride = strideBytesForVaryingIndex(gep, varyPos);
    if (byteStride <= 0)
        return false;

    int64_t byteStep = byteStride * iv.step;
    if (byteStep == 0)
        return false;
    if (byteStep > static_cast<int64_t>(numeric_limits<int>::max()) ||
        byteStep < static_cast<int64_t>(numeric_limits<int>::min()))
        return false;

    const int64_t laneByteOff = indexOffset * byteStride;
    if (laneByteOff > static_cast<int64_t>(numeric_limits<int>::max()) ||
        laneByteOff < static_cast<int64_t>(numeric_limits<int>::min()))
        return false;

    PhiInst *ptrPhi = nullptr;
    auto cacheIt = cache.find(key);
    if (cacheIt != cache.end())
    {
        ptrPhi = cacheIt->second;
    }
    else
    {
        Value *initBase =
            materializeInvariantInPreheader(rawBase, iv.preheader, loop, gep->getName() + "_base");
        if (!initBase)
            return false;

        vector<Value *> initIndices;
        initIndices.reserve(indices.size());
        for (size_t i = 0; i < indices.size(); ++i)
        {
            if (static_cast<int>(i) == varyPos)
                initIndices.push_back(iv.init); // 锚在纯 IV；lane 偏移用 addd
            else
                initIndices.push_back(indices[i]);
        }

        auto *ptrInit =
            new GetElementPtrInst(initBase, initIndices, gep->getName() + "_ptrinit");
        iv.preheader->insertBeforeTerminator(unique_ptr<Instruction>(ptrInit));

        ptrPhi = new PhiInst(gep->getType(), gep->getName() + "_ptr");
        loop.header->insert(unique_ptr<Instruction>(ptrPhi), 0);

        auto *byteStepC = new ConstantLong(LongType::getInstance(), byteStep);
        auto *ptrNext =
            new BinaryOperator(Opcode::Addd, ptrPhi, byteStepC, gep->getName() + "_ptrnext");
        iv.latch->insertBeforeTerminator(unique_ptr<Instruction>(ptrNext));

        ptrPhi->addIncoming(ptrInit, iv.preheader);
        ptrPhi->addIncoming(ptrNext, iv.latch);
        cache.emplace(key, ptrPhi);

        if (verbose)
        {
            debugInfo << "LoopPointerInduction: gep -> ptr phi (byteStep=" << byteStep << ") in "
                      << loop.header->getName() << " for " << gep->getName() << "\n";
        }
    }

    Value *replacement = ptrPhi;
    if (laneByteOff != 0)
    {
        auto *offC = new ConstantLong(LongType::getInstance(), laneByteOff);
        auto *addr = new BinaryOperator(Opcode::Addd, ptrPhi, offC, gep->getName() + "_ptrlane");
        // 插在原 GEP 前
        auto &insts = gepBB->getInstructions();
        for (auto it = insts.begin(); it != insts.end(); ++it)
        {
            if (it->get() == gep)
            {
                insts.insert(it, unique_ptr<Instruction>(addr));
                break;
            }
        }
        replacement = addr;
    }

    gep->replaceAllUsesWith(replacement);
    eraseInstruction(gepBB, gep, needToDelete);
    return true;
}

bool LoopPointerInductionPass::runOnFunction(Function *func)
{
    bool changed = false;
    func->setLoops(ControlFlowAnalysis::findLoops(func));

    vector<Loop *> order;
    for (auto &loop : func->getLoops())
        order.push_back(&loop);
    // 外层优先：先把行指针做成外环 phi，内环再基于其做列指针
    sort(order.begin(), order.end(),
         [](const Loop *a, const Loop *b) { return a->blocks.size() > b->blocks.size(); });

    for (Loop *loopPtr : order)
    {
        Loop &loop = *loopPtr;
        loop.computePreheader();

        InductionVarInfo iv;
        if (!findBasicIV(loop, iv))
            continue;

        vector<GetElementPtrInst *> candidates;
        for (auto *bb : loop.blocks)
        {
            if (bb == loop.header)
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                if (auto *gep = dynamic_cast<GetElementPtrInst *>(instPtr.get()))
                    candidates.push_back(gep);
            }
        }

        unordered_map<GepPromoKey, PhiInst *, GepPromoKeyHash> cache;
        for (auto *gep : candidates)
        {
            // 可能已被先前替换删除
            if (!findInstructionBlock(loop, gep))
                continue;
            changed |= tryPromoteGep(loop, iv, gep, cache);
        }
    }
    return changed;
}
