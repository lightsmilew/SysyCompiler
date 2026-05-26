#include "LoopWavefrontPass.h"
#include <algorithm>
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

    Value *stripCopyLocal(Value *v)
    {
        while (auto *cpy = dynamic_cast<CopyInst *>(v))
            v = cpy->getSource();
        return v;
    }

    bool sameValueLocal(Value *a, Value *b)
    {
        return stripCopyLocal(a) == stripCopyLocal(b);
    }

    void addMaxMinInsts(BasicBlock *bb, Value *a, Value *b, const string &name, bool isMax, Value *&out)
    {
        auto *cmp = new ICmpInst(ICmpInst::ICMP_SLT, a, b, name + "_cmp");
        bb->addInstruction(own(cmp));
        auto *sel = isMax ? new SelectInst(cmp, b, a, name) : new SelectInst(cmp, a, b, name);
        bb->addInstruction(own(sel));
        out = sel;
    }

    void cloneInstsToBlock(BasicBlock *bb, const vector<Instruction *> &src,
                           unordered_map<Value *, Value *> &vmap,
                           Value *iIV, Value *newI, Value *jIV, Value *newJ, Value *kIV, Value *newK)
    {
        for (Instruction *inst : src)
        {
            Instruction *cloned = inst->clone();
            vmap[inst] = cloned;
            for (size_t op = 0; op < cloned->getOperands().size(); ++op)
            {
                Value *operand = cloned->getOperandByIndex(op);
                if (vmap.count(operand))
                    cloned->setOperandByIndex(op, vmap[operand]);
                else if (iIV && newI && sameValueLocal(operand, iIV))
                    cloned->setOperandByIndex(op, newI);
                else if (jIV && newJ && sameValueLocal(operand, jIV))
                    cloned->setOperandByIndex(op, newJ);
                else if (kIV && newK && sameValueLocal(operand, kIV))
                    cloned->setOperandByIndex(op, newK);
            }
            bb->addInstruction(own(cloned));
        }
    }
}

Value *LoopWavefrontPass::stripCopy(Value *v)
{
    while (auto *cpy = dynamic_cast<CopyInst *>(v))
        v = cpy->getSource();
    return v;
}

bool LoopWavefrontPass::sameValue(Value *a, Value *b)
{
    return stripCopy(a) == stripCopy(b);
}

const Loop *LoopWavefrontPass::findParentLoop(const Loop &inner, const vector<Loop> &loops)
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

BasicBlock *LoopWavefrontPass::getLoopLatch(const Loop &loop)
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

BasicBlock *LoopWavefrontPass::getLoopExit(const Loop &loop)
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

bool LoopWavefrontPass::isSimpleTwoBlockLoop(const Loop &loop)
{
    return loop.header && loop.blocks.size() == 2;
}

bool LoopWavefrontPass::getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound,
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

PhiInst *LoopWavefrontPass::findPhiAtHeader(BasicBlock *header, Value *iv)
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

bool LoopWavefrontPass::isLoopIncrementByOne(BasicBlock *latch, Value *iv, Value *&incResult)
{
    incResult = nullptr;
    if (!latch || !iv)
        return false;
    for (auto &instPtr : latch->getInstructions())
    {
        auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get());
        if (!bin || bin->getOpcode() != Opcode::Add)
            continue;
        auto *c = dynamic_cast<ConstantInt *>(bin->getRHS());
        if (!c || c->Value != 1)
            continue;
        if (sameValue(bin->getLHS(), iv))
        {
            incResult = bin;
            return true;
        }
    }
    return false;
}

BasicBlock *LoopWavefrontPass::getLoopBodyBlock(const Loop &loop)
{
    auto *br = dynamic_cast<BranchInst *>(loop.header->getTerminator());
    if (!br || !br->isConditional())
        return nullptr;
    BasicBlock *candidates[2] = {br->getTrueBlock(), br->getFalseBlock()};
    for (BasicBlock *bb : candidates)
    {
        if (bb && bb != loop.header && loop.containsBlock(bb))
            return bb;
    }
    return nullptr;
}

