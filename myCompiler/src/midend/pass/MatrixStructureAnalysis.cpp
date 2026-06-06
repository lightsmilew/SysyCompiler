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

    bool isTransposeWitnessStore(StoreInst *store, Value *iIV, Value *jIV, Value *&srcBuffer,
                                 Value *&dstBuffer)
    {
        if (!store || !iIV || !jIV)
            return false;

        auto *val = stripCopy(store->getValueToStore());
        auto *load = dynamic_cast<LoadInst *>(val);
        if (!load)
            return false;

        Value *storeRow = nullptr, *storeCol = nullptr, *storeBase = nullptr;
        Value *loadRow = nullptr, *loadCol = nullptr, *loadBase = nullptr;
        if (!parse2DAccess(store->getPointer(), storeRow, storeCol, storeBase))
            return false;
        if (!parse2DAccess(load->getPointer(), loadRow, loadCol, loadBase))
            return false;

        if (!matchesLoopIV(storeRow, iIV) || !matchesLoopIV(storeCol, jIV))
            return false;
        if (!matchesLoopIV(loadRow, jIV) || !matchesLoopIV(loadCol, iIV))
            return false;
        if (sameArray(storeBase, loadBase))
            return false;

        srcBuffer = loadBase;
        dstBuffer = storeBase;
        return true;
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
                              Value *&lhsArray, Value *&rhsArray)
    {
        lhsLoad = rhsLoad = nullptr;
        lhsArray = rhsArray = nullptr;
        if (!kBody || !kPhi || !sumPhi || !iIV || !jIV)
            return false;

        for (auto &instPtr : kBody->getInstructions())
        {
            auto *add = dynamic_cast<BinaryOperator *>(instPtr.get());
            if (!add || add->getOpcode() != Opcode::Add)
                continue;
            if (!sameValue(add->getLHS(), sumPhi) && !sameValue(add->getRHS(), sumPhi))
                continue;

            Value *other = sameValue(add->getLHS(), sumPhi) ? add->getRHS() : add->getLHS();
            auto *mul = dynamic_cast<BinaryOperator *>(other);
            if (!mul || mul->getOpcode() != Opcode::Mul)
                continue;

            LoadInst *l1 = dynamic_cast<LoadInst *>(stripCopy(mul->getLHS()));
            LoadInst *l2 = dynamic_cast<LoadInst *>(stripCopy(mul->getRHS()));
            if (!l1 || !l2)
                continue;

            Value *r1 = nullptr, *c1 = nullptr, *b1 = nullptr;
            Value *r2 = nullptr, *c2 = nullptr, *b2 = nullptr;
            if (!parse2DAccess(l1->getPointer(), r1, c1, b1))
                continue;
            if (!parse2DAccess(l2->getPointer(), r2, c2, b2))
                continue;

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
        }
        return false;
    }

    bool isMatMulOutputStore(StoreInst *store, PhiInst *sumPhi, BasicBlock *kHeader, Value *iIV,
                             Value *jIV, Value *rhsArray)
    {
        if (!store || !storeUsesSum(store, sumPhi, kHeader))
            return false;

        Value *row = nullptr, *col = nullptr, *base = nullptr;
        if (!parse2DAccess(store->getPointer(), row, col, base))
            return false;
        if (!matchesLoopIV(row, iIV) || !matchesLoopIV(col, jIV))
            return false;
        return sameArray(base, rhsArray);
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
        if (!isMatMulAccumWitness(kBody, kPhi, sumPhi, iIV, jIV, lhsLoad, rhsLoad, lhsArray,
                                  rhsArray))
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
        if (!isMatMulOutputStore(outputStore, sumPhi, kHeader, iIV, jIV, rhsArray))
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

    KJLoadUseKind classifyKJLoadUser(Instruction *user, LoadInst *kjLoad, Value *iIV, Value *kIV,
                                     const TransposeBufferRelation &rel)
    {
        auto *mul = dynamic_cast<BinaryOperator *>(user);
        if (!mul || mul->getOpcode() != Opcode::Mul)
            return KJLoadUseKind::Other;

        auto pickOther = [&](Value *lhs, Value *rhs) -> Value * {
            if (lhs == kjLoad || sameValue(lhs, kjLoad))
                return rhs;
            if (rhs == kjLoad || sameValue(rhs, kjLoad))
                return lhs;
            return nullptr;
        };
        Value *other = pickOther(stripCopy(mul->getLHS()), stripCopy(mul->getRHS()));
        if (!other)
            return KJLoadUseKind::Other;

        auto *partner = dynamic_cast<LoadInst *>(stripCopy(other));
        if (!partner)
            return KJLoadUseKind::Other;

        Value *pRow = nullptr, *pCol = nullptr, *pBase = nullptr;
        if (!parse2DAccess(partner->getPointer(), pRow, pCol, pBase))
            return KJLoadUseKind::Other;
        if (!isIKMatrixAccess(pRow, pCol, iIV, kIV))
            return KJLoadUseKind::Other;

        if (sameArray(pBase, rel.srcBuffer))
            return KJLoadUseKind::ParityWithAIK;
        if (sameArray(pBase, rel.dstBuffer))
            return KJLoadUseKind::AccumWithBIK;
        return KJLoadUseKind::Other;
    }

    bool isReachableFrom(BasicBlock *from, BasicBlock *to,
                         const unordered_set<BasicBlock *> &forbidden)
    {
        if (!from || !to)
            return false;
        if (forbidden.count(to))
            return false;

        unordered_set<BasicBlock *> visited;
        vector<BasicBlock *> stack = {from};
        while (!stack.empty())
        {
            BasicBlock *cur = stack.back();
            stack.pop_back();
            if (cur == to)
                return true;
            if (!visited.insert(cur).second)
                continue;
            for (auto *succ : cur->getSuccessors())
                stack.push_back(succ);
        }
        return false;
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

    static optional<TransposeBufferRelation> analyzeTransposePair(Function *func,
                                                                  const vector<Loop> &loops)
    {
        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            for (auto &instPtr : bb->getInstructions())
            {
                auto *store = dynamic_cast<StoreInst *>(instPtr.get());
                if (!store)
                    continue;

                const Loop *jLoop = nullptr;
                size_t bestBlocks = SIZE_MAX;
                for (const auto &loop : loops)
                {
                    if (!loop.header || !loop.containsBlock(bb) || loop.blocks.size() != 2)
                        continue;
                    if (loop.blocks.size() <= bestBlocks)
                    {
                        jLoop = &loop;
                        bestBlocks = loop.blocks.size();
                    }
                }
                if (!jLoop)
                    continue;

                SquareIJLoopNest nest;
                if (!findSquareIJNest(*jLoop, loops, nest))
                    continue;

                Value *srcBuffer = nullptr;
                Value *dstBuffer = nullptr;
                if (!isTransposeWitnessStore(store, nest.iIV, nest.jIV, srcBuffer, dstBuffer))
                    continue;

                BasicBlock *regionEntry = nullptr;
                auto *br = dynamic_cast<BranchInst *>(nest.iLoop->header->getTerminator());
                if (br && br->isConditional())
                {
                    for (BasicBlock *succ : {br->getTrueBlock(), br->getFalseBlock()})
                    {
                        if (succ && !nest.iLoop->containsBlock(succ))
                        {
                            regionEntry = succ;
                            break;
                        }
                    }
                }
                if (!regionEntry)
                    continue;

                TransposeBufferRelation rel;
                rel.valid = true;
                rel.srcBuffer = srcBuffer;
                rel.dstBuffer = dstBuffer;
                rel.regionEntry = regionEntry;
                rel.witnessStore = store;
                for (auto *b : nest.iLoop->blocks)
                    rel.transposeLoopBlocks.insert(b);
                return rel;
            }
        }
        return nullopt;
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

    MatrixFunctionAnalysis analyzeFunction(Function *func)
    {
        MatrixFunctionAnalysis analysis;
        func->setLoops(ControlFlowAnalysis::findLoops(func));
        const auto &loops = func->getLoops();

        if (auto rel = analyzeTransposePair(func, loops))
            analysis.transposePair = *rel;
        analysis.skewSymmetricNests = analyzeSkewSymmetricNests(func, loops);
        analysis.matMulDotProductNests = analyzeMatMulDotProductNests(loops);
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
        if (stored.transposePair && stored.transposePair->valid)
        {
            debugInfo << "MatrixStructureAnalysis: transpose buffers "
                      << stored.transposePair->srcBuffer->getName() << " -> "
                      << stored.transposePair->dstBuffer->getName() << " in " << func->getName()
                      << "\n";
        }
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
    }
    return stored.transposePair.has_value() || !stored.skewSymmetricNests.empty() ||
           !stored.matMulDotProductNests.empty();
}
