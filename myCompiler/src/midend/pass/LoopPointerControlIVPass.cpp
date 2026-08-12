#include "LoopPointerControlIVPass.h"
#include <unordered_set>
#include <utility>

using namespace std;
using namespace optimization;

Value *LoopPointerControlIVPass::stripCopy(Value *v)
{
    while (auto *cpy = dynamic_cast<CopyInst *>(v))
        v = cpy->getSource();
    return v;
}

bool LoopPointerControlIVPass::sameLoopValue(Value *a, Value *b)
{
    if (!a || !b)
        return false;
    return stripCopy(a) == stripCopy(b);
}

bool LoopPointerControlIVPass::sameAddress(Value *a, Value *b)
{
    a = stripCopy(a);
    b = stripCopy(b);
    if (!a || !b)
        return false;
    if (a == b)
        return true;

    auto *ga = dynamic_cast<GetElementPtrInst *>(a);
    auto *gb = dynamic_cast<GetElementPtrInst *>(b);
    if (!ga || !gb)
        return false;
    if (!sameAddress(ga->getPointerOperand(), gb->getPointerOperand()))
        return false;
    if (ga->getIndices().size() != gb->getIndices().size())
        return false;
    for (size_t i = 0; i < ga->getIndices().size(); ++i)
    {
        Value *ia = stripCopy(ga->getIndices()[i]);
        Value *ib = stripCopy(gb->getIndices()[i]);
        if (ia == ib)
            continue;
        auto *ca = dynamic_cast<ConstantInt *>(ia);
        auto *cb = dynamic_cast<ConstantInt *>(ib);
        if (ca && cb && ca->Value == cb->Value)
            continue;
        return false;
    }
    return true;
}

bool LoopPointerControlIVPass::isLoopInvariant(Value *val, const Loop &loop) const
{
    if (!val)
        return false;
    auto *def = dynamic_cast<Instruction *>(val);
    if (!def)
        return true;
    if (!loop.containsInst(def))
        return true;

    // header 内纯由循环不变量算出的表达式（如 unroll_bound = n-8）视为可外提不变量
    bool inHeader = false;
    for (auto &ip : loop.header->getInstructions())
    {
        if (ip.get() == def)
        {
            inHeader = true;
            break;
        }
    }
    if (!inHeader)
        return false;
    if (auto *bin = dynamic_cast<BinaryOperator *>(def))
        return isLoopInvariant(bin->getLHS(), loop) && isLoopInvariant(bin->getRHS(), loop);
    if (auto *cpy = dynamic_cast<CopyInst *>(def))
        return isLoopInvariant(cpy->getSource(), loop);
    return false;
}

Value *LoopPointerControlIVPass::ensureAvailableInPreheader(Value *val, BasicBlock *preheader,
                                                            BasicBlock *header, const string &name)
{
    if (!val || !preheader || !header)
        return val;
    auto *def = dynamic_cast<Instruction *>(val);
    if (!def)
        return val;

    bool inHeader = false;
    for (auto &ip : header->getInstructions())
    {
        if (ip.get() == def)
        {
            inHeader = true;
            break;
        }
    }
    if (!inHeader)
        return val;

    if (auto *bin = dynamic_cast<BinaryOperator *>(def))
    {
        Value *lhs = ensureAvailableInPreheader(bin->getLHS(), preheader, header, name + "_l");
        Value *rhs = ensureAvailableInPreheader(bin->getRHS(), preheader, header, name + "_r");
        auto *cloned = new BinaryOperator(bin->getOpcode(), lhs, rhs, name);
        preheader->insertBeforeTerminator(unique_ptr<Instruction>(cloned));
        return cloned;
    }
    if (auto *cpy = dynamic_cast<CopyInst *>(def))
    {
        Value *src = ensureAvailableInPreheader(cpy->getSource(), preheader, header, name);
        auto *cloned = new CopyInst(src, name);
        preheader->insertBeforeTerminator(unique_ptr<Instruction>(cloned));
        return cloned;
    }
    return val;
}

