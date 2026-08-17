#include "LoopNestInteriorSplitPass.h"
#include <algorithm>
#include <cstdint>
#include <unordered_set>
using namespace std;
using namespace optimization;

namespace
{
    static ConstantInt *ci(int v)
    {
        return new ConstantInt(IntegerType::getInstance(), v);
    }

    static unique_ptr<Instruction> own(Instruction *inst)
    {
        return unique_ptr<Instruction>(inst);
    }

    static Value *stripCopy(Value *v)
    {
        while (auto *cpy = dynamic_cast<CopyInst *>(v))
            v = cpy->getSource();
        return v;
    }

    static bool sameValue(Value *a, Value *b)
    {
        if (!a || !b)
            return false;
        return stripCopy(a) == stripCopy(b);
    }

    static bool sameBound(Value *a, Value *b)
    {
        if (sameValue(a, b))
            return true;
        a = stripCopy(a);
        b = stripCopy(b);
        if (!a || !b)
            return false;
        if (auto *ca = dynamic_cast<ConstantInt *>(a))
            if (auto *cb = dynamic_cast<ConstantInt *>(b))
                return ca->Value == cb->Value;
        auto *la = dynamic_cast<LoadInst *>(a);
        auto *lb = dynamic_cast<LoadInst *>(b);
        if (!la || !lb)
            return false;
        auto *ga = dynamic_cast<GlobalVariable *>(stripCopy(la->getPointer()));
        auto *gb = dynamic_cast<GlobalVariable *>(stripCopy(lb->getPointer()));
        return ga && ga == gb;
    }

    static void wireEdge(BasicBlock *from, BasicBlock *to)
    {
        from->addSuccessor(to);
        to->addPredecessor(from);
    }

    static void replaceTerminator(BasicBlock *bb, unique_ptr<Instruction> term)
    {
        auto &insts = bb->getInstructions();
        if (!insts.empty() && insts.back()->Op == Opcode::Br)
        {
            insts.back()->removeThisFromOperands();
            insts.pop_back();
            for (auto *succ : bb->getSuccessors())
                succ->removePredecessor(bb);
            while (!bb->getSuccessors().empty())
                bb->removeSuccessor(bb->getSuccessors().back());
        }
        bb->addInstruction(std::move(term));
    }

    static BranchInst *getTerminator(BasicBlock *bb)
    {
        if (!bb || bb->getInstructions().empty())
            return nullptr;
        return dynamic_cast<BranchInst *>(bb->getInstructions().back().get());
    }

