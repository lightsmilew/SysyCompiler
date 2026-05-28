#include "LoopTilingPass.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
using namespace std;
using namespace optimization;

namespace
{
    ConstantInt *ci(int v)
    {
        return new ConstantInt(IntegerType::getInstance(), v);
    }

    unique_ptr<Instruction> own(Instruction *inst)
    {
        return unique_ptr<Instruction>(inst);
    }

    void wireEdge(BasicBlock *from, BasicBlock *to)
    {
        from->addSuccessor(to);
        to->addPredecessor(from);
    }

    void replaceTerminator(BasicBlock *bb, unique_ptr<Instruction> term)
    {
        auto &insts = bb->getInstructions();
        if (!insts.empty() && insts.back()->Op == Opcode::Br)
        {
            insts.back()->removeThisFromOperands();
            insts.pop_back();
        }
        bb->addInstruction(std::move(term));
    }

    BasicBlock *findPreheader(BasicBlock *header, const Loop &loop)
    {
        BasicBlock *pre = nullptr;
        for (auto *pred : header->getPredecessors())
        {
            if (!loop.containsBlock(pred))
            {
                if (pre)
                    return nullptr;
                pre = pred;
            }
        }
        return pre;
    }

    void retargetBranchTo(BasicBlock *from, BasicBlock *oldTarget, BasicBlock *newTarget)
    {
        auto *br = dynamic_cast<BranchInst *>(from->getTerminator());
        if (!br)
            return;
        if (br->isConditional())
        {
            if (br->getTrueBlock() == oldTarget)
                br->setTrueBlock(newTarget);
            if (br->getFalseBlock() == oldTarget)
                br->setFalseBlock(newTarget);
        }
        else if (br->getTrueBlock() == oldTarget)
        {
            br->setTrueBlock(newTarget);
        }
        from->removeSuccessor(oldTarget);
        oldTarget->removePredecessor(from);
        wireEdge(from, newTarget);
    }

    GetElementPtrInst *cloneGepWithRowMap(GetElementPtrInst *gep, Value *newRow,
                                          const string &suffix)
    {
        auto indices = gep->getIndices();
        if (indices.empty())
            return nullptr;
        vector<Value *> newIdx = indices;
        newIdx[0] = newRow;
        auto *ng = new GetElementPtrInst(gep->getPointerOperand(), newIdx, gep->getName() + suffix);
        return ng;
    }
}

Value *LoopTilingPass::stripCopy(Value *v)
{
    while (auto *cpy = dynamic_cast<CopyInst *>(v))
        v = cpy->getSource();
    return v;
}

bool LoopTilingPass::sameValue(Value *a, Value *b)
{
    return stripCopy(a) == stripCopy(b);
}

bool LoopTilingPass::sameBound(Value *a, Value *b)
{
    auto *ca = dynamic_cast<ConstantInt *>(stripCopy(a));
    auto *cb = dynamic_cast<ConstantInt *>(stripCopy(b));
    if (ca && cb)
        return ca->Value == cb->Value;
    return sameValue(a, b);
}

bool LoopTilingPass::sameIndex(Value *a, Value *b)
{
    if (sameValue(a, b))
        return true;
    string na = stripCopy(a)->getName();
    string nb = stripCopy(b)->getName();
    return !na.empty() && na == nb;
}

const Loop *LoopTilingPass::findParentLoop(const Loop &inner, const vector<Loop> &loops)
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

BasicBlock *LoopTilingPass::getLoopLatch(const Loop &loop)
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

BasicBlock *LoopTilingPass::getLoopExit(const Loop &loop)
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

bool LoopTilingPass::isSimpleTwoBlockLoop(const Loop &loop)
{
    return loop.header && loop.blocks.size() == 2;
}

bool LoopTilingPass::getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound,
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

bool LoopTilingPass::parse2DAccess(Value *ptr, Value *&rowIdx, Value *&colIdx, Value *&arrayBase)
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
        rowIdx = indices[0];
        colIdx = indices[1];
        arrayBase = gep->getPointerOperand();
        return true;
    }
    if (indices.size() == 1)
    {
        colIdx = indices[0];
        auto *baseGep = dynamic_cast<GetElementPtrInst *>(gep->getPointerOperand());
        if (!baseGep || baseGep->getIndices().size() != 2)
            return false;
        rowIdx = baseGep->getIndices()[0];
        arrayBase = baseGep->getPointerOperand();
        return true;
    }
    return false;
}

