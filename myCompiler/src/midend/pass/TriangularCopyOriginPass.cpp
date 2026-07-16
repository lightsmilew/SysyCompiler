#include "TriangularCopyOriginPass.h"
#include "ControlFlowAnalysis.h"
#include <algorithm>
#include <unordered_set>

using namespace std;
using namespace optimization;
using namespace matrixStructure;

namespace
{
    ConstantInt *ci(int v) { return new ConstantInt(IntegerType::getInstance(), v); }

    unique_ptr<Instruction> own(Instruction *inst) { return unique_ptr<Instruction>(inst); }

    void wireEdge(BasicBlock *from, BasicBlock *to)
    {
        from->addSuccessor(to);
        to->addPredecessor(from);
    }

    void clearSuccessors(BasicBlock *bb)
    {
        auto succs = bb->getSuccessors();
        for (BasicBlock *s : succs)
        {
            bb->removeSuccessor(s);
            s->removePredecessor(bb);
        }
    }

    /// 将 startTime 之后的指令挪到不可达块，并让 startTimeBlock 跳转到 newEntry。
    bool redirectAfterStartTime(BasicBlock *startBB, Instruction *startTime, BasicBlock *newEntry,
                                Function *func)
    {
        auto &insts = startBB->getInstructions();
        size_t idx = insts.size();
        for (size_t i = 0; i < insts.size(); ++i)
        {
            if (insts[i].get() == startTime)
            {
                idx = i;
                break;
            }
        }
        if (idx >= insts.size())
            return false;

        BasicBlock *deadCont = func->addBasicBlock("tco_dead_cont");
        vector<unique_ptr<Instruction>> moved;
        while (insts.size() > idx + 1)
        {
            moved.push_back(std::move(insts[idx + 1]));
            insts.erase(insts.begin() + idx + 1);
        }

        // 原后继改挂到 deadCont（稍后整图按可达性删除）
        auto oldSuccs = startBB->getSuccessors();
        clearSuccessors(startBB);
        for (BasicBlock *s : oldSuccs)
        {
            deadCont->addSuccessor(s);
            s->addPredecessor(deadCont);
        }

        for (auto &m : moved)
            deadCont->addInstruction(std::move(m));

        if (!deadCont->getTerminator())
            deadCont->addInstruction(own(new BranchInst(deadCont)));

        startBB->addInstruction(own(new BranchInst(newEntry)));
        wireEdge(startBB, newEntry);
        return true;
    }

    /// DCE 只删「无前驱」块，删不掉仍 internally 连通的不可达 SCC；这里按入口 BFS 清掉。
    void eraseUnreachableBlocks(Function *func, Pass *pass)
    {
        auto &bbs = func->getBasicBlocks();
        if (bbs.empty())
            return;
        BasicBlock *entry = bbs[0].get();

        unordered_set<BasicBlock *> reachable;
        vector<BasicBlock *> work{entry};
        reachable.insert(entry);
        for (size_t i = 0; i < work.size(); ++i)
        {
            for (BasicBlock *s : work[i]->getSuccessors())
            {
                if (reachable.insert(s).second)
                    work.push_back(s);
            }
        }

        vector<BasicBlock *> toDelete;
        for (auto &bbPtr : bbs)
        {
            if (!reachable.count(bbPtr.get()))
                toDelete.push_back(bbPtr.get());
        }
        if (toDelete.empty())
            return;

        unordered_set<BasicBlock *> delSet(toDelete.begin(), toDelete.end());

        // 存活块里的 phi 去掉对将删块的 incoming
        for (auto &bbPtr : bbs)
        {
            if (delSet.count(bbPtr.get()))
                continue;
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
                if (!phi)
                    continue;
                for (int i = static_cast<int>(phi->getNumIncomingValues()) - 1; i >= 0; --i)
                {
                    if (delSet.count(phi->getIncomingBlock(static_cast<unsigned>(i))))
                        phi->removeIncoming(static_cast<unsigned>(i));
                }
            }
        }