    static ICmpInst *getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound)
    {
        iv = nullptr;
        bound = nullptr;
        if (!header)
            return nullptr;
        for (auto &instPtr : header->getInstructions())
        {
            auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get());
            if (!icmp || icmp->getPredicate() != ICmpInst::ICMP_SLT)
                continue;
            iv = icmp->getLHS();
            bound = icmp->getRHS();
            return icmp;
        }
        return nullptr;
    }

    static int getConstantBound(Value *bound)
    {
        bound = stripCopy(bound);
        if (auto *c = dynamic_cast<ConstantInt *>(bound))
            return c->Value;
        if (auto *load = dynamic_cast<LoadInst *>(bound))
        {
            auto *gv = dynamic_cast<GlobalVariable *>(stripCopy(load->getPointer()));
            if (gv && gv->IsConstant)
                if (auto *init = dynamic_cast<ConstantInt *>(gv->Initializer))
                    return init->Value;
        }
        return -1;
    }

    static const Loop *findParentLoop(const Loop &inner, const vector<Loop> &loops)
    {
        const Loop *best = nullptr;
        size_t bestSize = 0;
        for (const auto &cand : loops)
        {
            if (&cand == &inner || cand.header == inner.header)
                continue;
            if (!cand.containsBlock(inner.header))
                continue;
            if (!best || cand.blocks.size() < bestSize)
            {
                best = &cand;
                bestSize = cand.blocks.size();
            }
        }
        return best;
    }

    static BasicBlock *getLoopLatch(const Loop &loop)
    {
        BasicBlock *latch = nullptr;
        for (auto *pred : loop.header->getPredecessors())
        {
            if (loop.containsBlock(pred) && pred != loop.header)
            {
                if (latch)
                    return nullptr;
                latch = pred;
            }
        }
        return latch;
    }

    static BasicBlock *getLoopExit(const Loop &loop)
    {
        for (auto *succ : loop.header->getSuccessors())
        {
            if (!loop.containsBlock(succ))
                return succ;
        }
        return nullptr;
    }

    static BasicBlock *getTrueBody(BasicBlock *header, const Loop &loop)
    {
        auto *br = getTerminator(header);
        if (!br || !br->isConditional())
            return nullptr;
        BasicBlock *body = br->getTrueBlock();
        if (!body || !loop.containsBlock(body))
            return nullptr;
        return body;
    }

    static bool isLoadedGlobalBound(Value *bound)
    {
        bound = stripCopy(bound);
        if (auto *load = dynamic_cast<LoadInst *>(bound))
            return dynamic_cast<GlobalVariable *>(load->getPointer()) != nullptr;
        return false;
    }

    static Value *stripArrayBase(Value *v)
    {
        while (v)
        {
            if (auto *arg = dynamic_cast<Argument *>(v))
                return v;
            if (auto *gv = dynamic_cast<GlobalVariable *>(v))
                return v;
            if (auto *gep = dynamic_cast<GetElementPtrInst *>(v))
            {
                v = gep->getPointerOperand();
                continue;
            }
            if (auto *cpy = dynamic_cast<CopyInst *>(v))
            {
                v = cpy->getSource();
                continue;
            }
            break;
        }
        return nullptr;
    }

    static bool exprReferences(Value *expr, Value *target)
    {
        if (!expr || !target)
            return false;
        expr = stripCopy(expr);
        if (sameValue(expr, target))
            return true;
        if (auto *bin = dynamic_cast<BinaryOperator *>(expr))
            return exprReferences(bin->getLHS(), target) || exprReferences(bin->getRHS(), target);
        return false;
    }

    static Value *linearIndexFromPointer(Value *ptr)
    {
        auto *gep = dynamic_cast<GetElementPtrInst *>(ptr);
        if (!gep)
            return nullptr;
        auto indices = gep->getIndices();
        if (indices.empty())
            return nullptr;
        return stripCopy(indices.back());
    }

    static bool indexUsesKernelStride(Value *idx, Value *krIV, int kSize)
    {
        if (!idx)
            return false;
        idx = stripCopy(idx);
        if (auto *mul = dynamic_cast<BinaryOperator *>(idx))
        {
            if (mul->getOpcode() == Opcode::Mul)
            {
                auto *lhs = stripCopy(mul->getLHS());
                auto *rhs = stripCopy(mul->getRHS());
                auto matches = [&](Value *a, Value *b) {
                    if (!exprReferences(a, krIV))
                        return false;
                    if (auto *c = dynamic_cast<ConstantInt *>(b))
                        return c->Value == kSize;
                    return false;
                };
                if (matches(lhs, rhs) || matches(rhs, lhs))
                    return true;
            }
            return indexUsesKernelStride(mul->getLHS(), krIV, kSize) ||
                   indexUsesKernelStride(mul->getRHS(), krIV, kSize);
        }
        if (auto *bin = dynamic_cast<BinaryOperator *>(idx))
            return indexUsesKernelStride(bin->getLHS(), krIV, kSize) ||
                   indexUsesKernelStride(bin->getRHS(), krIV, kSize);
        return false;
    }

    static bool indexUsesRowStride(Value *idx, Value *stride)
    {
        if (!idx || !stride)
            return false;
        idx = stripCopy(idx);
        if (auto *mul = dynamic_cast<BinaryOperator *>(idx))
        {
            if (mul->getOpcode() == Opcode::Mul &&
                (sameBound(stripCopy(mul->getLHS()), stride) ||
                 sameBound(stripCopy(mul->getRHS()), stride) ||
                 exprReferences(mul->getLHS(), stride) || exprReferences(mul->getRHS(), stride)))
                return true;
            return indexUsesRowStride(mul->getLHS(), stride) ||
                   indexUsesRowStride(mul->getRHS(), stride);
        }
        if (auto *bin = dynamic_cast<BinaryOperator *>(idx))
            return indexUsesRowStride(bin->getLHS(), stride) ||
                   indexUsesRowStride(bin->getRHS(), stride);
        return false;
    }

    static void collectLoopLoads(const Loop &loop, vector<pair<Value *, Value *>> &sites)
    {
        for (BasicBlock *bb : loop.blocks)
        {
            if (!bb)
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                auto *load = dynamic_cast<LoadInst *>(instPtr.get());
                if (!load)
                    continue;
                Value *base = stripArrayBase(load->getPointer());
                Value *idx = linearIndexFromPointer(load->getPointer());
                if (base && idx)
                    sites.emplace_back(base, idx);
            }
        }
    }

    static bool identifyKernelArrays(const Loop &krLoop, const Loop &kcLoop, Value *krIV,
                                     Value *kcIV, Value *stride, int kSize, Value *&inputArray,
                                     Value *&weightArray)
    {
        inputArray = nullptr;
        weightArray = nullptr;
        vector<pair<Value *, Value *>> sites;
        collectLoopLoads(krLoop, sites);
        collectLoopLoads(kcLoop, sites);
        for (const auto &[base, idx] : sites)
        {
            if (indexUsesKernelStride(idx, krIV, kSize))
                weightArray = base;
            if (indexUsesRowStride(idx, stride))
                inputArray = base;
        }
        return inputArray && weightArray && inputArray != weightArray;
    }

    static bool identifyOutputArray(const vector<BasicBlock *> &blocks, Value *rIV, Value *cIV,
                                    Value *stride, Value *inputArray, Value *weightArray,
                                    Value *&outputArray, BasicBlock *&storeBB)
    {
        outputArray = nullptr;
        storeBB = nullptr;
        for (BasicBlock *bb : blocks)
        {
            if (!bb)
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                auto *st = dynamic_cast<StoreInst *>(instPtr.get());
                if (!st)
                    continue;
                Value *base = stripArrayBase(st->getPointer());
                Value *idx = linearIndexFromPointer(st->getPointer());
                if (!base || !idx || base == inputArray || base == weightArray)
                    continue;
                if (!exprReferences(idx, rIV) || !exprReferences(idx, cIV))
                    continue;
                if (!indexUsesRowStride(idx, stride))
                    continue;
                outputArray = base;
                storeBB = bb;
                return true;
            }
        }
        return false;
    }

    static bool blockLoadsFromArray(BasicBlock *bb, Value *arrayBase, LoadInst *&outLoad)
    {
        outLoad = nullptr;
        if (!bb || !arrayBase)
            return false;
        for (auto &instPtr : bb->getInstructions())
        {
            auto *load = dynamic_cast<LoadInst *>(instPtr.get());
            if (!load)
                continue;
            auto *gep = dynamic_cast<GetElementPtrInst *>(load->getPointer());
            if (!gep)
                continue;
            if (sameValue(gep->getPointerOperand(), arrayBase))
            {
                outLoad = load;
                return true;
            }
        }
        return false;
    }

    static bool findGuardedAccumulate(const Loop &searchLoop, Value *inArray, Value *kArray,
                                       BasicBlock *&guardEntry, BasicBlock *&thenBB,
                                       BasicBlock *&mergeBB)
    {
        guardEntry = nullptr;
        thenBB = nullptr;
        mergeBB = nullptr;
        for (BasicBlock *bb : searchLoop.blocks)
        {
            if (bb == searchLoop.header)
                continue;
            LoadInst *inLoad = nullptr;
            LoadInst *kLoad = nullptr;
            if (!blockLoadsFromArray(bb, inArray, inLoad))
                continue;
            if (!blockLoadsFromArray(bb, kArray, kLoad))
                continue;

            bool hasMul = false;
            bool hasAdd = false;
            for (auto &instPtr : bb->getInstructions())
            {
                if (auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get()))
                {
                    if (bin->getOpcode() == Opcode::Mul)
                        hasMul = true;
                    if (bin->getOpcode() == Opcode::Add)
                        hasAdd = true;
                }
            }
            if (!hasMul || !hasAdd)
                continue;
            thenBB = bb;
            break;
        }
        if (!thenBB)
            return false;

        for (BasicBlock *bb : searchLoop.blocks)
        {
            if (bb == searchLoop.header || bb == thenBB)
                continue;
            auto *br = getTerminator(bb);
            if (!br || !br->isConditional())
                continue;
            if (br->getTrueBlock() == thenBB || br->getFalseBlock() == thenBB)
            {
                guardEntry = bb;
                mergeBB = br->getTrueBlock() == thenBB ? br->getFalseBlock() : br->getTrueBlock();
                break;
            }
        }
        if (!guardEntry)
        {
            for (BasicBlock *bb : searchLoop.blocks)
            {
                if (bb == searchLoop.header || bb == thenBB)
                    continue;
                for (auto *succ : bb->getSuccessors())
                {
                    if (succ == thenBB)
                    {
                        guardEntry = bb;
                        break;
                    }
                }
                if (guardEntry)
                    break;
            }
        }
        if (!mergeBB)
        {
            for (auto *succ : thenBB->getSuccessors())
            {
                if (searchLoop.containsBlock(succ))
                {
                    mergeBB = succ;
                    break;
                }
            }
        }
        return thenBB != nullptr;
    }

    static bool hasKernelAccumulate(const Loop &searchLoop, Value *inArray, Value *kArray)
    {
        BasicBlock *guardEntry = nullptr;
        BasicBlock *thenBB = nullptr;
        BasicBlock *mergeBB = nullptr;
        if (findGuardedAccumulate(searchLoop, inArray, kArray, guardEntry, thenBB, mergeBB))
            return true;
        for (BasicBlock *bb : searchLoop.blocks)
        {
            LoadInst *inLoad = nullptr;
            LoadInst *kLoad = nullptr;
            if (!blockLoadsFromArray(bb, inArray, inLoad))
                continue;
            if (!blockLoadsFromArray(bb, kArray, kLoad))
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                if (auto *mul = dynamic_cast<BinaryOperator *>(instPtr.get()))
                {
                    if (mul->getOpcode() == Opcode::Mul)
                        return true;
                }
            }
        }
        return false;
    }

    static bool isZeroConst(Value *v)
    {
        auto *c = dynamic_cast<ConstantInt *>(stripCopy(v));
        return c && c->Value == 0;
    }

    struct Affine4
    {
        int64_t c = 0;
        int64_t r = 0;
        int64_t col = 0;
        int64_t kr = 0;
        int64_t kc = 0;
    };

    static bool addScaled(Affine4 &dst, const Affine4 &src, int64_t scale)
    {
        auto mulAdd = [&](int64_t &a, int64_t b) -> bool {
            __int128 v = (__int128)a + (__int128)b * scale;
            if (v > INT64_MAX || v < INT64_MIN)
                return false;
            a = static_cast<int64_t>(v);
            return true;
        };
        return mulAdd(dst.c, src.c) && mulAdd(dst.r, src.r) && mulAdd(dst.col, src.col) &&
               mulAdd(dst.kr, src.kr) && mulAdd(dst.kc, src.kc);
    }

    static bool parseAffine4(Value *v, Value *rIV, Value *cIV, Value *krIV, Value *kcIV, Affine4 &out,
                             int depth = 0)
    {
        if (!v || depth > 24)
            return false;
        v = stripCopy(v);
        out = Affine4{};
        if (auto *cst = dynamic_cast<ConstantInt *>(v))
        {
            out.c = cst->Value;
            return true;
        }
        if (sameValue(v, rIV))
        {
            out.r = 1;
            return true;
        }
        if (sameValue(v, cIV))
        {
            out.col = 1;
            return true;
        }
        if (sameValue(v, krIV))
        {
            out.kr = 1;
            return true;
        }
        if (sameValue(v, kcIV))
        {
            out.kc = 1;
            return true;
        }
        auto *bin = dynamic_cast<BinaryOperator *>(v);
        if (!bin)
            return false;
        Affine4 lhs, rhs;
        if (bin->getOpcode() == Opcode::Add)
        {
            if (!parseAffine4(bin->getLHS(), rIV, cIV, krIV, kcIV, lhs, depth + 1) ||
                !parseAffine4(bin->getRHS(), rIV, cIV, krIV, kcIV, rhs, depth + 1))
                return false;
            out = lhs;
            return addScaled(out, rhs, 1);
        }
        if (bin->getOpcode() == Opcode::Sub)
        {
            if (!parseAffine4(bin->getLHS(), rIV, cIV, krIV, kcIV, lhs, depth + 1) ||
                !parseAffine4(bin->getRHS(), rIV, cIV, krIV, kcIV, rhs, depth + 1))
                return false;
            out = lhs;
            return addScaled(out, rhs, -1);
        }
        if (bin->getOpcode() == Opcode::Mul)
        {
            auto *lc = dynamic_cast<ConstantInt *>(stripCopy(bin->getLHS()));
            auto *rc = dynamic_cast<ConstantInt *>(stripCopy(bin->getRHS()));
            if (lc && !rc)
            {
                if (!parseAffine4(bin->getRHS(), rIV, cIV, krIV, kcIV, rhs, depth + 1))
                    return false;
                return addScaled(out, rhs, lc->Value);
            }
            if (rc && !lc)
            {
                if (!parseAffine4(bin->getLHS(), rIV, cIV, krIV, kcIV, lhs, depth + 1))
                    return false;
                return addScaled(out, lhs, rc->Value);
            }
        }
        return false;
    }

    static bool collectTrueICmps(Value *v, vector<ICmpInst *> &out, unordered_set<Value *> &vis,
                                 int depth = 0)
    {
        if (!v || depth > 32)
            return false;
        v = stripCopy(v);
        if (!vis.insert(v).second)
            return true;

        if (auto *cmp = dynamic_cast<ICmpInst *>(v))
        {
            if (cmp->getPredicate() == ICmpInst::ICMP_NE && isZeroConst(cmp->getRHS()))
                return collectTrueICmps(cmp->getLHS(), out, vis, depth + 1);
            if (cmp->getPredicate() == ICmpInst::ICMP_NE && isZeroConst(cmp->getLHS()))
                return collectTrueICmps(cmp->getRHS(), out, vis, depth + 1);
            out.push_back(cmp);
            return true;
        }
        if (auto *bin = dynamic_cast<BinaryOperator *>(v))
        {
            if (bin->getOpcode() == Opcode::And)
                return collectTrueICmps(bin->getLHS(), out, vis, depth + 1) &&
                       collectTrueICmps(bin->getRHS(), out, vis, depth + 1);
            return false;
        }
        if (auto *phi = dynamic_cast<PhiInst *>(v))
        {
            if (phi->getNumIncomingValues() != 2)
                return false;
            int zeroIdx = -1;
            int otherIdx = -1;
            for (unsigned i = 0; i < 2; ++i)
            {
                if (isZeroConst(phi->getIncomingValue(i)))
                    zeroIdx = static_cast<int>(i);
                else
                    otherIdx = static_cast<int>(i);
            }
            if (zeroIdx < 0 || otherIdx < 0)
                return false;
            BasicBlock *zeroBB = phi->getIncomingBlock(static_cast<unsigned>(zeroIdx));
            auto *br = getTerminator(zeroBB);
            BasicBlock *phiBB = nullptr;
            for (auto *succ : zeroBB->getSuccessors())
            {
                for (auto &ip : succ->getInstructions())
                {
                    if (ip.get() == phi)
                    {
                        phiBB = succ;
                        break;
                    }
                }
                if (phiBB)
                    break;
            }
            if (br && br->isConditional() && phiBB && br->getFalseBlock() == phiBB)
            {
                if (!collectTrueICmps(br->getCondition(), out, vis, depth + 1))
                    return false;
            }
            else if (br && br->isConditional() && phiBB && br->getTrueBlock() == phiBB)
            {
                return false;
            }
            return collectTrueICmps(phi->getIncomingValue(static_cast<unsigned>(otherIdx)), out, vis,
                                    depth + 1);
        }
        return false;
    }

    static int64_t innerExt(int64_t coeff, int64_t lo, int64_t hi, bool wantMin)
    {
        if (coeff == 0)
            return 0;
        if (wantMin)
            return coeff > 0 ? coeff * lo : coeff * hi;
        return coeff > 0 ? coeff * hi : coeff * lo;
    }

    struct InteriorBox
    {
        int64_t lo = 0;
        int64_t hiSub = 0;
        int64_t pad = 0;
        bool hasRLo = false;
        bool hasRHi = false;
        bool hasCLo = false;
        bool hasCHi = false;
        int64_t rLo = INT64_MIN;
        int64_t rHiSub = INT64_MIN;
        int64_t cLo = INT64_MIN;
        int64_t cHiSub = INT64_MIN;
    };

    static bool applyOuterBound(bool isRow, bool isLower, int64_t value, InteriorBox &box)
    {
        if (isRow)
        {
            if (isLower)
            {
                box.rLo = box.hasRLo ? max(box.rLo, value) : value;
                box.hasRLo = true;
            }
            else
            {
                box.rHiSub = box.hasRHi ? max(box.rHiSub, value) : value;
                box.hasRHi = true;
            }
        }
        else
        {
            if (isLower)
            {
                box.cLo = box.hasCLo ? max(box.cLo, value) : value;
                box.hasCLo = true;
            }
            else
            {
                box.cHiSub = box.hasCHi ? max(box.cHiSub, value) : value;
                box.hasCHi = true;
            }
        }
        return true;
    }

    // icmp 在「为真」时必须成立。把比较规范化成：
    //   affine(r,c,kr,kc) >= 0  或  affine(r,c,kr,kc) < nEff
    // 再对 kr,kc ∈ [innerLo, innerHi] 取 min/max，得到仅含外层 IV 的充分条件。
    static bool constrainFromICmp(ICmpInst *cmp, Value *rIV, Value *cIV, Value *krIV, Value *kcIV,
                                  Value *nEff, int64_t innerLo, int64_t innerHi, InteriorBox &box)
    {
        if (!cmp)
            return false;
        ICmpInst::Predicate pred = cmp->getPredicate();
        Value *lhs = cmp->getLHS();
        Value *rhs = cmp->getRHS();

        Affine4 aff;
        bool isLower = false;
        Value *upperBound = nullptr;

        auto tryParse = [&](Value *expr) { return parseAffine4(expr, rIV, cIV, krIV, kcIV, aff); };

        if (pred == ICmpInst::ICMP_SGE && isZeroConst(rhs) && tryParse(lhs))
            isLower = true;
        else if (pred == ICmpInst::ICMP_SLE && isZeroConst(lhs) && tryParse(rhs))
            isLower = true;
        else if (pred == ICmpInst::ICMP_SGT && isZeroConst(rhs) && tryParse(lhs))
        {
            // expr > 0  ⇒  expr - 1 >= 0
            if (!addScaled(aff, Affine4{1, 0, 0, 0, 0}, -1))
                return false;
            isLower = true;
        }
        else if (pred == ICmpInst::ICMP_SLT && sameBound(rhs, nEff) && tryParse(lhs))
        {
            upperBound = nEff;
        }
        else if (pred == ICmpInst::ICMP_SGT && sameBound(lhs, nEff) && tryParse(rhs))
        {
            upperBound = nEff;
        }
        else if (pred == ICmpInst::ICMP_SLE && sameBound(rhs, nEff) && tryParse(lhs))
        {
            upperBound = nEff;
        }
        else if (pred == ICmpInst::ICMP_SLE)
        {
            Affine4 rhsAff, lhsAff;
            if (!parseAffine4(rhs, rIV, cIV, krIV, kcIV, rhsAff) ||
                !parseAffine4(lhs, rIV, cIV, krIV, kcIV, lhsAff))
                return false;
            aff = rhsAff;
            if (!addScaled(aff, lhsAff, -1))
                return false;
            isLower = true;
        }
        else if (pred == ICmpInst::ICMP_SGE && !isZeroConst(rhs))
        {
            Affine4 rhsAff;
            if (!tryParse(lhs) || !parseAffine4(rhs, rIV, cIV, krIV, kcIV, rhsAff))
                return false;
            if (!addScaled(aff, rhsAff, -1))
                return false;
            isLower = true;
        }
        else
            return false;

        const int64_t innerMin = aff.c + innerExt(aff.kr, innerLo, innerHi, true) +
                                 innerExt(aff.kc, innerLo, innerHi, true);
        const int64_t innerMax = aff.c + innerExt(aff.kr, innerLo, innerHi, false) +
                                 innerExt(aff.kc, innerLo, innerHi, false);

        if (isLower)
        {
            // min(aff) >= 0  ⇒  cr*r + cc*c >= -innerMin
            if (aff.r == 1 && aff.col == 0)
                return applyOuterBound(true, true, -innerMin, box);
            if (aff.r == 0 && aff.col == 1)
                return applyOuterBound(false, true, -innerMin, box);
            return false;
        }
        if (upperBound)
        {
            // max(aff) < n  ⇒  cr*r + cc*c < n - innerMax  ⇒  iv < n - innerMax
            if (aff.r == 1 && aff.col == 0)
                return applyOuterBound(true, false, innerMax, box);
            if (aff.r == 0 && aff.col == 1)
                return applyOuterBound(false, false, innerMax, box);
            return false;
        }
        return false;
    }

    static bool inferInteriorBox(const vector<ICmpInst *> &cmps, Value *rIV, Value *cIV, Value *krIV,
                                 Value *kcIV, Value *nEff, int64_t innerLo, int64_t innerHi,
                                 InteriorBox &box)
    {
        box = InteriorBox{};
        if (innerHi < innerLo)
            return false;
        int applied = 0;
        for (ICmpInst *cmp : cmps)
        {
            InteriorBox one = box;
            if (constrainFromICmp(cmp, rIV, cIV, krIV, kcIV, nEff, innerLo, innerHi, one))
            {
                box = one;
                ++applied;
            }
        }
        if (applied < 4 || !box.hasRLo || !box.hasRHi || !box.hasCLo || !box.hasCHi)
            return false;
        if (box.rLo != box.cLo || box.rHiSub != box.cHiSub)
            return false;
        if (box.rLo < 0 || box.rHiSub < 0)
            return false;
        if (box.rLo == 0 && box.rHiSub == 0)
            return false;
        box.lo = box.rLo;
        box.hiSub = box.rHiSub;
        box.pad = box.rLo;
        return true;
    }

    static void collectLoopICmps(const Loop &loop, vector<ICmpInst *> &out)
    {
        for (BasicBlock *bb : loop.blocks)
        {
            if (!bb)
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                auto *cmp = dynamic_cast<ICmpInst *>(instPtr.get());
                if (!cmp)
                    continue;
                auto pred = cmp->getPredicate();
                if (pred == ICmpInst::ICMP_EQ || pred == ICmpInst::ICMP_NE)
                    continue;
                out.push_back(cmp);
            }
        }
    }

    static Value *buildInteriorCond(BasicBlock *bb, Value *rIV, Value *cIV, Value *nEff, int lo,
                                    int hiSub, const string &tag)
    {
        auto *loC = ci(lo);
        auto *hiSubC = ci(hiSub);
        auto *nMinus = new BinaryOperator(Opcode::Sub, nEff, hiSubC, tag + "_n_mhi");
        bb->addInstruction(own(nMinus));

        auto *rGe = new ICmpInst(ICmpInst::ICMP_SGE, rIV, loC, tag + "_r_ge");
        bb->addInstruction(own(rGe));
        auto *rLt = new ICmpInst(ICmpInst::ICMP_SLT, rIV, nMinus, tag + "_r_lt");
        bb->addInstruction(own(rLt));
        auto *cGe = new ICmpInst(ICmpInst::ICMP_SGE, cIV, loC, tag + "_c_ge");
        bb->addInstruction(own(cGe));
        auto *cLt = new ICmpInst(ICmpInst::ICMP_SLT, cIV, nMinus, tag + "_c_lt");
        bb->addInstruction(own(cLt));

        auto *t1 = new BinaryOperator(Opcode::And, rGe, rLt, tag + "_r_ok");
        bb->addInstruction(own(t1));
        auto *t2 = new BinaryOperator(Opcode::And, cGe, cLt, tag + "_c_ok");
        bb->addInstruction(own(t2));
        auto *all = new BinaryOperator(Opcode::And, t1, t2, tag + "_interior");
        bb->addInstruction(own(all));
        return all;
    }

    struct KernelNestResult
    {
        BasicBlock *entry = nullptr;
    };

    static KernelNestResult buildUnguardedKernelNest(Function *func, Value *rIV, Value *cIV,
                                                     Value *nEff, Value *inArray, Value *outArray,
                                                     Value *kArray, int pad, int kSize,
                                                     BasicBlock *kernelEntryPred,
                                                     BasicBlock *afterStoreTarget,
                                                     const string &tag)
    {
        KernelNestResult res;
        auto *i32 = IntegerType::getInstance();
        auto *zero = ci(0);
        auto *one = ci(1);
        auto *kBound = ci(kSize);
        auto *padC = ci(pad);

        BasicBlock *krHeader = func->addBasicBlock(tag + "_kr_hdr");
        BasicBlock *krBody = func->addBasicBlock(tag + "_kr_body");
        BasicBlock *kcHeader = func->addBasicBlock(tag + "_kc_hdr");
        BasicBlock *kcBody = func->addBasicBlock(tag + "_kc_body");
        BasicBlock *kcLatch = func->addBasicBlock(tag + "_kc_latch");
        BasicBlock *krLatch = func->addBasicBlock(tag + "_kr_latch");
        BasicBlock *storeBB = func->addBasicBlock(tag + "_store");

        res.entry = krHeader;

        auto *krPhi = new PhiInst(i32, tag + "_kr");
        krHeader->addInstruction(own(krPhi));
        auto *sumKrPhi = new PhiInst(i32, tag + "_sum_kr");
        krHeader->addInstruction(own(sumKrPhi));

        auto *krCmp = new ICmpInst(ICmpInst::ICMP_SLT, krPhi, kBound, tag + "_kr_cmp");
        krHeader->addInstruction(own(krCmp));
        krHeader->addInstruction(own(new BranchInst(krCmp, krBody, storeBB)));
        wireEdge(krHeader, krBody);
        wireEdge(krHeader, storeBB);

        auto *rPlusKr = new BinaryOperator(Opcode::Add, rIV, krPhi, tag + "_r_plus_kr");
        krBody->addInstruction(own(rPlusKr));
        auto *rr = new BinaryOperator(Opcode::Sub, rPlusKr, padC, tag + "_rr");
        krBody->addInstruction(own(rr));
        auto *rowBase = new BinaryOperator(Opcode::Mul, rr, nEff, tag + "_row_base");
        krBody->addInstruction(own(rowBase));
        krBody->addInstruction(own(new BranchInst(kcHeader)));
        wireEdge(krBody, kcHeader);

        auto *sumKcPhi = new PhiInst(i32, tag + "_sum_kc");
        kcHeader->addInstruction(own(sumKcPhi));
        auto *kcPhi = new PhiInst(i32, tag + "_kc");
        kcHeader->addInstruction(own(kcPhi));

        auto *kcCmp = new ICmpInst(ICmpInst::ICMP_SLT, kcPhi, kBound, tag + "_kc_cmp");
        kcHeader->addInstruction(own(kcCmp));
        kcHeader->addInstruction(own(new BranchInst(kcCmp, kcBody, krLatch)));
        wireEdge(kcHeader, kcBody);
        wireEdge(kcHeader, krLatch);

        auto *cPlusKc = new BinaryOperator(Opcode::Add, cIV, kcPhi, tag + "_c_plus_kc");
        kcBody->addInstruction(own(cPlusKc));
        auto *cc = new BinaryOperator(Opcode::Sub, cPlusKc, padC, tag + "_cc");
        kcBody->addInstruction(own(cc));
        auto *inIdx = new BinaryOperator(Opcode::Add, rowBase, cc, tag + "_src_idx");
        kcBody->addInstruction(own(inIdx));
        auto *inGep = new GetElementPtrInst(inArray, {inIdx}, tag + "_src_gep");
        kcBody->addInstruction(own(inGep));
        auto *inLoad = new LoadInst(inGep, tag + "_src_val");
        kcBody->addInstruction(own(inLoad));

        auto *kRow = new BinaryOperator(Opcode::Mul, krPhi, kBound, tag + "_coef_row");
        kcBody->addInstruction(own(kRow));
        auto *kIdx = new BinaryOperator(Opcode::Add, kRow, kcPhi, tag + "_coef_idx");
        kcBody->addInstruction(own(kIdx));
        auto *kGep = new GetElementPtrInst(kArray, {kIdx}, tag + "_coef_gep");
        kcBody->addInstruction(own(kGep));
        auto *kLoad = new LoadInst(kGep, tag + "_coef_val");
        kcBody->addInstruction(own(kLoad));

        auto *prod = new BinaryOperator(Opcode::Mul, inLoad, kLoad, tag + "_prod");
        kcBody->addInstruction(own(prod));
        auto *newSum = new BinaryOperator(Opcode::Add, sumKcPhi, prod, tag + "_acc");
        kcBody->addInstruction(own(newSum));
        kcBody->addInstruction(own(new BranchInst(kcLatch)));
        wireEdge(kcBody, kcLatch);

        auto *kcNext = new BinaryOperator(Opcode::Add, kcPhi, one, tag + "_kc_next");
        kcLatch->addInstruction(own(kcNext));
        kcLatch->addInstruction(own(new BranchInst(kcHeader)));
        wireEdge(kcLatch, kcHeader);

        auto *krNext = new BinaryOperator(Opcode::Add, krPhi, one, tag + "_kr_next");
        krLatch->addInstruction(own(krNext));
        krLatch->addInstruction(own(new BranchInst(krHeader)));
        wireEdge(krLatch, krHeader);

        auto *rMul = new BinaryOperator(Opcode::Mul, rIV, nEff, tag + "_dst_row");
        storeBB->addInstruction(own(rMul));
        auto *outIdx = new BinaryOperator(Opcode::Add, rMul, cIV, tag + "_dst_idx");
        storeBB->addInstruction(own(outIdx));
        auto *outGep = new GetElementPtrInst(outArray, {outIdx}, tag + "_dst_gep");
        storeBB->addInstruction(own(outGep));
        // 用 sumKrPhi 写回；kc 完全展开后 newSum 会被删掉，但 sumKcPhi 会 replaceAllUsesWith 末次累加值并更新 phi 操作数
        storeBB->addInstruction(own(new StoreInst(sumKrPhi, outGep)));
        storeBB->addInstruction(own(new BranchInst(afterStoreTarget)));
        wireEdge(storeBB, afterStoreTarget);

        krPhi->addIncoming(zero, kernelEntryPred);
        krPhi->addIncoming(krNext, krLatch);
        sumKrPhi->addIncoming(zero, kernelEntryPred);
        sumKrPhi->addIncoming(sumKcPhi, krLatch);
        sumKcPhi->addIncoming(sumKrPhi, krBody);
        sumKcPhi->addIncoming(newSum, kcLatch);
        kcPhi->addIncoming(zero, krBody);
        kcPhi->addIncoming(kcNext, kcLatch);

        return res;
    }

    static BinaryOperator *findIVIncrementInBlock(BasicBlock *bb, Value *iv)
    {
        if (!bb || !iv)
            return nullptr;
        for (auto &instPtr : bb->getInstructions())
        {
            auto *add = dynamic_cast<BinaryOperator *>(instPtr.get());
            if (!add || add->getOpcode() != Opcode::Add)
                continue;
            bool ivOnLhs = sameValue(add->getLHS(), iv);
            bool ivOnRhs = sameValue(add->getRHS(), iv);
            if (!ivOnLhs && !ivOnRhs)
                continue;
            Value *other = ivOnLhs ? add->getRHS() : add->getLHS();
            if (auto *step = dynamic_cast<ConstantInt *>(stripCopy(other)))
            {
                if (step->Value == 1)
                    return add;
            }
        }
        return nullptr;
    }

    static void finishBorderSkip(BasicBlock *skipBB, Value *cIV, BasicBlock *cHeader,
                                 BasicBlock *krExitStore, const string &tag)
    {
        auto *one = ci(1);
        BinaryOperator *templateInc = findIVIncrementInBlock(krExitStore, cIV);
        BinaryOperator *cNext = nullptr;
        if (templateInc)
        {
            cNext = new BinaryOperator(Opcode::Add, templateInc->getLHS(), templateInc->getRHS(),
                                       tag + "_skip_c_next");
        }
        else
        {
            cNext = new BinaryOperator(Opcode::Add, cIV, one, tag + "_skip_c_next");
        }
        skipBB->addInstruction(own(cNext));

        if (auto *phi = dynamic_cast<PhiInst *>(cIV))
        {
            phi->addIncoming(cNext, skipBB);
        }
        else
        {
            CopyInst *ivCopy = dynamic_cast<CopyInst *>(cIV);
            if (!ivCopy)
            {
                for (auto &instPtr : krExitStore->getInstructions())
                {
                    if (auto *cpy = dynamic_cast<CopyInst *>(instPtr.get()))
                    {
                        if (sameValue(cpy, cIV))
                        {
                            ivCopy = cpy;
                            break;
                        }
                    }
                }
            }
            if (ivCopy)
                skipBB->addInstruction(own(new CopyInst(cNext, ivCopy->getName())));
        }

        skipBB->addInstruction(own(new BranchInst(cHeader)));
        wireEdge(skipBB, cHeader);
    }
}

