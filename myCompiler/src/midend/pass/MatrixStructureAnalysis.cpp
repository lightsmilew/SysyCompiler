#include "MatrixStructureAnalysis.h"

using namespace std;
using namespace optimization;

namespace
{
    unordered_map<Function *, MatrixFunctionAnalysis> gMatrixAnalysis;

    bool collectJZeroInitEvidence(const SquareIJLoopNest &nest, Value *jIV, bool &jInitFromZero)
    {
        jInitFromZero = matrixStructure::findJZeroInitCopy(nest, jIV) != nullptr;
        if (!jInitFromZero)
            jInitFromZero = matrixStructure::hasJPhiZeroInit(*nest.jLoop, jIV);
        return jInitFromZero;
    }
} // namespace

namespace optimization::matrixStructure
{
    Value *stripCopy(Value *v)
    {
        while (auto *cpy = dynamic_cast<CopyInst *>(v))
            v = cpy->getSource();
        return v;
    }

    bool sameValue(Value *a, Value *b) { return stripCopy(a) == stripCopy(b); }

    bool sameArray(Value *a, Value *b) { return isSameAddr(stripCopy(a), stripCopy(b)); }

    bool sameBound(Value *a, Value *b)
    {
        auto *ca = dynamic_cast<ConstantInt *>(stripCopy(a));
        auto *cb = dynamic_cast<ConstantInt *>(stripCopy(b));
        if (ca && cb)
            return ca->Value == cb->Value;
        return sameValue(a, b);
    }

    bool feedsInductionVar(Value *from, Value *iv, unsigned depth)
    {
        if (!from || !iv || depth > 8)
            return false;
        if (sameValue(from, iv))
            return true;
        for (auto *user : from->getUsers())
        {
            if (auto *cpy = dynamic_cast<CopyInst *>(user))
            {
                if (feedsInductionVar(cpy, iv, depth + 1))
                    return true;
            }
        }
        return false;
    }

    bool matchesLoopIV(Value *idx, Value *iv)
    {
        return sameValue(idx, iv) || feedsInductionVar(idx, iv) || feedsInductionVar(iv, idx);
    }

    const Loop *findParentLoop(const Loop &inner, const vector<Loop> &loops)
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

    const Loop *findInnermostLoopContaining(BasicBlock *bb, const vector<Loop> &loops)
    {
        const Loop *innermost = nullptr;
        size_t innermostBlocks = SIZE_MAX;
        for (const auto &loop : loops)
        {
            if (!loop.header || !loop.containsBlock(bb))
                continue;
            if (!innermost || loop.blocks.size() < innermostBlocks)
            {
                innermost = &loop;
                innermostBlocks = loop.blocks.size();
            }
        }
        return innermost;
    }

