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

bool LoopInterchangePass::applyInterchange(Function *func, MatMulDotProductNest &pat)
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
    auto *accLoad = new LoadInst(accGep, "licc_acc_load");
    jBody->addInstruction(own(accLoad));

    auto *rhsGep = new GetElementPtrInst(pat.rhsArray, {kPhi, jPhi}, "licc_rhs_gep");
    jBody->addInstruction(own(rhsGep));
    auto *rhsVal = new LoadInst(rhsGep, "licc_rhs_val");
    jBody->addInstruction(own(rhsVal));

    auto *prod = new BinaryOperator(Opcode::Mul, lhsVal, rhsVal, "licc_prod");
    jBody->addInstruction(own(prod));

    Value *addend = prod;
    if (pat.hasParityGuard)
    {
        auto *pkjGep = new GetElementPtrInst(pat.parityKjArray, {kPhi, jPhi}, "licc_pkj_gep");
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
    Value *outBase = pat.outArray ? pat.outArray : pat.rhsArray;
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