bool LoopNestInteriorSplitPass::analyzeKernelNest(Function *func, const vector<Loop> &loops,
                                                  KernelNestInfo &info, string *failReason)
{
    (void)func;
    int bestStage = 0;
    auto fail = [&](int stage, const char *msg) {
        if (failReason && stage >= bestStage)
        {
            bestStage = stage;
            *failReason = msg;
        }
    };
    fail(0, "no kc loop with const bound>=2");
    for (const auto &kcLoop : loops)
    {
        Value *kcIV = nullptr;
        Value *kcBound = nullptr;
        if (!getHeaderBoundCmp(kcLoop.header, kcIV, kcBound))
            continue;
        const int kSize = getConstantBound(kcBound);
        if (kSize < 2)
            continue;

        const Loop *krLoop = findParentLoop(kcLoop, loops);
        if (!krLoop)
        {
            fail(1, "kc ok, no parent kr");
            continue;
        }
        Value *krIV = nullptr;
        Value *krBound = nullptr;
        if (!getHeaderBoundCmp(krLoop->header, krIV, krBound))
        {
            fail(2, "kr header has no slt");
            continue;
        }
        if (getConstantBound(krBound) != kSize)
        {
            fail(3, "kr bound != kc bound");
            continue;
        }

        const Loop *cLoop = findParentLoop(*krLoop, loops);
        if (!cLoop)
        {
            fail(4, "no parent c loop");
            continue;
        }
        Value *cIV = nullptr;
        Value *cBound = nullptr;
        if (!getHeaderBoundCmp(cLoop->header, cIV, cBound))
        {
            fail(5, "c header has no slt");
            continue;
        }

        const Loop *rLoop = findParentLoop(*cLoop, loops);
        if (!rLoop)
        {
            fail(6, "no parent r loop");
            continue;
        }
        Value *rIV = nullptr;
        Value *rBound = nullptr;
        if (!getHeaderBoundCmp(rLoop->header, rIV, rBound))
        {
            fail(7, "r header has no slt");
            continue;
        }
        if (!sameBound(rBound, cBound))
        {
            fail(8, "r/c bounds differ");
            continue;
        }

        BasicBlock *krBody = getTrueBody(krLoop->header, *krLoop);
        if (!krBody)
        {
            fail(9, "no kr body");
            continue;
        }

        Value *inArray = nullptr;
        Value *kArray = nullptr;
        if (!identifyKernelArrays(*krLoop, kcLoop, krIV, kcIV, cBound, kSize, inArray, kArray))
        {
            fail(10, "cannot identify in/k arrays");
            continue;
        }

        BasicBlock *guardEntry = nullptr;
        BasicBlock *thenBB = nullptr;
        BasicBlock *mergeBB = nullptr;
        if (!findGuardedAccumulate(*krLoop, inArray, kArray, guardEntry, thenBB, mergeBB) &&
            !findGuardedAccumulate(kcLoop, inArray, kArray, guardEntry, thenBB, mergeBB))
        {
            fail(11, "no guarded accumulate");
            continue;
        }
        auto *guardBr = getTerminator(guardEntry);
        if (!guardBr || !guardBr->isConditional() || guardBr->getTrueBlock() != thenBB)
        {
            fail(12, "guard branch is not true->then");
            continue;
        }

        vector<ICmpInst *> guardCmps;
        unordered_set<Value *> guardVis;
        collectTrueICmps(guardBr->getCondition(), guardCmps, guardVis);
        collectLoopICmps(*krLoop, guardCmps);
        collectLoopICmps(kcLoop, guardCmps);
        {
            unordered_set<ICmpInst *> uniq;
            vector<ICmpInst *> dedup;
            for (auto *cmp : guardCmps)
                if (uniq.insert(cmp).second)
                    dedup.push_back(cmp);
            guardCmps.swap(dedup);
        }
        if (guardCmps.empty())
        {
            fail(13, "no bound icmps in kernel loops");
            continue;
        }

        InteriorBox box;
        if (!inferInteriorBox(guardCmps, rIV, cIV, krIV, kcIV, cBound, 0, kSize - 1, box))
        {
            fail(14, ("affine range does not yield interior box ncmp=" + to_string(guardCmps.size())).c_str());
            continue;
        }
        const int pad = static_cast<int>(box.pad);
        const int interiorLo = static_cast<int>(box.lo);
        const int interiorHiSub = static_cast<int>(box.hiSub);

        Value *outArray = nullptr;
        BasicBlock *krExit = nullptr;
        vector<BasicBlock *> outScanBlocks;
        for (BasicBlock *bb : krLoop->blocks)
            outScanBlocks.push_back(bb);
        for (BasicBlock *bb : krLoop->exits)
            outScanBlocks.push_back(bb);
        for (BasicBlock *bb : cLoop->blocks)
            outScanBlocks.push_back(bb);
        if (!identifyOutputArray(outScanBlocks, rIV, cIV, cBound, inArray, kArray, outArray,
                                 krExit))
        {
            fail(15, "cannot identify output array");
            continue;
        }

        BasicBlock *cBody = getTrueBody(cLoop->header, *cLoop);
        if (!cBody)
        {
            fail(16, "no c body");
            continue;
        }
        auto *cBodyBr = getTerminator(cBody);
        if (!cBodyBr || cBodyBr->isConditional())
        {
            fail(17, "c body is not an unconditional br to kernel");
            continue;
        }
        BasicBlock *krHeader = cBodyBr->getTrueBlock();
        if (!krHeader)
        {
            fail(18, "c body has no kr header target");
            continue;
        }

        const Loop *repeatLoop = findParentLoop(*rLoop, loops);
        BasicBlock *repeatBody = nullptr;
        if (repeatLoop)
        {
            Value *repIV = nullptr;
            Value *repBound = nullptr;
            if (getHeaderBoundCmp(repeatLoop->header, repIV, repBound) && isLoadedGlobalBound(repBound))
                repeatBody = getTrueBody(repeatLoop->header, *repeatLoop);
            else
                repeatLoop = nullptr;
        }

        info.kcLoop = &kcLoop;
        info.krLoop = krLoop;
        info.cLoop = cLoop;
        info.rLoop = rLoop;
        info.repeatLoop = repeatLoop;
        info.rIV = rIV;
        info.cIV = cIV;
        info.krIV = krIV;
        info.kcIV = kcIV;
        info.nEff = cBound;
        info.inArray = inArray;
        info.outArray = outArray;
        info.kArray = kArray;
        info.pad = pad;
        info.kSize = kSize;
        info.interiorLo = interiorLo;
        info.interiorHiSub = interiorHiSub;
        info.rHeader = rLoop->header;
        info.cBody = cBody;
        info.krHeader = krHeader;
        info.krExitStore = krExit;
        info.repeatBody = repeatBody;
        (void)func;
        return true;
    }
    return false;
}

