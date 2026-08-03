#include "LoopInterchangePass.h"
#include <algorithm>
using namespace std;
using namespace optimization;
using namespace matrixStructure;

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

    static void removeOldNest(Function *func, MatMulDotProductNest &pat, BasicBlock *continueBB)
    {
        (void)func;
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
                            phi->replaceIncomingBasicBlock(bb, continueBB);
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
                        phi->setIncomingBlock(i, continueBB);
                }
            }
        }
        wireEdge(continueBB, pat.jExit);
    }

    /// Emit j-body: load accum, load rhs, prod, optional parity scale, add+store.
    static void emitInnerJBody(BasicBlock *jBody, Value *accumPtr, Value *lhsVal, Value *rhsArray,
                               Value *kPhi, Value *jPhi, bool hasParity, Value *parityIkVal,
                               Value *parityKjArray, Value *zero, Value *one)
    {
        auto *accLoad = new LoadInst(accumPtr, "licc_acc_load");
        jBody->addInstruction(own(accLoad));

        auto *rhsGep = new GetElementPtrInst(rhsArray, {kPhi, jPhi}, "licc_rhs_gep");
        jBody->addInstruction(own(rhsGep));
        auto *rhsVal = new LoadInst(rhsGep, "licc_rhs_val");
        jBody->addInstruction(own(rhsVal));

        auto *prod = new BinaryOperator(Opcode::Mul, lhsVal, rhsVal, "licc_prod");
        jBody->addInstruction(own(prod));

        Value *addend = prod;
        if (hasParity)
        {
            auto *pkjGep = new GetElementPtrInst(parityKjArray, {kPhi, jPhi}, "licc_pkj_gep");
            jBody->addInstruction(own(pkjGep));
            auto *pkjLoad = new LoadInst(pkjGep, "licc_pkj_val");
            jBody->addInstruction(own(pkjLoad));
            auto *pa = new BinaryOperator(Opcode::And, parityIkVal, one, "licc_parity_a");
            jBody->addInstruction(own(pa));
            auto *pb = new BinaryOperator(Opcode::And, pkjLoad, one, "licc_parity_b");
            jBody->addInstruction(own(pb));
            auto *pand = new BinaryOperator(Opcode::And, pa, pb, "licc_parity_and");
            jBody->addInstruction(own(pand));
            auto *ok = new ICmpInst(ICmpInst::ICMP_EQ, pand, zero, "licc_parity_ok");
            jBody->addInstruction(own(ok));
            auto *scaled = new BinaryOperator(Opcode::Mul, ok, prod, "licc_scaled");
            jBody->addInstruction(own(scaled));
            addend = scaled;
        }

        auto *accAdd = new BinaryOperator(Opcode::Add, accLoad, addend, "licc_acc_add");
        jBody->addInstruction(own(accAdd));
        jBody->addInstruction(own(new StoreInst(accAdd, accumPtr)));
    }
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

namespace
{
    /// mid == bound/2：sdiv(bound,2)、sra(bound,1) 或有符号除法 lowering
    static bool isHalfOfBound(Value *mid, Value *bound)
    {
        auto *bin = dynamic_cast<BinaryOperator *>(stripCopy(mid));
        if (!bin)
            return false;
        Value *lhs = stripCopy(bin->getLHS());
        Value *rhs = stripCopy(bin->getRHS());
        auto *rhsc = dynamic_cast<ConstantInt *>(rhs);

        auto isSignedHalfLhs = [&](Value *v) -> bool {
            if (sameBound(v, bound))
                return true;
            auto *add = dynamic_cast<BinaryOperator *>(v);
            if (!add || add->getOpcode() != Opcode::Add)
                return false;
            Value *a = stripCopy(add->getLHS());
            Value *b = stripCopy(add->getRHS());
            auto isSltZero = [&](Value *x) -> bool {
                auto *icmp = dynamic_cast<ICmpInst *>(x);
                if (!icmp || icmp->getPredicate() != ICmpInst::ICMP_SLT)
                    return false;
                auto *zero = dynamic_cast<ConstantInt *>(stripCopy(icmp->getRHS()));
                return zero && zero->Value == 0 && sameBound(stripCopy(icmp->getLHS()), bound);
            };
            return (sameBound(a, bound) && isSltZero(b)) || (sameBound(b, bound) && isSltZero(a));
        };

        // Interchange 早于 SRFixed：常见仍是 sdiv bound, 2
        if (bin->getOpcode() == Opcode::SDiv && rhsc && rhsc->Value == 2)
            return isSignedHalfLhs(lhs);
        // 之后可能已是 sra(adj, 1)
        if (bin->getOpcode() == Opcode::Sra && rhsc && rhsc->Value == 1)
            return isSignedHalfLhs(lhs);
        return false;
    }
}

