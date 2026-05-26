#include "LoopInterchangePass.h"
#include <algorithm>
using namespace std;
using namespace optimization;

namespace
{
    constexpr unsigned kAccCapacity = 1024;

    static ConstantInt *ci(int v)
    {
        return new ConstantInt(IntegerType::getInstance(), v);
    }

    static unique_ptr<Instruction> own(Instruction *inst)
    {
        return unique_ptr<Instruction>(inst);
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
        }
        bb->addInstruction(std::move(term));
    }
}

Value *LoopInterchangePass::stripCopy(Value *v)
{
    while (auto *cpy = dynamic_cast<CopyInst *>(v))
        v = cpy->getSource();
    return v;
}

bool LoopInterchangePass::sameValue(Value *a, Value *b)
{
    return stripCopy(a) == stripCopy(b);
}

const Loop *LoopInterchangePass::findParentLoop(const Loop &inner, const vector<Loop> &loops)
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

BasicBlock *LoopInterchangePass::getLoopLatch(const Loop &loop)
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

BasicBlock *LoopInterchangePass::getLoopExit(const Loop &loop)
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

bool LoopInterchangePass::isSimpleTwoBlockLoop(const Loop &loop)
{
    return loop.header && loop.blocks.size() == 2;
}

bool LoopInterchangePass::getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound,
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

PhiInst *LoopInterchangePass::findPhiAtHeader(BasicBlock *header, Value *iv)
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