bool LoopNestInteriorSplitPass::applySplit(Function *func, KernelNestInfo &info, bool verbose,
                                           stringstream &dbg)
{
    if (info.kSize < 2 || info.pad < 0 || info.interiorLo < 0 || info.interiorHiSub < 0)
    {
        dbg << "LoopNestInteriorSplit: bad range pad=" << info.pad << " k=" << info.kSize
            << " lo=" << info.interiorLo << " hiSub=" << info.interiorHiSub << "\n";
        return false;
    }
    if (!info.cBody || !info.krHeader || !info.krExitStore || !info.rHeader)
    {
        dbg << "LoopNestInteriorSplit: missing blocks\n";
        return false;
    }

    BasicBlock *cHeader = info.cLoop->header;
    BasicBlock *entryFromRepeat = info.repeatBody ? info.repeatBody : nullptr;
    const string tag = "lnis";
    auto *loC = ci(info.interiorLo);
    auto *hiSubC = ci(info.interiorHiSub);

    BasicBlock *intRHeader = func->addBasicBlock(tag + "_r_hdr");
    BasicBlock *intRBody = func->addBasicBlock(tag + "_r_body");
    BasicBlock *intRExit = func->addBasicBlock(tag + "_r_exit");
    BasicBlock *intRLatch = func->addBasicBlock(tag + "_r_latch");
    BasicBlock *intCHeader = func->addBasicBlock(tag + "_c_hdr");
    BasicBlock *intCExit = func->addBasicBlock(tag + "_c_exit");
    BasicBlock *intCLatch = func->addBasicBlock(tag + "_c_latch");

    auto *i32 = IntegerType::getInstance();
    auto *one = ci(1);

    auto *nMinusHi = new BinaryOperator(Opcode::Sub, info.nEff, hiSubC, tag + "_n_mhi");
    intRHeader->addInstruction(own(nMinusHi));

    auto *intRPhi = new PhiInst(i32, tag + "_r");
    intRHeader->addInstruction(own(intRPhi));

    auto *intRCmp = new ICmpInst(ICmpInst::ICMP_SLT, intRPhi, nMinusHi, tag + "_r_cmp");
    intRHeader->addInstruction(own(intRCmp));
    intRHeader->addInstruction(own(new BranchInst(intRCmp, intRBody, intRExit)));
    wireEdge(intRHeader, intRBody);
    wireEdge(intRHeader, intRExit);

    intRBody->addInstruction(own(new BranchInst(intCHeader)));
    wireEdge(intRBody, intCHeader);

    auto *intCPhi = new PhiInst(i32, tag + "_c");
    intCHeader->addInstruction(own(intCPhi));

    auto *nMinusHic = new BinaryOperator(Opcode::Sub, info.nEff, hiSubC, tag + "_c_n_mhi");
    intCHeader->addInstruction(own(nMinusHic));
    auto *intCCmp = new ICmpInst(ICmpInst::ICMP_SLT, intCPhi, nMinusHic, tag + "_c_cmp");
    intCHeader->addInstruction(own(intCCmp));

    KernelNestResult kernel = buildUnguardedKernelNest(
        func, intRPhi, intCPhi, info.nEff, info.inArray, info.outArray, info.kArray, info.pad,
        info.kSize, intCHeader, intCLatch, tag + "_kern");

    intCHeader->addInstruction(own(new BranchInst(intCCmp, kernel.entry, intCExit)));
    wireEdge(intCHeader, kernel.entry);
    wireEdge(intCHeader, intCExit);

    auto *intCNext = new BinaryOperator(Opcode::Add, intCPhi, one, tag + "_c_next");
    intCLatch->addInstruction(own(intCNext));
    intCLatch->addInstruction(own(new BranchInst(intCHeader)));
    wireEdge(intCLatch, intCHeader);
    intCPhi->addIncoming(loC, intRBody);
    intCPhi->addIncoming(intCNext, intCLatch);

    intCExit->addInstruction(own(new BranchInst(intRLatch)));
    wireEdge(intCExit, intRLatch);

    auto *intRNext = new BinaryOperator(Opcode::Add, intRPhi, one, tag + "_r_next");
    intRLatch->addInstruction(own(intRNext));
    intRLatch->addInstruction(own(new BranchInst(intRHeader)));
    wireEdge(intRLatch, intRHeader);

    intRExit->addInstruction(own(new BranchInst(info.rHeader)));
    wireEdge(intRExit, info.rHeader);

    BasicBlock *intRInitPred = nullptr;
    if (entryFromRepeat)
    {
        replaceTerminator(entryFromRepeat, own(new BranchInst(intRHeader)));
        wireEdge(entryFromRepeat, intRHeader);
        intRInitPred = entryFromRepeat;
    }
    else
    {
        vector<BasicBlock *> rPreds = info.rHeader->getPredecessors();
        for (auto *pred : rPreds)
        {
            auto *br = getTerminator(pred);
            if (!br)
                continue;
            if (br->getTrueBlock() == info.rHeader)
                br->setTrueBlock(intRHeader);
            if (br->isConditional() && br->getFalseBlock() == info.rHeader)
                br->setFalseBlock(intRHeader);
            pred->removeSuccessor(info.rHeader);
            wireEdge(pred, intRHeader);
            if (!intRInitPred)
                intRInitPred = pred;
        }
    }
    if (intRInitPred)
        intRPhi->addIncoming(loC, intRInitPred);
    intRPhi->addIncoming(intRNext, intRLatch);

    BasicBlock *cBorderDispatch = func->addBasicBlock(tag + "_border_dispatch");
    BasicBlock *cSkipLatch = func->addBasicBlock(tag + "_border_skip");

    replaceTerminator(info.cBody, own(new BranchInst(cBorderDispatch)));
    wireEdge(info.cBody, cBorderDispatch);

    Value *interiorCond = buildInteriorCond(cBorderDispatch, info.rIV, info.cIV, info.nEff,
                                            info.interiorLo, info.interiorHiSub, tag + "_bd");
    cBorderDispatch->addInstruction(
        own(new BranchInst(interiorCond, cSkipLatch, info.krHeader)));
    wireEdge(cBorderDispatch, cSkipLatch);
    wireEdge(cBorderDispatch, info.krHeader);

    finishBorderSkip(cSkipLatch, info.cIV, cHeader, info.krExitStore, tag);

    if (verbose)
    {
        dbg << "LoopNestInteriorSplit: interior [" << info.interiorLo << ", n-"
            << info.interiorHiSub << ") k=" << info.kSize << " pad=" << info.pad
            << " at " << info.cBody->getName() << "\n";
    }
    return true;
}

bool LoopNestInteriorSplitPass::runOnFunction(Function *func)
{
    func->setLoops(ControlFlowAnalysis::findLoops(func));
    KernelNestInfo info;
    string failReason;
    if (!analyzeKernelNest(func, func->getLoops(), info, &failReason))
    {
        if (verbose)
            debugInfo << "LoopNestInteriorSplit: no kernel nest found in " << func->getName()
                      << " loops=" << func->getLoops().size()
                      << (failReason.empty() ? "" : (" (" + failReason + ")")) << "\n";
        return false;
    }
    bool changed = applySplit(func, info, verbose, debugInfo);
    if (changed)
        func->setLoops(ControlFlowAnalysis::findLoops(func));
    return changed;
}