bool LoopTilingPass::matchGuardedAccumulateNest(const vector<Loop> &loops, NestPattern &pat,
                                                bool verbose, std::stringstream &dbg)
{
    for (const auto &kLoop : loops)
    {
        if (!isSimpleTwoBlockLoop(kLoop))
            continue;

        BasicBlock *kHeader = kLoop.header;
        BasicBlock *kBody = getLoopLatch(kLoop);
        BasicBlock *kExit = getLoopExit(kLoop);
        if (!kBody || !kExit || kBody == kHeader)
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (bad latch/exit)\n";
            continue;
        }

        const Loop *jLoop = findParentLoop(kLoop, loops);
        if (!jLoop)
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (no j parent)\n";
            continue;
        }
        const Loop *iLoop = findParentLoop(*jLoop, loops);
        if (!iLoop)
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (no i parent)\n";
            continue;
        }

        Value *kIV = nullptr;
        Value *bound = nullptr;
        ICmpInst *kCmp = nullptr;
        if (!getHeaderBoundCmp(kHeader, kIV, bound, kCmp))
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (k bound cmp)\n";
            continue;
        }
        auto *boundC = dynamic_cast<ConstantInt *>(stripCopy(bound));
        if (!boundC || boundC->Value % kTileSize != 0)
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (k bound const)\n";
            continue;
        }

        BasicBlock *jHeader = jLoop->header;
        Value *jIV = nullptr;
        Value *jBound = nullptr;
        ICmpInst *jCmp = nullptr;
        if (!getHeaderBoundCmp(jHeader, jIV, jBound, jCmp))
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (j bound cmp)\n";
            continue;
        }
        if (!sameBound(jBound, bound))
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (j bound mismatch)\n";
            continue;
        }

        BasicBlock *iHeader = iLoop->header;
        Value *iIV = nullptr;
        Value *iBound = nullptr;
        ICmpInst *iCmp = nullptr;
        if (!getHeaderBoundCmp(iHeader, iIV, iBound, iCmp))
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (i bound cmp)\n";
            continue;
        }
        if (!sameBound(iBound, bound))
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (i bound mismatch)\n";
            continue;
        }

        BasicBlock *jBody = nullptr;
        auto *jBr = dynamic_cast<BranchInst *>(jHeader->getTerminator());
        if (jBr && jBr->isConditional())
            jBody = jBr->getTrueBlock();
        if (!jBody || !jLoop->containsBlock(jBody))
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (j body)\n";
            continue;
        }

        BasicBlock *iBody = nullptr;
        auto *iBr = dynamic_cast<BranchInst *>(iHeader->getTerminator());
        if (iBr && iBr->isConditional())
            iBody = iBr->getTrueBlock();
        if (!iBody || !iLoop->containsBlock(iBody))
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (i body)\n";
            continue;
        }

        BasicBlock *jExit = getLoopExit(*jLoop);
        BasicBlock *iExit = getLoopExit(*iLoop);
        if (!jExit || !iExit)
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (i/j exit)\n";
            continue;
        }

        StoreInst *cStore = nullptr;
        Value *storeRow = nullptr;
        Value *storeCol = nullptr;
        Value *cArray = nullptr;
        for (auto &instPtr : kExit->getInstructions())
        {
            if (auto *st = dynamic_cast<StoreInst *>(instPtr.get()))
            {
                if (!parse2DAccess(st->getPointer(), storeRow, storeCol, cArray))
                    continue;
                cStore = st;
                break;
            }
        }
        if (!cStore)
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (no c store)\n";
            continue;
        }
        if (!sameIndex(storeRow, iIV) || !sameIndex(storeCol, jIV))
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (store indices)\n";
            continue;
        }

        Value *acc = nullptr;
        for (auto &instPtr : kBody->getInstructions())
        {
            if (auto *add = dynamic_cast<BinaryOperator *>(instPtr.get()))
            {
                if (add->getOpcode() != Opcode::Add)
                    continue;
                if (add->getName().find("cga_new_acc") != string::npos)
                {
                    acc = stripCopy(add->getLHS());
                    break;
                }
            }
        }
        if (!acc)
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (no acc in k-body)\n";
            continue;
        }

        bool hasGuardedAcc = false;
        for (auto &instPtr : kBody->getInstructions())
        {
            if (auto *mul = dynamic_cast<BinaryOperator *>(instPtr.get()))
            {
                if (mul->getOpcode() != Opcode::Mul)
                    continue;
                if (mul->getName().find("cga_scaled") != string::npos)
                {
                    hasGuardedAcc = true;
                    break;
                }
            }
        }
        if (!hasGuardedAcc)
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (no cga_scaled)\n";
            continue;
        }

        BasicBlock *iPreheader = findPreheader(iHeader, *iLoop);
        if (!iPreheader)
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (no preheader)\n";
            continue;
        }

        Value *cRowGepBase = nullptr;
        if (auto *storeElemGep = dynamic_cast<GetElementPtrInst *>(cStore->getPointer()))
        {
            if (storeElemGep->getIndices().size() == 1)
                cRowGepBase = storeElemGep->getPointerOperand();
        }
        if (!cRowGepBase)
        {
            for (auto &instPtr : iBody->getInstructions())
            {
                if (auto *gep = dynamic_cast<GetElementPtrInst *>(instPtr.get()))
                {
                    if (stripCopy(gep->getPointerOperand()) == stripCopy(cArray))
                    {
                        cRowGepBase = gep;
                        break;
                    }
                }
            }
        }
        if (!cArray)
        {
            if (verbose)
                dbg << "LoopTiling: skip " << kHeader->getName() << " (no c array)\n";
            continue;
        }

        pat.iLoop = iLoop;
        pat.jLoop = jLoop;
        pat.kLoop = &kLoop;
        pat.iIV = iIV;
        pat.jIV = jIV;
        pat.kIV = kIV;
        pat.acc = acc;
        pat.bound = bound;
        pat.boundConst = boundC->Value;
        pat.iHeader = iHeader;
        pat.iBody = iBody;
        pat.iExit = iExit;
        pat.iPreheader = iPreheader;
        pat.jHeader = jHeader;
        pat.jBody = jBody;
        pat.jExit = jExit;
        pat.kHeader = kHeader;
        pat.kBody = kBody;
        pat.kExit = kExit;
        pat.cStore = cStore;
        pat.cRowGepBase = cRowGepBase;
        pat.cArray = cArray;
        return true;
    }
    return false;
}