BinaryOperator *LoopPointerControlIVPass::findIVIncrement(BasicBlock *latch, Value *iv,
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
        if (auto *found = tryAdd(dynamic_cast<BinaryOperator *>(stripCopy(cpy->getSource()))))
            return found;
    }

    if (auto *phi = dynamic_cast<PhiInst *>(iv))
    {
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            if (phi->getIncomingBlock(i) != latch)
                continue;
            if (auto *found = tryAdd(dynamic_cast<BinaryOperator *>(phi->getIncomingValue(i))))
                return found;
        }
    }

    return nullptr;
}

bool LoopPointerControlIVPass::findBasicIV(const Loop &loop, InductionVarInfo &info) const
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
    info.header = header;
    info.cmp = cmp;
    info.bound = stripCopy(cmp->getRHS());
    if (!isLoopInvariant(info.bound, loop))
        return false;

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
        if (!phi->getType() || !phi->getType()->isIntegerTy())
            continue;
        if (sameLoopValue(phi, cmpLHS))
        {
            ivPhi = phi;
            break;
        }
    }
    if (!ivPhi || ivPhi->getNumIncomingValues() != 2)
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
    if (!info.init || !isLoopInvariant(info.init, loop))
        return false;

    info.inc = findIVIncrement(latch, ivPhi, info.step);
    if (!info.inc || info.step <= 0 || info.step > 1024)
        return false;
    return true;
}

void LoopPointerControlIVPass::collectIVFamily(const InductionVarInfo &iv,
                                               unordered_set<Value *> &family) const
{
    family.clear();
    family.insert(iv.phi);
    family.insert(iv.inc);
    bool grew = true;
    while (grew)
    {
        grew = false;
        vector<Value *> pending;
        for (Value *v : family)
        {
            for (User *u : v->getUsers())
            {
                if (auto *cpy = dynamic_cast<CopyInst *>(u))
                {
                    if (!family.count(cpy))
                    {
                        pending.push_back(cpy);
                        grew = true;
                    }
                }
            }
        }
        for (Value *p : pending)
            family.insert(p);
    }
}

bool LoopPointerControlIVPass::ivOnlyUsedForControl(const InductionVarInfo &iv) const
{
    unordered_set<Value *> family;
    collectIVFamily(iv, family);

    for (Value *v : family)
    {
        for (User *u : v->getUsers())
        {
            if (family.count(u))
                continue;
            if (u == iv.cmp)
                continue;
            return false;
        }
    }
    return true;
}

bool LoopPointerControlIVPass::findAllPointerIVs(const Loop &loop, const InductionVarInfo &iv,
                                                 vector<PtrIVInfo> &out) const
{
    out.clear();
    BasicBlock *header = loop.header;

    for (auto &instPtr : header->getInstructions())
    {
        auto *ptrPhi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!ptrPhi || !ptrPhi->getType() || !ptrPhi->getType()->isPointerTy())
            continue;
        if (ptrPhi->getNumIncomingValues() != 2)
            continue;

        Value *ptrInit = nullptr;
        Value *latchVal = nullptr;
        for (unsigned i = 0; i < ptrPhi->getNumIncomingValues(); ++i)
        {
            BasicBlock *bb = ptrPhi->getIncomingBlock(i);
            if (bb == iv.preheader)
                ptrInit = ptrPhi->getIncomingValue(i);
            else if (bb == iv.latch)
                latchVal = ptrPhi->getIncomingValue(i);
            else
            {
                ptrInit = nullptr;
                break;
            }
        }
        if (!ptrInit || !latchVal)
            continue;

        auto *ptrNext = dynamic_cast<BinaryOperator *>(stripCopy(latchVal));
        if (!ptrNext || ptrNext->getOpcode() != Opcode::Addd)
            continue;
        if (!sameLoopValue(ptrNext->getLHS(), ptrPhi))
            continue;

        int64_t byteStep = 0;
        if (auto *stepL = dynamic_cast<ConstantLong *>(stripCopy(ptrNext->getRHS())))
            byteStep = stepL->Value;
        else if (auto *stepI = dynamic_cast<ConstantInt *>(stripCopy(ptrNext->getRHS())))
            byteStep = stepI->Value;
        else
            continue;

        if (byteStep <= 0 || byteStep % iv.step != 0)
            continue;
        int64_t byteStride = byteStep / iv.step;
        if (byteStride <= 0)
            continue;

        PtrIVInfo info;
        info.ptrPhi = ptrPhi;
        info.ptrInit = ptrInit;
        info.ptrNext = ptrNext;
        info.byteStep = byteStep;
        info.byteStride = byteStride;
        out.push_back(info);
    }
    return !out.empty();
}