bool LoopInterchangePass::parse2DAccess(Value *ptr, Value *&rowIdx, Value *&colIdx,
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

bool LoopInterchangePass::storeUsesSum(StoreInst *store, PhiInst *sumPhi, BasicBlock *kHeader)
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

bool LoopInterchangePass::matchDotProductNest(Function *func, const vector<Loop> &loops,
                                              NestPattern &pat)
{
    (void)func;
    for (const auto &kLoop : loops)
    {
        if (!isSimpleTwoBlockLoop(kLoop))
            continue;

        const Loop *jLoop = findParentLoop(kLoop, loops);
        if (!jLoop)
            continue;
        const Loop *iLoop = findParentLoop(*jLoop, loops);
        if (!iLoop)
            continue;

        BasicBlock *kHeader = kLoop.header;
        BasicBlock *kBody = getLoopLatch(kLoop);
        BasicBlock *kExit = getLoopExit(kLoop);
        if (!kBody || !kExit || kBody == kHeader)
            continue;

        Value *kIV = nullptr;
        Value *bound = nullptr;
        ICmpInst *kCmp = nullptr;
        if (!getHeaderBoundCmp(kHeader, kIV, bound, kCmp))
            continue;
        pat.kPhi = findPhiAtHeader(kHeader, kIV);
        if (!pat.kPhi)
            continue;

        vector<PhiInst *> kHeaderPhis;
        for (auto &instPtr : kHeader->getInstructions())
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                kHeaderPhis.push_back(phi);
        if (kHeaderPhis.size() != 2)
            continue;

        pat.sumPhi = (kHeaderPhis[0] == pat.kPhi) ? kHeaderPhis[1] : kHeaderPhis[0];
        if (pat.sumPhi == pat.kPhi)
            continue;

        Value *sumInit = nullptr;
        for (unsigned i = 0; i < pat.sumPhi->getNumIncomingValues(); ++i)
        {
            if (!kLoop.containsBlock(pat.sumPhi->getIncomingBlock(i)))
            {
                sumInit = pat.sumPhi->getIncomingValue(i);
                break;
            }
        }
        auto *sumInitC = dynamic_cast<ConstantInt *>(stripCopy(sumInit));
        if (!sumInitC || sumInitC->Value != 0)
            continue;

        BinaryOperator *mulOp = nullptr;
        for (auto &instPtr : kBody->getInstructions())
        {
            if (auto *add = dynamic_cast<BinaryOperator *>(instPtr.get()))
            {
                if (add->getOpcode() != Opcode::Add)
                    continue;
                if (add->getLHS() != pat.sumPhi && add->getRHS() != pat.sumPhi)
                    continue;
                Value *other = add->getLHS() == pat.sumPhi ? add->getRHS() : add->getLHS();
                mulOp = dynamic_cast<BinaryOperator *>(other);
                if (mulOp && mulOp->getOpcode() == Opcode::Mul)
                    break;
            }
        }
        if (!mulOp)
            continue;

        pat.cLoad = dynamic_cast<LoadInst *>(mulOp->getLHS());
        pat.aLoad = dynamic_cast<LoadInst *>(mulOp->getRHS());
        if (!pat.cLoad || !pat.aLoad)
        {
            pat.cLoad = dynamic_cast<LoadInst *>(mulOp->getRHS());
            pat.aLoad = dynamic_cast<LoadInst *>(mulOp->getLHS());
        }
        if (!pat.cLoad || !pat.aLoad)
            continue;

        Value *cRow = nullptr, *cCol = nullptr, *aRow = nullptr, *aCol = nullptr;
        Value *cArray = nullptr, *aArray = nullptr;
        if (!parse2DAccess(pat.cLoad->getPointer(), cRow, cCol, cArray))
            continue;
        if (!parse2DAccess(pat.aLoad->getPointer(), aRow, aCol, aArray))
            continue;
        if (!sameValue(cCol, pat.kPhi) || !sameValue(aRow, pat.kPhi))
            continue;

        BasicBlock *jHeader = jLoop->header;
        Value *jIV = nullptr;
        Value *jBound = nullptr;
        ICmpInst *jCmp = nullptr;
        if (!getHeaderBoundCmp(jHeader, jIV, jBound, jCmp))
            continue;
        if (!sameValue(jBound, bound))
            continue;
        if (!sameValue(aCol, jIV))
            continue;

        pat.aStore = nullptr;
        for (auto &instPtr : kExit->getInstructions())
        {
            if (auto *st = dynamic_cast<StoreInst *>(instPtr.get()))
            {
                pat.aStore = st;
                break;
            }
        }
        if (!pat.aStore || !storeUsesSum(pat.aStore, pat.sumPhi, kHeader))
            continue;

        Value *stRow = nullptr, *stCol = nullptr, *stArray = nullptr;
        if (!parse2DAccess(pat.aStore->getPointer(), stRow, stCol, stArray))
            continue;
        if (!sameValue(stCol, jIV))
            continue;
        if (stArray != aArray)
            continue;

        Value *iIV = nullptr;
        Value *iBound = nullptr;
        ICmpInst *iCmp = nullptr;
        if (!getHeaderBoundCmp(iLoop->header, iIV, iBound, iCmp))
            continue;
        if (!sameValue(cRow, iIV) || !sameValue(stRow, iIV))
            continue;

        auto *jBr = dynamic_cast<BranchInst *>(jHeader->getTerminator());
        if (!jBr || !jBr->isConditional())
            continue;

        BasicBlock *jExit = getLoopExit(*jLoop);
        if (!jExit)
            continue;

        BasicBlock *iBody = nullptr;
        auto *iBr = dynamic_cast<BranchInst *>(iLoop->header->getTerminator());
        if (iBr && iBr->isConditional())
            iBody = iBr->getTrueBlock();
        if (!iBody || !iLoop->containsBlock(iBody))
            continue;

        BinaryOperator *kInc = nullptr;
        for (auto &instPtr : kBody->getInstructions())
        {
            if (auto *add = dynamic_cast<BinaryOperator *>(instPtr.get()))
            {
                if (add->getOpcode() != Opcode::Add)
                    continue;
                if (sameValue(add->getLHS(), pat.kPhi) || sameValue(add->getRHS(), pat.kPhi))
                {
                    Value *other = sameValue(add->getLHS(), pat.kPhi) ? add->getRHS()
                                                                      : add->getLHS();
                    if (auto *step = dynamic_cast<ConstantInt *>(stripCopy(other)))
                    {
                        if (step->Value == 1)
                        {
                            kInc = add;
                            break;
                        }
                    }
                }
            }
        }
        if (!kInc)
            continue;

        pat.iLoop = iLoop;
        pat.jLoop = jLoop;
        pat.kLoop = &kLoop;
        pat.iIV = iIV;
        pat.jIV = jIV;
        pat.bound = bound;
        pat.cArray = cArray;
        pat.aArray = aArray;
        pat.jHeader = jHeader;
        pat.kHeader = kHeader;
        pat.kBody = kBody;
        pat.kExit = kExit;
        pat.jExit = jExit;
        pat.iBody = iBody;
        return true;
    }
    return false;
}

AllocaInst *LoopInterchangePass::getOrCreateAccBuffer(Function *func)
{
    BasicBlock *entry = func->getEntryBlock();
    for (auto &instPtr : entry->getInstructions())
    {
        if (auto *alloca = dynamic_cast<AllocaInst *>(instPtr.get()))
        {
            if (alloca->getName() == "licc_acc")
                return alloca;
        }
    }
    auto *arrTy = ArrayType::getInstance(IntegerType::getInstance(), kAccCapacity);
    auto *acc = new AllocaInst(arrTy, "licc_acc");
    entry->insert(own(acc), 0);
    return acc;
}

bool LoopInterchangePass::applyInterchange(Function *func, NestPattern &pat)
{
    AllocaInst *acc = getOrCreateAccBuffer(func);
    auto *zero = ci(0);
    auto *one = ci(1);

    BasicBlock *accInitHeader = func->addBasicBlock("licc_acc_init");
    BasicBlock *accInitBody = func->addBasicBlock("licc_acc_init_body");
    BasicBlock *accInitExit = func->addBasicBlock("licc_acc_init_exit");
    BasicBlock *kHeader = func->addBasicBlock("licc_k_header");
    BasicBlock *kBody = func->addBasicBlock("licc_k_body");
    BasicBlock *jHeader = func->addBasicBlock("licc_j_header");
    BasicBlock *jBody = func->addBasicBlock("licc_j_body");
    BasicBlock *kLatch = func->addBasicBlock("licc_k_latch");
    BasicBlock *kExit = func->addBasicBlock("licc_k_exit");
    BasicBlock *storeHeader = func->addBasicBlock("licc_store_header");
    BasicBlock *storeBody = func->addBasicBlock("licc_store_body");
    BasicBlock *storeExit = func->addBasicBlock("licc_store_exit");

    replaceTerminator(pat.iBody, own(new BranchInst(accInitHeader)));
    wireEdge(pat.iBody, accInitHeader);

    auto *jInitPhi = new PhiInst(IntegerType::getInstance(), "licc_j_init");
    jInitPhi->addIncoming(zero, pat.iBody);

    accInitHeader->addInstruction(own(jInitPhi));
    auto *accInitCmp = new ICmpInst(ICmpInst::ICMP_SLT, jInitPhi, pat.bound, "licc_acc_init_cmp");
    accInitHeader->addInstruction(own(accInitCmp));
    accInitHeader->addInstruction(own(new BranchInst(accInitCmp, accInitBody, accInitExit)));

    auto *accElemGep = new GetElementPtrInst(acc, {zero, jInitPhi}, "licc_acc_gep_init");
    accInitBody->addInstruction(own(accElemGep));
    accInitBody->addInstruction(own(new StoreInst(zero, accElemGep)));
    auto *jInitInc = new BinaryOperator(Opcode::Add, jInitPhi, one, "licc_j_init_inc");
    accInitBody->addInstruction(own(jInitInc));
    jInitPhi->addIncoming(jInitInc, accInitBody);
    accInitBody->addInstruction(own(new BranchInst(accInitHeader)));
    wireEdge(accInitHeader, accInitBody);
    wireEdge(accInitBody, accInitHeader);
    wireEdge(accInitHeader, accInitExit);

    auto *kPhi = new PhiInst(IntegerType::getInstance(), "licc_k");
    kPhi->addIncoming(zero, accInitExit);

    kHeader->addInstruction(own(kPhi));
    auto *kCmp = new ICmpInst(ICmpInst::ICMP_SLT, kPhi, pat.bound, "licc_k_cmp");
    kHeader->addInstruction(own(kCmp));
    kHeader->addInstruction(own(new BranchInst(kCmp, kBody, kExit)));
    wireEdge(accInitExit, kHeader);
    wireEdge(kHeader, kBody);
    wireEdge(kHeader, kExit);

    auto *cElemGep = new GetElementPtrInst(pat.cArray, {pat.iIV, kPhi}, "licc_c_gep");
    kBody->addInstruction(own(cElemGep));
    auto *cVal = new LoadInst(cElemGep, "licc_c_val");
    kBody->addInstruction(own(cVal));
    kBody->addInstruction(own(new BranchInst(jHeader)));
    wireEdge(kBody, jHeader);

    auto *jPhi = new PhiInst(IntegerType::getInstance(), "licc_j");
    jPhi->addIncoming(zero, kBody);

    jHeader->addInstruction(own(jPhi));
    auto *jCmp = new ICmpInst(ICmpInst::ICMP_SLT, jPhi, pat.bound, "licc_j_cmp");
    jHeader->addInstruction(own(jCmp));
    jHeader->addInstruction(own(new BranchInst(jCmp, jBody, kLatch)));
    wireEdge(jHeader, jBody);
    wireEdge(jHeader, kLatch);

    auto *accGep = new GetElementPtrInst(acc, {zero, jPhi}, "licc_acc_gep");
    jBody->addInstruction(own(accGep));
    auto *accLoad = new LoadInst(accGep, "licc_acc_load");
    jBody->addInstruction(own(accLoad));

    auto *aElemGep = new GetElementPtrInst(pat.aArray, {kPhi, jPhi}, "licc_a_gep");
    jBody->addInstruction(own(aElemGep));
    auto *aVal = new LoadInst(aElemGep, "licc_a_val");
    jBody->addInstruction(own(aVal));

    auto *prod = new BinaryOperator(Opcode::Mul, cVal, aVal, "licc_prod");
    jBody->addInstruction(own(prod));
    auto *accAdd = new BinaryOperator(Opcode::Add, accLoad, prod, "licc_acc_add");
    jBody->addInstruction(own(accAdd));
    jBody->addInstruction(own(new StoreInst(accAdd, accGep)));

    auto *jInc = new BinaryOperator(Opcode::Add, jPhi, one, "licc_j_inc");
    jBody->addInstruction(own(jInc));
    jPhi->addIncoming(jInc, jBody);
    jBody->addInstruction(own(new BranchInst(jHeader)));
    wireEdge(jBody, jHeader);

    auto *kInc = new BinaryOperator(Opcode::Add, kPhi, one, "licc_k_inc");
    kLatch->addInstruction(own(kInc));
    kPhi->addIncoming(kInc, kLatch);
    kLatch->addInstruction(own(new BranchInst(kHeader)));
    wireEdge(kLatch, kHeader);

    auto *jStorePhi = new PhiInst(IntegerType::getInstance(), "licc_j_store");
    jStorePhi->addIncoming(zero, kExit);

    storeHeader->addInstruction(own(jStorePhi));
    auto *storeCmp = new ICmpInst(ICmpInst::ICMP_SLT, jStorePhi, pat.bound, "licc_store_cmp");
    storeHeader->addInstruction(own(storeCmp));
    storeHeader->addInstruction(own(new BranchInst(storeCmp, storeBody, storeExit)));
    wireEdge(kExit, storeHeader);
    wireEdge(storeHeader, storeBody);
    wireEdge(storeHeader, storeExit);

    auto *accReadGep = new GetElementPtrInst(acc, {zero, jStorePhi}, "licc_acc_read_gep");
    storeBody->addInstruction(own(accReadGep));
    auto *accRead = new LoadInst(accReadGep, "licc_acc_read");
    storeBody->addInstruction(own(accRead));
    auto *aOutGep = new GetElementPtrInst(pat.aArray, {pat.iIV, jStorePhi}, "licc_a_out_gep");
    storeBody->addInstruction(own(aOutGep));
    storeBody->addInstruction(own(new StoreInst(accRead, aOutGep)));
    auto *jStoreInc = new BinaryOperator(Opcode::Add, jStorePhi, one, "licc_j_store_inc");
    storeBody->addInstruction(own(jStoreInc));
    jStorePhi->addIncoming(jStoreInc, storeBody);
    storeBody->addInstruction(own(new BranchInst(storeHeader)));
    wireEdge(storeBody, storeHeader);

    storeExit->addInstruction(own(new BranchInst(pat.jExit)));
    wireEdge(storeExit, pat.jExit);

    vector<BasicBlock *> toRemove;
    for (BasicBlock *bb : pat.jLoop->blocks)
        toRemove.push_back(bb);
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
        for (auto &instPtr : pat.jExit->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
            {
                for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
                {
                    if (phi->getIncomingBlock(i) == bb && bb == pat.kExit)
                        phi->replaceIncomingBasicBlock(bb, storeExit);
                }
            }
        }
        bb->removeSelfBasicBlock();
    }

    for (auto &instPtr : pat.iLoop->header->getInstructions())
    {
        if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
        {
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                if (phi->getIncomingBlock(i) == pat.jExit)
                    phi->setIncomingBlock(i, storeExit);
            }
        }
    }
    wireEdge(storeExit, pat.jExit);

    if (verbose)
    {
        debugInfo << "LoopInterchange: interchanged dot-product nest at "
                  << pat.kHeader->getName() << " in " << func->getName() << "\n";
    }
    return true;
}

bool LoopInterchangePass::runOnFunction(Function *func)
{
    func->setLoops(ControlFlowAnalysis::findLoops(func));
    NestPattern pat;
    if (!matchDotProductNest(func, func->getLoops(), pat))
        return false;
    bool changed = applyInterchange(func, pat);
    if (changed)
        func->setLoops(ControlFlowAnalysis::findLoops(func));
    return changed;
}
