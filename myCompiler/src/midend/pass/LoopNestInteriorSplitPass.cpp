#include "LoopNestInteriorSplitPass.h"
#include <algorithm>
using namespace std;
using namespace optimization;

namespace
{
    static ConstantInt *ci(int v)
    {
        return new ConstantInt(IntegerType::getInstance(), v);
    }

    static unique_ptr<Instruction> own(Instruction *inst)
    {
        return unique_ptr<Instruction>(inst);
    }

    static Value *stripCopy(Value *v)
    {
        while (auto *cpy = dynamic_cast<CopyInst *>(v))
            v = cpy->getSource();
        return v;
    }

    static bool sameValue(Value *a, Value *b)
    {
        if (!a || !b)
            return false;
        return stripCopy(a) == stripCopy(b);
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
            for (auto *succ : bb->getSuccessors())
                succ->removePredecessor(bb);
            while (!bb->getSuccessors().empty())
                bb->removeSuccessor(bb->getSuccessors().back());
        }
        bb->addInstruction(std::move(term));
    }

    static BranchInst *getTerminator(BasicBlock *bb)
    {
        if (!bb || bb->getInstructions().empty())
            return nullptr;
        return dynamic_cast<BranchInst *>(bb->getInstructions().back().get());
    }

    static ICmpInst *getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound)
    {
        iv = nullptr;
        bound = nullptr;
        if (!header)
            return nullptr;
        for (auto &instPtr : header->getInstructions())
        {
            auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get());
            if (!icmp || icmp->getPredicate() != ICmpInst::ICMP_SLT)
                continue;
            iv = icmp->getLHS();
            bound = icmp->getRHS();
            return icmp;
        }
        return nullptr;
    }

    static int getConstantBound(Value *bound)
    {
        if (auto *c = dynamic_cast<ConstantInt *>(stripCopy(bound)))
            return c->Value;
        return -1;
    }

    static const Loop *findParentLoop(const Loop &inner, const vector<Loop> &loops)
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

    static BasicBlock *getLoopLatch(const Loop &loop)
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

    static BasicBlock *getLoopExit(const Loop &loop)
    {
        for (auto *succ : loop.header->getSuccessors())
        {
            if (!loop.containsBlock(succ))
                return succ;
        }
        return nullptr;
    }

    static BasicBlock *getTrueBody(BasicBlock *header, const Loop &loop)
    {
        auto *br = getTerminator(header);
        if (!br || !br->isConditional())
            return nullptr;
        BasicBlock *body = br->getTrueBlock();
        if (!body || !loop.containsBlock(body))
            return nullptr;
        return body;
    }

    static bool isLoadedGlobalBound(Value *bound)
    {
        bound = stripCopy(bound);
        if (auto *load = dynamic_cast<LoadInst *>(bound))
            return dynamic_cast<GlobalVariable *>(load->getPointer()) != nullptr;
        return false;
    }

    static bool computePadSub(const Loop &krLoop, int &padOut)
    {
        for (BasicBlock *bb : krLoop.blocks)
        {
            for (auto &instPtr : bb->getInstructions())
            {
                auto *sub = dynamic_cast<BinaryOperator *>(instPtr.get());
                if (!sub || sub->getOpcode() != Opcode::Sub)
                    continue;
                if (auto *padC = dynamic_cast<ConstantInt *>(stripCopy(sub->getRHS())))
                {
                    if (padC->Value == 2)
                    {
                        padOut = padC->Value;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    static Value *stripArrayBase(Value *v)
    {
        while (v)
        {
            if (auto *arg = dynamic_cast<Argument *>(v))
                return v;
            if (auto *gv = dynamic_cast<GlobalVariable *>(v))
                return v;
            if (auto *gep = dynamic_cast<GetElementPtrInst *>(v))
            {
                v = gep->getPointerOperand();
                continue;
            }
            if (auto *cpy = dynamic_cast<CopyInst *>(v))
            {
                v = cpy->getSource();
                continue;
            }
            break;
        }
        return nullptr;
    }

    static bool exprReferences(Value *expr, Value *target)
    {
        if (!expr || !target)
            return false;
        expr = stripCopy(expr);
        if (sameValue(expr, target))
            return true;
        if (auto *bin = dynamic_cast<BinaryOperator *>(expr))
            return exprReferences(bin->getLHS(), target) || exprReferences(bin->getRHS(), target);
        return false;
    }

    static Value *linearIndexFromPointer(Value *ptr)
    {
        auto *gep = dynamic_cast<GetElementPtrInst *>(ptr);
        if (!gep)
            return nullptr;
        auto indices = gep->getIndices();
        if (indices.empty())
            return nullptr;
        return stripCopy(indices.back());
    }

    static bool indexUsesKernelStride(Value *idx, Value *krIV, int kSize)
    {
        if (!idx)
            return false;
        idx = stripCopy(idx);
        if (auto *mul = dynamic_cast<BinaryOperator *>(idx))
        {
            if (mul->getOpcode() == Opcode::Mul)
            {
                auto *lhs = stripCopy(mul->getLHS());
                auto *rhs = stripCopy(mul->getRHS());
                auto matches = [&](Value *a, Value *b) {
                    if (!exprReferences(a, krIV))
                        return false;
                    if (auto *c = dynamic_cast<ConstantInt *>(b))
                        return c->Value == kSize;
                    return false;
                };
                if (matches(lhs, rhs) || matches(rhs, lhs))
                    return true;
            }
            return indexUsesKernelStride(mul->getLHS(), krIV, kSize) ||
                   indexUsesKernelStride(mul->getRHS(), krIV, kSize);
        }
        if (auto *bin = dynamic_cast<BinaryOperator *>(idx))
            return indexUsesKernelStride(bin->getLHS(), krIV, kSize) ||
                   indexUsesKernelStride(bin->getRHS(), krIV, kSize);
        return false;
    }

    static bool indexUsesRowStride(Value *idx, Value *stride)
    {
        if (!idx || !stride)
            return false;
        idx = stripCopy(idx);
        if (auto *mul = dynamic_cast<BinaryOperator *>(idx))
        {
            if (mul->getOpcode() == Opcode::Mul &&
                (sameValue(stripCopy(mul->getLHS()), stride) ||
                 sameValue(stripCopy(mul->getRHS()), stride) ||
                 exprReferences(mul->getLHS(), stride) || exprReferences(mul->getRHS(), stride)))
                return true;
            return indexUsesRowStride(mul->getLHS(), stride) ||
                   indexUsesRowStride(mul->getRHS(), stride);
        }
        if (auto *bin = dynamic_cast<BinaryOperator *>(idx))
            return indexUsesRowStride(bin->getLHS(), stride) ||
                   indexUsesRowStride(bin->getRHS(), stride);
        return false;
    }

    static void collectLoopLoads(const Loop &loop, vector<pair<Value *, Value *>> &sites)
    {
        for (BasicBlock *bb : loop.blocks)
        {
            if (!bb)
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                auto *load = dynamic_cast<LoadInst *>(instPtr.get());
                if (!load)
                    continue;
                Value *base = stripArrayBase(load->getPointer());
                Value *idx = linearIndexFromPointer(load->getPointer());
                if (base && idx)
                    sites.emplace_back(base, idx);
            }
        }
    }

    static bool identifyKernelArrays(const Loop &krLoop, const Loop &kcLoop, Value *krIV,
                                     Value *kcIV, Value *stride, int kSize, Value *&inputArray,
                                     Value *&weightArray)
    {
        inputArray = nullptr;
        weightArray = nullptr;
        vector<pair<Value *, Value *>> sites;
        collectLoopLoads(krLoop, sites);
        collectLoopLoads(kcLoop, sites);
        for (const auto &[base, idx] : sites)
        {
            if (indexUsesKernelStride(idx, krIV, kSize))
                weightArray = base;
            if (indexUsesRowStride(idx, stride))
                inputArray = base;
        }
        return inputArray && weightArray && inputArray != weightArray;
    }

    static bool identifyOutputArray(const vector<BasicBlock *> &blocks, Value *rIV, Value *cIV,
                                    Value *stride, Value *inputArray, Value *weightArray,
                                    Value *&outputArray, BasicBlock *&storeBB)
    {
        outputArray = nullptr;
        storeBB = nullptr;
        for (BasicBlock *bb : blocks)
        {
            if (!bb)
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                auto *st = dynamic_cast<StoreInst *>(instPtr.get());
                if (!st)
                    continue;
                Value *base = stripArrayBase(st->getPointer());
                Value *idx = linearIndexFromPointer(st->getPointer());
                if (!base || !idx || base == inputArray || base == weightArray)
                    continue;
                if (!exprReferences(idx, rIV) || !exprReferences(idx, cIV))
                    continue;
                if (!indexUsesRowStride(idx, stride))
                    continue;
                outputArray = base;
                storeBB = bb;
                return true;
            }
        }
        return false;
    }

    static bool blockLoadsFromArray(BasicBlock *bb, Value *arrayBase, LoadInst *&outLoad)
    {
        outLoad = nullptr;
        if (!bb || !arrayBase)
            return false;
        for (auto &instPtr : bb->getInstructions())
        {
            auto *load = dynamic_cast<LoadInst *>(instPtr.get());
            if (!load)
                continue;
            auto *gep = dynamic_cast<GetElementPtrInst *>(load->getPointer());
            if (!gep)
                continue;
            if (sameValue(gep->getPointerOperand(), arrayBase))
            {
                outLoad = load;
                return true;
            }
        }
        return false;
    }

    static bool findGuardedAccumulate(const Loop &searchLoop, Value *inArray, Value *kArray,
                                       BasicBlock *&guardEntry, BasicBlock *&thenBB,
                                       BasicBlock *&mergeBB)
    {
        guardEntry = nullptr;
        thenBB = nullptr;
        mergeBB = nullptr;
        for (BasicBlock *bb : searchLoop.blocks)
        {
            if (bb == searchLoop.header)
                continue;
            LoadInst *inLoad = nullptr;
            LoadInst *kLoad = nullptr;
            if (!blockLoadsFromArray(bb, inArray, inLoad))
                continue;
            if (!blockLoadsFromArray(bb, kArray, kLoad))
                continue;

            bool hasMul = false;
            bool hasAdd = false;
            for (auto &instPtr : bb->getInstructions())
            {
                if (auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get()))
                {
                    if (bin->getOpcode() == Opcode::Mul)
                        hasMul = true;
                    if (bin->getOpcode() == Opcode::Add)
                        hasAdd = true;
                }
            }
            if (!hasMul || !hasAdd)
                continue;
            thenBB = bb;
            break;
        }
        if (!thenBB)
            return false;

        for (BasicBlock *bb : searchLoop.blocks)
        {
            if (bb == searchLoop.header || bb == thenBB)
                continue;
            auto *br = getTerminator(bb);
            if (!br || !br->isConditional())
                continue;
            if (br->getTrueBlock() == thenBB || br->getFalseBlock() == thenBB)
            {
                guardEntry = bb;
                mergeBB = br->getTrueBlock() == thenBB ? br->getFalseBlock() : br->getTrueBlock();
                break;
            }
        }
        if (!guardEntry)
        {
            for (BasicBlock *bb : searchLoop.blocks)
            {
                if (bb == searchLoop.header || bb == thenBB)
                    continue;
                for (auto *succ : bb->getSuccessors())
                {
                    if (succ == thenBB)
                    {
                        guardEntry = bb;
                        break;
                    }
                }
                if (guardEntry)
                    break;
            }
        }
        if (!mergeBB)
        {
            for (auto *succ : thenBB->getSuccessors())
            {
                if (searchLoop.containsBlock(succ))
                {
                    mergeBB = succ;
                    break;
                }
            }
        }
        return thenBB != nullptr;
    }

    static bool hasKernelAccumulate(const Loop &searchLoop, Value *inArray, Value *kArray)
    {
        BasicBlock *guardEntry = nullptr;
        BasicBlock *thenBB = nullptr;
        BasicBlock *mergeBB = nullptr;
        if (findGuardedAccumulate(searchLoop, inArray, kArray, guardEntry, thenBB, mergeBB))
            return true;
        for (BasicBlock *bb : searchLoop.blocks)
        {
            LoadInst *inLoad = nullptr;
            LoadInst *kLoad = nullptr;
            if (!blockLoadsFromArray(bb, inArray, inLoad))
                continue;
            if (!blockLoadsFromArray(bb, kArray, kLoad))
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                if (auto *mul = dynamic_cast<BinaryOperator *>(instPtr.get()))
                {
                    if (mul->getOpcode() == Opcode::Mul)
                        return true;
                }
            }
        }
        return false;
    }

    static Value *buildInteriorCond(BasicBlock *bb, Value *rIV, Value *cIV, Value *nEff,
                                    const string &tag)
    {
        auto *two = ci(2);
        auto *nMinus2 = new BinaryOperator(Opcode::Sub, nEff, two, tag + "_n_m2");
        bb->addInstruction(own(nMinus2));

        auto *rGe = new ICmpInst(ICmpInst::ICMP_SGE, rIV, two, tag + "_r_ge");
        bb->addInstruction(own(rGe));
        auto *rLt = new ICmpInst(ICmpInst::ICMP_SLT, rIV, nMinus2, tag + "_r_lt");
        bb->addInstruction(own(rLt));
        auto *cGe = new ICmpInst(ICmpInst::ICMP_SGE, cIV, two, tag + "_c_ge");
        bb->addInstruction(own(cGe));
        auto *cLt = new ICmpInst(ICmpInst::ICMP_SLT, cIV, nMinus2, tag + "_c_lt");
        bb->addInstruction(own(cLt));

        auto *t1 = new BinaryOperator(Opcode::And, rGe, rLt, tag + "_r_ok");
        bb->addInstruction(own(t1));
        auto *t2 = new BinaryOperator(Opcode::And, cGe, cLt, tag + "_c_ok");
        bb->addInstruction(own(t2));
        auto *all = new BinaryOperator(Opcode::And, t1, t2, tag + "_interior");
        bb->addInstruction(own(all));
        return all;
    }

    struct KernelNestResult
    {
        BasicBlock *entry = nullptr;
    };

    static KernelNestResult buildUnguardedKernelNest(Function *func, Value *rIV, Value *cIV,
                                                     Value *nEff, Value *inArray, Value *outArray,
                                                     Value *kArray, int pad, int kSize,
                                                     BasicBlock *kernelEntryPred,
                                                     BasicBlock *afterStoreTarget,
                                                     const string &tag)
    {
        KernelNestResult res;
        auto *i32 = IntegerType::getInstance();
        auto *zero = ci(0);
        auto *one = ci(1);
        auto *kBound = ci(kSize);
        auto *padC = ci(pad);

        BasicBlock *krHeader = func->addBasicBlock(tag + "_kr_hdr");
        BasicBlock *krBody = func->addBasicBlock(tag + "_kr_body");
        BasicBlock *kcHeader = func->addBasicBlock(tag + "_kc_hdr");
        BasicBlock *kcBody = func->addBasicBlock(tag + "_kc_body");
        BasicBlock *kcLatch = func->addBasicBlock(tag + "_kc_latch");
        BasicBlock *krLatch = func->addBasicBlock(tag + "_kr_latch");
        BasicBlock *storeBB = func->addBasicBlock(tag + "_store");

        res.entry = krHeader;

        auto *krPhi = new PhiInst(i32, tag + "_kr");
        krHeader->addInstruction(own(krPhi));
        auto *sumKrPhi = new PhiInst(i32, tag + "_sum_kr");
        krHeader->addInstruction(own(sumKrPhi));

        auto *krCmp = new ICmpInst(ICmpInst::ICMP_SLT, krPhi, kBound, tag + "_kr_cmp");
        krHeader->addInstruction(own(krCmp));
        krHeader->addInstruction(own(new BranchInst(krCmp, krBody, storeBB)));
        wireEdge(krHeader, krBody);
        wireEdge(krHeader, storeBB);

        auto *rPlusKr = new BinaryOperator(Opcode::Add, rIV, krPhi, tag + "_r_plus_kr");
        krBody->addInstruction(own(rPlusKr));
        auto *rr = new BinaryOperator(Opcode::Sub, rPlusKr, padC, tag + "_rr");
        krBody->addInstruction(own(rr));
        auto *rowBase = new BinaryOperator(Opcode::Mul, rr, nEff, tag + "_row_base");
        krBody->addInstruction(own(rowBase));
        krBody->addInstruction(own(new BranchInst(kcHeader)));
        wireEdge(krBody, kcHeader);

        auto *sumKcPhi = new PhiInst(i32, tag + "_sum_kc");
        kcHeader->addInstruction(own(sumKcPhi));
        auto *kcPhi = new PhiInst(i32, tag + "_kc");
        kcHeader->addInstruction(own(kcPhi));

        auto *kcCmp = new ICmpInst(ICmpInst::ICMP_SLT, kcPhi, kBound, tag + "_kc_cmp");
        kcHeader->addInstruction(own(kcCmp));
        kcHeader->addInstruction(own(new BranchInst(kcCmp, kcBody, krLatch)));
        wireEdge(kcHeader, kcBody);
        wireEdge(kcHeader, krLatch);

        auto *cPlusKc = new BinaryOperator(Opcode::Add, cIV, kcPhi, tag + "_c_plus_kc");
        kcBody->addInstruction(own(cPlusKc));
        auto *cc = new BinaryOperator(Opcode::Sub, cPlusKc, padC, tag + "_cc");
        kcBody->addInstruction(own(cc));
        auto *inIdx = new BinaryOperator(Opcode::Add, rowBase, cc, tag + "_src_idx");
        kcBody->addInstruction(own(inIdx));
        auto *inGep = new GetElementPtrInst(inArray, {inIdx}, tag + "_src_gep");
        kcBody->addInstruction(own(inGep));
        auto *inLoad = new LoadInst(inGep, tag + "_src_val");
        kcBody->addInstruction(own(inLoad));

        auto *kRow = new BinaryOperator(Opcode::Mul, krPhi, kBound, tag + "_coef_row");
        kcBody->addInstruction(own(kRow));
        auto *kIdx = new BinaryOperator(Opcode::Add, kRow, kcPhi, tag + "_coef_idx");
        kcBody->addInstruction(own(kIdx));
        auto *kGep = new GetElementPtrInst(kArray, {kIdx}, tag + "_coef_gep");
        kcBody->addInstruction(own(kGep));
        auto *kLoad = new LoadInst(kGep, tag + "_coef_val");
        kcBody->addInstruction(own(kLoad));

        auto *prod = new BinaryOperator(Opcode::Mul, inLoad, kLoad, tag + "_prod");
        kcBody->addInstruction(own(prod));
        auto *newSum = new BinaryOperator(Opcode::Add, sumKcPhi, prod, tag + "_acc");
        kcBody->addInstruction(own(newSum));
        kcBody->addInstruction(own(new BranchInst(kcLatch)));
        wireEdge(kcBody, kcLatch);

        auto *kcNext = new BinaryOperator(Opcode::Add, kcPhi, one, tag + "_kc_next");
        kcLatch->addInstruction(own(kcNext));
        kcLatch->addInstruction(own(new BranchInst(kcHeader)));
        wireEdge(kcLatch, kcHeader);

        auto *krNext = new BinaryOperator(Opcode::Add, krPhi, one, tag + "_kr_next");
        krLatch->addInstruction(own(krNext));
        krLatch->addInstruction(own(new BranchInst(krHeader)));
        wireEdge(krLatch, krHeader);

        auto *rMul = new BinaryOperator(Opcode::Mul, rIV, nEff, tag + "_dst_row");
        storeBB->addInstruction(own(rMul));
        auto *outIdx = new BinaryOperator(Opcode::Add, rMul, cIV, tag + "_dst_idx");
        storeBB->addInstruction(own(outIdx));
        auto *outGep = new GetElementPtrInst(outArray, {outIdx}, tag + "_dst_gep");
        storeBB->addInstruction(own(outGep));
        // 用 sumKrPhi 写回；kc 完全展开后 newSum 会被删掉，但 sumKcPhi 会 replaceAllUsesWith 末次累加值并更新 phi 操作数
        storeBB->addInstruction(own(new StoreInst(sumKrPhi, outGep)));
        storeBB->addInstruction(own(new BranchInst(afterStoreTarget)));
        wireEdge(storeBB, afterStoreTarget);

        krPhi->addIncoming(zero, kernelEntryPred);
        krPhi->addIncoming(krNext, krLatch);
        sumKrPhi->addIncoming(zero, kernelEntryPred);
        sumKrPhi->addIncoming(sumKcPhi, krLatch);
        sumKcPhi->addIncoming(sumKrPhi, krBody);
        sumKcPhi->addIncoming(newSum, kcLatch);
        kcPhi->addIncoming(zero, krBody);
        kcPhi->addIncoming(kcNext, kcLatch);

        return res;
    }

    static BinaryOperator *findIVIncrementInBlock(BasicBlock *bb, Value *iv)
    {
        if (!bb || !iv)
            return nullptr;
        for (auto &instPtr : bb->getInstructions())
        {
            auto *add = dynamic_cast<BinaryOperator *>(instPtr.get());
            if (!add || add->getOpcode() != Opcode::Add)
                continue;
            bool ivOnLhs = sameValue(add->getLHS(), iv);
            bool ivOnRhs = sameValue(add->getRHS(), iv);
            if (!ivOnLhs && !ivOnRhs)
                continue;
            Value *other = ivOnLhs ? add->getRHS() : add->getLHS();
            if (auto *step = dynamic_cast<ConstantInt *>(stripCopy(other)))
            {
                if (step->Value == 1)
                    return add;
            }
        }
        return nullptr;
    }

    static void finishBorderSkip(BasicBlock *skipBB, Value *cIV, BasicBlock *cHeader,
                                 BasicBlock *krExitStore, const string &tag)
    {
        auto *one = ci(1);
        BinaryOperator *templateInc = findIVIncrementInBlock(krExitStore, cIV);
        BinaryOperator *cNext = nullptr;
        if (templateInc)
        {
            cNext = new BinaryOperator(Opcode::Add, templateInc->getLHS(), templateInc->getRHS(),
                                       tag + "_skip_c_next");
        }
        else
        {
            cNext = new BinaryOperator(Opcode::Add, cIV, one, tag + "_skip_c_next");
        }
        skipBB->addInstruction(own(cNext));

        if (auto *phi = dynamic_cast<PhiInst *>(cIV))
        {
            phi->addIncoming(cNext, skipBB);
        }
        else
        {
            CopyInst *ivCopy = dynamic_cast<CopyInst *>(cIV);
            if (!ivCopy)
            {
                for (auto &instPtr : krExitStore->getInstructions())
                {
                    if (auto *cpy = dynamic_cast<CopyInst *>(instPtr.get()))
                    {
                        if (sameValue(cpy, cIV))
                        {
                            ivCopy = cpy;
                            break;
                        }
                    }
                }
            }
            if (ivCopy)
                skipBB->addInstruction(own(new CopyInst(cNext, ivCopy->getName())));
        }

        skipBB->addInstruction(own(new BranchInst(cHeader)));
        wireEdge(skipBB, cHeader);
    }
}

