#include "ScaledRowDensePackPass.h"
#include "ControlFlowAnalysis.h"
#include <algorithm>
#include <climits>
#include <unordered_set>
using namespace std;
using namespace optimization;
using namespace matrixStructure;

namespace
{
    constexpr unsigned kDenseCapacity = 1024 * 1024;
    static unsigned gNestSerial = 0;

    static ConstantInt *ci(int v) { return new ConstantInt(IntegerType::getInstance(), v); }

    static unique_ptr<Instruction> own(Instruction *inst) { return unique_ptr<Instruction>(inst); }

    static void wireEdge(BasicBlock *from, BasicBlock *to)
    {
        from->addSuccessor(to);
        to->addPredecessor(from);
    }

    static void replaceTerminator(BasicBlock *bb, unique_ptr<Instruction> term)
    {
        for (BasicBlock *succ : vector<BasicBlock *>(bb->getSuccessors()))
        {
            bb->removeSuccessor(succ);
            succ->removePredecessor(bb);
        }
        auto &insts = bb->getInstructions();
        if (!insts.empty() && insts.back()->Op == Opcode::Br)
        {
            insts.back()->removeThisFromOperands();
            insts.pop_back();
        }
        bb->addInstruction(std::move(term));
    }

    static void fixPhisForRemovedBlock(BasicBlock *phiBB, BasicBlock *removed, BasicBlock *replacement)
    {
        if (!phiBB || !removed)
            return;
        for (auto &instPtr : phiBB->getInstructions())
        {
            auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
            if (!phi)
                continue;
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                if (phi->getIncomingBlock(i) == removed)
                    phi->replaceIncomingBasicBlock(removed, replacement);
            }
        }
    }

    static void removeScaledRowNest(ScaledRowUpdateNest &pat, BasicBlock *continueBB)
    {
        vector<BasicBlock *> toRemove = pat.kLoop->blocks;
        sort(toRemove.begin(), toRemove.end());

        for (BasicBlock *bb : toRemove)
        {
            if (!bb || bb == continueBB)
                continue;
            fixPhisForRemovedBlock(continueBB, bb, continueBB);
            for (auto &bbPtr : pat.kLoop->header->Parent->getBasicBlocks())
            {
                BasicBlock *other = bbPtr.get();
                if (other == bb || !other)
                    continue;
                fixPhisForRemovedBlock(other, bb, continueBB);
            }
        }

        for (BasicBlock *bb : toRemove)
        {
            if (!bb || bb == continueBB)
                continue;
            bb->removeSelfBasicBlock();
        }
    }

    static BasicBlock *findLoopPreheader(const Loop &loop)
    {
        if (!loop.header)
            return nullptr;
        for (BasicBlock *pred : loop.header->getPredecessors())
        {
            if (!loop.containsBlock(pred))
                return pred;
        }
        return nullptr;
    }

    static unsigned basicBlockIndex(Function *func, BasicBlock *bb)
    {
        unsigned idx = 0;
        for (auto &bbPtr : func->getBasicBlocks())
        {
            if (bbPtr.get() == bb)
                return idx;
            ++idx;
        }
        return UINT_MAX;
    }

    static void eraseUnreachableBlocks(Function *func, Pass *pass)
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
            for (BasicBlock *p : vector<BasicBlock *>(bb->getPredecessors()))
            {
                p->removeSuccessor(bb);
                bb->removePredecessor(p);
            }
            for (BasicBlock *s : vector<BasicBlock *>(bb->getSuccessors()))
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

    static string srdpTag(unsigned id, const string &base) { return base + "_" + to_string(id); }

    static Value *emitFlatIndex(BasicBlock *bb, Value *row, Value *col, Value *n, const string &name)
    {
        auto *mul = new BinaryOperator(Opcode::Mul, row, n, name + "_row");
        bb->addInstruction(own(mul));
        auto *add = new BinaryOperator(Opcode::Add, mul, col, name);
        bb->addInstruction(own(add));
        return add;
    }

    static Value *emitDenseGep(BasicBlock *bb, Value *buf, Value *idx, Value *zero, const string &name)
    {
        auto *gep = new GetElementPtrInst(buf, {zero, idx}, name);
        bb->addInstruction(own(gep));
        return gep;
    }

    static Value *emitI32Ptr(BasicBlock *bb, Value *buf, const string &name)
    {
        auto *ty = PointerType::getInstance(IntegerType::getInstance());
        auto *cast = new CastInst(Opcode::BitCast, buf, ty, name);
        bb->addInstruction(own(cast));
        return cast;
    }

    static Value *emitPtrAddBytes(BasicBlock *bb, Value *ptr, Value *bytes, const string &name)
    {
        auto *addd = new BinaryOperator(Opcode::Addd, ptr, bytes, name);
        bb->addInstruction(own(addd));
        return addd;
    }

    static Value *emitIndexBytes(BasicBlock *bb, Value *idx, const string &name)
    {
        auto *shl = new BinaryOperator(Opcode::Sll, idx, ci(2), name);
        bb->addInstruction(own(shl));
        return shl;
    }

    static void emitPackAColMajor(BasicBlock *entry, BasicBlock *exit, Value *bound, Value *n,
                                  Value *zero, Value *one, Value *ap, Value *aArray, unsigned id)
    {
        Function *func = entry->Parent;
        BasicBlock *kHeader = func->addBasicBlock(srdpTag(id, "srdp_pack_a_k"));
        BasicBlock *kBody = func->addBasicBlock(srdpTag(id, "srdp_pack_a_k_body"));
        BasicBlock *iHeader = func->addBasicBlock(srdpTag(id, "srdp_pack_a_i"));
        BasicBlock *iBody = func->addBasicBlock(srdpTag(id, "srdp_pack_a_i_body"));
        BasicBlock *kLatch = func->addBasicBlock(srdpTag(id, "srdp_pack_a_k_latch"));

        entry->addInstruction(own(new BranchInst(kHeader)));
        wireEdge(entry, kHeader);

        auto *kPhi = new PhiInst(IntegerType::getInstance(), srdpTag(id, "srdp_pack_a_kiv"));
        kPhi->addIncoming(zero, entry);
        kHeader->addInstruction(own(kPhi));
        auto *kCmp = new ICmpInst(ICmpInst::ICMP_SLT, kPhi, bound, srdpTag(id, "srdp_pack_a_kcmp"));
        kHeader->addInstruction(own(kCmp));
        kHeader->addInstruction(own(new BranchInst(kCmp, kBody, exit)));
        wireEdge(kHeader, kBody);
        wireEdge(kHeader, exit);

        kBody->addInstruction(own(new BranchInst(iHeader)));
        wireEdge(kBody, iHeader);

        auto *iPhi = new PhiInst(IntegerType::getInstance(), srdpTag(id, "srdp_pack_a_iiv"));
        iPhi->addIncoming(zero, kBody);
        iHeader->addInstruction(own(iPhi));
        auto *iCmp = new ICmpInst(ICmpInst::ICMP_SLT, iPhi, bound, srdpTag(id, "srdp_pack_a_icmp"));
        iHeader->addInstruction(own(iCmp));
        iHeader->addInstruction(own(new BranchInst(iCmp, iBody, kLatch)));
        wireEdge(iHeader, iBody);
        wireEdge(iHeader, kLatch);

        auto *aGep = new GetElementPtrInst(aArray, {iPhi, kPhi}, srdpTag(id, "srdp_pack_a_src"));
        iBody->addInstruction(own(aGep));
        auto *aVal = new LoadInst(aGep, srdpTag(id, "srdp_pack_a_val"));
        iBody->addInstruction(own(aVal));
        Value *idx = emitFlatIndex(iBody, kPhi, iPhi, n, srdpTag(id, "srdp_pack_a_idx"));
        Value *dst = emitDenseGep(iBody, ap, idx, zero, srdpTag(id, "srdp_pack_a_dst"));
        iBody->addInstruction(own(new StoreInst(aVal, dst)));

        auto *iInc = new BinaryOperator(Opcode::Add, iPhi, one, srdpTag(id, "srdp_pack_a_iinc"));
        iBody->addInstruction(own(iInc));
        iPhi->addIncoming(iInc, iBody);
        iBody->addInstruction(own(new BranchInst(iHeader)));
        wireEdge(iBody, iHeader);

        auto *kInc = new BinaryOperator(Opcode::Add, kPhi, one, srdpTag(id, "srdp_pack_a_kinc"));
        kLatch->addInstruction(own(kInc));
        kPhi->addIncoming(kInc, kLatch);
        kLatch->addInstruction(own(new BranchInst(kHeader)));
        wireEdge(kLatch, kHeader);
    }

    static void emitPackBRowMajor(BasicBlock *entry, BasicBlock *exit, Value *bound, Value *n,
                                  Value *zero, Value *one, Value *bp, Value *bArray, unsigned id)
    {
        Function *func = entry->Parent;
        BasicBlock *kHeader = func->addBasicBlock(srdpTag(id, "srdp_pack_b_k"));
        BasicBlock *kBody = func->addBasicBlock(srdpTag(id, "srdp_pack_b_k_body"));
        BasicBlock *jHeader = func->addBasicBlock(srdpTag(id, "srdp_pack_b_j"));
        BasicBlock *jBody = func->addBasicBlock(srdpTag(id, "srdp_pack_b_j_body"));
        BasicBlock *kLatch = func->addBasicBlock(srdpTag(id, "srdp_pack_b_k_latch"));

        entry->addInstruction(own(new BranchInst(kHeader)));
        wireEdge(entry, kHeader);

        auto *kPhi = new PhiInst(IntegerType::getInstance(), srdpTag(id, "srdp_pack_b_kiv"));
        kPhi->addIncoming(zero, entry);
        kHeader->addInstruction(own(kPhi));
        auto *kCmp = new ICmpInst(ICmpInst::ICMP_SLT, kPhi, bound, srdpTag(id, "srdp_pack_b_kcmp"));
        kHeader->addInstruction(own(kCmp));
        kHeader->addInstruction(own(new BranchInst(kCmp, kBody, exit)));
        wireEdge(kHeader, kBody);
        wireEdge(kHeader, exit);

        kBody->addInstruction(own(new BranchInst(jHeader)));
        wireEdge(kBody, jHeader);

        auto *jPhi = new PhiInst(IntegerType::getInstance(), srdpTag(id, "srdp_pack_b_jiv"));
        jPhi->addIncoming(zero, kBody);
        jHeader->addInstruction(own(jPhi));
        auto *jCmp = new ICmpInst(ICmpInst::ICMP_SLT, jPhi, bound, srdpTag(id, "srdp_pack_b_jcmp"));
        jHeader->addInstruction(own(jCmp));
        jHeader->addInstruction(own(new BranchInst(jCmp, jBody, kLatch)));
        wireEdge(jHeader, jBody);
        wireEdge(jHeader, kLatch);

        auto *bGep = new GetElementPtrInst(bArray, {kPhi, jPhi}, srdpTag(id, "srdp_pack_b_src"));
        jBody->addInstruction(own(bGep));
        auto *bVal = new LoadInst(bGep, srdpTag(id, "srdp_pack_b_val"));
        jBody->addInstruction(own(bVal));
        Value *idx = emitFlatIndex(jBody, kPhi, jPhi, n, srdpTag(id, "srdp_pack_b_idx"));
        Value *dst = emitDenseGep(jBody, bp, idx, zero, srdpTag(id, "srdp_pack_b_dst"));
        jBody->addInstruction(own(new StoreInst(bVal, dst)));

        auto *jInc = new BinaryOperator(Opcode::Add, jPhi, one, srdpTag(id, "srdp_pack_b_jinc"));
        jBody->addInstruction(own(jInc));
        jPhi->addIncoming(jInc, jBody);
        jBody->addInstruction(own(new BranchInst(jHeader)));
        wireEdge(jBody, jHeader);

        auto *kInc = new BinaryOperator(Opcode::Add, kPhi, one, srdpTag(id, "srdp_pack_b_kinc"));
        kLatch->addInstruction(own(kInc));
        kPhi->addIncoming(kInc, kLatch);
        kLatch->addInstruction(own(new BranchInst(kHeader)));
        wireEdge(kLatch, kHeader);
    }

    static void emitZeroDense(BasicBlock *entry, BasicBlock *exit, Value *bound, Value *n, Value *zero,
                              Value *one, Value *cp, unsigned id)
    {
        Function *func = entry->Parent;
        BasicBlock *iHeader = func->addBasicBlock(srdpTag(id, "srdp_zero_i"));
        BasicBlock *iBody = func->addBasicBlock(srdpTag(id, "srdp_zero_i_body"));
        BasicBlock *jHeader = func->addBasicBlock(srdpTag(id, "srdp_zero_j"));
        BasicBlock *jBody = func->addBasicBlock(srdpTag(id, "srdp_zero_j_body"));
        BasicBlock *iLatch = func->addBasicBlock(srdpTag(id, "srdp_zero_i_latch"));

        entry->addInstruction(own(new BranchInst(iHeader)));
        wireEdge(entry, iHeader);

        auto *iPhi = new PhiInst(IntegerType::getInstance(), srdpTag(id, "srdp_zero_iiv"));
        iPhi->addIncoming(zero, entry);
        iHeader->addInstruction(own(iPhi));
        auto *iCmp = new ICmpInst(ICmpInst::ICMP_SLT, iPhi, bound, srdpTag(id, "srdp_zero_icmp"));
        iHeader->addInstruction(own(iCmp));
        iHeader->addInstruction(own(new BranchInst(iCmp, iBody, exit)));
        wireEdge(iHeader, iBody);
        wireEdge(iHeader, exit);

        iBody->addInstruction(own(new BranchInst(jHeader)));
        wireEdge(iBody, jHeader);

        auto *jPhi = new PhiInst(IntegerType::getInstance(), srdpTag(id, "srdp_zero_jiv"));
        jPhi->addIncoming(zero, iBody);
        jHeader->addInstruction(own(jPhi));
        auto *jCmp = new ICmpInst(ICmpInst::ICMP_SLT, jPhi, bound, srdpTag(id, "srdp_zero_jcmp"));
        jHeader->addInstruction(own(jCmp));
        jHeader->addInstruction(own(new BranchInst(jCmp, jBody, iLatch)));
        wireEdge(jHeader, jBody);
        wireEdge(jHeader, iLatch);

        Value *idx = emitFlatIndex(jBody, iPhi, jPhi, n, srdpTag(id, "srdp_zero_idx"));
        Value *dst = emitDenseGep(jBody, cp, idx, zero, srdpTag(id, "srdp_zero_dst"));
        jBody->addInstruction(own(new StoreInst(zero, dst)));

        auto *jInc = new BinaryOperator(Opcode::Add, jPhi, one, srdpTag(id, "srdp_zero_jinc"));
        jBody->addInstruction(own(jInc));
        jPhi->addIncoming(jInc, jBody);
        jBody->addInstruction(own(new BranchInst(jHeader)));
        wireEdge(jBody, jHeader);

        auto *iInc = new BinaryOperator(Opcode::Add, iPhi, one, srdpTag(id, "srdp_zero_iinc"));
        iLatch->addInstruction(own(iInc));
        iPhi->addIncoming(iInc, iLatch);
        iLatch->addInstruction(own(new BranchInst(iHeader)));
        wireEdge(iLatch, iHeader);
    }

    static void emitPointerWalkKernel(BasicBlock *entry, BasicBlock *exit, ScaledRowUpdateNest &pat,
                                      Value *n, Value *nBytes, Value *zero, Value *one,
                                      Value *fourBytes, Value *ap, Value *inBuf, Value *outBuf,
                                      unsigned id)
    {
        Function *func = entry->Parent;
        BasicBlock *kHeader = func->addBasicBlock(srdpTag(id, "srdp_kern_k"));
        BasicBlock *kBody = func->addBasicBlock(srdpTag(id, "srdp_kern_k_body"));
        BasicBlock *iHeader = func->addBasicBlock(srdpTag(id, "srdp_kern_i"));
        BasicBlock *iBody = func->addBasicBlock(srdpTag(id, "srdp_kern_i_body"));
        BasicBlock *skipTarget = func->addBasicBlock(srdpTag(id, "srdp_kern_i_skip"));
        BasicBlock *jHeader = func->addBasicBlock(srdpTag(id, "srdp_kern_j"));
        BasicBlock *jBody = func->addBasicBlock(srdpTag(id, "srdp_kern_j_body"));
        BasicBlock *iLatch = func->addBasicBlock(srdpTag(id, "srdp_kern_i_latch"));
        BasicBlock *kLatch = func->addBasicBlock(srdpTag(id, "srdp_kern_k_latch"));

        Value *apPtr = emitI32Ptr(entry, ap, srdpTag(id, "srdp_kern_ap_ptr"));
        Value *inPtr = emitI32Ptr(entry, inBuf, srdpTag(id, "srdp_kern_in_ptr"));
        Value *outPtr = emitI32Ptr(entry, outBuf, srdpTag(id, "srdp_kern_out_ptr"));
        entry->addInstruction(own(new BranchInst(kHeader)));
        wireEdge(entry, kHeader);

        auto *kPhi = new PhiInst(IntegerType::getInstance(), srdpTag(id, "srdp_kern_kiv"));
        kPhi->addIncoming(zero, entry);
        kHeader->addInstruction(own(kPhi));
        auto *kCmp = new ICmpInst(ICmpInst::ICMP_SLT, kPhi, pat.bound, srdpTag(id, "srdp_kern_kcmp"));
        kHeader->addInstruction(own(kCmp));
        kHeader->addInstruction(own(new BranchInst(kCmp, kBody, exit)));
        wireEdge(kHeader, kBody);
        wireEdge(kHeader, exit);

        Value *kRowIdx = emitFlatIndex(kBody, kPhi, zero, n, srdpTag(id, "srdp_kern_krow"));
        Value *kRowBytes = emitIndexBytes(kBody, kRowIdx, srdpTag(id, "srdp_kern_krow_b"));
        Value *aColStart = emitPtrAddBytes(kBody, apPtr, kRowBytes, srdpTag(id, "srdp_kern_acol"));
        Value *bRowStart = emitPtrAddBytes(kBody, inPtr, kRowBytes, srdpTag(id, "srdp_kern_brow"));

        kBody->addInstruction(own(new BranchInst(iHeader)));
        wireEdge(kBody, iHeader);

        auto *iPhi = new PhiInst(IntegerType::getInstance(), srdpTag(id, "srdp_kern_iiv"));
        iPhi->addIncoming(zero, kBody);
        auto *aColPhi = new PhiInst(PointerType::getInstance(IntegerType::getInstance()),
                                    srdpTag(id, "srdp_kern_acol_phi"));
        aColPhi->addIncoming(aColStart, kBody);
        iHeader->addInstruction(own(iPhi));
        iHeader->addInstruction(own(aColPhi));
        auto *iCmp = new ICmpInst(ICmpInst::ICMP_SLT, iPhi, pat.bound, srdpTag(id, "srdp_kern_icmp"));
        iHeader->addInstruction(own(iCmp));
        iHeader->addInstruction(own(new BranchInst(iCmp, iBody, kLatch)));
        wireEdge(iHeader, iBody);
        wireEdge(iHeader, kLatch);

        auto *aVal = new LoadInst(aColPhi, srdpTag(id, "srdp_kern_a_val"));
        iBody->addInstruction(own(aVal));

        Value *iRowIdx = emitFlatIndex(iBody, iPhi, zero, n, srdpTag(id, "srdp_kern_irow"));
        Value *iRowBytes = emitIndexBytes(iBody, iRowIdx, srdpTag(id, "srdp_kern_irow_b"));
        Value *cRowStart = emitPtrAddBytes(iBody, outPtr, iRowBytes, srdpTag(id, "srdp_kern_crow"));

        if (pat.hasSkipGuard)
        {
            auto *skipCmp = new ICmpInst(ICmpInst::ICMP_EQ, aVal, one, srdpTag(id, "srdp_kern_skip"));
            iBody->addInstruction(own(skipCmp));
            iBody->addInstruction(own(new BranchInst(skipCmp, skipTarget, jHeader)));
            wireEdge(iBody, skipTarget);
            wireEdge(iBody, jHeader);
        }
        else
        {
            iBody->addInstruction(own(new BranchInst(jHeader)));
            wireEdge(iBody, jHeader);
        }

        auto *jPhi = new PhiInst(IntegerType::getInstance(), srdpTag(id, "srdp_kern_jiv"));
        jPhi->addIncoming(zero, iBody);
        auto *bPtrPhi = new PhiInst(PointerType::getInstance(IntegerType::getInstance()),
                                    srdpTag(id, "srdp_kern_bptr"));
        bPtrPhi->addIncoming(bRowStart, iBody);
        auto *cPtrPhi = new PhiInst(PointerType::getInstance(IntegerType::getInstance()),
                                    srdpTag(id, "srdp_kern_cptr"));
        cPtrPhi->addIncoming(cRowStart, iBody);
        jHeader->addInstruction(own(jPhi));
        jHeader->addInstruction(own(bPtrPhi));
        jHeader->addInstruction(own(cPtrPhi));
        auto *jCmp = new ICmpInst(ICmpInst::ICMP_SLT, jPhi, pat.bound, srdpTag(id, "srdp_kern_jcmp"));
        jHeader->addInstruction(own(jCmp));
        jHeader->addInstruction(own(new BranchInst(jCmp, jBody, iLatch)));
        wireEdge(jHeader, jBody);
        wireEdge(jHeader, iLatch);

        auto *cLoad = new LoadInst(cPtrPhi, srdpTag(id, "srdp_kern_c_load"));
        jBody->addInstruction(own(cLoad));
        auto *prod = new BinaryOperator(Opcode::Mul, cLoad, aVal, srdpTag(id, "srdp_kern_prod"));
        jBody->addInstruction(own(prod));
        auto *bLoad = new LoadInst(bPtrPhi, srdpTag(id, "srdp_kern_b_load"));
        jBody->addInstruction(own(bLoad));
        auto *sum = new BinaryOperator(Opcode::Add, prod, bLoad, srdpTag(id, "srdp_kern_sum"));
        jBody->addInstruction(own(sum));
        jBody->addInstruction(own(new StoreInst(sum, cPtrPhi)));

        Value *bNext = emitPtrAddBytes(jBody, bPtrPhi, fourBytes, srdpTag(id, "srdp_kern_bnext"));
        Value *cNext = emitPtrAddBytes(jBody, cPtrPhi, fourBytes, srdpTag(id, "srdp_kern_cnext"));
        auto *jInc = new BinaryOperator(Opcode::Add, jPhi, one, srdpTag(id, "srdp_kern_jinc"));
        jBody->addInstruction(own(jInc));
        bPtrPhi->addIncoming(bNext, jBody);
        cPtrPhi->addIncoming(cNext, jBody);
        jPhi->addIncoming(jInc, jBody);
        jBody->addInstruction(own(new BranchInst(jHeader)));
        wireEdge(jBody, jHeader);

        Value *aColNext = emitPtrAddBytes(iLatch, aColPhi, fourBytes, srdpTag(id, "srdp_kern_acol_next"));
        auto *iInc = new BinaryOperator(Opcode::Add, iPhi, one, srdpTag(id, "srdp_kern_iinc"));
        iLatch->addInstruction(own(iInc));
        aColPhi->addIncoming(aColNext, iLatch);
        iPhi->addIncoming(iInc, iLatch);
        iLatch->addInstruction(own(new BranchInst(iHeader)));
        wireEdge(iLatch, iHeader);

        if (pat.hasSkipGuard)
        {
            Value *aColSkip = emitPtrAddBytes(skipTarget, aColPhi, fourBytes, srdpTag(id, "srdp_kern_acol_skip"));
            auto *skipInc = new BinaryOperator(Opcode::Add, iPhi, one, srdpTag(id, "srdp_kern_skip_inc"));
            skipTarget->addInstruction(own(skipInc));
            aColPhi->addIncoming(aColSkip, skipTarget);
            iPhi->addIncoming(skipInc, skipTarget);
            skipTarget->addInstruction(own(new BranchInst(iHeader)));
            wireEdge(skipTarget, iHeader);
        }

        auto *kInc = new BinaryOperator(Opcode::Add, kPhi, one, srdpTag(id, "srdp_kern_kinc"));
        kLatch->addInstruction(own(kInc));
        kPhi->addIncoming(kInc, kLatch);
        kLatch->addInstruction(own(new BranchInst(kHeader)));
        wireEdge(kLatch, kHeader);
    }

    static void emitUnpackDense(BasicBlock *entry, BasicBlock *exit, Value *bound, Value *n, Value *zero,
                                Value *one, Value *cp, Value *cArray, unsigned id)
    {
        Function *func = entry->Parent;
        BasicBlock *iHeader = func->addBasicBlock(srdpTag(id, "srdp_unpack_i"));
        BasicBlock *iBody = func->addBasicBlock(srdpTag(id, "srdp_unpack_i_body"));
        BasicBlock *jHeader = func->addBasicBlock(srdpTag(id, "srdp_unpack_j"));
        BasicBlock *jBody = func->addBasicBlock(srdpTag(id, "srdp_unpack_j_body"));
        BasicBlock *iLatch = func->addBasicBlock(srdpTag(id, "srdp_unpack_i_latch"));

        entry->addInstruction(own(new BranchInst(iHeader)));
        wireEdge(entry, iHeader);

        auto *iPhi = new PhiInst(IntegerType::getInstance(), srdpTag(id, "srdp_unpack_iiv"));
        iPhi->addIncoming(zero, entry);
        iHeader->addInstruction(own(iPhi));
        auto *iCmp = new ICmpInst(ICmpInst::ICMP_SLT, iPhi, bound, srdpTag(id, "srdp_unpack_icmp"));
        iHeader->addInstruction(own(iCmp));
        iHeader->addInstruction(own(new BranchInst(iCmp, iBody, exit)));
        wireEdge(iHeader, iBody);
        wireEdge(iHeader, exit);

        iBody->addInstruction(own(new BranchInst(jHeader)));
        wireEdge(iBody, jHeader);

        auto *jPhi = new PhiInst(IntegerType::getInstance(), srdpTag(id, "srdp_unpack_jiv"));
        jPhi->addIncoming(zero, iBody);
        jHeader->addInstruction(own(jPhi));
        auto *jCmp = new ICmpInst(ICmpInst::ICMP_SLT, jPhi, bound, srdpTag(id, "srdp_unpack_jcmp"));
        jHeader->addInstruction(own(jCmp));
        jHeader->addInstruction(own(new BranchInst(jCmp, jBody, iLatch)));
        wireEdge(jHeader, jBody);
        wireEdge(jHeader, iLatch);

        Value *idx = emitFlatIndex(jBody, iPhi, jPhi, n, srdpTag(id, "srdp_unpack_idx"));
        Value *src = emitDenseGep(jBody, cp, idx, zero, srdpTag(id, "srdp_unpack_src"));
        auto *val = new LoadInst(src, srdpTag(id, "srdp_unpack_val"));
        jBody->addInstruction(own(val));
        auto *cGep = new GetElementPtrInst(cArray, {iPhi, jPhi}, srdpTag(id, "srdp_unpack_dst"));
        jBody->addInstruction(own(cGep));
        jBody->addInstruction(own(new StoreInst(val, cGep)));

        auto *jInc = new BinaryOperator(Opcode::Add, jPhi, one, srdpTag(id, "srdp_unpack_jinc"));
        jBody->addInstruction(own(jInc));
        jPhi->addIncoming(jInc, jBody);
        jBody->addInstruction(own(new BranchInst(jHeader)));
        wireEdge(jBody, jHeader);

        auto *iInc = new BinaryOperator(Opcode::Add, iPhi, one, srdpTag(id, "srdp_unpack_iinc"));
        iLatch->addInstruction(own(iInc));
        iPhi->addIncoming(iInc, iLatch);
        iLatch->addInstruction(own(new BranchInst(iHeader)));
        wireEdge(iLatch, iHeader);
    }
} // namespace

