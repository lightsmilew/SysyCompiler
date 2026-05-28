#include "TransposePairLoadRewritePass.h"

using namespace std;
using namespace optimization;

Value *TransposePairLoadRewritePass::stripCopy(Value *v)
{
    while (auto *cpy = dynamic_cast<CopyInst *>(v))
        v = cpy->getSource();
    return v;
}

bool TransposePairLoadRewritePass::sameValue(Value *a, Value *b)
{
    return stripCopy(a) == stripCopy(b);
}

bool TransposePairLoadRewritePass::sameArray(Value *a, Value *b)
{
    return isSameAddr(stripCopy(a), stripCopy(b));
}

bool TransposePairLoadRewritePass::sameBound(Value *a, Value *b)
{
    auto *ca = dynamic_cast<ConstantInt *>(stripCopy(a));
    auto *cb = dynamic_cast<ConstantInt *>(stripCopy(b));
    if (ca && cb)
        return ca->Value == cb->Value;
    return sameValue(a, b);
}

bool TransposePairLoadRewritePass::feedsInductionVar(Value *from, Value *iv, unsigned depth)
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

bool TransposePairLoadRewritePass::matchesLoopIV(Value *idx, Value *iv)
{
    return sameValue(idx, iv) || feedsInductionVar(idx, iv) || feedsInductionVar(iv, idx);
}

bool TransposePairLoadRewritePass::isKJMatrixAccess(Value *row, Value *col, Value *iIV, Value *jIV,
                                                    Value *kIV)
{
    (void)kIV;
    // b[k][j] / a[k][j]：列下标为 j，行下标不是 i（区别于 b[i][k]、a[i][k]）
    if (!matchesLoopIV(col, jIV))
        return false;
    if (matchesLoopIV(row, iIV))
        return false;
    return true;
}

bool TransposePairLoadRewritePass::isIKMatrixAccess(Value *row, Value *col, Value *iIV, Value *kIV)
{
    if (!matchesLoopIV(col, kIV))
        return false;
    if (!matchesLoopIV(row, iIV))
        return false;
    return true;
}

TransposePairLoadRewritePass::KJLoadUseKind
TransposePairLoadRewritePass::classifyKJLoadUser(Instruction *user, LoadInst *kjLoad, Value *iIV,
                                               Value *kIV, const TransposeRelation &rel)
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

    if (sameArray(pBase, rel.arrayA))
        return KJLoadUseKind::ParityWithAIK;
    if (sameArray(pBase, rel.arrayB))
        return KJLoadUseKind::AccumWithBIK;
    return KJLoadUseKind::Other;
}

const Loop *TransposePairLoadRewritePass::findParentLoop(const Loop &inner,
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

bool TransposePairLoadRewritePass::getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound,
                                                     ICmpInst *&cmp)
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

bool TransposePairLoadRewritePass::parse2DAccess(Value *ptr, Value *&rowIdx, Value *&colIdx,
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

bool TransposePairLoadRewritePass::matchTransposeStore(StoreInst *store, Value *iIV, Value *jIV,
                                                       Value *&arrayA, Value *&arrayB)
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

    arrayA = loadBase;
    arrayB = storeBase;
    return true;
}

bool TransposePairLoadRewritePass::findFullMatrixTranspose(Function *func, TransposeRelation &rel)
{
    func->setLoops(ControlFlowAnalysis::findLoops(func));
    const auto &loops = func->getLoops();

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

            const Loop *iLoopPtr = findParentLoop(*jLoop, loops);
            if (!iLoopPtr)
                continue;
            const Loop &iLoop = *iLoopPtr;

            Value *jIV = nullptr, *jBound = nullptr;
            ICmpInst *jCmp = nullptr;
            if (!getHeaderBoundCmp(jLoop->header, jIV, jBound, jCmp))
                continue;

            Value *iIV = nullptr, *iBound = nullptr;
            ICmpInst *iCmp = nullptr;
            if (!getHeaderBoundCmp(iLoop.header, iIV, iBound, iCmp))
                continue;
            if (!sameBound(iBound, jBound))
                continue;

            Value *arrayA = nullptr;
            Value *arrayB = nullptr;
            if (!matchTransposeStore(store, iIV, jIV, arrayA, arrayB))
                continue;

            BasicBlock *outerExit = nullptr;
            auto *br = dynamic_cast<BranchInst *>(iLoop.header->getTerminator());
            if (br && br->isConditional())
            {
                for (BasicBlock *succ : {br->getTrueBlock(), br->getFalseBlock()})
                {
                    if (succ && !iLoop.containsBlock(succ))
                    {
                        outerExit = succ;
                        break;
                    }
                }
            }
            if (!outerExit)
                continue;

            rel.arrayA = arrayA;
            rel.arrayB = arrayB;
            rel.outerExit = outerExit;
            rel.skipBlocks.clear();
            for (auto *b : iLoop.blocks)
                rel.skipBlocks.insert(b);
            return true;
        }
    }
    return false;
}

