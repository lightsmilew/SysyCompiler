#include "Conv2dInteriorSplitPass.h"
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

    static bool isRepeatBound(Value *bound)
    {
        bound = stripCopy(bound);
        if (auto *load = dynamic_cast<LoadInst *>(bound))
        {
            if (auto *gv = dynamic_cast<GlobalVariable *>(load->getPointer()))
                return gv->getName() == "repeat_factor";
        }
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

    static Value *resolveArrayBase(Value *v, const string &name)
    {
        if (!v)
            return nullptr;
        if (auto *arg = dynamic_cast<Argument *>(v))
        {
            if (arg->getName() == name)
                return v;
        }
        if (auto *gv = dynamic_cast<GlobalVariable *>(v))
        {
            if (gv->getName() == name)
                return v;
        }
        return nullptr;
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

    static bool matchGuardedAccumulate(const Loop &searchLoop, Value *inArray, Value *kArray,
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

    static bool detectConv2dAccumulate(const Loop &searchLoop, Value *inArray, Value *kArray)
    {
        BasicBlock *guardEntry = nullptr;
        BasicBlock *thenBB = nullptr;
        BasicBlock *mergeBB = nullptr;
        if (matchGuardedAccumulate(searchLoop, inArray, kArray, guardEntry, thenBB, mergeBB))
            return true;
        for (BasicBlock *bb : searchLoop.blocks)
        {
            for (auto &instPtr : bb->getInstructions())
            {
                if (auto *mul = dynamic_cast<BinaryOperator *>(instPtr.get()))
                {
                    if (mul->getOpcode() == Opcode::Mul &&
                        mul->getName().find("cga_scaled") != string::npos)
                        return true;
                }
            }
        }
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
        auto *inIdx = new BinaryOperator(Opcode::Add, rowBase, cc, tag + "_in_idx");
        kcBody->addInstruction(own(inIdx));
        auto *inGep = new GetElementPtrInst(inArray, {inIdx}, tag + "_in_gep");
        kcBody->addInstruction(own(inGep));
        auto *inLoad = new LoadInst(inGep, tag + "_in_val");
        kcBody->addInstruction(own(inLoad));

        auto *kRow = new BinaryOperator(Opcode::Mul, krPhi, kBound, tag + "_k_row");
        kcBody->addInstruction(own(kRow));
        auto *kIdx = new BinaryOperator(Opcode::Add, kRow, kcPhi, tag + "_k_idx");
        kcBody->addInstruction(own(kIdx));
        auto *kGep = new GetElementPtrInst(kArray, {kIdx}, tag + "_k_gep");
        kcBody->addInstruction(own(kGep));
        auto *kLoad = new LoadInst(kGep, tag + "_k_val");
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

        auto *rMul = new BinaryOperator(Opcode::Mul, rIV, nEff, tag + "_out_row");
        storeBB->addInstruction(own(rMul));
        auto *outIdx = new BinaryOperator(Opcode::Add, rMul, cIV, tag + "_out_idx");
        storeBB->addInstruction(own(outIdx));
        auto *outGep = new GetElementPtrInst(outArray, {outIdx}, tag + "_out_gep");
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

bool Conv2dInteriorSplitPass::matchConv2dNest(Function *func, const vector<Loop> &loops,
                                              Conv2dPattern &pat)
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
        auto scanInK = [&](BasicBlock *bb) {
            if (!bb)
                return;
            for (auto &instPtr : bb->getInstructions())
            {
                auto *load = dynamic_cast<LoadInst *>(instPtr.get());
                if (!load)
                    continue;
                auto *gep = dynamic_cast<GetElementPtrInst *>(load->getPointer());
                if (!gep)
                    continue;
                Value *base = gep->getPointerOperand();
                if (Value *in = resolveArrayBase(base, "In"))
                    inArray = in;
                if (Value *k = resolveArrayBase(base, "K"))
                    kArray = k;
            }
        };
        for (BasicBlock *bb : krLoop->blocks)
            scanInK(bb);
        for (BasicBlock *bb : kcLoop.blocks)
            scanInK(bb);
        if (!inArray || !kArray)
            continue;

        if (!detectConv2dAccumulate(*krLoop, inArray, kArray) &&
            !detectConv2dAccumulate(kcLoop, inArray, kArray))
            continue;

        Value *outArray = nullptr;
        BasicBlock *krExit = nullptr;
        auto scanOutStore = [&](BasicBlock *bb) {
            if (!bb || outArray)
                return;
            for (auto &instPtr : bb->getInstructions())
            {
                auto *st = dynamic_cast<StoreInst *>(instPtr.get());
                if (!st)
                    continue;
                auto *gep = dynamic_cast<GetElementPtrInst *>(st->getPointer());
                if (!gep)
                    continue;
                if (Value *out = resolveArrayBase(gep->getPointerOperand(), "Out"))
                {
                    outArray = out;
                    krExit = bb;
                }
            }
        };
        for (BasicBlock *bb : krLoop->blocks)
            scanOutStore(bb);
        for (BasicBlock *bb : krLoop->exits)
            scanOutStore(bb);
        for (BasicBlock *bb : cLoop->blocks)
            scanOutStore(bb);
        if (!outArray || !krExit)
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
            if (getHeaderBoundCmp(repeatLoop->header, repIV, repBound) && isRepeatBound(repBound))
                repeatBody = getTrueBody(repeatLoop->header, *repeatLoop);
            else
                repeatLoop = nullptr;
        }

        pat.kcLoop = &kcLoop;
        pat.krLoop = krLoop;
        pat.cLoop = cLoop;
        pat.rLoop = rLoop;
        pat.repeatLoop = repeatLoop;
        pat.rIV = rIV;
        pat.cIV = cIV;
        pat.krIV = krIV;
        pat.kcIV = kcIV;
        pat.nEff = cBound;
        pat.inArray = inArray;
        pat.outArray = outArray;
        pat.kArray = kArray;
        pat.pad = pad;
        pat.kSize = kKernelSize;
        pat.rHeader = rLoop->header;
        pat.cBody = cBody;
        pat.krHeader = krHeader;
        pat.krExitStore = krExit;
        pat.repeatBody = repeatBody;
        (void)func;
        return true;
    }
    return false;
}

bool Conv2dInteriorSplitPass::applySplit(Function *func, Conv2dPattern &pat, bool verbose,
                                         stringstream &dbg)
{
    if (pat.pad != kPad || pat.kSize != kKernelSize)
    {
        dbg << "Conv2dInteriorSplit: bad pad=" << pat.pad << "\n";
        return false;
    }
    if (!pat.cBody || !pat.krHeader || !pat.krExitStore || !pat.rHeader)
    {
        dbg << "Conv2dInteriorSplit: missing blocks\n";
        return false;
    }

    BasicBlock *cHeader = pat.cLoop->header;
    BasicBlock *entryFromRepeat = pat.repeatBody ? pat.repeatBody : nullptr;
    const string tag = "c2int";

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

    auto *nMinus2 = new BinaryOperator(Opcode::Sub, pat.nEff, two, tag + "_n_m2");
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

    auto *nMinus2c = new BinaryOperator(Opcode::Sub, pat.nEff, two, tag + "_c_n_m2");
    intCHeader->addInstruction(own(nMinus2c));
    auto *intCCmp = new ICmpInst(ICmpInst::ICMP_SLT, intCPhi, nMinus2c, tag + "_c_cmp");
    intCHeader->addInstruction(own(intCCmp));

    KernelNestResult kernel = buildUnguardedKernelNest(
        func, intRPhi, intCPhi, pat.nEff, pat.inArray, pat.outArray, pat.kArray, pat.pad,
        pat.kSize, intCHeader, intCLatch, tag + "_kern");

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

    intRExit->addInstruction(own(new BranchInst(pat.rHeader)));
    wireEdge(intRExit, pat.rHeader);

    BasicBlock *intRInitPred = nullptr;
    if (entryFromRepeat)
    {
        replaceTerminator(entryFromRepeat, own(new BranchInst(intRHeader)));
        wireEdge(entryFromRepeat, intRHeader);
        intRInitPred = entryFromRepeat;
    }
    else
    {
        vector<BasicBlock *> rPreds = pat.rHeader->getPredecessors();
        for (auto *pred : rPreds)
        {
            auto *br = getTerminator(pred);
            if (!br)
                continue;
            if (br->getTrueBlock() == pat.rHeader)
                br->setTrueBlock(intRHeader);
            if (br->isConditional() && br->getFalseBlock() == pat.rHeader)
                br->setFalseBlock(intRHeader);
            pred->removeSuccessor(pat.rHeader);
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

    replaceTerminator(pat.cBody, own(new BranchInst(cBorderDispatch)));
    wireEdge(pat.cBody, cBorderDispatch);

    Value *interiorCond =
        buildInteriorCond(cBorderDispatch, pat.rIV, pat.cIV, pat.nEff, tag + "_bd");
    cBorderDispatch->addInstruction(
        own(new BranchInst(interiorCond, cSkipLatch, pat.krHeader)));
    wireEdge(cBorderDispatch, cSkipLatch);
    wireEdge(cBorderDispatch, pat.krHeader);

    finishBorderSkip(cSkipLatch, pat.cIV, cHeader, pat.krExitStore, tag);

    if (verbose)
    {
        dbg << "Conv2dInteriorSplit: interior nest inserted; border dispatch at "
            << pat.cBody->getName() << "\n";
    }
    return true;
}

bool Conv2dInteriorSplitPass::runOnFunction(Function *func)
{
    func->setLoops(ControlFlowAnalysis::findLoops(func));
    Conv2dPattern pat;
    if (!matchConv2dNest(func, func->getLoops(), pat))
    {
        if (verbose)
            debugInfo << "Conv2dInteriorSplit: no conv2d nest matched in " << func->getName()
                      << "\n";
        return false;
    }
    bool changed = applySplit(func, pat, verbose, debugInfo);
    if (changed)
        func->setLoops(ControlFlowAnalysis::findLoops(func));
    return changed;
}