bool LoopNestInteriorSplitPass::analyzeKernelNest(Function *func, const vector<Loop> &loops,
                                                  KernelNestInfo &info)
{
    (void)func;
    for (const auto &kcLoop : loops)
    {
        Value *kcIV = nullptr;
        Value *kcBound = nullptr;
        if (!getHeaderBoundCmp(kcLoop.header, kcIV, kcBound))
            continue;
        if (getConstantBound(kcBound) != kKernelSize)
            continue;

        const Loop *krLoop = findParentLoop(kcLoop, loops);
        if (!krLoop)
            continue;
        Value *krIV = nullptr;
        Value *krBound = nullptr;
        if (!getHeaderBoundCmp(krLoop->header, krIV, krBound))
            continue;
        if (getConstantBound(krBound) != kKernelSize)
            continue;

        const Loop *cLoop = findParentLoop(*krLoop, loops);
        if (!cLoop)
            continue;
        Value *cIV = nullptr;
        Value *cBound = nullptr;
        if (!getHeaderBoundCmp(cLoop->header, cIV, cBound))
            continue;

        const Loop *rLoop = findParentLoop(*cLoop, loops);
        if (!rLoop)
            continue;
        Value *rIV = nullptr;
        Value *rBound = nullptr;
        if (!getHeaderBoundCmp(rLoop->header, rIV, rBound))
            continue;
        if (!sameValue(stripCopy(rBound), stripCopy(cBound)))
            continue;

        BasicBlock *krBody = getTrueBody(krLoop->header, *krLoop);
        if (!krBody)
            continue;
        int pad = -1;
        if (!computePadSub(*krLoop, pad))
            continue;

        Value *inArray = nullptr;
        Value *kArray = nullptr;
        if (!identifyKernelArrays(*krLoop, kcLoop, krIV, kcIV, cBound, kKernelSize, inArray,
                                  kArray))
            continue;

        if (!hasKernelAccumulate(*krLoop, inArray, kArray) &&
            !hasKernelAccumulate(kcLoop, inArray, kArray))
            continue;

        Value *outArray = nullptr;
        BasicBlock *krExit = nullptr;
        vector<BasicBlock *> outScanBlocks;
        for (BasicBlock *bb : krLoop->blocks)
            outScanBlocks.push_back(bb);
        for (BasicBlock *bb : krLoop->exits)
            outScanBlocks.push_back(bb);
        for (BasicBlock *bb : cLoop->blocks)
            outScanBlocks.push_back(bb);
        if (!identifyOutputArray(outScanBlocks, rIV, cIV, cBound, inArray, kArray, outArray,
                                 krExit))
            continue;

        BasicBlock *cBody = getTrueBody(cLoop->header, *cLoop);
        if (!cBody)
            continue;
        auto *cBodyBr = getTerminator(cBody);
        if (!cBodyBr || cBodyBr->isConditional())
            continue;
        BasicBlock *krHeader = cBodyBr->getTrueBlock();
        if (!krHeader)
            continue;

        const Loop *repeatLoop = findParentLoop(*rLoop, loops);
        BasicBlock *repeatBody = nullptr;
        if (repeatLoop)
        {
            Value *repIV = nullptr;
            Value *repBound = nullptr;
            if (getHeaderBoundCmp(repeatLoop->header, repIV, repBound) && isLoadedGlobalBound(repBound))
                repeatBody = getTrueBody(repeatLoop->header, *repeatLoop);
            else
                repeatLoop = nullptr;
        }

        info.kcLoop = &kcLoop;
        info.krLoop = krLoop;
        info.cLoop = cLoop;
        info.rLoop = rLoop;
        info.repeatLoop = repeatLoop;
        info.rIV = rIV;
        info.cIV = cIV;
        info.krIV = krIV;
        info.kcIV = kcIV;
        info.nEff = cBound;
        info.inArray = inArray;
        info.outArray = outArray;
        info.kArray = kArray;
        info.pad = pad;
        info.kSize = kKernelSize;
        info.rHeader = rLoop->header;
        info.cBody = cBody;
        info.krHeader = krHeader;
        info.krExitStore = krExit;
        info.repeatBody = repeatBody;
        (void)func;
        return true;
    }
    return false;
}