GlobalVariable *ScaledRowDensePackPass::getOrCreateDenseBuffer(Function *func, const string &name)
{
    Module *module = func->getParent();
    if (GlobalVariable *existing = module->getGlobalVariable(name))
        return existing;
    auto *arrTy = ArrayType::getInstance(IntegerType::getInstance(), kDenseCapacity);
    return module->addGlobalVariable(arrTy, name, nullptr, false);
}

bool ScaledRowDensePackPass::applyFunctionGroup(Function *func, vector<ScaledRowUpdateNest> &nests)
{
    if (nests.empty())
        return false;

    sort(nests.begin(), nests.end(),
         [&](const ScaledRowUpdateNest &a, const ScaledRowUpdateNest &b) {
             return basicBlockIndex(func, a.kHeader) < basicBlockIndex(func, b.kHeader);
         });

    ScaledRowUpdateNest &first = nests.front();
    ScaledRowUpdateNest &last = nests.back();
    if (!first.valid || !first.kLoop || !first.bound)
        return false;

    BasicBlock *firstPreheader = findLoopPreheader(*first.kLoop);
    BasicBlock *lastExit = getLoopExit(*last.kLoop);
    if (!firstPreheader || !lastExit)
        return false;

    const unsigned id = ++gNestSerial;
    auto *zero = ci(0);
    auto *one = ci(1);
    auto *fourBytes = ci(4);
    Value *n = first.bound;

    GlobalVariable *ap = getOrCreateDenseBuffer(func, "srdp_ap");
    GlobalVariable *xp = getOrCreateDenseBuffer(func, "srdp_x");
    GlobalVariable *yp = getOrCreateDenseBuffer(func, "srdp_y");

    BasicBlock *packAEntry = func->addBasicBlock(srdpTag(id, "srdp_pack_a_entry"));
    BasicBlock *packBEntry = func->addBasicBlock(srdpTag(id, "srdp_pack_b_entry"));

    vector<BasicBlock *> zeroEntries;
    vector<BasicBlock *> kernEntries;
    vector<BasicBlock *> stageExits;
    zeroEntries.reserve(nests.size());
    kernEntries.reserve(nests.size());
    stageExits.reserve(nests.size());
    for (unsigned i = 0; i < nests.size(); ++i)
    {
        zeroEntries.push_back(func->addBasicBlock(srdpTag(id, "srdp_zero_entry_" + to_string(i))));
        kernEntries.push_back(func->addBasicBlock(srdpTag(id, "srdp_kern_entry_" + to_string(i))));
        stageExits.push_back(func->addBasicBlock(srdpTag(id, "srdp_stage_exit_" + to_string(i))));
    }

    BasicBlock *unpackEntry = func->addBasicBlock(srdpTag(id, "srdp_unpack_entry"));
    BasicBlock *done = func->addBasicBlock(srdpTag(id, "srdp_done"));

    replaceTerminator(firstPreheader, own(new BranchInst(packAEntry)));
    wireEdge(firstPreheader, packAEntry);

    emitPackAColMajor(packAEntry, packBEntry, first.bound, n, zero, one, ap, first.aArray, id);
    emitPackBRowMajor(packBEntry, zeroEntries.front(), first.bound, n, zero, one, xp, first.bArray,
                      id);

    bool useXAsIn = true;
    Value *lastOutBuf = yp;

    for (unsigned i = 0; i < nests.size(); ++i)
    {
        ScaledRowUpdateNest &pat = nests[i];
        Value *inBuf = useXAsIn ? xp : yp;
        Value *outBuf = useXAsIn ? yp : xp;
        lastOutBuf = outBuf;
        const unsigned stageId = id * 10 + i;

        emitZeroDense(zeroEntries[i], kernEntries[i], pat.bound, n, zero, one, outBuf, stageId);

        auto *nBytes =
            new BinaryOperator(Opcode::Sll, n, ci(2), srdpTag(stageId, "srdp_nbytes"));
        kernEntries[i]->addInstruction(own(nBytes));
        emitPointerWalkKernel(kernEntries[i], stageExits[i], pat, n, nBytes, zero, one, fourBytes, ap,
                              inBuf, outBuf, stageId);

        BasicBlock *next = (i + 1 < nests.size()) ? zeroEntries[i + 1] : unpackEntry;
        stageExits[i]->addInstruction(own(new BranchInst(next)));
        wireEdge(stageExits[i], next);

        useXAsIn = !useXAsIn;
    }

    emitUnpackDense(unpackEntry, done, last.bound, n, zero, one, lastOutBuf, last.cArray, id);

    done->addInstruction(own(new BranchInst(lastExit)));
    wireEdge(done, lastExit);

    for (ScaledRowUpdateNest &pat : nests)
        removeScaledRowNest(pat, done);

    eraseUnreachableBlocks(func, this);
    return true;
}