bool LoopInterchangePass::findConstRowTail(Function *func, Value *rhsArray, Value *bound,
                                           ConstRowTail &out)
{
    out = ConstRowTail{};
    if (!func || !rhsArray || !bound)
        return false;

    // 查找：for row = mid..bound; for col; store C to rhs[row][col]
    // mid 为 bound/2（sdiv 或 sra lowering）。
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        for (auto &instPtr : bb->getInstructions())
        {
            auto *st = dynamic_cast<StoreInst *>(instPtr.get());
            if (!st)
                continue;
            auto *cInt = dynamic_cast<ConstantInt *>(stripCopy(st->getValueToStore()));
            if (!cInt)
                continue;
            Value *row = nullptr, *col = nullptr, *base = nullptr;
            if (!parse2DAccess(st->getPointer(), row, col, base))
                continue;
            if (!sameArray(base, rhsArray))
                continue;

            auto *rowPhi = dynamic_cast<PhiInst *>(stripCopy(row));
            if (!rowPhi)
                continue;
            Value *midCand = nullptr;
            bool hasBoundBackedge = false;
            for (unsigned i = 0; i < rowPhi->getNumIncomingValues(); ++i)
            {
                Value *inc = stripCopy(rowPhi->getIncomingValue(i));
                if (auto *add = dynamic_cast<BinaryOperator *>(inc))
                {
                    if (add->getOpcode() == Opcode::Add)
                    {
                        hasBoundBackedge = true;
                        continue;
                    }
                }
                if (isHalfOfBound(inc, bound))
                    midCand = stripCopy(inc);
            }
            if (!midCand || !hasBoundBackedge)
                continue;

            out.mid = midCand;
            out.constVal = cInt->Value;
            out.valid = true;
            return true;
        }
    }
    return false;
}

