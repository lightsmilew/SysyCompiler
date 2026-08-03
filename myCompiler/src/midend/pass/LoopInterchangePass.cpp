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
    BasicBlock *lastKBody = func->addBasicBlock("licc_last_k_body");
    BasicBlock *lastJHeader = func->addBasicBlock("licc_last_j_header");
    BasicBlock *lastJBody = func->addBasicBlock("licc_last_j_body");
    BasicBlock *kLatch = func->addBasicBlock("licc_k_latch");
    BasicBlock *kExit = func->addBasicBlock("licc_k_exit");
    // kBound==0 时无 k 迭代，仍需把初值刷到 out
    BasicBlock *storeHeader = func->addBasicBlock("licc_store_header");
    BasicBlock *storeBody = func->addBasicBlock("licc_store_body");
    BasicBlock *storeExit = func->addBasicBlock("licc_store_exit");
    // store / last-k 共用，作为 removeOldNest 的 continue
    BasicBlock *rowDone = func->addBasicBlock("licc_row_done");

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

    // kBound==0：无乘加，直接刷初值；否则最后一轮 k 写 out，省掉独立 flush
    auto *kEmpty = new ICmpInst(ICmpInst::ICMP_EQ, kBound, zero, "licc_k_empty");
    accInitExit->addInstruction(own(kEmpty));
    accInitExit->addInstruction(own(new BranchInst(kEmpty, storeHeader, kHeader)));
    wireEdge(accInitExit, storeHeader);
    wireEdge(accInitExit, kHeader);

    auto *kLast = new BinaryOperator(Opcode::Sub, kBound, one, "licc_k_last");
    // 放在 kHeader 前驱可见处：随 kHeader 入口计算
    auto *kPhi = new PhiInst(IntegerType::getInstance(), "licc_k");
    kPhi->addIncoming(zero, accInitExit);
    kHeader->addInstruction(own(kPhi));
    kHeader->addInstruction(own(kLast));
    auto *kCmp = new ICmpInst(ICmpInst::ICMP_SLT, kPhi, kBound, "licc_k_cmp");
    kHeader->addInstruction(own(kCmp));
    auto *isLastK = new ICmpInst(ICmpInst::ICMP_EQ, kPhi, kLast, "licc_k_is_last");
    kHeader->addInstruction(own(isLastK));
    // k < kBound ? (last ? lastKBody : kBody) : kExit
    BasicBlock *kDispatch = func->addBasicBlock("licc_k_dispatch");
    kHeader->addInstruction(own(new BranchInst(kCmp, kDispatch, kExit)));
    wireEdge(kHeader, kDispatch);
    wireEdge(kHeader, kExit);
    kDispatch->addInstruction(own(new BranchInst(isLastK, lastKBody, kBody)));
    wireEdge(kDispatch, lastKBody);
    wireEdge(kDispatch, kBody);

    auto emitKBodyPrefix = [&](BasicBlock *kb, const char *lhsName, const char *pikName,
                               Value *&outLhs, Value *&outParity) {
        auto *lhsGep = new GetElementPtrInst(pat.lhsArray, {pat.iIV, kPhi}, lhsName);
        kb->addInstruction(own(lhsGep));
        auto *lhsVal = new LoadInst(lhsGep, string(lhsName) + "_val");
        kb->addInstruction(own(lhsVal));
        outLhs = lhsVal;
        outParity = nullptr;
        if (pat.hasParityGuard)
        {
            auto *pikGep = new GetElementPtrInst(pat.parityIkArray, {pat.iIV, kPhi}, pikName);
            kb->addInstruction(own(pikGep));
            auto *pikLoad = new LoadInst(pikGep, string(pikName) + "_val");
            kb->addInstruction(own(pikLoad));
            outParity = pikLoad;
        }
    };

    Value *lhsVal = nullptr;
    Value *parityIkVal = nullptr;
    emitKBodyPrefix(kBody, "licc_lhs_gep", "licc_pik_gep", lhsVal, parityIkVal);
    kBody->addInstruction(own(new BranchInst(jHeader)));
    wireEdge(kBody, jHeader);

    Value *lastLhsVal = nullptr;
    Value *lastParityIkVal = nullptr;
    emitKBodyPrefix(lastKBody, "licc_last_lhs_gep", "licc_last_pik_gep", lastLhsVal,
                    lastParityIkVal);
    lastKBody->addInstruction(own(new BranchInst(lastJHeader)));
    wireEdge(lastKBody, lastJHeader);

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

    // 最后一轮：从 scratch 累加后直写 out，不再回写 scratch
    auto *ljPhi = new PhiInst(IntegerType::getInstance(), "licc_last_j");
    ljPhi->addIncoming(zero, lastKBody);
    lastJHeader->addInstruction(own(ljPhi));
    auto *ljCmp = new ICmpInst(ICmpInst::ICMP_SLT, ljPhi, pat.bound, "licc_last_j_cmp");
    lastJHeader->addInstruction(own(ljCmp));
    lastJHeader->addInstruction(own(new BranchInst(ljCmp, lastJBody, kExit)));
    wireEdge(lastJHeader, lastJBody);
    wireEdge(lastJHeader, kExit); // last-k 完成后经 kExit → rowDone

    auto *lastAccGep = new GetElementPtrInst(acc, {zero, ljPhi}, "licc_last_acc_gep");
    lastJBody->addInstruction(own(lastAccGep));
    auto *lastOutGep = new GetElementPtrInst(outBase, {pat.iIV, ljPhi}, "licc_last_out_gep");
    lastJBody->addInstruction(own(lastOutGep));
    {
        auto *accLoad = new LoadInst(lastAccGep, "licc_last_acc_load");
        lastJBody->addInstruction(own(accLoad));
        auto *rhsGep = new GetElementPtrInst(pat.rhsArray, {kPhi, ljPhi}, "licc_last_rhs_gep");
        lastJBody->addInstruction(own(rhsGep));
        auto *rhsVal = new LoadInst(rhsGep, "licc_last_rhs_val");
        lastJBody->addInstruction(own(rhsVal));
        auto *prod = new BinaryOperator(Opcode::Mul, lastLhsVal, rhsVal, "licc_last_prod");
        lastJBody->addInstruction(own(prod));
        Value *addend = prod;
        if (pat.hasParityGuard)
        {
            auto *pkjGep =
                new GetElementPtrInst(pat.parityKjArray, {kPhi, ljPhi}, "licc_last_pkj_gep");
            lastJBody->addInstruction(own(pkjGep));
            auto *pkjLoad = new LoadInst(pkjGep, "licc_last_pkj_val");
            lastJBody->addInstruction(own(pkjLoad));
            auto *pa = new BinaryOperator(Opcode::And, lastParityIkVal, one, "licc_last_parity_a");
            lastJBody->addInstruction(own(pa));
            auto *pb = new BinaryOperator(Opcode::And, pkjLoad, one, "licc_last_parity_b");
            lastJBody->addInstruction(own(pb));
            auto *pand = new BinaryOperator(Opcode::And, pa, pb, "licc_last_parity_and");
            lastJBody->addInstruction(own(pand));
            auto *ok = new ICmpInst(ICmpInst::ICMP_EQ, pand, zero, "licc_last_parity_ok");
            lastJBody->addInstruction(own(ok));
            auto *scaled = new BinaryOperator(Opcode::Mul, ok, prod, "licc_last_scaled");
            lastJBody->addInstruction(own(scaled));
            addend = scaled;
        }
        auto *accAdd = new BinaryOperator(Opcode::Add, accLoad, addend, "licc_last_acc_add");
        lastJBody->addInstruction(own(accAdd));
        lastJBody->addInstruction(own(new StoreInst(accAdd, lastOutGep)));
    }
    auto *ljInc = new BinaryOperator(Opcode::Add, ljPhi, one, "licc_last_j_inc");
    lastJBody->addInstruction(own(ljInc));
    ljPhi->addIncoming(ljInc, lastJBody);
    lastJBody->addInstruction(own(new BranchInst(lastJHeader)));
    wireEdge(lastJBody, lastJHeader);

    auto *kInc = new BinaryOperator(Opcode::Add, kPhi, one, "licc_k_inc");
    kLatch->addInstruction(own(kInc));
    kPhi->addIncoming(kInc, kLatch);
    kLatch->addInstruction(own(new BranchInst(kHeader)));
    wireEdge(kLatch, kHeader);

    // 仅 kBound==0：刷 scratch 初值到 out
    auto *jStorePhi = new PhiInst(IntegerType::getInstance(), "licc_j_store");
    jStorePhi->addIncoming(zero, accInitExit);
    storeHeader->addInstruction(own(jStorePhi));
    auto *storeCmp = new ICmpInst(ICmpInst::ICMP_SLT, jStorePhi, pat.bound, "licc_store_cmp");
    storeHeader->addInstruction(own(storeCmp));
    storeHeader->addInstruction(own(new BranchInst(storeCmp, storeBody, storeExit)));
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

    storeExit->addInstruction(own(new BranchInst(rowDone)));
    wireEdge(storeExit, rowDone);
    kExit->addInstruction(own(new BranchInst(rowDone)));
    wireEdge(kExit, rowDone);
    rowDone->addInstruction(own(new BranchInst(pat.jExit)));
    removeOldNest(func, pat, rowDone);
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
    BasicBlock *jFastHeader = func->addBasicBlock("licc_j_fast_header");
    BasicBlock *jFastBody = func->addBasicBlock("licc_j_fast_body");
    BasicBlock *jSlowHeader = nullptr;
    BasicBlock *jSlowBody = nullptr;
    if (pat.hasParityGuard)
    {
        jSlowHeader = func->addBasicBlock("licc_j_slow_header");
        jSlowBody = func->addBasicBlock("licc_j_slow_body");
    }
    BasicBlock *kLatch = func->addBasicBlock("licc_k_latch");
    BasicBlock *kExit = func->addBasicBlock("licc_k_exit");

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

    if (pat.hasParityGuard)
    {
        auto *pikGep = new GetElementPtrInst(pat.parityIkArray, {pat.iIV, kPhi}, "licc_pik_gep");
        kBody->addInstruction(own(pikGep));
        auto *pikLoad = new LoadInst(pikGep, "licc_pik_val");
        kBody->addInstruction(own(pikLoad));
        auto *pa = new BinaryOperator(Opcode::And, pikLoad, one, "licc_parity_a");
        kBody->addInstruction(own(pa));
        // pa==0：乘积恒偶，整行无条件累加；pa==1：仅当 pkj 偶时累加
        auto *paOdd = new ICmpInst(ICmpInst::ICMP_NE, pa, zero, "licc_pa_odd");
        kBody->addInstruction(own(paOdd));
        // 独立 preheader，避免后续 gcc-unroll 把入口边接到 unroll_exit
        BasicBlock *fastPre = func->addBasicBlock("licc_j_fast_pre");
        BasicBlock *slowPre = func->addBasicBlock("licc_j_slow_pre");
        kBody->addInstruction(own(new BranchInst(paOdd, slowPre, fastPre)));
        wireEdge(kBody, slowPre);
        wireEdge(kBody, fastPre);
        fastPre->addInstruction(own(new BranchInst(jFastHeader)));
        wireEdge(fastPre, jFastHeader);
        slowPre->addInstruction(own(new BranchInst(jSlowHeader)));
        wireEdge(slowPre, jSlowHeader);

        auto emitJLoop = [&](BasicBlock *jHeader, BasicBlock *jBody, BasicBlock *entryFrom,
                             bool needPkj) {
            auto *jPhi = new PhiInst(IntegerType::getInstance(),
                                     needPkj ? "licc_j_slow" : "licc_j_fast");
            jPhi->addIncoming(zero, entryFrom);
            jHeader->addInstruction(own(jPhi));
            auto *jCmp = new ICmpInst(ICmpInst::ICMP_SLT, jPhi, pat.bound,
                                      needPkj ? "licc_j_slow_cmp" : "licc_j_fast_cmp");
            jHeader->addInstruction(own(jCmp));
            jHeader->addInstruction(own(new BranchInst(jCmp, jBody, kLatch)));
            wireEdge(jHeader, jBody);
            wireEdge(jHeader, kLatch);

            auto *outGep = new GetElementPtrInst(outBase, {pat.iIV, jPhi},
                                                 needPkj ? "licc_out_slow" : "licc_out_fast");
            jBody->addInstruction(own(outGep));
            auto *accLoad = new LoadInst(outGep, needPkj ? "licc_acc_slow" : "licc_acc_fast");
            jBody->addInstruction(own(accLoad));
            auto *rhsGep = new GetElementPtrInst(pat.rhsArray, {kPhi, jPhi},
                                                 needPkj ? "licc_rhs_slow" : "licc_rhs_fast");
            jBody->addInstruction(own(rhsGep));
            auto *rhsVal = new LoadInst(rhsGep, needPkj ? "licc_rhs_slow_v" : "licc_rhs_fast_v");
            jBody->addInstruction(own(rhsVal));
            auto *prod = new BinaryOperator(Opcode::Mul, lhsVal, rhsVal,
                                            needPkj ? "licc_prod_slow" : "licc_prod_fast");
            jBody->addInstruction(own(prod));
            Value *addend = prod;
            if (needPkj)
            {
                auto *pkjGep =
                    new GetElementPtrInst(pat.parityKjArray, {kPhi, jPhi}, "licc_pkj_gep");
                jBody->addInstruction(own(pkjGep));
                auto *pkjLoad = new LoadInst(pkjGep, "licc_pkj_val");
                jBody->addInstruction(own(pkjLoad));
                auto *pb = new BinaryOperator(Opcode::And, pkjLoad, one, "licc_parity_b");
                jBody->addInstruction(own(pb));
                auto *ok = new ICmpInst(ICmpInst::ICMP_EQ, pb, zero, "licc_parity_ok");
                jBody->addInstruction(own(ok));
                auto *scaled = new BinaryOperator(Opcode::Mul, ok, prod, "licc_scaled");
                jBody->addInstruction(own(scaled));
                addend = scaled;
            }
            auto *accAdd = new BinaryOperator(Opcode::Add, accLoad, addend,
                                              needPkj ? "licc_add_slow" : "licc_add_fast");
            jBody->addInstruction(own(accAdd));
            jBody->addInstruction(own(new StoreInst(accAdd, outGep)));
            auto *jInc = new BinaryOperator(Opcode::Add, jPhi, one,
                                            needPkj ? "licc_j_slow_inc" : "licc_j_fast_inc");
            jBody->addInstruction(own(jInc));
            jPhi->addIncoming(jInc, jBody);
            jBody->addInstruction(own(new BranchInst(jHeader)));
            wireEdge(jBody, jHeader);
        };

        emitJLoop(jFastHeader, jFastBody, fastPre, false);
        emitJLoop(jSlowHeader, jSlowBody, slowPre, true);
    }
    else
    {
        kBody->addInstruction(own(new BranchInst(jFastHeader)));
        wireEdge(kBody, jFastHeader);

        auto *jPhi = new PhiInst(IntegerType::getInstance(), "licc_j");
        jPhi->addIncoming(zero, kBody);
        jFastHeader->addInstruction(own(jPhi));
        auto *jCmp = new ICmpInst(ICmpInst::ICMP_SLT, jPhi, pat.bound, "licc_j_cmp");
        jFastHeader->addInstruction(own(jCmp));
        jFastHeader->addInstruction(own(new BranchInst(jCmp, jFastBody, kLatch)));
        wireEdge(jFastHeader, jFastBody);
        wireEdge(jFastHeader, kLatch);

        auto *outGep = new GetElementPtrInst(outBase, {pat.iIV, jPhi}, "licc_out_gep");
        jFastBody->addInstruction(own(outGep));
        emitInnerJBody(jFastBody, outGep, lhsVal, pat.rhsArray, kPhi, jPhi, false, nullptr, nullptr,
                       zero, one);

        auto *jInc = new BinaryOperator(Opcode::Add, jPhi, one, "licc_j_inc");
        jFastBody->addInstruction(own(jInc));
        jPhi->addIncoming(jInc, jFastBody);
        jFastBody->addInstruction(own(new BranchInst(jFastHeader)));
        wireEdge(jFastBody, jFastHeader);
    }

    auto *kInc = new BinaryOperator(Opcode::Add, kPhi, one, "licc_k_inc");
    kLatch->addInstruction(own(kInc));
    kPhi->addIncoming(kInc, kLatch);
    kLatch->addInstruction(own(new BranchInst(kHeader)));
    wireEdge(kLatch, kHeader);

    kExit->addInstruction(own(new BranchInst(pat.jExit)));
    wireEdge(kExit, pat.jExit);
    removeOldNest(func, pat, kExit);
    if (pat.hasParityGuard && verbose)
        debugInfo << "LoopInterchange: parity-a versioned j loops in " << func->getName() << "\n";
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
