#include "SymmetricNegateUpperTrianglePass.h"

using namespace std;
using namespace optimization;

Value *SymmetricNegateUpperTrianglePass::stripCopy(Value *v)
{
    while (auto *cpy = dynamic_cast<CopyInst *>(v))
        v = cpy->getSource();
    return v;
}

bool SymmetricNegateUpperTrianglePass::sameValue(Value *a, Value *b)
{
    return stripCopy(a) == stripCopy(b);
}

bool SymmetricNegateUpperTrianglePass::sameBound(Value *a, Value *b)
{
    auto *ca = dynamic_cast<ConstantInt *>(stripCopy(a));
    auto *cb = dynamic_cast<ConstantInt *>(stripCopy(b));
    if (ca && cb)
        return ca->Value == cb->Value;
    return sameValue(a, b);
}

const Loop *SymmetricNegateUpperTrianglePass::findParentLoop(const Loop &inner,
                                                             const vector<Loop> &loops)
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

bool SymmetricNegateUpperTrianglePass::getHeaderBoundCmp(BasicBlock *header, Value *&iv,
                                                           Value *&bound, ICmpInst *&cmp)
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

bool SymmetricNegateUpperTrianglePass::parse2DAccess(Value *ptr, Value *&rowIdx, Value *&colIdx,
                                                     Value *&arrayBase)
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

bool SymmetricNegateUpperTrianglePass::isZeroInit(Value *v)
{
    auto *c = dynamic_cast<ConstantInt *>(stripCopy(v));
    return c && c->Value == 0;
}

bool SymmetricNegateUpperTrianglePass::feedsInductionVar(Value *from, Value *iv, unsigned depth)
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

bool SymmetricNegateUpperTrianglePass::isNegatedLoad(Value *val, LoadInst *&loadOut)
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

bool SymmetricNegateUpperTrianglePass::matchSymmetricNegateStore(StoreInst *store, Value *iIV,
                                                                 Value *jIV, Value *cArray)
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

    if (cArray && !sameValue(storeBase, cArray))
        return false;
    if (!sameValue(storeBase, loadBase))
        return false;

    return sameValue(storeRow, iIV) && sameValue(storeCol, jIV) && sameValue(loadRow, jIV) &&
           sameValue(loadCol, iIV);
}

CopyInst *SymmetricNegateUpperTrianglePass::findJInitCopyInOuterBody(const Loop &iLoop,
                                                                   const Loop &jLoop, Value *jIV)
{
    BasicBlock *jPreheader = nullptr;
    for (auto *bb : iLoop.blocks)
    {
        if (jLoop.containsBlock(bb))
            continue;
        for (auto *succ : bb->getSuccessors())
        {
            if (succ == jLoop.header)
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

    for (auto *bb : iLoop.blocks)
    {
        if (bb == iLoop.header || jLoop.containsBlock(bb))
            continue;
        if (auto *found = tryBlock(bb))
            return found;
    }
    return nullptr;
}

bool SymmetricNegateUpperTrianglePass::setJInitToOuterIV(const Loop &iLoop, const Loop &jLoop,
                                                         Value *iIV, Value *jIV)
{
    bool changed = false;

    if (auto *initCopy = findJInitCopyInOuterBody(iLoop, jLoop, jIV))
    {
        initCopy->setOperandByIndex(0, iIV);
        changed = true;
        if (verbose)
            debugInfo << "SymmetricNegateUpperTriangle: j init copy 0 -> i\n";
    }

    BasicBlock *jHeader = jLoop.header;
    for (auto &instPtr : jHeader->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi || !sameValue(phi, jIV))
            continue;
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            BasicBlock *from = phi->getIncomingBlock(i);
            if (jLoop.containsBlock(from))
                continue;
            if (!isZeroInit(phi->getIncomingValue(i)))
                continue;
            phi->setIncomingValue(i, iIV);
            changed = true;
            if (verbose)
                debugInfo << "SymmetricNegateUpperTriangle: j phi init 0 -> i from "
                          << from->getName() << "\n";
        }
    }

    return changed;
}

bool SymmetricNegateUpperTrianglePass::runOnFunction(Function *func)
{
    bool changed = false;
    func->setLoops(ControlFlowAnalysis::findLoops(func));
    auto &loops = func->getLoops();

    for (const auto &jLoop : loops)
    {
        if (!jLoop.header || jLoop.blocks.size() != 2)
            continue;

        const Loop *iLoopPtr = findParentLoop(jLoop, loops);
        if (!iLoopPtr)
            continue;
        const Loop &iLoop = *iLoopPtr;

        Value *jIV = nullptr, *jBound = nullptr;
        ICmpInst *jCmp = nullptr;
        if (!getHeaderBoundCmp(jLoop.header, jIV, jBound, jCmp))
            continue;

        Value *iIV = nullptr, *iBound = nullptr;
        ICmpInst *iCmp = nullptr;
        if (!getHeaderBoundCmp(iLoop.header, iIV, iBound, iCmp))
            continue;

        if (!sameBound(jBound, iBound))
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

        StoreInst *matchedStore = nullptr;
        Value *cArray = nullptr;
        for (auto &instPtr : jBody->getInstructions())
        {
            auto *store = dynamic_cast<StoreInst *>(instPtr.get());
            if (!store)
                continue;
            if (matchSymmetricNegateStore(store, iIV, jIV, cArray))
            {
                if (matchedStore)
                {
                    matchedStore = nullptr;
                    break;
                }
                matchedStore = store;
                Value *sr = nullptr, *sc = nullptr, *sb = nullptr;
                parse2DAccess(store->getPointer(), sr, sc, sb);
                cArray = sb;
            }
        }
        if (!matchedStore)
            continue;

        if (!findJInitCopyInOuterBody(iLoop, jLoop, jIV))
        {
            bool hasPhiInit = false;
            for (auto &instPtr : jLoop.header->getInstructions())
            {
                auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
                if (!phi || !sameValue(phi, jIV))
                    continue;
                for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
                {
                    if (!jLoop.containsBlock(phi->getIncomingBlock(i)) &&
                        isZeroInit(phi->getIncomingValue(i)))
                        hasPhiInit = true;
                }
            }
            if (!hasPhiInit)
                continue;
        }

        if (setJInitToOuterIV(iLoop, jLoop, iIV, jIV))
        {
            changed = true;
            if (verbose)
                debugInfo << "SymmetricNegateUpperTriangle: raised j lower bound to i in "
                          << func->getName() << "\n";
        }
    }

    if (changed)
        func->setLoops(ControlFlowAnalysis::findLoops(func));
    return changed;
}