bool LoopInterchangePass::applyWithScratch(Function *func, MatMulDotProductNest &pat, Value *outBase)
{
    AllocaInst *acc = getOrCreateAccBuffer(func);
    auto *zero = ci(0);
    auto *one = ci(1);

    ConstRowTail tail;
    const bool foundTail =
        !pat.hasParityGuard && findConstRowTail(func, pat.rhsArray, pat.bound, tail);
    // out 与 rhs 别名（in-place）时，仅 i<=mid 仍保持 rhs[mid..) 为原常量，可安全折叠
    const bool aliasesRhs = sameArray(outBase, pat.rhsArray);

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

    BasicBlock *mergeBB = nullptr;
    Value *initAccVal = zero;
    Value *kBound = pat.bound;
    BasicBlock *accInitPred = pat.iBody;

    if (foundTail)
    {
        auto *dispatch = func->addBasicBlock("licc_tail_dispatch");
        auto *tailHeader = func->addBasicBlock("licc_tail_header");
        auto *tailBody = func->addBasicBlock("licc_tail_body");
        auto *tailExit = func->addBasicBlock("licc_tail_exit");
        mergeBB = func->addBasicBlock("licc_tail_merge");
        BasicBlock *nofold = aliasesRhs ? func->addBasicBlock("licc_nofold") : nullptr;

        replaceTerminator(pat.iBody, own(new BranchInst(dispatch)));
        wireEdge(pat.iBody, dispatch);

        if (aliasesRhs)
        {
            auto *safe = new ICmpInst(ICmpInst::ICMP_SLE, pat.iIV, tail.mid, "licc_tail_safe");
            dispatch->addInstruction(own(safe));
            dispatch->addInstruction(own(new BranchInst(safe, tailHeader, nofold)));
            wireEdge(dispatch, tailHeader);
            wireEdge(dispatch, nofold);
        }
        else
        {
            dispatch->addInstruction(own(new BranchInst(tailHeader)));
            wireEdge(dispatch, tailHeader);
        }

        auto *tkPhi = new PhiInst(IntegerType::getInstance(), "licc_tail_k");
        tkPhi->addIncoming(tail.mid, dispatch);
        tailHeader->addInstruction(own(tkPhi));
        auto *tailSumPhi = new PhiInst(IntegerType::getInstance(), "licc_tail_sum");
        tailSumPhi->addIncoming(zero, dispatch);
        tailHeader->addInstruction(own(tailSumPhi));
        auto *tCmp = new ICmpInst(ICmpInst::ICMP_SLT, tkPhi, pat.bound, "licc_tail_cmp");
        tailHeader->addInstruction(own(tCmp));
        tailHeader->addInstruction(own(new BranchInst(tCmp, tailBody, tailExit)));
        wireEdge(tailHeader, tailBody);
        wireEdge(tailHeader, tailExit);

        auto *tGep = new GetElementPtrInst(pat.lhsArray, {pat.iIV, tkPhi}, "licc_tail_gep");
        tailBody->addInstruction(own(tGep));
        auto *tLoad = new LoadInst(tGep, "licc_tail_val");
        tailBody->addInstruction(own(tLoad));
        auto *tAdd = new BinaryOperator(Opcode::Add, tailSumPhi, tLoad, "licc_tail_add");
        tailBody->addInstruction(own(tAdd));
        auto *tInc = new BinaryOperator(Opcode::Add, tkPhi, one, "licc_tail_inc");
        tailBody->addInstruction(own(tInc));
        tkPhi->addIncoming(tInc, tailBody);
        tailSumPhi->addIncoming(tAdd, tailBody);
        tailBody->addInstruction(own(new BranchInst(tailHeader)));
        wireEdge(tailBody, tailHeader);

        auto *cMul = new BinaryOperator(Opcode::Mul, tailSumPhi, ci(tail.constVal), "licc_tail_prod");
        tailExit->addInstruction(own(cMul));
        tailExit->addInstruction(own(new BranchInst(mergeBB)));
        wireEdge(tailExit, mergeBB);

        auto *initPhi = new PhiInst(IntegerType::getInstance(), "licc_acc_init_val");
        auto *kBoundPhi = new PhiInst(IntegerType::getInstance(), "licc_k_bound");
        initPhi->addIncoming(cMul, tailExit);
        kBoundPhi->addIncoming(tail.mid, tailExit);
        if (nofold)
        {
            nofold->addInstruction(own(new BranchInst(mergeBB)));
            wireEdge(nofold, mergeBB);
            initPhi->addIncoming(zero, nofold);
            kBoundPhi->addIncoming(pat.bound, nofold);
        }
        mergeBB->addInstruction(own(initPhi));
        mergeBB->addInstruction(own(kBoundPhi));
        mergeBB->addInstruction(own(new BranchInst(accInitHeader)));
        wireEdge(mergeBB, accInitHeader);

        initAccVal = initPhi;
        kBound = kBoundPhi;
        accInitPred = mergeBB;
    }
    else
    {
        replaceTerminator(pat.iBody, own(new BranchInst(accInitHeader)));
        wireEdge(pat.iBody, accInitHeader);
    }

    auto *jInitPhi = new PhiInst(IntegerType::getInstance(), "licc_j_init");
    jInitPhi->addIncoming(zero, accInitPred);
    accInitHeader->addInstruction(own(jInitPhi));
    auto *accInitCmp = new ICmpInst(ICmpInst::ICMP_SLT, jInitPhi, pat.bound, "licc_acc_init_cmp");
    accInitHeader->addInstruction(own(accInitCmp));
    accInitHeader->addInstruction(own(new BranchInst(accInitCmp, accInitBody, accInitExit)));

    auto *accElemGep = new GetElementPtrInst(acc, {zero, jInitPhi}, "licc_acc_gep_init");
    accInitBody->addInstruction(own(accElemGep));
    accInitBody->addInstruction(own(new StoreInst(initAccVal, accElemGep)));
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
    auto *kCmp = new ICmpInst(ICmpInst::ICMP_SLT, kPhi, kBound, "licc_k_cmp");
    kHeader->addInstruction(own(kCmp));
    kHeader->addInstruction(own(new BranchInst(kCmp, kBody, kExit)));
    wireEdge(accInitExit, kHeader);
    wireEdge(kHeader, kBody);
    wireEdge(kHeader, kExit);

    auto *lhsGep = new GetElementPtrInst(pat.lhsArray, {pat.iIV, kPhi}, "licc_lhs_gep");
    kBody->addInstruction(own(lhsGep));
    auto *lhsVal = new LoadInst(lhsGep, "licc_lhs_val");
    kBody->addInstruction(own(lhsVal));

    Value *parityIkVal = nullptr;
    if (pat.hasParityGuard)
    {
        auto *pikGep = new GetElementPtrInst(pat.parityIkArray, {pat.iIV, kPhi}, "licc_pik_gep");
        kBody->addInstruction(own(pikGep));
        auto *pikLoad = new LoadInst(pikGep, "licc_pik_val");
        kBody->addInstruction(own(pikLoad));
        parityIkVal = pikLoad;
    }
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
    emitInnerJBody(jBody, accGep, lhsVal, pat.rhsArray, kPhi, jPhi, pat.hasParityGuard,
                   parityIkVal, pat.parityKjArray, zero, one);

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
    auto *outGep = new GetElementPtrInst(outBase, {pat.iIV, jStorePhi}, "licc_out_gep");
    storeBody->addInstruction(own(outGep));
    storeBody->addInstruction(own(new StoreInst(accRead, outGep)));
    auto *jStoreInc = new BinaryOperator(Opcode::Add, jStorePhi, one, "licc_j_store_inc");
    storeBody->addInstruction(own(jStoreInc));
    jStorePhi->addIncoming(jStoreInc, storeBody);
    storeBody->addInstruction(own(new BranchInst(storeHeader)));
    wireEdge(storeBody, storeHeader);

    storeExit->addInstruction(own(new BranchInst(pat.jExit)));
    wireEdge(storeExit, pat.jExit);
    removeOldNest(func, pat, storeExit);
    if (foundTail && verbose)
        debugInfo << "LoopInterchange: const-row tail fold c=" << tail.constVal
                  << (aliasesRhs ? " (in-place i<=mid)\n" : "\n");
    return true;
}