    bool getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound, ICmpInst *&cmp)
    {
        cmp = nullptr;
        iv = nullptr;
        bound = nullptr;
        if (!header)
            return false;
        for (auto &instPtr : header->getInstructions())
        {
            auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get());
            if (!icmp || icmp->getPredicate() != ICmpInst::ICMP_SLT)
                continue;
            cmp = icmp;
            iv = icmp->getLHS();
            bound = icmp->getRHS();
            return true;
        }
        return false;
    }

    bool parse2DAccess(Value *ptr, Value *&rowIdx, Value *&colIdx, Value *&arrayBase)
    {
        rowIdx = nullptr;
        colIdx = nullptr;
        arrayBase = nullptr;
        auto *gep = dynamic_cast<GetElementPtrInst *>(ptr);
        if (!gep)
            return false;

        auto indices = gep->getIndices();
        if (indices.size() == 2)
        {
            rowIdx = stripCopy(indices[0]);
            colIdx = stripCopy(indices[1]);
            arrayBase = gep->getPointerOperand();
            return true;
        }

        if (indices.size() == 1)
        {
            colIdx = stripCopy(indices[0]);
            auto *rowGep = dynamic_cast<GetElementPtrInst *>(gep->getPointerOperand());
            if (!rowGep)
                return false;
            auto rowIndices = rowGep->getIndices();
            if (rowIndices.size() != 2)
                return false;
            rowIdx = stripCopy(rowIndices[0]);
            arrayBase = rowGep->getPointerOperand();
            return true;
        }
        return false;
    }

    bool findSquareIJNest(const Loop &jLoop, const vector<Loop> &loops, SquareIJLoopNest &out)
    {
        if (!jLoop.header || jLoop.blocks.size() != 2)
            return false;

        const Loop *iLoopPtr = findParentLoop(jLoop, loops);
        if (!iLoopPtr)
            return false;

        Value *jIV = nullptr, *jBound = nullptr;
        ICmpInst *jCmp = nullptr;
        if (!getHeaderBoundCmp(jLoop.header, jIV, jBound, jCmp))
            return false;

        Value *iIV = nullptr, *iBound = nullptr;
        ICmpInst *iCmp = nullptr;
        if (!getHeaderBoundCmp(iLoopPtr->header, iIV, iBound, iCmp))
            return false;
        if (!sameBound(iBound, jBound))
            return false;

        out.iLoop = iLoopPtr;
        out.jLoop = &jLoop;
        out.iIV = iIV;
        out.jIV = jIV;
        out.bound = jBound;
        return true;
    }

    bool findIJKInductionVars(BasicBlock *bb, const vector<Loop> &loops, Value *&iIV, Value *&jIV,
                              Value *&kIV)
    {
        iIV = jIV = kIV = nullptr;
        const Loop *kLoop = findInnermostLoopContaining(bb, loops);
        if (!kLoop)
            return false;

        const Loop *jLoop = findParentLoop(*kLoop, loops);
        if (!jLoop)
            return false;
        const Loop *iLoop = findParentLoop(*jLoop, loops);
        if (!iLoop)
            return false;

        ICmpInst *cmp = nullptr;
        Value *bound = nullptr;
        if (!getHeaderBoundCmp(kLoop->header, kIV, bound, cmp))
            return false;
        if (!getHeaderBoundCmp(jLoop->header, jIV, bound, cmp))
            return false;
        if (!getHeaderBoundCmp(iLoop->header, iIV, bound, cmp))
            return false;
        return iIV && jIV && kIV;
    }

    BasicBlock *getLoopLatch(const Loop &loop)
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

    BasicBlock *getLoopExit(const Loop &loop)
    {
        auto *br = dynamic_cast<BranchInst *>(loop.header->getTerminator());
        if (!br || !br->isConditional())
            return nullptr;
        BasicBlock *candidates[2] = {br->getTrueBlock(), br->getFalseBlock()};
        for (BasicBlock *bb : candidates)
        {
            if (bb && !loop.containsBlock(bb))
                return bb;
        }
        return nullptr;
    }

    bool isSimpleTwoBlockLoop(const Loop &loop) { return loop.header && loop.blocks.size() == 2; }

    PhiInst *findPhiAtHeader(BasicBlock *header, Value *iv)
    {
        if (!header || !iv)
            return nullptr;
        for (auto &instPtr : header->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
            {
                if (phi == iv || sameValue(phi, iv))
                    return phi;
            }
        }
        return nullptr;
    }

    bool storeUsesSum(StoreInst *store, PhiInst *sumPhi, BasicBlock *kHeader)
    {
        if (!store || !sumPhi || !kHeader)
            return false;
        Value *val = store->getValueToStore();
        if (val == sumPhi)
            return true;
        auto *exitPhi = dynamic_cast<PhiInst *>(val);
        if (!exitPhi)
            return false;
        for (unsigned i = 0; i < exitPhi->getNumIncomingValues(); ++i)
        {
            if (exitPhi->getIncomingBlock(i) == kHeader &&
                exitPhi->getIncomingValue(i) == sumPhi)
                return true;
        }
        return false;
    }

    bool isZeroInit(Value *v)
    {
        auto *c = dynamic_cast<ConstantInt *>(stripCopy(v));
        return c && c->Value == 0;
    }

    bool isNegatedLoad(Value *val, LoadInst *&loadOut)
    {
        loadOut = nullptr;
        val = stripCopy(val);
        if (auto *load = dynamic_cast<LoadInst *>(val))
        {
            loadOut = load;
            return true;
        }
        if (auto *sub = dynamic_cast<BinaryOperator *>(val))
        {
            if (sub->getOpcode() != Opcode::Sub)
                return false;
            auto *zero = dynamic_cast<ConstantInt *>(stripCopy(sub->getLHS()));
            if (!zero || zero->Value != 0)
                return false;
            loadOut = dynamic_cast<LoadInst *>(stripCopy(sub->getRHS()));
            return loadOut != nullptr;
        }
        return false;
    }

    bool isSkewSymmetricWitnessStore(StoreInst *store, Value *iIV, Value *jIV, Value *matrix)
    {
        if (!store || !iIV || !jIV)
            return false;

        LoadInst *load = nullptr;
        if (!isNegatedLoad(store->getValueToStore(), load))
            return false;

        Value *storeRow = nullptr, *storeCol = nullptr, *storeBase = nullptr;
        Value *loadRow = nullptr, *loadCol = nullptr, *loadBase = nullptr;
        if (!parse2DAccess(store->getPointer(), storeRow, storeCol, storeBase))
            return false;
        if (!parse2DAccess(load->getPointer(), loadRow, loadCol, loadBase))
            return false;

        if (matrix && !sameValue(storeBase, matrix))
            return false;
        if (!sameValue(storeBase, loadBase))
            return false;

        return sameValue(storeRow, iIV) && sameValue(storeCol, jIV) && sameValue(loadRow, jIV) &&
               sameValue(loadCol, iIV);
    }

    bool isKJMatrixAccess(Value *row, Value *col, Value *iIV, Value *jIV, Value *kIV)
    {
        (void)iIV;
        (void)jIV;
        if (!matchesLoopIV(row, kIV))
            return false;
        if (matchesLoopIV(col, kIV))
            return false;
        return true;
    }

    bool isIKMatrixAccess(Value *row, Value *col, Value *iIV, Value *kIV)
    {
        return matchesLoopIV(col, kIV) && matchesLoopIV(row, iIV);
    }

    bool isMatMulAccumWitness(BasicBlock *kBody, PhiInst *kPhi, PhiInst *sumPhi, Value *iIV,
                              Value *jIV, LoadInst *&lhsLoad, LoadInst *&rhsLoad,
                              Value *&lhsArray, Value *&rhsArray, bool &hasParityGuard,
                              Value *&parityIkArray, Value *&parityKjArray)
    {
        lhsLoad = rhsLoad = nullptr;
        lhsArray = rhsArray = nullptr;
        hasParityGuard = false;
        parityIkArray = parityKjArray = nullptr;
        if (!kBody || !kPhi || !sumPhi || !iIV || !jIV)
            return false;

        auto tryAcceptProduct = [&](BinaryOperator *prodMul) -> bool {
            if (!prodMul || prodMul->getOpcode() != Opcode::Mul)
                return false;
            LoadInst *l1 = dynamic_cast<LoadInst *>(stripCopy(prodMul->getLHS()));
            LoadInst *l2 = dynamic_cast<LoadInst *>(stripCopy(prodMul->getRHS()));
            if (!l1 || !l2)
                return false;

            Value *r1 = nullptr, *c1 = nullptr, *b1 = nullptr;
            Value *r2 = nullptr, *c2 = nullptr, *b2 = nullptr;
            if (!parse2DAccess(l1->getPointer(), r1, c1, b1))
                return false;
            if (!parse2DAccess(l2->getPointer(), r2, c2, b2))
                return false;

            if (isIKMatrixAccess(r1, c1, iIV, kPhi) && isKJMatrixAccess(r2, c2, iIV, jIV, kPhi))
            {
                lhsLoad = l1;
                rhsLoad = l2;
                lhsArray = b1;
                rhsArray = b2;
                return true;
            }
            if (isIKMatrixAccess(r2, c2, iIV, kPhi) && isKJMatrixAccess(r1, c1, iIV, jIV, kPhi))
            {
                lhsLoad = l2;
                rhsLoad = l1;
                lhsArray = b2;
                rhsArray = b1;
                return true;
            }
            return false;
        };

        auto tryFindParityArrays = [&](Value *cond) {
            // 收集 cond 依赖的 load，识别 P[i][k] 与 Q[k][j]
            vector<LoadInst *> loads;
            vector<Value *> work{stripCopy(cond)};
            unordered_set<Value *> seen;
            for (size_t wi = 0; wi < work.size() && wi < 32; ++wi)
            {
                Value *v = stripCopy(work[wi]);
                if (!v || !seen.insert(v).second)
                    continue;
                if (auto *ld = dynamic_cast<LoadInst *>(v))
                {
                    loads.push_back(ld);
                    continue;
                }
                if (auto *inst = dynamic_cast<Instruction *>(v))
                {
                    for (unsigned oi = 0; oi < inst->getNumOperands(); ++oi)
                        work.push_back(inst->getOperandByIndex(oi));
                }
            }
            Value *ikArr = nullptr, *kjArr = nullptr;
            for (LoadInst *ld : loads)
            {
                Value *r = nullptr, *c = nullptr, *b = nullptr;
                if (!parse2DAccess(ld->getPointer(), r, c, b))
                    continue;
                if (isIKMatrixAccess(r, c, iIV, kPhi))
                    ikArr = b;
                else if (isKJMatrixAccess(r, c, iIV, jIV, kPhi))
                    kjArr = b;
            }
            if (ikArr && kjArr)
            {
                parityIkArray = ikArr;
                parityKjArray = kjArr;
                hasParityGuard = true;
            }
        };

        for (auto &instPtr : kBody->getInstructions())
        {
            auto *add = dynamic_cast<BinaryOperator *>(instPtr.get());
            if (!add || add->getOpcode() != Opcode::Add)
                continue;
            if (!sameValue(add->getLHS(), sumPhi) && !sameValue(add->getRHS(), sumPhi))
                continue;

            Value *other = sameValue(add->getLHS(), sumPhi) ? add->getRHS() : add->getLHS();
            auto *mul = dynamic_cast<BinaryOperator *>(stripCopy(other));
            if (!mul || mul->getOpcode() != Opcode::Mul)
                continue;

            // 经典：sum += load * load
            if (tryAcceptProduct(mul))
                return true;

            // CondGuarded：sum += cond * (load * load)
            Value *a = stripCopy(mul->getLHS());
            Value *b = stripCopy(mul->getRHS());
            BinaryOperator *prod = dynamic_cast<BinaryOperator *>(a);
            Value *cond = b;
            if (!prod || prod->getOpcode() != Opcode::Mul)
            {
                prod = dynamic_cast<BinaryOperator *>(b);
                cond = a;
            }
            if (!prod || prod->getOpcode() != Opcode::Mul)
                continue;
            if (!tryAcceptProduct(prod))
                continue;
            tryFindParityArrays(cond);
            return true;
        }
        return false;
    }

    bool isMatMulOutputStore(StoreInst *store, PhiInst *sumPhi, BasicBlock *kHeader, Value *iIV,
                             Value *jIV, Value *&outArray)
    {
        outArray = nullptr;
        if (!store || !storeUsesSum(store, sumPhi, kHeader))
            return false;

        Value *row = nullptr, *col = nullptr, *base = nullptr;
        if (!parse2DAccess(store->getPointer(), row, col, base))
            return false;
        if (!matchesLoopIV(row, iIV) || !matchesLoopIV(col, jIV))
            return false;
        outArray = base;
        return true;
    }

    bool findMatMulDotProductNest(const Loop &kLoop, const vector<Loop> &loops,
                                  MatMulDotProductNest &out)
    {
        if (!isSimpleTwoBlockLoop(kLoop))
            return false;

        const Loop *jLoop = findParentLoop(kLoop, loops);
        if (!jLoop)
            return false;
        const Loop *iLoop = findParentLoop(*jLoop, loops);
        if (!iLoop)
            return false;

        BasicBlock *kHeader = kLoop.header;
        BasicBlock *kBody = getLoopLatch(kLoop);
        BasicBlock *kExit = getLoopExit(kLoop);
        if (!kBody || !kExit || kBody == kHeader)
            return false;

        Value *kIV = nullptr;
        Value *bound = nullptr;
        ICmpInst *kCmp = nullptr;
        if (!getHeaderBoundCmp(kHeader, kIV, bound, kCmp))
            return false;
        PhiInst *kPhi = findPhiAtHeader(kHeader, kIV);
        if (!kPhi)
            return false;

        vector<PhiInst *> kHeaderPhis;
        for (auto &instPtr : kHeader->getInstructions())
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                kHeaderPhis.push_back(phi);
        if (kHeaderPhis.size() != 2)
            return false;

        PhiInst *sumPhi = (kHeaderPhis[0] == kPhi) ? kHeaderPhis[1] : kHeaderPhis[0];
        if (sumPhi == kPhi)
            return false;

        Value *sumInit = nullptr;
        for (unsigned i = 0; i < sumPhi->getNumIncomingValues(); ++i)
        {
            if (!kLoop.containsBlock(sumPhi->getIncomingBlock(i)))
            {
                sumInit = sumPhi->getIncomingValue(i);
                break;
            }
        }
        if (!isZeroInit(sumInit))
            return false;

        Value *iIV = nullptr;
        Value *jIV = nullptr;
        Value *jBound = nullptr;
        ICmpInst *jCmp = nullptr;
        if (!getHeaderBoundCmp(jLoop->header, jIV, jBound, jCmp))
            return false;
        if (!sameBound(jBound, bound))
            return false;

        Value *iBound = nullptr;
        ICmpInst *iCmp = nullptr;
        if (!getHeaderBoundCmp(iLoop->header, iIV, iBound, iCmp))
            return false;
        if (!sameBound(iBound, bound))
            return false;

        LoadInst *lhsLoad = nullptr;
        LoadInst *rhsLoad = nullptr;
        Value *lhsArray = nullptr;
        Value *rhsArray = nullptr;
        bool hasParityGuard = false;
        Value *parityIkArray = nullptr;
        Value *parityKjArray = nullptr;
        if (!isMatMulAccumWitness(kBody, kPhi, sumPhi, iIV, jIV, lhsLoad, rhsLoad, lhsArray,
                                  rhsArray, hasParityGuard, parityIkArray, parityKjArray))
            return false;

        StoreInst *outputStore = nullptr;
        for (auto &instPtr : kExit->getInstructions())
        {
            if (auto *st = dynamic_cast<StoreInst *>(instPtr.get()))
            {
                outputStore = st;
                break;
            }
        }
        Value *outArray = nullptr;
        if (!isMatMulOutputStore(outputStore, sumPhi, kHeader, iIV, jIV, outArray))
            return false;

        bool hasKInc = false;
        for (auto &instPtr : kBody->getInstructions())
        {
            if (auto *add = dynamic_cast<BinaryOperator *>(instPtr.get()))
            {
                if (add->getOpcode() != Opcode::Add)
                    continue;
                if (!sameValue(add->getLHS(), kPhi) && !sameValue(add->getRHS(), kPhi))
                    continue;
                Value *other = sameValue(add->getLHS(), kPhi) ? add->getRHS() : add->getLHS();
                if (auto *step = dynamic_cast<ConstantInt *>(stripCopy(other)))
                {
                    if (step->Value == 1)
                    {
                        hasKInc = true;
                        break;
                    }
                }
            }
        }
        if (!hasKInc)
            return false;

        BasicBlock *jExit = getLoopExit(*jLoop);
        if (!jExit)
            return false;

        BasicBlock *iBody = nullptr;
        auto *iBr = dynamic_cast<BranchInst *>(iLoop->header->getTerminator());
        if (iBr && iBr->isConditional())
            iBody = iBr->getTrueBlock();
        if (!iBody || !iLoop->containsBlock(iBody))
            return false;

        // 有奇偶守卫时必须识别到 P[i][k]、Q[k][j]
        if (hasParityGuard && (!parityIkArray || !parityKjArray))
            return false;

        out.valid = true;
        out.iLoop = iLoop;
        out.jLoop = jLoop;
        out.kLoop = &kLoop;
        out.iIV = iIV;
        out.jIV = jIV;
        out.kPhi = kPhi;
        out.sumPhi = sumPhi;
        out.bound = bound;
        out.lhsArray = lhsArray;
        out.rhsArray = rhsArray;
        out.outArray = outArray;
        out.hasParityGuard = hasParityGuard;
        out.parityIkArray = parityIkArray;
        out.parityKjArray = parityKjArray;
        out.jHeader = jLoop->header;
        out.kHeader = kHeader;
        out.kBody = kBody;
        out.kExit = kExit;
        out.jExit = jExit;
        out.iBody = iBody;
        out.lhsLoad = lhsLoad;
        out.rhsLoad = rhsLoad;
        out.outputStore = outputStore;
        return true;
    }

    CopyInst *findJZeroInitCopy(const SquareIJLoopNest &nest, Value *jIV)
    {
        BasicBlock *jPreheader = nullptr;
        for (auto *bb : nest.iLoop->blocks)
        {
            if (nest.jLoop->containsBlock(bb))
                continue;
            for (auto *succ : bb->getSuccessors())
            {
                if (succ == nest.jLoop->header)
                {
                    jPreheader = bb;
                    break;
                }
            }
            if (jPreheader)
                break;
        }

        auto tryBlock = [&](BasicBlock *bb) -> CopyInst * {
            if (!bb)
                return nullptr;
            for (auto &instPtr : bb->getInstructions())
            {
                auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
                if (!cpy || !isZeroInit(cpy->getSource()))
                    continue;
                if (sameValue(cpy, jIV) || feedsInductionVar(cpy, jIV))
                    return cpy;
            }
            return nullptr;
        };

        if (auto *found = tryBlock(jPreheader))
            return found;

        for (auto *bb : nest.iLoop->blocks)
        {
            if (bb == nest.iLoop->header || nest.jLoop->containsBlock(bb))
                continue;
            if (auto *found = tryBlock(bb))
                return found;
        }
        return nullptr;
    }

    bool hasJPhiZeroInit(const Loop &jLoop, Value *jIV)
    {
        for (auto &instPtr : jLoop.header->getInstructions())
        {
            auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
            if (!phi || !sameValue(phi, jIV))
                continue;
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                if (!jLoop.containsBlock(phi->getIncomingBlock(i)) &&
                    isZeroInit(phi->getIncomingValue(i)))
                    return true;
            }
        }
        return false;
    }

    static vector<SkewSymmetricMatrixNest> analyzeSkewSymmetricNests(Function * /*func*/,
                                                                     const vector<Loop> &loops)
    {
        vector<SkewSymmetricMatrixNest> result;
        for (const auto &jLoop : loops)
        {
            SquareIJLoopNest nest;
            if (!findSquareIJNest(jLoop, loops, nest))
                continue;

            BasicBlock *jBody = nullptr;
            for (auto *bb : jLoop.blocks)
            {
                if (bb != jLoop.header)
                {
                    jBody = bb;
                    break;
                }
            }
            if (!jBody)
                continue;

            StoreInst *witness = nullptr;
            Value *matrix = nullptr;
            for (auto &instPtr : jBody->getInstructions())
            {
                auto *store = dynamic_cast<StoreInst *>(instPtr.get());
                if (!store)
                    continue;
                if (!isSkewSymmetricWitnessStore(store, nest.iIV, nest.jIV, matrix))
                    continue;
                if (witness)
                {
                    witness = nullptr;
                    break;
                }
                witness = store;
                Value *sr = nullptr, *sc = nullptr, *sb = nullptr;
                parse2DAccess(store->getPointer(), sr, sc, sb);
                matrix = sb;
            }
            if (!witness)
                continue;

            bool jInitFromZero = false;
            if (!collectJZeroInitEvidence(nest, nest.jIV, jInitFromZero))
                continue;

            SkewSymmetricMatrixNest info;
            info.valid = true;
            info.nest = nest;
            info.matrix = matrix;
            info.witnessStore = witness;
            info.jInitFromZero = jInitFromZero;
            result.push_back(info);
        }
        return result;
    }

    static vector<MatMulDotProductNest> analyzeMatMulDotProductNests(const vector<Loop> &loops)
    {
        vector<MatMulDotProductNest> result;
        for (const auto &kLoop : loops)
        {
            MatMulDotProductNest nest;
            if (!findMatMulDotProductNest(kLoop, loops, nest))
                continue;
            result.push_back(nest);
        }
        return result;
    }

    static Value *arrayRoot(Value *ptr)
    {
        ptr = stripCopy(ptr);
        while (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr))
            ptr = stripCopy(gep->getPointerOperand());
        return ptr;
    }

    bool valueDependsOn(Value *expr, Value *target, unsigned depth)
    {
        if (!expr || !target || depth > 10)
            return false;
        expr = stripCopy(expr);
        target = stripCopy(target);
        if (expr == target)
            return true;
        if (auto *bin = dynamic_cast<BinaryOperator *>(expr))
            return valueDependsOn(bin->getLHS(), target, depth + 1) ||
                   valueDependsOn(bin->getRHS(), target, depth + 1);
        if (auto *sel = dynamic_cast<SelectInst *>(expr))
            return valueDependsOn(sel->getTrueValue(), target, depth + 1) ||
                   valueDependsOn(sel->getFalseValue(), target, depth + 1);
        if (auto *phi = dynamic_cast<PhiInst *>(expr))
        {
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                if (valueDependsOn(phi->getIncomingValue(i), target, depth + 1))
                    return true;
            }
        }
        return false;
    }

    bool parseFlatAffine2(Value *idx, Value *&termA, Value *&termB)
    {
        termA = termB = nullptr;
        idx = stripCopy(idx);
        auto *add = dynamic_cast<BinaryOperator *>(idx);
        if (!add || add->getOpcode() != Opcode::Add)
            return false;
        Value *lhs = stripCopy(add->getLHS());
        Value *rhs = stripCopy(add->getRHS());
        auto takeMul = [&](Value *v, Value *&x, Value *&c) -> bool {
            auto *mul = dynamic_cast<BinaryOperator *>(v);
            if (!mul || mul->getOpcode() != Opcode::Mul)
                return false;
            x = stripCopy(mul->getLHS());
            c = stripCopy(mul->getRHS());
            return true;
        };
        Value *x = nullptr, *c = nullptr;
        if (takeMul(lhs, x, c))
        {
            termA = x;
            termB = rhs;
            return true;
        }
        if (takeMul(rhs, x, c))
        {
            termA = x;
            termB = lhs;
            return true;
        }
        // Strength-reduced: add(stridePhi, iv) — treat both addends as terms
        termA = lhs;
        termB = rhs;
        return true;
    }

    bool isTriangularCopyWitnessStore(StoreInst *store, Value *rowIV, Value *colIV, Value *rowSize,
                                      Value *colSize, Value *&matrixOut)
    {
        matrixOut = nullptr;
        if (!store)
            return false;
        auto *loaded = dynamic_cast<LoadInst *>(stripCopy(store->getValueToStore()));
        if (!loaded)
            return false;

        Value *srcBase = arrayRoot(loaded->getPointer());
        Value *dstBase = arrayRoot(store->getPointer());
        if (!srcBase || !dstBase || !sameArray(srcBase, dstBase))
            return false;
        auto *gv = dynamic_cast<GlobalVariable *>(srcBase);
        if (!gv || !gv->isArray())
            return false;

        auto *srcGep = dynamic_cast<GetElementPtrInst *>(stripCopy(loaded->getPointer()));
        auto *dstGep = dynamic_cast<GetElementPtrInst *>(stripCopy(store->getPointer()));
        if (!srcGep || !dstGep)
            return false;
        auto srcIdxs = srcGep->getIndices();
        auto dstIdxs = dstGep->getIndices();
        if (srcIdxs.size() != 1 || dstIdxs.size() != 1)
            return false;

        Value *srcIdx = stripCopy(srcIdxs[0]);
        Value *dstIdx = stripCopy(dstIdxs[0]);

        // Prefer classic M[i*R+j] -> M[j*C+i]
        Value *sA = nullptr, *sB = nullptr, *dA = nullptr, *dB = nullptr;
        bool srcAffine = parseFlatAffine2(srcIdx, sA, sB);
        bool dstAffine = parseFlatAffine2(dstIdx, dA, dB);
        if (srcAffine && dstAffine)
        {
            bool srcUsesIJ =
                (matchesLoopIV(sA, rowIV) || matchesLoopIV(sB, rowIV)) &&
                (matchesLoopIV(sA, colIV) || matchesLoopIV(sB, colIV) || valueDependsOn(srcIdx, colIV));
            bool dstUsesIJ =
                (matchesLoopIV(dA, rowIV) || matchesLoopIV(dB, rowIV) || valueDependsOn(dstIdx, rowIV)) &&
                (matchesLoopIV(dA, colIV) || matchesLoopIV(dB, colIV));
            bool sizesOk = valueDependsOn(srcIdx, rowSize) && valueDependsOn(dstIdx, colSize);
            if (srcUsesIJ && dstUsesIJ && sizesOk)
            {
                matrixOut = gv;
                return true;
            }
        }

        // After strength reduction / folding: same-array load→store whose addresses
        // still mention both IVs and both sizes.
        if (valueDependsOn(srcIdx, rowIV) && valueDependsOn(srcIdx, colIV) &&
            valueDependsOn(dstIdx, rowIV) && valueDependsOn(dstIdx, colIV) &&
            valueDependsOn(srcIdx, rowSize) && valueDependsOn(dstIdx, colSize))
        {
            matrixOut = gv;
            return true;
        }
        return false;
    }

    static bool boundMentions(Value *bound, Value *v)
    {
        return valueDependsOn(bound, v);
    }

    static LoadInst *matchShapeLoad(Value *rowSize, Value *&shapeVecOut, Value *&indexOut)
    {
        shapeVecOut = nullptr;
        indexOut = nullptr;
        auto *ld = dynamic_cast<LoadInst *>(stripCopy(rowSize));
        if (!ld)
            return nullptr;
        auto *gep = dynamic_cast<GetElementPtrInst *>(stripCopy(ld->getPointer()));
        if (!gep || gep->getIndices().size() != 1)
            return nullptr;
        Value *base = arrayRoot(gep);
        auto *gv = dynamic_cast<GlobalVariable *>(base);
        if (!gv || !gv->isArray())
            return nullptr;
        shapeVecOut = gv;
        indexOut = stripCopy(gep->getIndices()[0]);
        return ld;
    }

    static bool hasPrefixReductionOn(Function *func, Value *matrix, Value *boundHint,
                                     Value *&reduceBoundOut)
    {
        reduceBoundOut = nullptr;
        const auto &loops = func->getLoops();
        for (const auto &loop : loops)
        {
            Value *iv = nullptr, *bound = nullptr;
            ICmpInst *cmp = nullptr;
            if (!getHeaderBoundCmp(loop.header, iv, bound, cmp))
                continue;
            if (boundHint && !sameValue(bound, boundHint) && !sameBound(bound, boundHint))
                continue;

            bool loadsMatrix = false;
            bool mulSelfIV = false;
            for (auto *bb : loop.blocks)
            {
                if (bb == loop.header)
                    continue;
                for (auto &instPtr : bb->getInstructions())
                {
                    if (auto *ld = dynamic_cast<LoadInst *>(instPtr.get()))
                    {
                        if (sameArray(arrayRoot(ld->getPointer()), matrix))
                            loadsMatrix = true;
                    }
                    if (auto *mul = dynamic_cast<BinaryOperator *>(instPtr.get()))
                    {
                        if (mul->getOpcode() == Opcode::Mul &&
                            sameValue(mul->getLHS(), mul->getRHS()) &&
                            (matchesLoopIV(mul->getLHS(), iv) || valueDependsOn(mul->getLHS(), iv)))
                            mulSelfIV = true;
                    }
                }
            }
            if (loadsMatrix && mulSelfIV)
            {
                reduceBoundOut = bound;
                return true;
            }
        }
        // Fallback: any loop bound that loads matrix (weaker)
        for (const auto &loop : loops)
        {
            Value *iv = nullptr, *bound = nullptr;
            ICmpInst *cmp = nullptr;
            if (!getHeaderBoundCmp(loop.header, iv, bound, cmp))
                continue;
            for (auto *bb : loop.blocks)
            {
                for (auto &instPtr : bb->getInstructions())
                {
                    auto *ld = dynamic_cast<LoadInst *>(instPtr.get());
                    if (!ld)
                        continue;
                    if (sameArray(arrayRoot(ld->getPointer()), matrix))
                    {
                        reduceBoundOut = boundHint ? boundHint : bound;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    bool refineCopyOriginSemantics(Function *func, InPlaceCopyOriginChain &chain)
    {
        // --- shape 下标方向 ---
        Value *shapeIdx = nullptr;
        if (auto *ld = dynamic_cast<LoadInst *>(stripCopy(chain.rowSize)))
        {
            if (auto *gep = dynamic_cast<GetElementPtrInst *>(stripCopy(ld->getPointer())))
            {
                if (gep->getIndices().size() == 1)
                    shapeIdx = stripCopy(gep->getIndices()[0]);
            }
        }
        chain.shapeLoadReversed = false;
        if (shapeIdx && chain.shapeIV && chain.reduceBound)
        {
            if (auto *sub = dynamic_cast<BinaryOperator *>(shapeIdx))
            {
                if (sub->getOpcode() == Opcode::Sub)
                {
                    Value *lhs = stripCopy(sub->getLHS());
                    Value *rhs = stripCopy(sub->getRHS());
                    // (bound-1)-iv 或 bound-(iv+1) 等
                    if (matchesLoopIV(rhs, chain.shapeIV) || valueDependsOn(rhs, chain.shapeIV))
                        chain.shapeLoadReversed = true;
                    if (auto *inner = dynamic_cast<BinaryOperator *>(lhs))
                    {
                        if (inner->getOpcode() == Opcode::Sub &&
                            (sameValue(inner->getLHS(), chain.reduceBound) ||
                             valueDependsOn(inner->getLHS(), chain.reduceBound)))
                            chain.shapeLoadReversed = true;
                    }
                }
            }
            else if (matchesLoopIV(shapeIdx, chain.shapeIV))
            {
                chain.shapeLoadReversed = false;
            }
        }

        // --- 纯 init：M[i]=i，以及可选的 mask 替换写 ---
        chain.initKind = ArrayPureInitKind::Identity;
        chain.initMask = 0;
        chain.initReplace = 0;
        bool sawIdentityStore = false;
        int maskedRepl = 0;
        int maskedBits = 0;
        bool sawMaskedStore = false;

        for (auto &bbPtr : func->getBasicBlocks())
        {
            if (chain.shapeLoop && chain.shapeLoop->containsBlock(bbPtr.get()))
                continue;
            if (chain.rowLoop && chain.rowLoop->containsBlock(bbPtr.get()))
                continue;
            if (chain.colLoop && chain.colLoop->containsBlock(bbPtr.get()))
                continue;

            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *st = dynamic_cast<StoreInst *>(instPtr.get());
                if (!st)
                    continue;
                Value *base = arrayRoot(st->getPointer());
                if (!sameArray(base, chain.matrix))
                    continue;
                auto *gep = dynamic_cast<GetElementPtrInst *>(stripCopy(st->getPointer()));
                if (!gep || gep->getIndices().size() != 1)
                    continue;
                Value *idx = stripCopy(gep->getIndices()[0]);
                Value *val = stripCopy(st->getValueToStore());

                if (sameValue(val, idx))
                {
                    sawIdentityStore = true;
                    continue;
                }
                if (auto *ci = dynamic_cast<ConstantInt *>(val))
                {
                    // 条件写：由 and(idx, mask)==0 或类似支配
                    BasicBlock *bb = bbPtr.get();
                    for (BasicBlock *pred : bb->getPredecessors())
                    {
                        auto *br = dynamic_cast<BranchInst *>(pred->getTerminator());
                        if (!br || !br->isConditional())
                            continue;
                        auto *cond = dynamic_cast<ICmpInst *>(stripCopy(br->getCondition()));
                        if (!cond)
                            continue;
                        Value *lhs = stripCopy(cond->getLHS());
                        Value *rhs = stripCopy(cond->getRHS());
                        auto *andOp = dynamic_cast<BinaryOperator *>(lhs);
                        if (andOp && andOp->getOpcode() == Opcode::And)
                        {
                            Value *a = stripCopy(andOp->getLHS());
                            Value *b = stripCopy(andOp->getRHS());
                            ConstantInt *maskCi = dynamic_cast<ConstantInt *>(b);
                            if (!maskCi)
                                maskCi = dynamic_cast<ConstantInt *>(a);
                            Value *idxOp = maskCi == dynamic_cast<ConstantInt *>(b) ? a : b;
                            if (maskCi && (sameValue(idxOp, idx) || valueDependsOn(idxOp, idx)))
                            {
                                auto *zero = dynamic_cast<ConstantInt *>(rhs);
                                if (zero && zero->Value == 0 &&
                                    (cond->getPredicate() == ICmpInst::ICMP_EQ ||
                                     cond->getPredicate() == ICmpInst::ICMP_NE))
                                {
                                    sawMaskedStore = true;
                                    maskedBits = maskCi->Value;
                                    maskedRepl = ci->Value;
                                }
                            }
                        }
                        // i % c == 0  → rem/and
                        if (auto *rem = dynamic_cast<BinaryOperator *>(lhs))
                        {
                            if (rem->getOpcode() == Opcode::SRem || rem->getOpcode() == Opcode::And)
                            {
                                if (auto *modCi = dynamic_cast<ConstantInt *>(stripCopy(rem->getRHS())))
                                {
                                    if (valueDependsOn(rem->getLHS(), idx) || sameValue(rem->getLHS(), idx))
                                    {
                                        auto *zero = dynamic_cast<ConstantInt *>(rhs);
                                        if (zero && zero->Value == 0)
                                        {
                                            sawMaskedStore = true;
                                            maskedBits = (rem->getOpcode() == Opcode::And)
                                                             ? modCi->Value
                                                             : (modCi->Value - 1);
                                            maskedRepl = ci->Value;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        if (sawMaskedStore && sawIdentityStore)
        {
            chain.initKind = ArrayPureInitKind::IdentityUnlessMasked;
            chain.initMask = maskedBits;
            chain.initReplace = maskedRepl;
        }
        else if (sawIdentityStore)
        {
            chain.initKind = ArrayPureInitKind::Identity;
        }
        else
        {
            return false; // 无法证明纯 init
        }

        auto inCopyNests = [&](BasicBlock *bb) -> bool {
            if (!bb)
                return false;
            if (chain.shapeLoop && chain.shapeLoop->containsBlock(bb))
                return true;
            if (chain.rowLoop && chain.rowLoop->containsBlock(bb))
                return true;
            if (chain.colLoop && chain.colLoop->containsBlock(bb))
                return true;
            return false;
        };

        // --- replaceEntry：最早对 matrix 做 identity init 的循环头（或该 store 所在块）---
        chain.replaceEntry = nullptr;
        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            if (inCopyNests(bb))
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                auto *st = dynamic_cast<StoreInst *>(instPtr.get());
                if (!st)
                    continue;
                if (!sameArray(arrayRoot(st->getPointer()), chain.matrix))
                    continue;
                auto *gep = dynamic_cast<GetElementPtrInst *>(stripCopy(st->getPointer()));
                if (!gep || gep->getIndices().size() != 1)
                    continue;
                if (!sameValue(stripCopy(st->getValueToStore()), stripCopy(gep->getIndices()[0])))
                    continue;
                const Loop *initLoop = findInnermostLoopContaining(bb, func->getLoops());
                chain.replaceEntry = initLoop && initLoop->header ? initLoop->header : bb;
                break;
            }
            if (chain.replaceEntry)
                break;
        }
        if (!chain.replaceEntry)
            chain.replaceEntry = chain.computeEntry ? chain.computeEntry : chain.shapeLoop->header;
        if (!chain.replaceEntry)
            return false;

        // --- 归约结果 oldResult + 延续块 continueBB（数据流锚定 Call）---
        chain.oldResult = nullptr;
        chain.continueBB = nullptr;
        chain.reductionAbs = false;

        PhiInst *reduceAns = nullptr;
        for (const auto &loop : func->getLoops())
        {
            Value *iv = nullptr, *bound = nullptr;
            ICmpInst *cmp = nullptr;
            if (!getHeaderBoundCmp(loop.header, iv, bound, cmp))
                continue;
            if (!sameValue(bound, chain.reduceBound) && !sameBound(bound, chain.reduceBound))
                continue;
            bool loadsM = false, mulSelf = false;
            PhiInst *sumPhi = nullptr;
            for (auto &instPtr : loop.header->getInstructions())
            {
                auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
                if (!phi || sameValue(phi, iv))
                    continue;
                sumPhi = phi;
            }
            for (auto *bb : loop.blocks)
            {
                if (bb == loop.header)
                    continue;
                for (auto &instPtr : bb->getInstructions())
                {
                    if (auto *ld = dynamic_cast<LoadInst *>(instPtr.get()))
                    {
                        if (sameArray(arrayRoot(ld->getPointer()), chain.matrix))
                            loadsM = true;
                    }
                    if (auto *mul = dynamic_cast<BinaryOperator *>(instPtr.get()))
                    {
                        if (mul->getOpcode() == Opcode::Mul && sameValue(mul->getLHS(), mul->getRHS()) &&
                            (matchesLoopIV(mul->getLHS(), iv) || valueDependsOn(mul->getLHS(), iv)))
                            mulSelf = true;
                    }
                }
            }
            if (loadsM && mulSelf && sumPhi)
            {
                reduceAns = sumPhi;
                break;
            }
        }
        if (!reduceAns)
            return false;

        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            if (inCopyNests(bb) || bb == chain.replaceEntry)
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                auto *call = dynamic_cast<CallInst *>(instPtr.get());
                if (!call)
                    continue;
                for (Value *arg : call->getArguments())
                {
                    Value *a = stripCopy(arg);
                    if (!sameValue(a, reduceAns) && !valueDependsOn(a, reduceAns))
                        continue;
                    chain.oldResult = a;
                    chain.continueBB = bb;
                    chain.reductionAbs = !sameValue(a, reduceAns);
                    break;
                }
                if (chain.continueBB)
                    break;
            }
            if (chain.continueBB)
                break;
        }

        if (!chain.continueBB)
        {
            for (auto &bbPtr : func->getBasicBlocks())
            {
                for (auto &instPtr : bbPtr->getInstructions())
                {
                    if (dynamic_cast<ReturnInst *>(instPtr.get()))
                    {
                        chain.continueBB = bbPtr.get();
                        chain.oldResult = reduceAns;
                        break;
                    }
                }
                if (chain.continueBB)
                    break;
            }
        }
        return chain.continueBB != nullptr && chain.oldResult != nullptr;
    }

    std::optional<InPlaceCopyOriginChain> analyzeInPlaceCopyOriginChain(Function *func)
    {
        if (!func || func->getBasicBlocks().empty())
            return std::nullopt;

        func->setLoops(ControlFlowAnalysis::findLoops(func));
        const auto &loops = func->getLoops();

        for (auto &bbPtr : func->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *div = dynamic_cast<BinaryOperator *>(instPtr.get());
                if (!div || div->getOpcode() != Opcode::SDiv)
                    continue;

                Value *extentN = stripCopy(div->getLHS());
                Value *rowSize = stripCopy(div->getRHS());
                Value *shapeVec = nullptr, *shapeIdx = nullptr;
                if (!matchShapeLoad(rowSize, shapeVec, shapeIdx))
                    continue;
                Value *colSize = div;

                for (const auto &candRow : loops)
                {
                    Value *rowIV = nullptr, *rowBound = nullptr;
                    ICmpInst *rowCmp = nullptr;
                    if (!getHeaderBoundCmp(candRow.header, rowIV, rowBound, rowCmp))
                        continue;
                    if (!boundMentions(rowBound, colSize))
                        continue;

                    for (const auto &candCol : loops)
                    {
                        if (!candRow.containsBlock(candCol.header) || &candCol == &candRow)
                            continue;
                        Value *colIV = nullptr, *colBound = nullptr;
                        ICmpInst *colCmp = nullptr;
                        if (!getHeaderBoundCmp(candCol.header, colIV, colBound, colCmp))
                            continue;
                        if (!boundMentions(colBound, rowSize))
                            continue;

                        StoreInst *witness = nullptr;
                        Value *matrix = nullptr;
                        for (auto *bb : candCol.blocks)
                        {
                            for (auto &ii : bb->getInstructions())
                            {
                                auto *st = dynamic_cast<StoreInst *>(ii.get());
                                if (!st)
                                    continue;
                                Value *m = nullptr;
                                if (isTriangularCopyWitnessStore(st, rowIV, colIV, rowSize, colSize, m))
                                {
                                    if (sameArray(m, shapeVec))
                                        continue;
                                    witness = st;
                                    matrix = m;
                                    break;
                                }
                                auto *ld = dynamic_cast<LoadInst *>(stripCopy(st->getValueToStore()));
                                if (!ld)
                                    continue;
                                Value *src = arrayRoot(ld->getPointer());
                                Value *dst = arrayRoot(st->getPointer());
                                if (!src || !dst || !sameArray(src, dst) || sameArray(src, shapeVec))
                                    continue;
                                auto *gv = dynamic_cast<GlobalVariable *>(src);
                                if (!gv || !gv->isArray())
                                    continue;
                                witness = st;
                                matrix = gv;
                                break;
                            }
                            if (witness)
                                break;
                        }
                        if (!witness || !matrix)
                            continue;

                        const Loop *shapeLoop = nullptr;
                        Value *shapeIV = nullptr;
                        {
                            const Loop *p = findParentLoop(candRow, loops);
                            while (p)
                            {
                                Value *iv = nullptr, *bound = nullptr;
                                ICmpInst *cmp = nullptr;
                                if (getHeaderBoundCmp(p->header, iv, bound, cmp))
                                {
                                    if (valueDependsOn(shapeIdx, iv) || matchesLoopIV(shapeIdx, iv) ||
                                        p->containsBlock(bbPtr.get()))
                                    {
                                        shapeLoop = p;
                                        shapeIV = iv;
                                        break;
                                    }
                                }
                                p = findParentLoop(*p, loops);
                            }
                            if (!shapeLoop)
                            {
                                shapeLoop = findInnermostLoopContaining(bbPtr.get(), loops);
                                if (shapeLoop && shapeLoop->header)
                                {
                                    Value *b = nullptr;
                                    ICmpInst *c = nullptr;
                                    getHeaderBoundCmp(shapeLoop->header, shapeIV, b, c);
                                }
                            }
                        }
                        if (!shapeLoop || !shapeIV)
                            continue;

                        Value *shapeBound = nullptr;
                        {
                            Value *iv = nullptr;
                            ICmpInst *cmp = nullptr;
                            getHeaderBoundCmp(shapeLoop->header, iv, shapeBound, cmp);
                        }

                        Value *reduceBound = nullptr;
                        if (!hasPrefixReductionOn(func, matrix, shapeBound, reduceBound))
                            continue;
                        if (!reduceBound)
                            reduceBound = shapeBound;
                        if (!reduceBound)
                            continue;

                        InPlaceCopyOriginChain chain;
                        chain.valid = true;
                        chain.matrix = matrix;
                        chain.shapeVec = shapeVec;
                        chain.extentN = extentN;
                        chain.reduceBound = reduceBound;
                        chain.shapeLoop = shapeLoop;
                        chain.rowLoop = &candRow;
                        chain.colLoop = &candCol;
                        chain.rowIV = rowIV;
                        chain.colIV = colIV;
                        chain.shapeIV = shapeIV;
                        chain.rowSize = rowSize;
                        chain.colSize = colSize;
                        chain.copyWitness = witness;
                        chain.computeEntry = shapeLoop->header;
                        if (!refineCopyOriginSemantics(func, chain))
                            continue;
                        return chain;
                    }
                }
            }
        }
        return std::nullopt;
    }

    MatrixFunctionAnalysis analyzeFunction(Function *func)
    {
        MatrixFunctionAnalysis analysis;
        func->setLoops(ControlFlowAnalysis::findLoops(func));
        const auto &loops = func->getLoops();

        analysis.skewSymmetricNests = analyzeSkewSymmetricNests(func, loops);
        analysis.matMulDotProductNests = analyzeMatMulDotProductNests(loops);
        if (auto chain = analyzeInPlaceCopyOriginChain(func))
            analysis.inPlaceCopyOriginChain = *chain;
        return analysis;
    }

    const MatrixFunctionAnalysis *getAnalysis(Function *func)
    {
        auto it = gMatrixAnalysis.find(func);
        if (it == gMatrixAnalysis.end())
            return nullptr;
        return &it->second;
    }

    void clearAnalysis(Function *func) { gMatrixAnalysis.erase(func); }
} // namespace matrixStructure

bool MatrixStructureAnalysisPass::runOnFunction(Function *func)
{
    matrixStructure::clearAnalysis(func);
    gMatrixAnalysis[func] = matrixStructure::analyzeFunction(func);

    const auto &stored = gMatrixAnalysis[func];
    if (verbose)
    {
        if (!stored.skewSymmetricNests.empty())
        {
            debugInfo << "MatrixStructureAnalysis: found " << stored.skewSymmetricNests.size()
                      << " skew-symmetric nest(s) in " << func->getName() << "\n";
        }
        if (!stored.matMulDotProductNests.empty())
        {
            debugInfo << "MatrixStructureAnalysis: found " << stored.matMulDotProductNests.size()
                      << " mat-mul dot-product nest(s) in " << func->getName() << "\n";
        }
        if (stored.inPlaceCopyOriginChain && stored.inPlaceCopyOriginChain->valid)
        {
            debugInfo << "MatrixStructureAnalysis: in-place copy origin chain on @"
                      << stored.inPlaceCopyOriginChain->matrix->getName() << " in " << func->getName()
                      << "\n";
        }
    }
    return !stored.skewSymmetricNests.empty() || !stored.matMulDotProductNests.empty() ||
           (stored.inPlaceCopyOriginChain && stored.inPlaceCopyOriginChain->valid);
}