bool ScaledRowDensePackPass::runOnFunction(Function *func)
{
    const MatrixFunctionAnalysis *analysis = getAnalysis(func);
    if (!analysis || analysis->scaledRowUpdateNests.empty())
        return false;

    vector<ScaledRowUpdateNest> validNests;
    for (const ScaledRowUpdateNest &pat : analysis->scaledRowUpdateNests)
    {
        if (pat.valid)
            validNests.push_back(pat);
    }
    if (validNests.empty())
        return false;

    sort(validNests.begin(), validNests.end(),
         [](const ScaledRowUpdateNest &a, const ScaledRowUpdateNest &b) {
             return a.aArray->getName() < b.aArray->getName();
         });

    bool changed = false;
    vector<ScaledRowUpdateNest> group;
    Value *groupA = nullptr;
    auto flushGroup = [&]() {
        if (group.empty())
            return;
        if (applyFunctionGroup(func, group))
        {
            changed = true;
            if (verbose)
            {
                debugInfo << "ScaledRowDensePack: packed " << group.size() << " nest(s) sharing @"
                          << groupA->getName() << " in " << func->getName() << "\n";
            }
        }
        group.clear();
        groupA = nullptr;
    };

    for (ScaledRowUpdateNest &pat : validNests)
    {
        if (!groupA || sameArray(groupA, pat.aArray))
        {
            group.push_back(pat);
            groupA = pat.aArray;
        }
        else
        {
            flushGroup();
            group.push_back(pat);
            groupA = pat.aArray;
        }
    }
    flushGroup();

    if (changed)
        func->setLoops(ControlFlowAnalysis::findLoops(func));
    return changed;
}