Value *LoopTilingPass::makeTileUpperBound(Value *tileStart, Value *globalBound, int tileSize,
                                          BasicBlock *bb, const string &name)
{
    auto *tileEnd = new BinaryOperator(Opcode::Add, tileStart, ci(tileSize), name + "_end");
    bb->addInstruction(own(tileEnd));
    auto *cmp = new ICmpInst(ICmpInst::ICMP_SLT, tileEnd, globalBound, name + "_end_cmp");
    bb->addInstruction(own(cmp));
    auto *sel = new SelectInst(cmp, tileEnd, globalBound, name + "_ub");
    bb->addInstruction(own(sel));
    return sel;
}

void LoopTilingPass::moveKBodyCompute(BasicBlock *src, BasicBlock *dst,
                                      unordered_map<Value *, Value *> &vmap, Value *oldKIV,
                                      Value *oldAcc)
{
    auto remap = [&](Value *v) -> Value * {
        Value *s = stripCopy(v);
        auto it = vmap.find(s);
        if (it != vmap.end())
            return it->second;
        it = vmap.find(v);
        if (it != vmap.end())
            return it->second;
        return v;
    };

    auto &srcInsts = src->getInstructions();
    for (auto it = srcInsts.begin(); it != srcInsts.end();)
    {
        Instruction *old = it->get();
        if (dynamic_cast<BranchInst *>(old))
        {
            ++it;
            continue;
        }

        if (auto *cpy = dynamic_cast<CopyInst *>(old))
        {
            if (sameValue(cpy, oldAcc) || sameValue(cpy, oldKIV) ||
                sameValue(stripCopy(cpy->getSource()), oldAcc) ||
                sameValue(stripCopy(cpy->getSource()), oldKIV))
            {
                ++it;
                continue;
            }
        }

        if (auto *add = dynamic_cast<BinaryOperator *>(old))
        {
            if (add->getOpcode() == Opcode::Add)
            {
                Value *lhs = stripCopy(add->getLHS());
                Value *rhs = stripCopy(add->getRHS());
                if ((sameValue(lhs, oldKIV) && dynamic_cast<ConstantInt *>(rhs) &&
                     static_cast<ConstantInt *>(rhs)->Value == 1) ||
                    (sameValue(rhs, oldKIV) && dynamic_cast<ConstantInt *>(lhs) &&
                     static_cast<ConstantInt *>(lhs)->Value == 1))
                {
                    ++it;
                    continue;
                }
            }
        }

        unique_ptr<Instruction> inst(it->release());
        for (size_t k = 0; k < inst->getOperands().size(); ++k)
            inst->setOperandByIndex(k, remap(inst->getOperands()[k]));

        Instruction *moved = inst.get();
        vmap[old] = moved;
        vmap[stripCopy(old)] = moved;
        dst->addInstruction(std::move(inst));
        it = srcInsts.erase(it);
    }
}