GlobalVariable *LoopWavefrontPass::getRootGlobal(Value *ptr)
{
    ptr = stripCopy(ptr);
    while (ptr)
    {
        if (auto *gv = dynamic_cast<GlobalVariable *>(ptr))
            return gv;
        if (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr))
            ptr = gep->getPointerOperand();
        else if (auto *cast = dynamic_cast<CastInst *>(ptr))
            ptr = cast->getOperand();
        else if (auto *add = dynamic_cast<BinaryOperator *>(ptr))
        {
            if (add->getOpcode() != Opcode::Addd)
                break;
            Value *base = nullptr;
            if (add->getLHS() && add->getLHS()->getType()->isPointerTy())
                base = add->getLHS();
            else if (add->getRHS() && add->getRHS()->getType()->isPointerTy())
                base = add->getRHS();
            ptr = base;
        }
        else
            break;
    }
    return nullptr;
}

bool LoopWavefrontPass::isStencilKBody(BasicBlock *body, Value *kIV, GlobalVariable *&arrayOut)
{
    arrayOut = nullptr;
    if (!body || !kIV)
        return false;

    StoreInst *stencilStore = nullptr;
    int loadCount = 0;
    bool hasSDiv = false;

    for (auto &instPtr : body->getInstructions())
    {
        if (auto *store = dynamic_cast<StoreInst *>(instPtr.get()))
        {
            if (store->Op != Opcode::Store && store->Op != Opcode::Stored)
                continue;
            GlobalVariable *gv = getRootGlobal(store->getOriginalPointer());
            if (!gv)
                continue;
            auto *val = store->getValueToStore();
            auto *div = dynamic_cast<BinaryOperator *>(val);
            if (!div || div->getOpcode() != Opcode::SDiv)
                continue;
            stencilStore = store;
            arrayOut = gv;
            hasSDiv = true;
        }
        if (auto *load = dynamic_cast<LoadInst *>(instPtr.get()))
        {
            GlobalVariable *gv = getRootGlobal(load->getOriginalPointer());
            if (gv)
                ++loadCount;
        }
    }

    if (!stencilStore || !hasSDiv || loadCount < 6)
        return false;

    (void)kIV;
    return true;
}

void LoopWavefrontPass::collectCloneableInsts(BasicBlock *bb, Value *iv,
                                              vector<Instruction *> &out)
{
    out.clear();
    if (!bb)
        return;
    for (auto &instPtr : bb->getInstructions())
    {
        Instruction *inst = instPtr.get();
        if (inst->Op == Opcode::Br)
            continue;
        if (auto *bin = dynamic_cast<BinaryOperator *>(inst))
        {
            if (bin->getOpcode() == Opcode::Add)
            {
                auto *c = dynamic_cast<ConstantInt *>(bin->getRHS());
                if (c && c->Value == 1 && sameValue(bin->getLHS(), iv))
                    continue;
            }
        }
        if (auto *cpy = dynamic_cast<CopyInst *>(inst))
        {
            if (sameValue(cpy->getSource(), iv) || sameValue(cpy->getDest(), iv))
                continue;
        }
        out.push_back(inst);
    }
}