bool LoopInterchangePass::applyDirectAccumulate(Function *func, MatMulDotProductNest &pat,
                                                Value *outBase)
{
    auto *zero = ci(0);
    auto *one = ci(1);

    BasicBlock *kHeader = func->addBasicBlock("licc_k_header");
    BasicBlock *kBody = func->addBasicBlock("licc_k_body");
    BasicBlock *jHeader = func->addBasicBlock("licc_j_header");
    BasicBlock *jBody = func->addBasicBlock("licc_j_body");
    BasicBlock *kLatch = func->addBasicBlock("licc_k_latch");
    BasicBlock *kExit = func->addBasicBlock("licc_k_exit");

    // out 已是 0，无需清零；直接进入 k 循环
    replaceTerminator(pat.iBody, own(new BranchInst(kHeader)));
    wireEdge(pat.iBody, kHeader);

    auto *kPhi = new PhiInst(IntegerType::getInstance(), "licc_k");
    kPhi->addIncoming(zero, pat.iBody);
    kHeader->addInstruction(own(kPhi));
    auto *kCmp = new ICmpInst(ICmpInst::ICMP_SLT, kPhi, pat.bound, "licc_k_cmp");
    kHeader->addInstruction(own(kCmp));
    kHeader->addInstruction(own(new BranchInst(kCmp, kBody, kExit)));
    wireEdge(kHeader, kBody);
    wireEdge(kHeader, kExit);

    auto *lhsGep = new GetElementPtrInst(pat.lhsArray, {pat.iIV, kPhi}, "licc_lhs_gep");
    kBody->addInstruction(own(lhsGep));
    auto *lhsVal = new LoadInst(lhsGep, "licc_lhs_val");
    kBody->addInstruction(own(lhsVal));

    Value *parityIkVal = nullptr;
    if (pat.hasParityGuard)
    {
        auto *pikGep = new GetElementPtrInst(pat.parityIkArray, {pat.iIV, kPhi}, "licc_pik_gep");
        kBody->addInstruction(own(pikGep));
        auto *pikLoad = new LoadInst(pikGep, "licc_pik_val");
        kBody->addInstruction(own(pikLoad));
        parityIkVal = pikLoad;
    }
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

    auto *outGep = new GetElementPtrInst(outBase, {pat.iIV, jPhi}, "licc_out_gep");
    jBody->addInstruction(own(outGep));
    emitInnerJBody(jBody, outGep, lhsVal, pat.rhsArray, kPhi, jPhi, pat.hasParityGuard,
                   parityIkVal, pat.parityKjArray, zero, one);

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

    kExit->addInstruction(own(new BranchInst(pat.jExit)));
    wireEdge(kExit, pat.jExit);
    removeOldNest(func, pat, kExit);
    return true;
}

bool LoopInterchangePass::applyInterchange(Function *func, MatMulDotProductNest &pat)
{
    Value *outBase = pat.outArray ? pat.outArray : pat.rhsArray;
    // out 与 rhs/lhs 别名时必须用行缓冲（如 many_mat_cal 的 A[i][j]=Σ A[k][j]）
    bool needsScratch = sameArray(outBase, pat.rhsArray) || sameArray(outBase, pat.lhsArray);
    bool ok =
        needsScratch ? applyWithScratch(func, pat, outBase) : applyDirectAccumulate(func, pat, outBase);
    if (ok && verbose)
    {
        debugInfo << "LoopInterchange: interchanged dot-product nest at " << pat.kHeader->getName()
                  << " in " << func->getName() << (needsScratch ? " (scratch)\n" : " (direct)\n");
    }
    return ok;
}

bool LoopInterchangePass::runOnFunction(Function *func)
{
    const MatrixFunctionAnalysis *analysis = getAnalysis(func);
    if (!analysis || analysis->matMulDotProductNests.empty())
        return false;

    func->setLoops(ControlFlowAnalysis::findLoops(func));
    MatMulDotProductNest pat = analysis->matMulDotProductNests.front();
    bool changed = applyInterchange(func, pat);
    if (changed)
        func->setLoops(ControlFlowAnalysis::findLoops(func));
    return changed;
}