bool LoopTilingPass::applyTiling(Function *func, NestPattern &pat)
{
    auto *zero = ci(0);
    auto *one = ci(1);
    auto *tileStep = ci(kTileSize);

    BasicBlock *iiHeader = func->addBasicBlock("tile_ii_header");
    BasicBlock *iiBody = func->addBasicBlock("tile_ii_body");
    BasicBlock *iiExit = func->addBasicBlock("tile_ii_exit");

    BasicBlock *jjHeader = func->addBasicBlock("tile_jj_header");
    BasicBlock *jjBody = func->addBasicBlock("tile_jj_body");
    BasicBlock *jjExit = func->addBasicBlock("tile_jj_exit");

    BasicBlock *iHeader = func->addBasicBlock("tile_i_header");
    BasicBlock *iBody = func->addBasicBlock("tile_i_body");
    BasicBlock *iTileExit = func->addBasicBlock("tile_i_exit");

    BasicBlock *jHeader = func->addBasicBlock("tile_j_header");
    BasicBlock *jBody = func->addBasicBlock("tile_j_body");
    BasicBlock *jTileExit = func->addBasicBlock("tile_j_exit");

    BasicBlock *kkHeader = func->addBasicBlock("tile_kk_header");
    BasicBlock *kkBody = func->addBasicBlock("tile_kk_body");
    BasicBlock *kkTileExit = func->addBasicBlock("tile_kk_exit");

    BasicBlock *kHeader = func->addBasicBlock("tile_k_header");
    BasicBlock *kBody = func->addBasicBlock("tile_k_body");
    BasicBlock *kLatch = func->addBasicBlock("tile_k_latch");
    BasicBlock *kTileExit = func->addBasicBlock("tile_k_exit");

    retargetBranchTo(pat.iPreheader, pat.iHeader, iiHeader);

    auto *iiPhi = new PhiInst(IntegerType::getInstance(), "tile_ii");
    iiPhi->addIncoming(zero, pat.iPreheader);
    iiHeader->addInstruction(own(iiPhi));
    auto *iiCmp = new ICmpInst(ICmpInst::ICMP_SLT, iiPhi, pat.bound, "tile_ii_cmp");
    iiHeader->addInstruction(own(iiCmp));
    iiHeader->addInstruction(own(new BranchInst(iiCmp, iiBody, iiExit)));
    wireEdge(iiHeader, iiBody);
    wireEdge(iiHeader, iiExit);

    auto *jjPhi = new PhiInst(IntegerType::getInstance(), "tile_jj");
    jjPhi->addIncoming(zero, iiBody);
    iiBody->addInstruction(own(new BranchInst(jjHeader)));
    wireEdge(iiBody, jjHeader);

    jjHeader->addInstruction(own(jjPhi));
    auto *jjCmp = new ICmpInst(ICmpInst::ICMP_SLT, jjPhi, pat.bound, "tile_jj_cmp");
    jjHeader->addInstruction(own(jjCmp));
    jjHeader->addInstruction(own(new BranchInst(jjCmp, jjBody, jjExit)));
    wireEdge(jjHeader, jjBody);
    wireEdge(jjHeader, jjExit);

    auto *iPhi = new PhiInst(IntegerType::getInstance(), "tile_i");
    iPhi->addIncoming(iiPhi, jjBody);
    jjBody->addInstruction(own(new BranchInst(iHeader)));
    wireEdge(jjBody, iHeader);

    iHeader->addInstruction(own(iPhi));
    Value *iUb = makeTileUpperBound(iiPhi, pat.bound, kTileSize, iHeader, "tile_i");
    auto *iCmp = new ICmpInst(ICmpInst::ICMP_SLT, iPhi, iUb, "tile_i_cmp");
    iHeader->addInstruction(own(iCmp));
    iHeader->addInstruction(own(new BranchInst(iCmp, iBody, iTileExit)));
    wireEdge(iHeader, iBody);
    wireEdge(iHeader, iTileExit);

    auto *jPhi = new PhiInst(IntegerType::getInstance(), "tile_j");
    jPhi->addIncoming(jjPhi, iBody);
    unordered_map<Value *, Value *> hoistMap;
    hoistMap[pat.iIV] = iPhi;
    for (auto &instPtr : pat.iBody->getInstructions())
    {
        if (dynamic_cast<BranchInst *>(instPtr.get()))
            continue;
        if (auto *cpy = dynamic_cast<CopyInst *>(instPtr.get()))
        {
            if (sameValue(stripCopy(cpy->getSource()), zero))
                continue;
        }
        if (auto *gep = dynamic_cast<GetElementPtrInst *>(instPtr.get()))
        {
            auto *ng = cloneGepWithRowMap(gep, iPhi, "_tile");
            if (ng)
            {
                iBody->addInstruction(own(ng));
                hoistMap[gep] = ng;
            }
        }
    }
    Value *cBase = nullptr;
    if (pat.cRowGepBase && hoistMap.count(pat.cRowGepBase))
        cBase = hoistMap.at(pat.cRowGepBase);
    else if (pat.cArray)
    {
        auto *cRowGep = new GetElementPtrInst(pat.cArray, {iPhi, zero}, "tile_c_row_gep");
        iBody->addInstruction(own(cRowGep));
        cBase = cRowGep;
    }
    iBody->addInstruction(own(new BranchInst(jHeader)));
    wireEdge(iBody, jHeader);

    jHeader->addInstruction(own(jPhi));
    Value *jUb = makeTileUpperBound(jjPhi, pat.bound, kTileSize, jHeader, "tile_j");
    auto *jCmp = new ICmpInst(ICmpInst::ICMP_SLT, jPhi, jUb, "tile_j_cmp");
    jHeader->addInstruction(own(jCmp));
    jHeader->addInstruction(own(new BranchInst(jCmp, jBody, jTileExit)));
    wireEdge(jHeader, jBody);
    wireEdge(jHeader, jTileExit);

    auto *accPhi = new PhiInst(IntegerType::getInstance(), "tile_acc");
    accPhi->addIncoming(zero, jBody);
    auto *kkPhi = new PhiInst(IntegerType::getInstance(), "tile_kk");
    kkPhi->addIncoming(zero, jBody);
    jBody->addInstruction(own(new BranchInst(kkHeader)));
    wireEdge(jBody, kkHeader);

    kkHeader->addInstruction(own(kkPhi));
    auto *kkCmp = new ICmpInst(ICmpInst::ICMP_SLT, kkPhi, pat.bound, "tile_kk_cmp");
    kkHeader->addInstruction(own(kkCmp));
    kkHeader->addInstruction(own(new BranchInst(kkCmp, kkBody, kkTileExit)));
    wireEdge(kkHeader, kkBody);
    wireEdge(kkHeader, kkTileExit);

    auto *kPhi = new PhiInst(IntegerType::getInstance(), "tile_k");
    kPhi->addIncoming(kkPhi, kkBody);
    kkBody->addInstruction(own(new BranchInst(kHeader)));
    wireEdge(kkBody, kHeader);

    Value *kUb = makeTileUpperBound(kkPhi, pat.bound, kTileSize, kHeader, "tile_k");
    kHeader->addInstruction(own(kPhi));
    kHeader->addInstruction(own(accPhi));
    auto *kCmp = new ICmpInst(ICmpInst::ICMP_SLT, kPhi, kUb, "tile_k_cmp");
    kHeader->addInstruction(own(kCmp));
    kHeader->addInstruction(own(new BranchInst(kCmp, kBody, kTileExit)));
    wireEdge(kHeader, kBody);
    wireEdge(kHeader, kTileExit);

    unordered_map<Value *, Value *> vmap = hoistMap;
    vmap[pat.jIV] = jPhi;
    vmap[pat.kIV] = kPhi;
    vmap[pat.acc] = accPhi;
    vmap[stripCopy(pat.acc)] = accPhi;

    moveKBodyCompute(pat.kBody, kBody, vmap, pat.kIV, pat.acc);
    kBody->addInstruction(own(new BranchInst(kLatch)));
    wireEdge(kBody, kLatch);

    Value *newAcc = nullptr;
    for (auto it = kBody->getInstructions().rbegin(); it != kBody->getInstructions().rend(); ++it)
    {
        if (auto *add = dynamic_cast<BinaryOperator *>(it->get()))
        {
            if (add->getOpcode() != Opcode::Add)
                continue;
            newAcc = add;
            break;
        }
    }
    if (!newAcc)
        return false;

    auto *kInc = new BinaryOperator(Opcode::Add, kPhi, one, "tile_k_inc");
    kLatch->addInstruction(own(kInc));
    kPhi->addIncoming(kInc, kLatch);
    accPhi->addIncoming(newAcc, kLatch);
    kLatch->addInstruction(own(new BranchInst(kHeader)));
    wireEdge(kLatch, kHeader);

    auto *kkInc = new BinaryOperator(Opcode::Add, kkPhi, tileStep, "tile_kk_inc");
    kTileExit->addInstruction(own(kkInc));
    kkPhi->addIncoming(kkInc, kTileExit);
    kTileExit->addInstruction(own(new BranchInst(kkHeader)));
    wireEdge(kTileExit, kkHeader);

    Value *cBaseForStore = cBase;
    if (!cBaseForStore && pat.cRowGepBase)
        cBaseForStore = pat.cRowGepBase;
    auto *cElemGep = new GetElementPtrInst(cBaseForStore, {jPhi}, "tile_c_gep");
    kkTileExit->addInstruction(own(cElemGep));
    kkTileExit->addInstruction(own(new StoreInst(accPhi, cElemGep)));
    auto *jInc = new BinaryOperator(Opcode::Add, jPhi, one, "tile_j_inc");
    kkTileExit->addInstruction(own(jInc));
    jPhi->addIncoming(jInc, kkTileExit);
    kkTileExit->addInstruction(own(new BranchInst(jHeader)));
    wireEdge(kkTileExit, jHeader);

    auto *iInc = new BinaryOperator(Opcode::Add, iPhi, one, "tile_i_inc");
    jTileExit->addInstruction(own(iInc));
    iPhi->addIncoming(iInc, jTileExit);
    jTileExit->addInstruction(own(new BranchInst(iHeader)));
    wireEdge(jTileExit, iHeader);

    auto *jjInc = new BinaryOperator(Opcode::Add, jjPhi, tileStep, "tile_jj_inc");
    iTileExit->addInstruction(own(jjInc));
    jjPhi->addIncoming(jjInc, iTileExit);
    iTileExit->addInstruction(own(new BranchInst(jjHeader)));
    wireEdge(iTileExit, jjHeader);

    auto *iiInc = new BinaryOperator(Opcode::Add, iiPhi, tileStep, "tile_ii_inc");
    jjExit->addInstruction(own(iiInc));
    iiPhi->addIncoming(iiInc, jjExit);
    jjExit->addInstruction(own(new BranchInst(iiHeader)));
    wireEdge(jjExit, iiHeader);

    BasicBlock *afterI = pat.iExit;
    iiExit->addInstruction(own(new BranchInst(afterI)));
    wireEdge(iiExit, afterI);

    vector<BasicBlock *> toRemove;
    for (BasicBlock *bb : pat.iLoop->blocks)
        toRemove.push_back(bb);
    for (BasicBlock *bb : pat.jLoop->blocks)
    {
        if (find(toRemove.begin(), toRemove.end(), bb) == toRemove.end())
            toRemove.push_back(bb);
    }
    for (BasicBlock *bb : pat.kLoop->blocks)
    {
        if (find(toRemove.begin(), toRemove.end(), bb) == toRemove.end())
            toRemove.push_back(bb);
    }
    if (find(toRemove.begin(), toRemove.end(), pat.kExit) == toRemove.end())
        toRemove.push_back(pat.kExit);

    for (BasicBlock *bb : toRemove)
    {
        if (!bb)
            continue;
        for (auto &instPtr : afterI->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
            {
                for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
                {
                    if (phi->getIncomingBlock(i) == bb && bb == pat.iExit)
                        phi->replaceIncomingBasicBlock(bb, iiExit);
                }
            }
        }
        bb->removeSelfBasicBlock();
    }

    if (verbose)
    {
        debugInfo << "LoopTiling: tiled i-j-k guarded-accumulate nest (tile=" << kTileSize
                  << ") in " << func->getName() << "\n";
    }
    return true;
}

bool LoopTilingPass::runOnFunction(Function *func)
{
    func->setLoops(ControlFlowAnalysis::findLoops(func));
    NestPattern pat;
    if (!matchGuardedAccumulateNest(func->getLoops(), pat, verbose, debugInfo))
        return false;
    bool changed = applyTiling(func, pat);
    if (changed)
        func->setLoops(ControlFlowAnalysis::findLoops(func));
    return changed;
}