const LoopPointerControlIVPass::PtrIVInfo *
LoopPointerControlIVPass::matchPtrIVForGep(GetElementPtrInst *gep, const InductionVarInfo &iv,
                                           const vector<PtrIVInfo> &ptrs) const
{
    if (!gep)
        return nullptr;
    const auto &indices = gep->getIndices();
    if (indices.size() != 1)
        return nullptr;
    if (!sameLoopValue(indices[0], iv.phi))
        return nullptr;

    Value *gepBase = stripCopy(gep->getPointerOperand());

    for (const auto &p : ptrs)
    {
        auto *initGep = dynamic_cast<GetElementPtrInst *>(stripCopy(p.ptrInit));
        if (!initGep || initGep->getIndices().size() != 1)
            continue;
        if (!sameAddress(initGep->getPointerOperand(), gepBase))
            continue;

        Value *initIdx = stripCopy(initGep->getIndices()[0]);
        if (sameLoopValue(initIdx, iv.init))
            return &p;
        auto *ci = dynamic_cast<ConstantInt *>(initIdx);
        auto *ii = dynamic_cast<ConstantInt *>(stripCopy(iv.init));
        if (ci && ii && ci->Value == ii->Value)
            return &p;
    }
    return nullptr;
}

Value *LoopPointerControlIVPass::materializePtrEnd(const InductionVarInfo &iv, const PtrIVInfo &ptr,
                                                   const string &namePrefix)
{
    Value *bound =
        ensureAvailableInPreheader(iv.bound, iv.preheader, iv.header, namePrefix + "_bound");
    Value *init = stripCopy(iv.init);
    Value *ptrInit = stripCopy(ptr.ptrInit);

    auto *boundC = dynamic_cast<ConstantInt *>(stripCopy(bound));
    auto *initC = dynamic_cast<ConstantInt *>(init);
    if (boundC && initC)
    {
        int64_t bytes =
            (static_cast<int64_t>(boundC->Value) - static_cast<int64_t>(initC->Value)) *
            ptr.byteStride;
        auto *off = new ConstantLong(LongType::getInstance(), bytes);
        auto *end = new BinaryOperator(Opcode::Addd, ptrInit, off, namePrefix + "_ptrend");
        iv.preheader->insertBeforeTerminator(unique_ptr<Instruction>(end));
        return end;
    }

    Value *count = stripCopy(bound);
    if (!initC || initC->Value != 0)
    {
        auto *sub =
            new BinaryOperator(Opcode::Sub, stripCopy(bound), init, namePrefix + "_ivcnt");
        iv.preheader->insertBeforeTerminator(unique_ptr<Instruction>(sub));
        count = sub;
    }

    Value *bytes = count;
    if (ptr.byteStride != 1)
    {
        if (ptr.byteStride > 0 && (ptr.byteStride & (ptr.byteStride - 1)) == 0)
        {
            int sh = 0;
            for (int64_t s = ptr.byteStride; s > 1; s >>= 1)
                ++sh;
            auto *shamt = new ConstantInt(IntegerType::getInstance(), sh);
            auto *sll = new BinaryOperator(Opcode::Sll, count, shamt, namePrefix + "_ivbytes");
            iv.preheader->insertBeforeTerminator(unique_ptr<Instruction>(sll));
            bytes = sll;
        }
        else
        {
            auto *sc =
                new ConstantInt(IntegerType::getInstance(), static_cast<int>(ptr.byteStride));
            auto *mul = new BinaryOperator(Opcode::Mul, count, sc, namePrefix + "_ivbytes");
            iv.preheader->insertBeforeTerminator(unique_ptr<Instruction>(mul));
            bytes = mul;
        }
    }

    auto *end = new BinaryOperator(Opcode::Addd, ptrInit, bytes, namePrefix + "_ptrend");
    iv.preheader->insertBeforeTerminator(unique_ptr<Instruction>(end));
    return end;
}