bool TransposePairLoadRewritePass::findIJKInductionVars(BasicBlock *bb,
                                                          const vector<Loop> &loops,
                                                          Value *&iIV, Value *&jIV, Value *&kIV)
{
    iIV = jIV = kIV = nullptr;
    // 最内层循环可含 if/merge（如 matmul 的 k 循环为 4 块），不能要求恰好 2 块
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
    if (!innermost)
        return false;

    const Loop *kLoop = innermost;
    const Loop *canonicalK = kLoop;
    if (kLoop->header->getName().find("unroll") != string::npos)
    {
        canonicalK = findParentLoop(*kLoop, loops);
        if (!canonicalK)
            return false;
    }

    const Loop *jLoop = findParentLoop(*canonicalK, loops);
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

bool TransposePairLoadRewritePass::canRewriteInBlock(BasicBlock *bb, const TransposeRelation &rel)
{
    if (!bb || !rel.outerExit)
        return false;
    if (rel.skipBlocks.count(bb))
        return false;

    unordered_set<BasicBlock *> visited;
    vector<BasicBlock *> stack = {rel.outerExit};
    while (!stack.empty())
    {
        BasicBlock *cur = stack.back();
        stack.pop_back();
        if (cur == bb)
            return true;
        if (!visited.insert(cur).second)
            continue;
        for (auto *succ : cur->getSuccessors())
            stack.push_back(succ);
    }
    return false;
}

LoadInst *TransposePairLoadRewritePass::materializeTransposedLoad(BasicBlock *bb, Value *rowIdx,
                                                                 Value *colIdx, Value *newBase,
                                                                 unsigned insertIndex,
                                                                 const string &tag)
{
    auto *i32 = IntegerType::getInstance();
    auto *zero = new ConstantInt(i32, 0);
    const string rowName = "transpose_row_gep_" + tag;
    const string colName = "transpose_col_gep_" + tag;
    const string loadName = "transpose_load_" + tag;
    auto *rowGep = new GetElementPtrInst(newBase, vector<Value *>{colIdx, zero}, rowName);
    auto *elemGep = new GetElementPtrInst(rowGep, vector<Value *>{rowIdx}, colName);
    auto *newLoad = new LoadInst(elemGep, loadName);

    auto &insts = bb->getInstructions();
    insts.insert(insts.begin() + static_cast<int>(insertIndex),
                 unique_ptr<Instruction>(rowGep));
    insts.insert(insts.begin() + static_cast<int>(insertIndex) + 1,
                 unique_ptr<Instruction>(elemGep));
    insts.insert(insts.begin() + static_cast<int>(insertIndex) + 2,
                 unique_ptr<Instruction>(newLoad));
    return newLoad;
}

static bool isTransposeGeneratedLoad(LoadInst *load)
{
    auto *gep = dynamic_cast<GetElementPtrInst *>(load->getPointer());
    if (!gep)
        return false;
    const string &n = gep->getName();
    return n.find("transpose_col_gep") != string::npos ||
           n.find("transpose_row_gep") != string::npos;
}

bool TransposePairLoadRewritePass::tryRewriteLoad(Function *func, LoadInst *load,
                                                 BasicBlock *bb, const TransposeRelation &rel)
{
    if (!canRewriteInBlock(bb, rel))
        return false;
    if (isTransposeGeneratedLoad(load))
        return false;

    Value *row = nullptr, *col = nullptr, *base = nullptr;
    if (!parse2DAccess(load->getPointer(), row, col, base))
        return false;

    Value *iIV = nullptr, *jIV = nullptr, *kIV = nullptr;
    if (!findIJKInductionVars(bb, func->getLoops(), iIV, jIV, kIV))
        return false;
    if (!isKJMatrixAccess(row, col, iIV, jIV, kIV))
        return false;

    auto &insts = bb->getInstructions();
    unsigned insertIndex = 0;
    for (; insertIndex < insts.size(); ++insertIndex)
    {
        if (insts[insertIndex].get() == load)
            break;
    }
    if (insertIndex >= insts.size())
        return false;

    vector<Instruction *> parityUsers;
    vector<Instruction *> accumUsers;
    vector<Instruction *> otherUsers;
    for (auto *user : load->getUsers())
    {
        auto *inst = dynamic_cast<Instruction *>(user);
        if (!inst)
            continue;
        switch (classifyKJLoadUser(inst, load, iIV, kIV, rel))
        {
        case KJLoadUseKind::ParityWithAIK:
            parityUsers.push_back(inst);
            break;
        case KJLoadUseKind::AccumWithBIK:
            accumUsers.push_back(inst);
            break;
        default:
            otherUsers.push_back(inst);
            break;
        }
    }

    auto replaceInUsers = [&](LoadInst *newLoad, const vector<Instruction *> &users) {
        for (auto *inst : users)
            inst->replaceOperand(load, newLoad);
    };

    auto logRewrite = [&](Value *dstBase) {
        if (verbose)
        {
            debugInfo << "TransposePairLoadRewrite: load " << base->getName() << "[" << row->getName()
                      << "][" << col->getName() << "] -> " << dstBase->getName() << "["
                      << col->getName() << "][" << row->getName() << "] in " << bb->getName()
                      << "\n";
        }
    };

    // a[k][j]：仅出现在 b[i][k]*a[k][j] 累加项，等价于 b[j][k]
    if (sameArray(base, rel.arrayA))
    {
        LoadInst *newLoad =
            materializeTransposedLoad(bb, row, col, rel.arrayB, insertIndex, "accum");
        replaceInUsers(newLoad, accumUsers);
        replaceInUsers(newLoad, otherUsers);
        load->removeThisFromOperands();
        needToDelete.push_back(insts[insertIndex + 3].release());
        insts.erase(insts.begin() + static_cast<int>(insertIndex) + 3);
        logRewrite(rel.arrayB);
        return true;
    }

    if (!sameArray(base, rel.arrayB))
        return false;

    // b[k][j]：与 a[i][k] 相乘用于条件 → a[j][k]；与 b[i][k] 相乘用于累加 → b[j][k]（=a[k][j]）
    const bool needParity = !parityUsers.empty() || !otherUsers.empty();
    const bool needAccum = !accumUsers.empty();

    LoadInst *parityLoad = nullptr;
    LoadInst *accumLoad = nullptr;
    unsigned cursor = insertIndex;
    if (needParity)
    {
        parityLoad = materializeTransposedLoad(bb, row, col, rel.arrayA, cursor, "parity");
        cursor += 3;
    }
    if (needAccum)
    {
        accumLoad = materializeTransposedLoad(bb, row, col, rel.arrayB, cursor, "accum");
        cursor += 3;
    }

    if (needParity)
    {
        replaceInUsers(parityLoad, parityUsers);
        replaceInUsers(parityLoad, otherUsers);
    }
    if (needAccum)
        replaceInUsers(accumLoad, accumUsers);

    load->removeThisFromOperands();
    const unsigned eraseIdx =
        insertIndex + (needParity ? 3u : 0u) + (needAccum ? 3u : 0u);
    needToDelete.push_back(insts[eraseIdx].release());
    insts.erase(insts.begin() + static_cast<int>(eraseIdx));

    if (needParity)
        logRewrite(rel.arrayA);
    if (needAccum)
        logRewrite(rel.arrayB);
    return needParity || needAccum;
}

bool TransposePairLoadRewritePass::runOnFunction(Function *func)
{
    TransposeRelation rel;
    if (!findFullMatrixTranspose(func, rel))
        return false;

    func->setLoops(ControlFlowAnalysis::findLoops(func));
    vector<pair<LoadInst *, BasicBlock *>> candidates;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        for (auto &instPtr : bb->getInstructions())
        {
            if (auto *load = dynamic_cast<LoadInst *>(instPtr.get()))
                candidates.emplace_back(load, bb);
        }
    }

    // 先改写 b[k][j]（按用途拆分 parity/accum），再改写 a[k][j]
    stable_sort(candidates.begin(), candidates.end(),
                [&](const pair<LoadInst *, BasicBlock *> &lhs,
                    const pair<LoadInst *, BasicBlock *> &rhs) {
                    Value *r = nullptr, *c = nullptr, *baseL = nullptr;
                    Value *r2 = nullptr, *c2 = nullptr, *baseR = nullptr;
                    parse2DAccess(lhs.first->getPointer(), r, c, baseL);
                    parse2DAccess(rhs.first->getPointer(), r2, c2, baseR);
                    const bool lA = sameArray(baseL, rel.arrayA);
                    const bool rA = sameArray(baseR, rel.arrayA);
                    if (lA != rA)
                        return !lA;
                    return false;
                });

    auto stillInBlock = [](LoadInst *load, BasicBlock *bb) {
        for (auto &instPtr : bb->getInstructions())
            if (instPtr.get() == load)
                return true;
        return false;
    };

    bool changed = false;
    for (auto &[load, bb] : candidates)
    {
        if (!stillInBlock(load, bb))
            continue;
        if (tryRewriteLoad(func, load, bb, rel))
            changed = true;
    }

    if (changed && verbose)
        debugInfo << "TransposePairLoadRewrite: established b/a transpose pair in "
                  << func->getName() << "\n";
    return changed;
}