bool LoopWavefrontPass::matchStencilNest(Function *func, const vector<Loop> &loops,
                                       WavefrontPattern &pat)
{
    (void)func;
    for (const auto &kLoop : loops)
    {
        if (!isSimpleTwoBlockLoop(kLoop))
            continue;
        if (kLoop.header->getName().find("_unroll") != string::npos)
            continue;

        const Loop *jLoop = findParentLoop(kLoop, loops);
        if (!jLoop)
            continue;
        const Loop *iLoop = findParentLoop(*jLoop, loops);
        if (!iLoop)
            continue;

        Value *kIV = nullptr;
        Value *kBound = nullptr;
        ICmpInst *kCmp = nullptr;
        if (!getHeaderBoundCmp(kLoop.header, kIV, kBound, kCmp))
            continue;

        Value *jIV = nullptr;
        Value *jBound = nullptr;
        ICmpInst *jCmp = nullptr;
        if (!getHeaderBoundCmp(jLoop->header, jIV, jBound, jCmp))
            continue;

        Value *iIV = nullptr;
        Value *iBound = nullptr;
        ICmpInst *iCmp = nullptr;
        if (!getHeaderBoundCmp(iLoop->header, iIV, iBound, iCmp))
            continue;

        if (!sameValue(kBound, jBound) || !sameValue(kBound, iBound))
            continue;

        BasicBlock *kLatch = getLoopLatch(kLoop);
        BasicBlock *kExitBB = getLoopExit(kLoop);
        BasicBlock *jExitBB = getLoopExit(*jLoop);
        if (!kLatch || !kExitBB || !jExitBB)
            continue;

        Value *kInc = nullptr;
        Value *jInc = nullptr;
        Value *iInc = nullptr;
        if (!isLoopIncrementByOne(kLatch, kIV, kInc))
            continue;
        if (!isLoopIncrementByOne(kExitBB, jIV, jInc))
            continue;
        if (!isLoopIncrementByOne(jExitBB, iIV, iInc))
            continue;

        BasicBlock *kBody = getLoopBodyBlock(kLoop);
        if (!kBody)
            continue;

        GlobalVariable *stencilArr = nullptr;
        if (!isStencilKBody(kBody, kIV, stencilArr))
            continue;

        BasicBlock *jBody = getLoopBodyBlock(*jLoop);
        BasicBlock *iBody = getLoopBodyBlock(*iLoop);
        if (!jBody || !iBody)
            continue;

        BasicBlock *iExit = getLoopExit(*iLoop);
        if (!iExit)
            continue;

        BasicBlock *iPre = nullptr;
        for (auto *pred : iLoop->header->getPredecessors())
        {
            if (!iLoop->containsBlock(pred))
            {
                iPre = pred;
                break;
            }
        }
        if (!iPre)
            continue;

        pat.iLoop = iLoop;
        pat.jLoop = jLoop;
        pat.kLoop = &kLoop;
        pat.iIV = iIV;
        pat.jIV = jIV;
        pat.kIV = kIV;
        pat.bound = iBound;
        pat.stencilArray = stencilArr;
        pat.iHeader = iLoop->header;
        pat.iBody = iBody;
        pat.iExit = iExit;
        pat.jHeader = jLoop->header;
        pat.jBody = jBody;
        pat.jExit = kExitBB;
        pat.kHeader = kLoop.header;
        pat.kBody = kBody;
        pat.iPreheader = iPre;

        collectCloneableInsts(iBody, iIV, pat.iSetupInsts);
        collectCloneableInsts(jBody, jIV, pat.jSetupInsts);
        collectCloneableInsts(kBody, kIV, pat.kStencilInsts);

        if (pat.kStencilInsts.empty())
            return false;

        return true;
    }
    return false;
}