bool LoopPointerControlIVPass::rewriteRemainderExitUses(const Loop &loop,
                                                        const InductionVarInfo &iv,
                                                        const vector<PtrIVInfo> &ptrs)
{
    unordered_set<Value *> family;
    collectIVFamily(iv, family);

    bool changed = false;

    // 1) gep(base, iv) → live-out ptrPhi
    vector<GetElementPtrInst *> remGeps;
    for (Value *v : family)
    {
        // 拷贝 users，避免迭代中修改
        vector<User *> users(v->getUsers().begin(), v->getUsers().end());
        for (User *u : users)
        {
            if (family.count(u) || u == iv.cmp)
                continue;
            if (auto *gep = dynamic_cast<GetElementPtrInst *>(u))
                remGeps.push_back(gep);
        }
    }
    for (auto *gep : remGeps)
    {
        const PtrIVInfo *matched = matchPtrIVForGep(gep, iv, ptrs);
        if (!matched)
            continue;
        gep->replaceAllUsesWith(matched->ptrPhi);
        if (BasicBlock *bb = findInstructionBlock(loop.header->Parent, gep))
            eraseInstruction(bb, gep, needToDelete);
        changed = true;
        if (verbose)
        {
            debugInfo << "LoopPointerControlIV: rem gep -> live ptr "
                      << matched->ptrPhi->getName() << "\n";
        }
    }

    // 2) sub(fullBound, iv) + sll/mul → addd 的 ptrend，改成 ptrInit+(fullBound-init)*stride
    // 注意：内层展开的 init 常是外层 IV；外层若误匹配到内层 materialize 产生的
    // sub(bound, outer_iv) 会把内层 ptrend 改成整表终点 → 余数环越界。
    // 因此只改写「addd 指针操作数属于本环 ptr IV」的 endOp。
    auto matchPtrForAddd = [&](BinaryOperator *addd) -> const PtrIVInfo * {
        if (!addd || addd->getOpcode() != Opcode::Addd)
            return nullptr;
        Value *base = stripCopy(addd->getLHS());
        for (const auto &p : ptrs)
        {
            if (sameLoopValue(base, p.ptrPhi) || sameLoopValue(base, p.ptrInit) ||
                sameAddress(base, p.ptrInit))
                return &p;
        }
        return nullptr;
    };

    vector<BinaryOperator *> remSubs;
    collectIVFamily(iv, family);
    for (Value *v : family)
    {
        vector<User *> users(v->getUsers().begin(), v->getUsers().end());
        for (User *u : users)
        {
            if (family.count(u) || u == iv.cmp)
                continue;
            auto *bin = dynamic_cast<BinaryOperator *>(u);
            if (!bin || bin->getOpcode() != Opcode::Sub)
                continue;
            // materializePtrEnd 生成 sub(bound, init)；init 即为展开 IV
            if (!sameLoopValue(bin->getRHS(), iv.phi))
                continue;
            remSubs.push_back(bin);
        }
    }

    for (auto *sub : remSubs)
    {
        Value *fullBound = stripCopy(sub->getLHS());
        // fullBound 一般为入口 getint 等循环外值
        if (auto *def = dynamic_cast<Instruction *>(fullBound))
        {
            if (loop.containsInst(def) && !isLoopInvariant(fullBound, loop))
                continue;
        }

        vector<pair<BinaryOperator *, const PtrIVInfo *>> endOps;
        vector<User *> subUsers(sub->getUsers().begin(), sub->getUsers().end());
        for (User *u : subUsers)
        {
            auto *bop = dynamic_cast<BinaryOperator *>(u);
            if (!bop)
                continue;
            if (bop->getOpcode() != Opcode::Sll && bop->getOpcode() != Opcode::Mul)
                continue;
            vector<User *> bUsers(bop->getUsers().begin(), bop->getUsers().end());
            for (User *uu : bUsers)
            {
                auto *addd = dynamic_cast<BinaryOperator *>(uu);
                if (!addd || addd->getOpcode() != Opcode::Addd)
                    continue;
                if (const PtrIVInfo *matched = matchPtrForAddd(addd))
                    endOps.emplace_back(addd, matched);
            }
        }
        // 也接受 addd(ptr, sub)（stride==1 时无 sll）
        for (User *u : subUsers)
        {
            auto *addd = dynamic_cast<BinaryOperator *>(u);
            if (!addd || addd->getOpcode() != Opcode::Addd)
                continue;
            if (const PtrIVInfo *matched = matchPtrForAddd(addd))
                endOps.emplace_back(addd, matched);
        }
        if (endOps.empty())
            continue;

        BasicBlock *insertBB = findInstructionBlock(loop.header->Parent, sub);
        if (!insertBB)
            insertBB = iv.preheader;

        // 按匹配到的 ptr IV 分组改写，避免误用其它数组的 anchor
        unordered_set<BinaryOperator *> replaced;
        unordered_set<Value *> newEnds;
        for (auto &[endOp, matched] : endOps)
        {
            if (!endOp || replaced.count(endOp))
                continue;
            InductionVarInfo tmp = iv;
            tmp.bound = fullBound;
            tmp.preheader = insertBB;
            Value *newEnd =
                materializePtrEnd(tmp, *matched, iv.phi->getName() + "_full");
            newEnds.insert(newEnd);
            for (auto &[other, om] : endOps)
            {
                if (!other || replaced.count(other) || om != matched)
                    continue;
                if (other != newEnd)
                    other->replaceAllUsesWith(newEnd);
                replaced.insert(other);
            }
        }
        for (auto *dead : replaced)
        {
            if (newEnds.count(dead) || !dead->getUsers().empty())
                continue;
            if (BasicBlock *bb = findInstructionBlock(loop.header->Parent, dead))
                eraseInstruction(bb, dead, needToDelete);
        }
        // 再清 sll/mul / sub
        subUsers.assign(sub->getUsers().begin(), sub->getUsers().end());
        for (User *u : subUsers)
        {
            auto *bop = dynamic_cast<BinaryOperator *>(u);
            if (!bop)
                continue;
            if (!bop->getUsers().empty())
                continue;
            if (BasicBlock *bb = findInstructionBlock(loop.header->Parent, bop))
                eraseInstruction(bb, bop, needToDelete);
        }
        if (sub->getUsers().empty())
        {
            if (BasicBlock *bb = findInstructionBlock(loop.header->Parent, sub))
                eraseInstruction(bb, sub, needToDelete);
        }
        changed = true;
        if (verbose)
        {
            debugInfo << "LoopPointerControlIV: rem ptrend -> full bound end in "
                      << insertBB->getName() << "\n";
        }
    }

    return changed;
}