        for (BasicBlock *bb : toDelete)
        {
            // 断开 CFG
            auto preds = bb->getPredecessors();
            auto succs = bb->getSuccessors();
            for (BasicBlock *p : preds)
            {
                p->removeSuccessor(bb);
                bb->removePredecessor(p);
            }
            for (BasicBlock *s : succs)
            {
                bb->removeSuccessor(s);
                s->removePredecessor(bb);
            }
            for (auto &instPtr : bb->getInstructions())
                instPtr->removeThisFromOperands();
            bb->clearInstructions();
        }

        for (auto it = bbs.begin(); it != bbs.end();)
        {
            if (delSet.count(it->get()))
            {
                if (pass)
                    pass->needToDelete.push_back(it->release());
                else
                    it->release();
                it = bbs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
} // namespace

bool TriangularCopyOriginPass::applyRewrite(Function *func, const TriangularInPlaceCopyChain &chain)
{
    BasicBlock *startBB = chain.startTimeBlock;
    Instruction *startTime = chain.startTime;
    GlobalVariable *matrix = chain.matrix;
    GlobalVariable *aArray = chain.aArray;
    Value *n = chain.n;
    Value *len = chain.len;

    Function *stopFn = chain.stopTime->getCalledFunction();
    Function *putintFn = chain.putintCall->getCalledFunction();
    Function *putchFn = chain.putchCall->getCalledFunction();
    if (!stopFn || !putintFn || !putchFn)
        return false;

    // --- 新建 CFG ---
    BasicBlock *oriHdr = func->addBasicBlock("tco_ori_hdr");
    BasicBlock *oriBody = func->addBasicBlock("tco_ori_body");
    BasicBlock *oriExit = func->addBasicBlock("tco_ori_exit");

    BasicBlock *revHdr = func->addBasicBlock("tco_rev_hdr");
    BasicBlock *revBody = func->addBasicBlock("tco_rev_body");
    BasicBlock *revSkip = func->addBasicBlock("tco_rev_skip"); // C==0
    BasicBlock *kHdr = func->addBasicBlock("tco_k_hdr");
    BasicBlock *kBody = func->addBasicBlock("tco_k_body");
    BasicBlock *chLoop = func->addBasicBlock("tco_ch_loop");
    BasicBlock *chKey = func->addBasicBlock("tco_ch_key");   // written 成立后再算 key
    BasicBlock *chBack = func->addBasicBlock("tco_ch_back"); // 回边块：PhiElim 只在此插入 copy
    BasicBlock *chExit = func->addBasicBlock("tco_ch_exit");
    BasicBlock *kLatch = func->addBasicBlock("tco_k_latch");
    BasicBlock *revLatch = func->addBasicBlock("tco_rev_latch");

    BasicBlock *redHdr = func->addBasicBlock("tco_red_hdr");
    BasicBlock *redBody = func->addBasicBlock("tco_red_body");
    BasicBlock *redExit = func->addBasicBlock("tco_red_exit");
    BasicBlock *absThen = func->addBasicBlock("tco_abs_then");
    BasicBlock *absMerge = func->addBasicBlock("tco_abs_merge");
    BasicBlock *epilogue = func->addBasicBlock("tco_epilogue");

    if (!redirectAfterStartTime(startBB, startTime, oriHdr, func))
        return false;

    auto *zero = ci(0);
    auto *one = ci(1);
    auto *three = ci(3);
    auto *four = ci(4);

    // origin[k] = k  for k in [0, len)
    auto *kOriPhi = new PhiInst(IntegerType::getInstance(), "tco_k_ori");
    kOriPhi->addIncoming(zero, startBB);
    oriHdr->addInstruction(own(kOriPhi));
    auto *oriCmp = new ICmpInst(ICmpInst::ICMP_SLT, kOriPhi, len, "tco_ori_cmp");
    oriHdr->addInstruction(own(oriCmp));
    oriHdr->addInstruction(own(new BranchInst(oriCmp, oriBody, oriExit)));
    wireEdge(oriHdr, oriBody);
    wireEdge(oriHdr, oriExit);

    auto *oriGep = new GetElementPtrInst(matrix, {kOriPhi}, "tco_ori_gep");
    oriBody->addInstruction(own(oriGep));
    oriBody->addInstruction(own(new StoreInst(kOriPhi, oriGep)));
    auto *kOriInc = new BinaryOperator(Opcode::Add, kOriPhi, one, "tco_k_ori_inc");
    oriBody->addInstruction(own(kOriInc));
    kOriPhi->addIncoming(kOriInc, oriBody);
    oriBody->addInstruction(own(new BranchInst(oriHdr)));
    wireEdge(oriBody, oriHdr);

    oriExit->addInstruction(own(new BranchInst(revHdr)));
    wireEdge(oriExit, revHdr);

    // for t in [0, len): R = a[len-1-t], C = n/R
    auto *tPhi = new PhiInst(IntegerType::getInstance(), "tco_t");
    tPhi->addIncoming(zero, oriExit);
    revHdr->addInstruction(own(tPhi));
    auto *revCmp = new ICmpInst(ICmpInst::ICMP_SLT, tPhi, len, "tco_rev_cmp");
    revHdr->addInstruction(own(revCmp));
    revHdr->addInstruction(own(new BranchInst(revCmp, revBody, redHdr)));
    wireEdge(revHdr, revBody);
    wireEdge(revHdr, redHdr);

    auto *lenMinus1 = new BinaryOperator(Opcode::Sub, len, one, "tco_len_m1");
    revBody->addInstruction(own(lenMinus1));
    auto *tIdx = new BinaryOperator(Opcode::Sub, lenMinus1, tPhi, "tco_t_idx");
    revBody->addInstruction(own(tIdx));
    auto *aGep = new GetElementPtrInst(aArray, {tIdx}, "tco_a_gep");
    revBody->addInstruction(own(aGep));
    auto *R = new LoadInst(aGep, "tco_R");
    revBody->addInstruction(own(R));
    auto *C = new BinaryOperator(Opcode::SDiv, n, R, "tco_C");
    revBody->addInstruction(own(C));
    auto *cOk = new ICmpInst(ICmpInst::ICMP_SGT, C, zero, "tco_c_ok");
    revBody->addInstruction(own(cOk));
    revBody->addInstruction(own(new BranchInst(cOk, kHdr, revSkip)));
    wireEdge(revBody, kHdr);
    wireEdge(revBody, revSkip);

    revSkip->addInstruction(own(new BranchInst(revLatch)));
    wireEdge(revSkip, revLatch);

    // for k in [0, len): update origin
    auto *kPhi = new PhiInst(IntegerType::getInstance(), "tco_k");
    kPhi->addIncoming(zero, revBody);
    kHdr->addInstruction(own(kPhi));
    auto *kCmp = new ICmpInst(ICmpInst::ICMP_SLT, kPhi, len, "tco_k_cmp");
    kHdr->addInstruction(own(kCmp));
    kHdr->addInstruction(own(new BranchInst(kCmp, kBody, revLatch)));
    wireEdge(kHdr, kBody);
    wireEdge(kHdr, revLatch);

    auto *locGep = new GetElementPtrInst(matrix, {kPhi}, "tco_loc_gep");
    kBody->addInstruction(own(locGep));
    auto *loc0 = new LoadInst(locGep, "tco_loc0");
    kBody->addInstruction(own(loc0));
    // max_key 初值 C*R：首跳时 key<max_key 对合法 written 恒真
    auto *maxKey0 = new BinaryOperator(Opcode::Mul, C, R, "tco_max_key0");
    kBody->addInstruction(own(maxKey0));
    kBody->addInstruction(own(new BranchInst(chLoop)));
    wireEdge(kBody, chLoop);

    // 拆回边：written → keyLt → chBack→chLoop；失败路径不写 loc，避免 PhiElim 误更新
    auto *locPhi = new PhiInst(IntegerType::getInstance(), "tco_loc");
    auto *maxKey = new PhiInst(IntegerType::getInstance(), "tco_max_key");
    locPhi->addIncoming(loc0, kBody);
    maxKey->addIncoming(maxKey0, kBody);
    chLoop->addInstruction(own(locPhi));
    chLoop->addInstruction(own(maxKey));

    auto *jVal = new BinaryOperator(Opcode::SDiv, locPhi, C, "tco_j");
    chLoop->addInstruction(own(jVal));
    auto *jMulC = new BinaryOperator(Opcode::Mul, jVal, C, "tco_jC");
    chLoop->addInstruction(own(jMulC));
    auto *iVal = new BinaryOperator(Opcode::Sub, locPhi, jMulC, "tco_i");
    chLoop->addInstruction(own(iVal));
    auto *jLtR = new ICmpInst(ICmpInst::ICMP_SLT, jVal, R, "tco_j_lt_R");
    chLoop->addInstruction(own(jLtR));
    auto *jLeI = new ICmpInst(ICmpInst::ICMP_SLE, jVal, iVal, "tco_j_le_i");
    chLoop->addInstruction(own(jLeI));
    auto *written = new BinaryOperator(Opcode::And, jLtR, jLeI, "tco_written");
    chLoop->addInstruction(own(written));
    chLoop->addInstruction(own(new BranchInst(written, chKey, chExit)));
    wireEdge(chLoop, chKey);
    wireEdge(chLoop, chExit);

    auto *iMulR = new BinaryOperator(Opcode::Mul, iVal, R, "tco_iR");
    chKey->addInstruction(own(iMulR));
    auto *key = new BinaryOperator(Opcode::Add, iMulR, jVal, "tco_key");
    chKey->addInstruction(own(key));
    auto *keyLt = new ICmpInst(ICmpInst::ICMP_SLT, key, maxKey, "tco_key_lt");
    chKey->addInstruction(own(keyLt));
    chKey->addInstruction(own(new BranchInst(keyLt, chBack, chExit)));
    wireEdge(chKey, chBack);
    wireEdge(chKey, chExit);

    locPhi->addIncoming(key, chBack);
    maxKey->addIncoming(key, chBack);
    chBack->addInstruction(own(new BranchInst(chLoop)));
    wireEdge(chBack, chLoop);

    auto *storeGep = new GetElementPtrInst(matrix, {kPhi}, "tco_store_gep");
    chExit->addInstruction(own(storeGep));
    chExit->addInstruction(own(new StoreInst(locPhi, storeGep)));
    chExit->addInstruction(own(new BranchInst(kLatch)));
    wireEdge(chExit, kLatch);

    auto *kInc = new BinaryOperator(Opcode::Add, kPhi, one, "tco_k_inc");
    kLatch->addInstruction(own(kInc));
    kPhi->addIncoming(kInc, kLatch);
    kLatch->addInstruction(own(new BranchInst(kHdr)));
    wireEdge(kLatch, kHdr);

    auto *tInc = new BinaryOperator(Opcode::Add, tPhi, one, "tco_t_inc");
    revLatch->addInstruction(own(tInc));
    tPhi->addIncoming(tInc, revLatch);
    revLatch->addInstruction(own(new BranchInst(revHdr)));
    wireEdge(revLatch, revHdr);

    // ans = sum k*k*init(origin[k])
    auto *ansPhi = new PhiInst(IntegerType::getInstance(), "tco_ans");
    auto *kRedPhi = new PhiInst(IntegerType::getInstance(), "tco_k_red");
    ansPhi->addIncoming(zero, revHdr);
    kRedPhi->addIncoming(zero, revHdr);
    redHdr->addInstruction(own(ansPhi));
    redHdr->addInstruction(own(kRedPhi));
    auto *redCmp = new ICmpInst(ICmpInst::ICMP_SLT, kRedPhi, len, "tco_red_cmp");
    redHdr->addInstruction(own(redCmp));
    redHdr->addInstruction(own(new BranchInst(redCmp, redBody, redExit)));
    wireEdge(redHdr, redBody);
    wireEdge(redHdr, redExit);

    auto *redGep = new GetElementPtrInst(matrix, {kRedPhi}, "tco_red_gep");
    redBody->addInstruction(own(redGep));
    auto *origin = new LoadInst(redGep, "tco_origin");
    redBody->addInstruction(own(origin));
    auto *and3 = new BinaryOperator(Opcode::And, origin, three, "tco_and3");
    redBody->addInstruction(own(and3));
    auto *isMul4 = new ICmpInst(ICmpInst::ICMP_EQ, and3, zero, "tco_is4");
    redBody->addInstruction(own(isMul4));
    auto *initVal = new SelectInst(isMul4, four, origin, "tco_init");
    redBody->addInstruction(own(initVal));
    auto *kk = new BinaryOperator(Opcode::Mul, kRedPhi, kRedPhi, "tco_kk");
    redBody->addInstruction(own(kk));
    auto *term = new BinaryOperator(Opcode::Mul, kk, initVal, "tco_term");
    redBody->addInstruction(own(term));
    auto *ansNext = new BinaryOperator(Opcode::Add, ansPhi, term, "tco_ans_n");
    redBody->addInstruction(own(ansNext));
    auto *kRedInc = new BinaryOperator(Opcode::Add, kRedPhi, one, "tco_k_red_inc");
    redBody->addInstruction(own(kRedInc));
    ansPhi->addIncoming(ansNext, redBody);
    kRedPhi->addIncoming(kRedInc, redBody);
    redBody->addInstruction(own(new BranchInst(redHdr)));
    wireEdge(redBody, redHdr);

    auto *ansNeg = new ICmpInst(ICmpInst::ICMP_SLT, ansPhi, zero, "tco_ans_neg");
    redExit->addInstruction(own(ansNeg));
    redExit->addInstruction(own(new BranchInst(ansNeg, absThen, absMerge)));
    wireEdge(redExit, absThen);
    wireEdge(redExit, absMerge);

    auto *negated = new BinaryOperator(Opcode::Sub, zero, ansPhi, "tco_neg");
    absThen->addInstruction(own(negated));
    absThen->addInstruction(own(new BranchInst(absMerge)));
    wireEdge(absThen, absMerge);

    auto *finalAns = new PhiInst(IntegerType::getInstance(), "tco_final");
    finalAns->addIncoming(ansPhi, redExit);
    finalAns->addIncoming(negated, absThen);
    absMerge->addInstruction(own(finalAns));
    absMerge->addInstruction(own(new BranchInst(epilogue)));
    wireEdge(absMerge, epilogue);

    epilogue->addInstruction(
        own(new CallInst(stopFn, {ci(chain.stopTimeLine)})));
    epilogue->addInstruction(own(new CallInst(putintFn, {finalAns})));
    epilogue->addInstruction(own(new CallInst(putchFn, {ci(chain.putchChar)})));
    epilogue->addInstruction(own(new ReturnInst(ci(0))));

    eraseUnreachableBlocks(func, this);
    func->setLoops(ControlFlowAnalysis::findLoops(func));
    matrixStructure::clearAnalysis(func);

    if (verbose)
    {
        debugInfo << "TriangularCopyOrigin: rewrote @" << matrix->getName()
                  << " origin-tracking in " << func->getName() << "\n";
    }
    return true;
}

bool TriangularCopyOriginPass::runOnFunction(Function *func)
{
    const MatrixFunctionAnalysis *analysis = getAnalysis(func);
    if (!analysis || !analysis->triangularCopyChain || !analysis->triangularCopyChain->valid)
        return false;
    return applyRewrite(func, *analysis->triangularCopyChain);
}