bool LoopNestInteriorSplitPass::applySplit(Function *func, KernelNestInfo &info, bool verbose,
                                           stringstream &dbg)
{
    if (info.pad != kPad || info.kSize != kKernelSize)
    {
        dbg << "LoopNestInteriorSplit: bad pad=" << info.pad << "\n";
        return false;
    }
    if (!info.cBody || !info.krHeader || !info.krExitStore || !info.rHeader)
    {
        dbg << "LoopNestInteriorSplit: missing blocks\n";
        return false;
    }

    BasicBlock *cHeader = info.cLoop->header;
    BasicBlock *entryFromRepeat = info.repeatBody ? info.repeatBody : nullptr;
    const string tag = "lnis";

    BasicBlock *intRHeader = func->addBasicBlock(tag + "_r_hdr");
    BasicBlock *intRBody = func->addBasicBlock(tag + "_r_body");
    BasicBlock *intRExit = func->addBasicBlock(tag + "_r_exit");
    BasicBlock *intRLatch = func->addBasicBlock(tag + "_r_latch");
    BasicBlock *intCHeader = func->addBasicBlock(tag + "_c_hdr");
    BasicBlock *intCExit = func->addBasicBlock(tag + "_c_exit");
    BasicBlock *intCLatch = func->addBasicBlock(tag + "_c_latch");

    auto *i32 = IntegerType::getInstance();
    auto *two = ci(2);
    auto *one = ci(1);

    auto *nMinus2 = new BinaryOperator(Opcode::Sub, info.nEff, two, tag + "_n_m2");
    intRHeader->addInstruction(own(nMinus2));

    auto *intRPhi = new PhiInst(i32, tag + "_r");
    intRHeader->addInstruction(own(intRPhi));

    auto *intRCmp = new ICmpInst(ICmpInst::ICMP_SLT, intRPhi, nMinus2, tag + "_r_cmp");
    intRHeader->addInstruction(own(intRCmp));
    intRHeader->addInstruction(own(new BranchInst(intRCmp, intRBody, intRExit)));
    wireEdge(intRHeader, intRBody);
    wireEdge(intRHeader, intRExit);

    intRBody->addInstruction(own(new BranchInst(intCHeader)));
    wireEdge(intRBody, intCHeader);

    auto *intCPhi = new PhiInst(i32, tag + "_c");
    intCHeader->addInstruction(own(intCPhi));

    auto *nMinus2c = new BinaryOperator(Opcode::Sub, info.nEff, two, tag + "_c_n_m2");
    intCHeader->addInstruction(own(nMinus2c));
    auto *intCCmp = new ICmpInst(ICmpInst::ICMP_SLT, intCPhi, nMinus2c, tag + "_c_cmp");
    intCHeader->addInstruction(own(intCCmp));

    KernelNestResult kernel = buildUnguardedKernelNest(
        func, intRPhi, intCPhi, info.nEff, info.inArray, info.outArray, info.kArray, info.pad,
        info.kSize, intCHeader, intCLatch, tag + "_kern");

    intCHeader->addInstruction(own(new BranchInst(intCCmp, kernel.entry, intCExit)));
    wireEdge(intCHeader, kernel.entry);
    wireEdge(intCHeader, intCExit);

    auto *intCNext = new BinaryOperator(Opcode::Add, intCPhi, one, tag + "_c_next");
    intCLatch->addInstruction(own(intCNext));
    intCLatch->addInstruction(own(new BranchInst(intCHeader)));
    wireEdge(intCLatch, intCHeader);
    intCPhi->addIncoming(two, intRBody);
    intCPhi->addIncoming(intCNext, intCLatch);

    intCExit->addInstruction(own(new BranchInst(intRLatch)));
    wireEdge(intCExit, intRLatch);

    auto *intRNext = new BinaryOperator(Opcode::Add, intRPhi, one, tag + "_r_next");
    intRLatch->addInstruction(own(intRNext));
    intRLatch->addInstruction(own(new BranchInst(intRHeader)));
    wireEdge(intRLatch, intRHeader);

    intRExit->addInstruction(own(new BranchInst(info.rHeader)));
    wireEdge(intRExit, info.rHeader);

    BasicBlock *intRInitPred = nullptr;
    if (entryFromRepeat)
    {
        replaceTerminator(entryFromRepeat, own(new BranchInst(intRHeader)));
        wireEdge(entryFromRepeat, intRHeader);
        intRInitPred = entryFromRepeat;
    }
    else
    {
        vector<BasicBlock *> rPreds = info.rHeader->getPredecessors();
        for (auto *pred : rPreds)
        {
            auto *br = getTerminator(pred);
            if (!br)
                continue;
            if (br->getTrueBlock() == info.rHeader)
                br->setTrueBlock(intRHeader);
            if (br->isConditional() && br->getFalseBlock() == info.rHeader)
                br->setFalseBlock(intRHeader);
            pred->removeSuccessor(info.rHeader);
            wireEdge(pred, intRHeader);
            if (!intRInitPred)
                intRInitPred = pred;
        }
    }
    if (intRInitPred)
        intRPhi->addIncoming(two, intRInitPred);
    intRPhi->addIncoming(intRNext, intRLatch);

    BasicBlock *cBorderDispatch = func->addBasicBlock(tag + "_border_dispatch");
    BasicBlock *cSkipLatch = func->addBasicBlock(tag + "_border_skip");

    replaceTerminator(info.cBody, own(new BranchInst(cBorderDispatch)));
    wireEdge(info.cBody, cBorderDispatch);

    Value *interiorCond =
        buildInteriorCond(cBorderDispatch, info.rIV, info.cIV, info.nEff, tag + "_bd");
    cBorderDispatch->addInstruction(
        own(new BranchInst(interiorCond, cSkipLatch, info.krHeader)));
    wireEdge(cBorderDispatch, cSkipLatch);
    wireEdge(cBorderDispatch, info.krHeader);

    finishBorderSkip(cSkipLatch, info.cIV, cHeader, info.krExitStore, tag);

    if (verbose)
    {
        dbg << "LoopNestInteriorSplit: interior nest inserted; border dispatch at "
            << info.cBody->getName() << "\n";
    }
    return true;
}

bool LoopNestInteriorSplitPass::runOnFunction(Function *func)
{
    func->setLoops(ControlFlowAnalysis::findLoops(func));
    KernelNestInfo info;
    if (!analyzeKernelNest(func, func->getLoops(), info))
    {
        if (verbose)
            debugInfo << "LoopNestInteriorSplit: no kernel nest found in " << func->getName()
                      << "\n";
        return false;
    }
    bool changed = applySplit(func, info, verbose, debugInfo);
    if (changed)
        func->setLoops(ControlFlowAnalysis::findLoops(func));
    return changed;
}