bool LoopPointerControlIVPass::eraseDeadIVAddChains(const Loop &loop, const InductionVarInfo &iv)
{
    bool changed = false;
    bool progress = true;
    while (progress)
    {
        progress = false;
        vector<BinaryOperator *> deadAdds;
        for (User *u : iv.phi->getUsers())
        {
            auto *add = dynamic_cast<BinaryOperator *>(u);
            if (!add || add->getOpcode() != Opcode::Add || add == iv.inc)
                continue;
            // iv + const（非主步长自增）
            Value *other = sameLoopValue(add->getLHS(), iv.phi) ? add->getRHS() : add->getLHS();
            if (!dynamic_cast<ConstantInt *>(stripCopy(other)))
                continue;
            if (add->getUsers().empty())
                deadAdds.push_back(add);
        }
        // 也清理「add 链」末端：只被同类 add 使用的先不处理；迭代删无用户节点
        for (auto *bb : loop.blocks)
        {
            for (auto &ip : bb->getInstructions())
            {
                auto *add = dynamic_cast<BinaryOperator *>(ip.get());
                if (!add || add->getOpcode() != Opcode::Add || add == iv.inc)
                    continue;
                if (!add->getUsers().empty())
                    continue;
                // 操作数之一最终来自 iv family
                Value *lhs = stripCopy(add->getLHS());
                Value *rhs = stripCopy(add->getRHS());
                bool fromIV = sameLoopValue(lhs, iv.phi) || sameLoopValue(rhs, iv.phi);
                if (!fromIV)
                {
                    // 来自其它 add 派生
                    auto *la = dynamic_cast<BinaryOperator *>(lhs);
                    auto *ra = dynamic_cast<BinaryOperator *>(rhs);
                    fromIV = (la && la->getOpcode() == Opcode::Add) ||
                             (ra && ra->getOpcode() == Opcode::Add);
                }
                if (fromIV)
                    deadAdds.push_back(add);
            }
        }

        for (auto *add : deadAdds)
        {
            if (BasicBlock *bb = findInstructionBlock(loop.header->Parent, add))
            {
                eraseInstruction(bb, add, needToDelete);
                progress = true;
                changed = true;
            }
        }
    }
    return changed;
}