bool LoopWavefrontPass::applyWavefront(Function *func, WavefrontPattern &pat)
{
    (void)func;
    auto *one = ci(1);
    auto *two = ci(2);
    auto *three = ci(3);

    BasicBlock *tHeader = func->addBasicBlock("wf_t_header");
    BasicBlock *tBody = func->addBasicBlock("wf_t_body");
    BasicBlock *tLatch = func->addBasicBlock("wf_t_latch");
    BasicBlock *iHeader = func->addBasicBlock("wf_i_header");
    BasicBlock *wfIBody = func->addBasicBlock("wf_i_body");
    BasicBlock *iLatch = func->addBasicBlock("wf_i_latch");
    BasicBlock *jHeader = func->addBasicBlock("wf_j_header");
    BasicBlock *jBody = func->addBasicBlock("wf_j_body");
    BasicBlock *jLatch = func->addBasicBlock("wf_j_latch");

    replaceTerminator(pat.iPreheader, own(new BranchInst(tHeader)));
    wireEdge(pat.iPreheader, tHeader);

    auto *tPhi = new PhiInst(IntegerType::getInstance(), "wf_t");
    tPhi->addIncoming(three, pat.iPreheader);
    tHeader->addInstruction(own(tPhi));

    auto *lim = new BinaryOperator(Opcode::Sub, pat.bound, one, "wf_lim");
    tHeader->addInstruction(own(lim));
    auto *twoLim = new BinaryOperator(Opcode::Mul, two, lim, "wf_two_lim");
    tHeader->addInstruction(own(twoLim));
    auto *threeLim = new BinaryOperator(Opcode::Mul, three, lim, "wf_three_lim");
    tHeader->addInstruction(own(threeLim));
    auto *tEnd = new BinaryOperator(Opcode::Add, threeLim, one, "wf_t_end");
    tHeader->addInstruction(own(tEnd));
    auto *tCmp = new ICmpInst(ICmpInst::ICMP_SLT, tPhi, tEnd, "wf_t_cmp");
    tHeader->addInstruction(own(tCmp));

    BasicBlock *tDone = func->addBasicBlock("wf_t_done");
    tHeader->addInstruction(own(new BranchInst(tCmp, tBody, tDone)));
    wireEdge(tHeader, tBody);
    wireEdge(tHeader, tDone);

    auto *exitI = new CopyInst(pat.bound, "wf_exit_i");
    auto *exitJ = new CopyInst(pat.bound, "wf_exit_j");
    tDone->addInstruction(own(exitI));
    tDone->addInstruction(own(exitJ));
    tDone->addInstruction(own(new BranchInst(pat.iExit)));
    wireEdge(tDone, pat.iExit);

    for (auto &instPtr : pat.iExit->getInstructions())
    {
        Instruction *inst = instPtr.get();
        for (size_t op = 0; op < inst->getOperands().size(); ++op)
        {
            Value *operand = inst->getOperandByIndex(op);
            if (sameValue(operand, pat.iIV))
                inst->setOperandByIndex(op, exitI);
            else if (sameValue(operand, pat.jIV))
                inst->setOperandByIndex(op, exitJ);
        }
    }

    auto *tSubTwoLim = new BinaryOperator(Opcode::Sub, tPhi, twoLim, "wf_t_sub_2lim");
    tBody->addInstruction(own(tSubTwoLim));
    Value *iLow = nullptr;
    addMaxMinInsts(tBody, one, tSubTwoLim, "wf_i_low", true, iLow);

    auto *tSub2 = new BinaryOperator(Opcode::Sub, tPhi, two, "wf_t_sub_2");
    tBody->addInstruction(own(tSub2));
    Value *iHigh = nullptr;
    addMaxMinInsts(tBody, lim, tSub2, "wf_i_high", false, iHigh);
    auto *iHighPlus1 = new BinaryOperator(Opcode::Add, iHigh, one, "wf_i_high_p1");
    tBody->addInstruction(own(iHighPlus1));
    tBody->addInstruction(own(new BranchInst(iHeader)));
    wireEdge(tBody, iHeader);

    auto *iPhi = new PhiInst(IntegerType::getInstance(), "wf_i");
    iPhi->addIncoming(iLow, tBody);
    iHeader->addInstruction(own(iPhi));
    auto *iCmp = new ICmpInst(ICmpInst::ICMP_SLT, iPhi, iHighPlus1, "wf_i_cmp");
    iHeader->addInstruction(own(iCmp));
    iHeader->addInstruction(own(new BranchInst(iCmp, wfIBody, tLatch)));
    wireEdge(iHeader, wfIBody);
    wireEdge(iHeader, tLatch);

    unordered_map<Value *, Value *> vmap;
    vmap[tPhi] = tPhi;
    vmap[lim] = lim;
    cloneInstsToBlock(wfIBody, pat.iSetupInsts, vmap, pat.iIV, iPhi, pat.jIV, nullptr, pat.kIV, nullptr);

    auto *tSubI = new BinaryOperator(Opcode::Sub, tPhi, iPhi, "wf_t_sub_i");
    wfIBody->addInstruction(own(tSubI));
    auto *tSubILim = new BinaryOperator(Opcode::Sub, tSubI, lim, "wf_t_sub_i_lim");
    wfIBody->addInstruction(own(tSubILim));
    Value *jLow = nullptr;
    addMaxMinInsts(wfIBody, one, tSubILim, "wf_j_low", true, jLow);

    auto *tSubI1 = new BinaryOperator(Opcode::Sub, tSubI, one, "wf_t_sub_i_1");
    wfIBody->addInstruction(own(tSubI1));
    Value *jHigh = nullptr;
    addMaxMinInsts(wfIBody, lim, tSubI1, "wf_j_high", false, jHigh);
    auto *jHighPlus1 = new BinaryOperator(Opcode::Add, jHigh, one, "wf_j_high_p1");
    wfIBody->addInstruction(own(jHighPlus1));
    wfIBody->addInstruction(own(new BranchInst(jHeader)));
    wireEdge(wfIBody, jHeader);

    auto *jPhi = new PhiInst(IntegerType::getInstance(), "wf_j");
    jPhi->addIncoming(jLow, wfIBody);
    jHeader->addInstruction(own(jPhi));
    auto *jCmp = new ICmpInst(ICmpInst::ICMP_SLT, jPhi, jHighPlus1, "wf_j_cmp");
    jHeader->addInstruction(own(jCmp));
    jHeader->addInstruction(own(new BranchInst(jCmp, jBody, iLatch)));
    wireEdge(jHeader, jBody);
    wireEdge(jHeader, iLatch);

    auto *kVal = new BinaryOperator(Opcode::Sub, tSubI, jPhi, "wf_k");
    jBody->addInstruction(own(kVal));
    cloneInstsToBlock(jBody, pat.jSetupInsts, vmap, pat.iIV, iPhi, pat.jIV, jPhi, pat.kIV, kVal);
    cloneInstsToBlock(jBody, pat.kStencilInsts, vmap, pat.iIV, iPhi, pat.jIV, jPhi, pat.kIV, kVal);
    jBody->addInstruction(own(new BranchInst(jLatch)));
    wireEdge(jBody, jLatch);

    auto *jInc = new BinaryOperator(Opcode::Add, jPhi, one, "wf_j_inc");
    jLatch->addInstruction(own(jInc));
    jPhi->addIncoming(jInc, jLatch);
    jLatch->addInstruction(own(new BranchInst(jHeader)));
    wireEdge(jLatch, jHeader);

    auto *iInc = new BinaryOperator(Opcode::Add, iPhi, one, "wf_i_inc");
    iLatch->addInstruction(own(iInc));
    iPhi->addIncoming(iInc, iLatch);
    iLatch->addInstruction(own(new BranchInst(iHeader)));
    wireEdge(iLatch, iHeader);

    auto *tInc = new BinaryOperator(Opcode::Add, tPhi, one, "wf_t_inc");
    tLatch->addInstruction(own(tInc));
    tPhi->addIncoming(tInc, tLatch);
    tLatch->addInstruction(own(new BranchInst(tHeader)));
    wireEdge(jLatch, jHeader);
    wireEdge(tLatch, tHeader);

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
    if (find(toRemove.begin(), toRemove.end(), pat.jExit) == toRemove.end())
        toRemove.push_back(pat.jExit);

    for (BasicBlock *bb : toRemove)
    {
        if (!bb)
            continue;
        bb->removeSelfBasicBlock();
    }

    if (verbose)
    {
        debugInfo << "LoopWavefront: transformed stencil nest at " << pat.kHeader->getName()
                  << " in " << func->getName() << " (t=i+j+k wavefront)\n";
    }
    return true;
}

bool LoopWavefrontPass::runOnFunction(Function *func)
{
    func->setLoops(ControlFlowAnalysis::findLoops(func));
    WavefrontPattern pat;
    if (!matchStencilNest(func, func->getLoops(), pat))
        return false;
    bool changed = applyWavefront(func, pat);
    if (changed)
        func->setLoops(ControlFlowAnalysis::findLoops(func));
    return changed;
}