void LoopPointerControlIVPass::eraseInstruction(BasicBlock *bb, Instruction *inst,
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

BasicBlock *LoopPointerControlIVPass::findInstructionBlock(Function *func, Instruction *inst) const
{
    if (!func || !inst)
        return nullptr;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        for (auto &ip : bb->getInstructions())
        {
            if (ip.get() == inst)
                return bb;
        }
    }
    return nullptr;
}

bool LoopPointerControlIVPass::tryRewriteLoop(Loop &loop)
{
    loop.computePreheader();

    InductionVarInfo iv;
    if (!findBasicIV(loop, iv))
        return false;

    vector<PtrIVInfo> ptrs;
    if (!findAllPointerIVs(loop, iv, ptrs))
        return false;

    // 展开余数：先消掉对 IV 的 gep/sub 依赖
    rewriteRemainderExitUses(loop, iv, ptrs);
    eraseDeadIVAddChains(loop, iv);

    if (!ivOnlyUsedForControl(iv))
        return false;

    // 选最小 stride 的指针做循环控制
    const PtrIVInfo *best = &ptrs.front();
    for (const auto &p : ptrs)
    {
        if (p.byteStride < best->byteStride)
            best = &p;
    }

    Value *ptrEnd = materializePtrEnd(iv, *best, iv.phi->getName());
    if (!ptrEnd)
        return false;

    iv.cmp->setOperandByIndex(0, best->ptrPhi);
    iv.cmp->setOperandByIndex(1, ptrEnd);

    unordered_set<Value *> family;
    collectIVFamily(iv, family);

    for (unsigned i = 0; i < iv.phi->getNumIncomingValues(); ++i)
    {
        if (iv.phi->getIncomingBlock(i) == iv.latch)
            iv.phi->setIncomingValue(i, iv.init);
    }

    vector<CopyInst *> copies;
    for (Value *v : family)
    {
        if (auto *cpy = dynamic_cast<CopyInst *>(v))
            copies.push_back(cpy);
    }
    for (auto *cpy : copies)
    {
        if (!cpy->getUsers().empty())
            continue;
        if (BasicBlock *bb = findInstructionBlock(loop.header->Parent, cpy))
            eraseInstruction(bb, cpy, needToDelete);
    }
    if (iv.inc->getUsers().empty())
    {
        if (BasicBlock *bb = findInstructionBlock(loop.header->Parent, iv.inc))
            eraseInstruction(bb, iv.inc, needToDelete);
    }
    if (iv.phi->getUsers().empty())
        eraseInstruction(loop.header, iv.phi, needToDelete);

    if (verbose)
    {
        debugInfo << "LoopPointerControlIV: replace int IV with ptr IV in "
                  << loop.header->getName() << " (byteStride=" << best->byteStride << ")\n";
    }
    return true;
}

bool LoopPointerControlIVPass::runOnFunction(Function *func)
{
    bool changed = false;
    // 余数环与展开环 blocks 数常相同；若先处理展开环，余数环的 phi 仍引用
    // unroll IV，无法删 IV；余数环随后用 sub(n, unroll_iv) 建 ptrend 后也不会再回访。
    // 迭代到不动点，保证「先余数、后展开」无论初始顺序都能完成。
    for (int iter = 0; iter < 8; ++iter)
    {
        bool iterChanged = false;
        func->setLoops(ControlFlowAnalysis::findLoops(func));

        vector<Loop *> order;
        for (auto &loop : func->getLoops())
            order.push_back(&loop);
        sort(order.begin(), order.end(),
             [](const Loop *a, const Loop *b) { return a->blocks.size() < b->blocks.size(); });

        for (Loop *loopPtr : order)
            iterChanged |= tryRewriteLoop(*loopPtr);

        changed |= iterChanged;
        if (!iterChanged)
            break;
    }
    return changed;
}
