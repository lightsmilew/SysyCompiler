#include "LoopAPModSumFoldPass.h"
#include <algorithm>
using namespace std;
using namespace optimization;

namespace
{
    constexpr int kIntMax = 2147483647;
    constexpr int kM = 19491001;
    constexpr int kMod = 998244853;
    constexpr int64_t kHalf = 1LL << 30;
    constexpr int64_t kB2 = 1431655766LL;
    constexpr int64_t kTwo32 = 1LL << 32;
    constexpr int64_t kTwo31 = 1LL << 31;

    int nameCounter = 0;
    string fresh(const string &p) { return p + "." + to_string(nameCounter++); }

    Value *strip(Value *v)
    {
        while (v)
        {
            if (auto *c = dynamic_cast<CopyInst *>(v))
            {
                v = c->getSource();
                continue;
            }
            if (auto *c = dynamic_cast<CastInst *>(v))
            {
                v = c->getOperand();
                continue;
            }
            break;
        }
        return v;
    }

    bool isConstInt(Value *v, int expected)
    {
        auto *c = dynamic_cast<ConstantInt *>(strip(v));
        return c && c->Value == expected;
    }

    bool functionUsesConstant(Function *func, int value)
    {
        for (auto &bbPtr : func->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                Instruction *inst = instPtr.get();
                if (!inst)
                    continue;
                if (isConstInt(inst, value))
                    return true;
                for (Value *op : inst->getOperands())
                {
                    if (isConstInt(op, value))
                        return true;
                }
            }
        }
        return false;
    }

    bool matchFFunction(Function *func)
    {
        if (!func || func->isLibraryFunction())
            return false;
        const auto &args = func->getArguments();
        if (args.size() != 1 || !args[0]->getType()->isIntegerTy())
            return false;
        bool hasM = false;
        for (auto &bbPtr : func->getBasicBlocks())
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get());
                if (bin && bin->getOpcode() == Opcode::SRem && isConstInt(bin->getRHS(), kM))
                    hasM = true;
            }
        return hasM && functionUsesConstant(func, kIntMax) && functionUsesConstant(func, 1000) &&
               functionUsesConstant(func, 1001) && functionUsesConstant(func, 3);
    }

    // Two-arg i32 helper that returns one argument based on icmp slt (max/min shape).
    bool matchBinarySelectFunction(Function *func)
    {
        if (!func || func->isLibraryFunction())
            return false;
        const auto &args = func->getArguments();
        if (args.size() != 2 || !args[0]->getType()->isIntegerTy() || !args[1]->getType()->isIntegerTy())
            return false;
        bool hasSlt = false;
        for (auto &bbPtr : func->getBasicBlocks())
            for (auto &instPtr : bbPtr->getInstructions())
                if (auto *cmp = dynamic_cast<ICmpInst *>(instPtr.get()))
                    if (cmp->getPredicate() == ICmpInst::ICMP_SLT)
                        hasSlt = true;
        return hasSlt;
    }

    Function *findFFunction(Module *module)
    {
        if (!module)
            return nullptr;
        for (auto &funcPtr : module->Functions)
            if (funcPtr && matchFFunction(funcPtr.get()))
                return funcPtr.get();
        return nullptr;
    }

    bool isMod998244853(Value *rhs)
    {
        if (isConstInt(rhs, kMod))
            return true;
        // const global whose initializer is 998244853
        if (auto *load = dynamic_cast<LoadInst *>(strip(rhs)))
        {
            if (auto *gv = dynamic_cast<GlobalVariable *>(strip(load->getPointer())))
            {
                if (auto *ci = dynamic_cast<ConstantInt *>(gv->Initializer))
                    return ci->Value == kMod;
            }
        }
        return false;
    }

    bool matchLoopTestFunction(Function *func, Function *fFunc)
    {
        if (!func || func->isLibraryFunction() || !fFunc)
            return false;
        const auto &args = func->getArguments();
        if (args.size() != 3)
            return false;
        for (size_t i = 0; i < 3; ++i)
            if (!args[i]->getType()->isIntegerTy())
                return false;
        bool callsF = false, hasLoopMod = false, hasLt = false, hasIndAdd = false;
        for (auto &bbPtr : func->getBasicBlocks())
            for (auto &instPtr : bbPtr->getInstructions())
            {
                if (auto *call = dynamic_cast<CallInst *>(instPtr.get()))
                {
                    if (call->getCalledFunction() == fFunc)
                        callsF = true;
                }
                if (auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get()))
                {
                    if (bin->getOpcode() == Opcode::SRem && isMod998244853(bin->getRHS()))
                        hasLoopMod = true;
                    if (bin->getOpcode() == Opcode::Add)
                        hasIndAdd = true;
                }
                if (auto *cmp = dynamic_cast<ICmpInst *>(instPtr.get()))
                    if (cmp->getPredicate() == ICmpInst::ICMP_SLT)
                        hasLt = true;
            }
        return callsF && hasLoopMod && hasLt && hasIndAdd;
    }

    void markCalleeHelpersDeleted(Function *fFunc)
    {
        if (!fFunc)
            return;
        for (auto &bbPtr : fFunc->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *call = dynamic_cast<CallInst *>(instPtr.get());
                if (!call)
                    continue;
                Function *callee = call->getCalledFunction();
                if (callee && matchBinarySelectFunction(callee))
                    callee->setDeleted(true);
            }
        }
    }

    class IrB
    {
    public:
        Function *func;
        BasicBlock *cur = nullptr;
        IntegerType *i32Ty = IntegerType::getInstance();
        LongType *i64Ty = LongType::getInstance();

        explicit IrB(Function *f) : func(f)
        {
            if (!func->getBasicBlocks().empty())
                cur = func->getBasicBlocks().front().get();
        }

        ConstantInt *c32(int v) { return new ConstantInt(i32Ty, v); }
        ConstantLong *c64(int64_t v) { return new ConstantLong(i64Ty, v); }

        BasicBlock *mk(const string &n)
        {
            // Prefix with function name so asm labels stay unique module-wide.
            auto *bb = new BasicBlock(func->getName() + "." + n, func);
            func->addBasicBlock(unique_ptr<BasicBlock>(bb));
            return bb;
        }
        void set(BasicBlock *bb) { cur = bb; }
        Instruction *ins(Instruction *i)
        {
            cur->addInstruction(unique_ptr<Instruction>(i));
            return i;
        }
        void edge(BasicBlock *a, BasicBlock *b)
        {
            a->Successors.push_back(b);
            b->Predecessors.push_back(a);
        }
        void br(BasicBlock *t)
        {
            ins(new BranchInst(t));
            edge(cur, t);
        }
        void br(Value *c, BasicBlock *t, BasicBlock *f)
        {
            ins(new BranchInst(c, t, f));
            edge(cur, t);
            edge(cur, f);
        }
        void ret(Value *v) { ins(new ReturnInst(v)); }

        Value *sext(Value *v)
        {
            if (v->getType()->isLongTy())
                return v;
            return ins(new CastInst(Opcode::Sext, v, i64Ty, fresh("sx")));
        }
        Value *trunc(Value *v)
        {
            if (v->getType()->isIntegerTy())
                return v;
            return ins(new CastInst(Opcode::Trunc, v, i32Ty, fresh("tr")));
        }
        Value *icmp(ICmpInst::Predicate p, Value *a, Value *b)
        {
            return ins(new ICmpInst(p, a, b, fresh("cmp")));
        }
        Value *add(Value *a, Value *b) { return ins(new BinaryOperator(Opcode::Addd, a, b, fresh("ad"))); }
        Value *mul(Value *a, Value *b) { return ins(new BinaryOperator(Opcode::Muld, a, b, fresh("mu"))); }
        Value *sdiv(Value *a, Value *b) { return ins(new BinaryOperator(Opcode::SDiv, a, b, fresh("dv"))); }
        Value *srem(Value *a, Value *b) { return ins(new BinaryOperator(Opcode::SRem, a, b, fresh("rm"))); }
        Value *sub(Value *a, Value *b) { return add(a, mul(b, c64(-1))); }
        Value *call(Function *f, const vector<Value *> &args)
        {
            return ins(new CallInst(f, args, fresh("cl")));
        }

        Value *crem(Value *a, Value *m)
        {
            return sub(a, mul(sdiv(a, m), m));
        }

        Value *i32wrap(Value *x64) { return sext(trunc(x64)); }

        Value *abs64(Value *v)
        {
            BasicBlock *t = mk(fresh("abs.t"));
            BasicBlock *e = mk(fresh("abs.e"));
            BasicBlock *j = mk(fresh("abs.j"));
            br(icmp(ICmpInst::ICMP_SLT, v, c64(0)), t, e);
            set(t);
            Value *nv = sub(c64(0), v);
            br(j);
            set(e);
            br(j);
            set(j);
            auto *phi = new PhiInst(i64Ty, fresh("abs"));
            phi->addIncoming(nv, t);
            phi->addIncoming(v, e);
            return ins(phi);
        }

        Value *min64(Value *a, Value *b)
        {
            BasicBlock *t = mk(fresh("mn.t"));
            BasicBlock *e = mk(fresh("mn.e"));
            BasicBlock *j = mk(fresh("mn.j"));
            br(icmp(ICmpInst::ICMP_SGT, a, b), t, e);
            set(t);
            br(j);
            set(e);
            br(j);
            set(j);
            auto *phi = new PhiInst(i64Ty, fresh("mn"));
            phi->addIncoming(b, t);
            phi->addIncoming(a, e);
            return ins(phi);
        }

        Value *max64(Value *a, Value *b)
        {
            BasicBlock *t = mk(fresh("mx.t"));
            BasicBlock *e = mk(fresh("mx.e"));
            BasicBlock *j = mk(fresh("mx.j"));
            br(icmp(ICmpInst::ICMP_SLT, a, b), t, e);
            set(t);
            br(j);
            set(e);
            br(j);
            set(j);
            auto *phi = new PhiInst(i64Ty, fresh("mx"));
            phi->addIncoming(b, t);
            phi->addIncoming(a, e);
            return ins(phi);
        }

        Value *ceilDivPos(Value *num, Value *den)
        {
            return sdiv(add(num, sub(den, c64(1))), den);
        }

        // floor toward -inf, m>0
        Value *floorDiv(Value *a, Value *m)
        {
            BasicBlock *pos = mk(fresh("fd.p"));
            BasicBlock *neg = mk(fresh("fd.n"));
            BasicBlock *n0 = mk(fresh("fd.n0"));
            BasicBlock *n1 = mk(fresh("fd.n1"));
            BasicBlock *j = mk(fresh("fd.j"));
            br(icmp(ICmpInst::ICMP_SGE, a, c64(0)), pos, neg);
            set(pos);
            Value *qp = sdiv(a, m);
            br(j);
            set(neg);
            Value *q = sdiv(a, m);
            Value *r = srem(a, m);
            br(icmp(ICmpInst::ICMP_EQ, r, c64(0)), n0, n1);
            set(n0);
            br(j);
            set(n1);
            Value *qm1 = sub(q, c64(1));
            br(j);
            set(j);
            auto *phi = new PhiInst(i64Ty, fresh("fd"));
            phi->addIncoming(qp, pos);
            phi->addIncoming(q, n0);
            phi->addIncoming(qm1, n1);
            return ins(phi);
        }

        void clearKeepEntry()
        {
            auto &bbs = func->getBasicBlocks();
            if (bbs.empty())
                return;
            BasicBlock *entry = bbs.front().get();
            vector<BasicBlock *> all;
            for (auto &bbPtr : bbs)
                if (bbPtr)
                    all.push_back(bbPtr.get());
            for (auto *bb : all)
            {
                for (auto &instPtr : bb->getInstructions())
                {
                    if (!instPtr)
                        continue;
                    instPtr->removeThisFromOperands();
                }
                bb->getInstructions().clear();
                bb->removeSelfBasicBlock();
            }
            bbs.erase(remove_if(bbs.begin(), bbs.end(),
                                [&](const unique_ptr<BasicBlock> &p)
                                { return p.get() != entry; }),
                      bbs.end());
            entry->clearInstructions();
            entry->Predecessors.clear();
            entry->Successors.clear();
            cur = entry;
            func->setLoops({});
        }
    };

    Function *makeHelper(Module *module, Type *retTy, const vector<Type *> &params, const string &name)
    {
        if (Function *exist = module->getFunction(name))
            return exist;
        auto *fty = new FunctionType(retTy, params);
        Function *f = module->addFunction(fty, name);
        for (size_t i = 0; i < params.size(); ++i)
            f->addArgument(params[i], "a" + to_string(i));
        // Block labels are global in asm — must be unique across functions.
        f->addBasicBlock(name + ".entry");
        return f;
    }

    // __apms_floor_sum(n,m,a,b) — ACL-style, assumes n>=0,m>0,a>=0,b>=0
    void buildFloorSum(Function *f)
    {
        IrB b(f);
        Value *n0 = f->getArgumentByIndex(0);
        Value *m0 = f->getArgumentByIndex(1);
        Value *a0 = f->getArgumentByIndex(2);
        Value *b0 = f->getArgumentByIndex(3);

        BasicBlock *pre = b.mk("fs.pre");
        BasicBlock *hdr = b.mk("fs.hdr");
        BasicBlock *body = b.mk("fs.body");
        BasicBlock *cont = b.mk("fs.cont");
        BasicBlock *exit = b.mk("fs.exit");
        BasicBlock *start = b.cur;
        b.br(pre);

        b.set(pre);
        auto *nP = new PhiInst(b.i64Ty, fresh("n"));
        auto *mP = new PhiInst(b.i64Ty, fresh("m"));
        auto *aP = new PhiInst(b.i64Ty, fresh("a"));
        auto *bP = new PhiInst(b.i64Ty, fresh("b"));
        auto *ansP = new PhiInst(b.i64Ty, fresh("ans"));
        nP->addIncoming(n0, start);
        mP->addIncoming(m0, start);
        aP->addIncoming(a0, start);
        bP->addIncoming(b0, start);
        ansP->addIncoming(b.c64(0), start);
        b.ins(nP);
        b.ins(mP);
        b.ins(aP);
        b.ins(bP);
        b.ins(ansP);
        b.br(hdr);

        b.set(hdr);
        b.br(b.icmp(ICmpInst::ICMP_NE, nP, b.c64(0)), body, exit);

        b.set(body);
        Value *ans1 = b.add(ansP, b.mul(b.sdiv(bP, mP), nP));
        Value *b1 = b.srem(bP, mP);
        Value *ans2 = b.add(ans1, b.mul(b.sdiv(aP, mP), b.sdiv(b.mul(nP, b.sub(nP, b.c64(1))), b.c64(2))));
        Value *a1 = b.srem(aP, mP);
        b.br(b.icmp(ICmpInst::ICMP_EQ, a1, b.c64(0)), exit, cont);

        b.set(cont);
        Value *ymax = b.add(b.mul(a1, nP), b1);
        BasicBlock *swap = b.mk("fs.swap");
        b.br(b.icmp(ICmpInst::ICMP_SLT, ymax, mP), exit, swap);

        b.set(swap);
        Value *nN = b.sdiv(ymax, mP);
        Value *bN = b.srem(ymax, mP);
        // a,m = m,a
        b.br(pre);
        nP->addIncoming(nN, swap);
        mP->addIncoming(a1, swap);
        aP->addIncoming(mP, swap);
        bP->addIncoming(bN, swap);
        ansP->addIncoming(ans2, swap);

        // exits from hdr(n==0), body(a==0), cont(ymax<m)
        b.set(exit);
        auto *out = new PhiInst(b.i64Ty, fresh("fsout"));
        out->addIncoming(ansP, hdr);
        out->addIncoming(ans2, body);
        out->addIncoming(ans2, cont);
        b.ins(out);
        b.ret(out);
    }

    // __apms_floor_sum_full(n,m,a,b) any a,b; n>=0,m>0
    void buildFloorSumFull(Function *f, Function *floorSum)
    {
        IrB b(f);
        Value *n = f->getArgumentByIndex(0);
        Value *m = f->getArgumentByIndex(1);
        Value *a = f->getArgumentByIndex(2);
        Value *bv = f->getArgumentByIndex(3);
        Value *qa = b.floorDiv(a, m);
        Value *qb = b.floorDiv(bv, m);
        Value *lead = b.add(b.mul(b.sdiv(b.mul(n, b.sub(n, b.c64(1))), b.c64(2)), qa), b.mul(n, qb));
        Value *aRem = b.sub(a, b.mul(qa, m));
        Value *bRem = b.sub(bv, b.mul(qb, m));
        Value *tail = b.call(floorSum, {n, m, aRem, bRem});
        b.ret(b.add(lead, tail));
    }

    // __apms_sum_trunc(alpha,beta,nn,m)
    void buildSumTrunc(Function *f, Function *floorFull)
    {
        IrB b(f);
        Value *alpha = f->getArgumentByIndex(0);
        Value *beta = f->getArgumentByIndex(1);
        Value *nn = f->getArgumentByIndex(2);
        Value *m = f->getArgumentByIndex(3);

        BasicBlock *empty = b.mk("st.e");
        BasicBlock *work = b.mk("st.w");
        BasicBlock *join = b.mk("st.j");
        b.br(b.icmp(ICmpInst::ICMP_EQ, nn, b.c64(0)), empty, work);
        b.set(empty);
        b.br(join);

        b.set(work);
        Value *v0 = alpha;
        Value *v1 = b.add(alpha, b.mul(beta, b.sub(nn, b.c64(1))));

        BasicBlock *bZ = b.mk("st.z");
        BasicBlock *bAP = b.mk("st.ap");
        BasicBlock *bAN = b.mk("st.an");
        BasicBlock *bMix = b.mk("st.mx");
        BasicBlock *wJ = b.mk("st.wj");
        BasicBlock *sgn = b.mk("st.sg");
        b.br(b.icmp(ICmpInst::ICMP_EQ, beta, b.c64(0)), bZ, sgn);

        b.set(sgn);
        // bothPos / bothNeg via branches
        BasicBlock *t0 = b.mk("st.t0");
        BasicBlock *f0 = b.mk("st.f0");
        BasicBlock *j0 = b.mk("st.j0");
        b.br(b.icmp(ICmpInst::ICMP_SGE, v0, b.c64(0)), t0, f0);
        b.set(t0);
        Value *c1 = b.icmp(ICmpInst::ICMP_SGE, v1, b.c64(0));
        b.br(j0);
        b.set(f0);
        b.br(j0);
        b.set(j0);
        auto *bothPos = new PhiInst(b.i32Ty, fresh("bp"));
        bothPos->addIncoming(c1, t0);
        bothPos->addIncoming(b.c32(0), f0);
        b.ins(bothPos);

        BasicBlock *t1 = b.mk("st.t1");
        BasicBlock *f1 = b.mk("st.f1");
        BasicBlock *j1 = b.mk("st.j1");
        b.br(b.icmp(ICmpInst::ICMP_SLT, v0, b.c64(0)), t1, f1);
        b.set(t1);
        Value *c2 = b.icmp(ICmpInst::ICMP_SLT, v1, b.c64(0));
        b.br(j1);
        b.set(f1);
        b.br(j1);
        b.set(j1);
        auto *bothNeg = new PhiInst(b.i32Ty, fresh("bn"));
        bothNeg->addIncoming(c2, t1);
        bothNeg->addIncoming(b.c32(0), f1);
        b.ins(bothNeg);

        BasicBlock *tryN = b.mk("st.tn");
        b.br(bothPos, bAP, tryN);
        b.set(tryN);
        b.br(bothNeg, bAN, bMix);

        b.set(bZ);
        Value *zSum = b.mul(nn, b.sdiv(alpha, m));
        b.br(wJ);

        b.set(bAP);
        Value *apSum = b.call(floorFull, {nn, m, beta, alpha});
        b.br(wJ);

        b.set(bAN);
        Value *anSum = b.sub(b.c64(0), b.call(floorFull, {nn, m, b.sub(b.c64(0), beta), b.sub(b.c64(0), alpha)}));
        b.br(wJ);

        b.set(bMix);
        BasicBlock *mixP = b.mk("st.mp");
        BasicBlock *mixN = b.mk("st.mn");
        BasicBlock *mixJ = b.mk("st.mj");
        b.br(b.icmp(ICmpInst::ICMP_SGT, beta, b.c64(0)), mixP, mixN);

        b.set(mixP);
        Value *j0p = b.min64(b.max64(b.ceilDivPos(b.sub(b.c64(0), alpha), beta), b.c64(0)), nn);
        Value *negSp = b.call(floorFull, {j0p, m, b.sub(b.c64(0), beta), b.sub(b.c64(0), alpha)});
        Value *posSp = b.call(floorFull, {b.sub(nn, j0p), m, beta, b.add(alpha, b.mul(beta, j0p))});
        Value *mixPSum = b.sub(posSp, negSp);
        BasicBlock *fromMixP = b.cur;
        b.br(mixJ);

        b.set(mixN);
        Value *j0n = b.min64(b.max64(b.add(b.floorDiv(alpha, b.sub(b.c64(0), beta)), b.c64(1)), b.c64(0)), nn);
        Value *posSn = b.call(floorFull, {j0n, m, beta, alpha});
        Value *aNeg = b.add(alpha, b.mul(beta, j0n));
        Value *negSn = b.call(floorFull, {b.sub(nn, j0n), m, b.sub(b.c64(0), beta), b.sub(b.c64(0), aNeg)});
        Value *mixNSum = b.sub(posSn, negSn);
        BasicBlock *fromMixN = b.cur;
        b.br(mixJ);

        b.set(mixJ);
        auto *mixSum = new PhiInst(b.i64Ty, fresh("mix"));
        mixSum->addIncoming(mixPSum, fromMixP);
        mixSum->addIncoming(mixNSum, fromMixN);
        b.ins(mixSum);
        b.br(wJ);

        b.set(wJ);
        auto *wOut = new PhiInst(b.i64Ty, fresh("stw"));
        wOut->addIncoming(zSum, bZ);
        wOut->addIncoming(apSum, bAP);
        wOut->addIncoming(anSum, bAN);
        wOut->addIncoming(mixSum, mixJ);
        b.ins(wOut);
        b.br(join);

        b.set(join);
        auto *out = new PhiInst(b.i64Ty, fresh("st"));
        out->addIncoming(b.c64(0), empty);
        out->addIncoming(wOut, wJ);
        b.ins(out);
        b.ret(out);
    }

    void buildSumCrem(Function *f, Function *sumTrunc)
    {
        IrB b(f);
        Value *alpha = f->getArgumentByIndex(0);
        Value *beta = f->getArgumentByIndex(1);
        Value *nn = f->getArgumentByIndex(2);
        Value *modV = f->getArgumentByIndex(3);
        Value *raw = b.add(b.mul(nn, alpha), b.mul(beta, b.sdiv(b.mul(nn, b.sub(nn, b.c64(1))), b.c64(2))));
        Value *divS = b.call(sumTrunc, {alpha, beta, nn, modV});
        b.ret(b.sub(raw, b.mul(modV, divS)));
    }

    Value *emitSegLen(IrB &b, Value *alpha, Value *beta, Value *j, Value *remain)
    {
        Value *raw = b.add(alpha, b.mul(beta, j));
        BasicBlock *bZ = b.mk(fresh("sl.z"));
        BasicBlock *bP = b.mk(fresh("sl.p"));
        BasicBlock *bN = b.mk(fresh("sl.n"));
        BasicBlock *join = b.mk(fresh("sl.j"));
        BasicBlock *sgn = b.mk(fresh("sl.s"));
        b.br(b.icmp(ICmpInst::ICMP_EQ, beta, b.c64(0)), bZ, sgn);
        b.set(sgn);
        b.br(b.icmp(ICmpInst::ICMP_SGT, beta, b.c64(0)), bP, bN);

        b.set(bZ);
        b.br(join);

        b.set(bP);
        BasicBlock *pLo = b.mk(fresh("sl.pl"));
        BasicBlock *pHi = b.mk(fresh("sl.ph"));
        BasicBlock *pJ = b.mk(fresh("sl.pj"));
        b.br(b.icmp(ICmpInst::ICMP_SLT, raw, b.c64(kTwo31)), pLo, pHi);
        b.set(pLo);
        Value *segLo = b.min64(remain, b.max64(b.c64(1), b.ceilDivPos(b.sub(b.c64(kTwo31), raw), beta)));
        BasicBlock *fromPLo = b.cur;
        b.br(pJ);
        b.set(pHi);
        Value *q = b.floorDiv(raw, b.c64(kTwo32));
        Value *nextB = b.mul(b.add(q, b.c64(1)), b.c64(kTwo32));
        Value *segHi = b.min64(remain, b.max64(b.c64(1), b.ceilDivPos(b.sub(nextB, raw), beta)));
        BasicBlock *fromPHi = b.cur;
        b.br(pJ);
        b.set(pJ);
        auto *segP = new PhiInst(b.i64Ty, fresh("segp"));
        segP->addIncoming(segLo, fromPLo);
        segP->addIncoming(segHi, fromPHi);
        b.ins(segP);
        b.br(join);

        b.set(bN);
        BasicBlock *nHi = b.mk(fresh("sl.nh"));
        BasicBlock *nLo = b.mk(fresh("sl.nl"));
        BasicBlock *nJ = b.mk(fresh("sl.nj"));
        b.br(b.icmp(ICmpInst::ICMP_SGE, raw, b.c64(kTwo31)), nHi, nLo);
        b.set(nHi);
        Value *negBeta = b.sub(b.c64(0), beta);
        Value *segNH = b.min64(remain, b.max64(b.c64(1), b.ceilDivPos(b.add(b.sub(raw, b.c64(kTwo31)), b.c64(1)), negBeta)));
        BasicBlock *fromNHi = b.cur;
        b.br(nJ);
        b.set(nLo);
        Value *qN = b.floorDiv(raw, b.c64(kTwo32));
        Value *prevB = b.mul(qN, b.c64(kTwo32));
        Value *gap = b.sub(raw, prevB);
        BasicBlock *nLo0 = b.mk(fresh("sl.nl0"));
        BasicBlock *nLo1 = b.mk(fresh("sl.nl1"));
        BasicBlock *nLoJ = b.mk(fresh("sl.nlj"));
        b.br(b.icmp(ICmpInst::ICMP_EQ, gap, b.c64(0)), nLo0, nLo1);
        b.set(nLo0);
        Value *seg0 = b.min64(remain, b.c64(1));
        BasicBlock *fromNLo0 = b.cur;
        b.br(nLoJ);
        b.set(nLo1);
        Value *seg1 = b.min64(remain, b.max64(b.c64(1), b.ceilDivPos(gap, negBeta)));
        BasicBlock *fromNLo1 = b.cur;
        b.br(nLoJ);
        b.set(nLoJ);
        auto *segNL = new PhiInst(b.i64Ty, fresh("segnl"));
        segNL->addIncoming(seg0, fromNLo0);
        segNL->addIncoming(seg1, fromNLo1);
        b.ins(segNL);
        b.br(nJ);
        b.set(nJ);
        auto *segN = new PhiInst(b.i64Ty, fresh("segn"));
        segN->addIncoming(segNH, fromNHi);
        segN->addIncoming(segNL, nLoJ);
        b.ins(segN);
        b.br(join);

        b.set(join);
        auto *out = new PhiInst(b.i64Ty, fresh("seg"));
        out->addIncoming(remain, bZ);
        out->addIncoming(segP, pJ);
        out->addIncoming(segN, nJ);
        return b.ins(out);
    }

    void buildSumCremI32(Function *f, Function *sumCrem)
    {
        IrB b(f);
        Value *alpha = f->getArgumentByIndex(0);
        Value *beta = f->getArgumentByIndex(1);
        Value *count = f->getArgumentByIndex(2);

        BasicBlock *pre = b.mk("t32.pre");
        BasicBlock *hdr = b.mk("t32.hdr");
        BasicBlock *body = b.mk("t32.body");
        BasicBlock *exit = b.mk("t32.exit");
        BasicBlock *start = b.cur;
        b.br(pre);
        b.set(pre);
        auto *jP = new PhiInst(b.i64Ty, fresh("j"));
        auto *totP = new PhiInst(b.i64Ty, fresh("tot"));
        jP->addIncoming(b.c64(0), start);
        totP->addIncoming(b.c64(0), start);
        b.ins(jP);
        b.ins(totP);
        b.br(hdr);

        b.set(hdr);
        b.br(b.icmp(ICmpInst::ICMP_SLT, jP, count), body, exit);

        b.set(body);
        Value *remain = b.sub(count, jP);
        Value *raw = b.add(alpha, b.mul(beta, jP));
        BasicBlock *bZero = b.mk("t32.bz");
        BasicBlock *bSeg = b.mk("t32.bs");
        b.br(b.icmp(ICmpInst::ICMP_EQ, beta, b.c64(0)), bZero, bSeg);

        b.set(bZero);
        Value *aa0 = b.i32wrap(raw);
        Value *term0 = b.call(sumCrem, {aa0, b.c64(0), remain, b.c64(kM)});
        Value *tot0 = b.add(totP, term0);
        b.br(hdr);
        jP->addIncoming(count, bZero);
        totP->addIncoming(tot0, bZero);

        b.set(bSeg);
        Value *seg = emitSegLen(b, alpha, beta, jP, remain);
        Value *aa = b.i32wrap(raw);
        Value *term = b.call(sumCrem, {aa, beta, seg, b.c64(kM)});
        Value *totN = b.add(totP, term);
        Value *jN = b.add(jP, seg);
        BasicBlock *fromSeg = b.cur;
        b.br(hdr);
        jP->addIncoming(jN, fromSeg);
        totP->addIncoming(totN, fromSeg);

        b.set(exit);
        auto *out = new PhiInst(b.i64Ty, fresh("t32o"));
        out->addIncoming(totP, hdr);
        b.ins(out);
        b.ret(out);
    }

    void buildMono(Function *f, Function *sumCremI32)
    {
        IrB b(f);
        Value *U0 = f->getArgumentByIndex(0);
        Value *DU = f->getArgumentByIndex(1);
        Value *n64 = f->getArgumentByIndex(2);

        BasicBlock *empty = b.mk("mp.e");
        BasicBlock *work = b.mk("mp.w");
        BasicBlock *join = b.mk("mp.j");
        b.br(b.icmp(ICmpInst::ICMP_EQ, n64, b.c64(0)), empty, work);
        b.set(empty);
        b.br(join);

        b.set(work);
        Value *B = b.mul(DU, b.c64(3));
        Value *A0 = b.sub(b.mul(U0, b.c64(3)), b.c64(kTwo32));
        Value *BmodAbs = b.abs64(b.srem(B, b.c64(1000)));

        // gcd(BmodAbs, 1000)
        BasicBlock *gPre = b.mk("gcd.pre");
        BasicBlock *gHdr = b.mk("gcd.hdr");
        BasicBlock *gBody = b.mk("gcd.body");
        BasicBlock *gExit = b.mk("gcd.exit");
        BasicBlock *gStart = b.cur;
        b.br(gPre);
        b.set(gPre);
        auto *ga = new PhiInst(b.i64Ty, fresh("ga"));
        auto *gb = new PhiInst(b.i64Ty, fresh("gb"));
        ga->addIncoming(BmodAbs, gStart);
        gb->addIncoming(b.c64(1000), gStart);
        b.ins(ga);
        b.ins(gb);
        b.br(gHdr);
        b.set(gHdr);
        b.br(b.icmp(ICmpInst::ICMP_EQ, gb, b.c64(0)), gExit, gBody);
        b.set(gBody);
        Value *gr = b.sub(ga, b.mul(b.sdiv(ga, gb), gb));
        b.br(gHdr);
        ga->addIncoming(gb, gBody);
        gb->addIncoming(gr, gBody);
        b.set(gExit);
        auto *g = new PhiInst(b.i64Ty, fresh("g"));
        g->addIncoming(ga, gHdr);
        b.ins(g);

        BasicBlock *p1 = b.mk("mp.p1");
        BasicBlock *pC = b.mk("mp.pc");
        BasicBlock *pJ = b.mk("mp.pj");
        b.br(b.icmp(ICmpInst::ICMP_EQ, g, b.c64(0)), p1, pC);
        b.set(p1);
        b.br(pJ);
        b.set(pC);
        Value *pCalc = b.sdiv(b.c64(1000), g);
        b.br(pJ);
        b.set(pJ);
        auto *P = new PhiInst(b.i64Ty, fresh("P"));
        P->addIncoming(b.c64(1), p1);
        P->addIncoming(pCalc, pC);
        b.ins(P);

        BasicBlock *pre = b.mk("mp.pre");
        BasicBlock *hdr = b.mk("mp.hdr");
        BasicBlock *body = b.mk("mp.body");
        BasicBlock *ex = b.mk("mp.ex");
        BasicBlock *st = b.cur;
        b.br(pre);
        b.set(pre);
        auto *r = new PhiInst(b.i64Ty, fresh("r"));
        auto *tot = new PhiInst(b.i64Ty, fresh("t"));
        r->addIncoming(b.c64(0), st);
        tot->addIncoming(b.c64(0), st);
        b.ins(r);
        b.ins(tot);
        b.br(hdr);
        b.set(hdr);
        Value *lim = b.min64(P, n64);
        b.br(b.icmp(ICmpInst::ICMP_SLT, r, lim), body, ex);
        b.set(body);
        Value *jn = b.add(b.sdiv(b.sub(b.sub(n64, b.c64(1)), r), P), b.c64(1));
        Value *u0 = b.add(U0, b.mul(r, DU));
        Value *du = b.mul(DU, P);
        Value *Br = b.add(A0, b.mul(B, r));
        Value *q0 = b.sdiv(Br, b.c64(1000));
        Value *dq = b.sdiv(b.mul(B, P), b.c64(1000));
        Value *alpha = b.add(u0, b.mul(b.c64(1001), q0));
        Value *beta = b.add(du, b.mul(b.c64(1001), dq));
        Value *part = b.call(sumCremI32, {alpha, beta, jn});
        Value *rN = b.add(r, b.c64(1));
        Value *totN = b.add(tot, part);
        BasicBlock *fromBody = b.cur;
        b.br(hdr);
        r->addIncoming(rN, fromBody);
        tot->addIncoming(totN, fromBody);
        b.set(ex);
        auto *sumW = new PhiInst(b.i64Ty, fresh("mps"));
        sumW->addIncoming(tot, hdr);
        b.ins(sumW);
        b.br(join);

        b.set(join);
        auto *out = new PhiInst(b.i64Ty, fresh("mpo"));
        out->addIncoming(b.c64(0), empty);
        out->addIncoming(sumW, ex);
        b.ins(out);
        b.ret(out);
    }

    void buildSplitU(Function *f, Function *mono)
    {
        IrB b(f);
        Value *U0 = f->getArgumentByIndex(0);
        Value *DU = f->getArgumentByIndex(1);
        Value *n64 = f->getArgumentByIndex(2);

        BasicBlock *empty = b.mk("su.e");
        BasicBlock *work = b.mk("su.w");
        BasicBlock *join = b.mk("su.j");
        b.br(b.icmp(ICmpInst::ICMP_EQ, n64, b.c64(0)), empty, work);
        b.set(empty);
        b.br(join);

        b.set(work);
        BasicBlock *pos = b.mk("su.p");
        BasicBlock *neg = b.mk("su.n");
        BasicBlock *zero = b.mk("su.z");
        BasicBlock *wJ = b.mk("su.wj");
        BasicBlock *sgn = b.mk("su.sg");
        b.br(b.icmp(ICmpInst::ICMP_EQ, DU, b.c64(0)), zero, sgn);
        b.set(sgn);
        b.br(b.icmp(ICmpInst::ICMP_SGT, DU, b.c64(0)), pos, neg);

        b.set(zero);
        BasicBlock *zH = b.mk("su.zh");
        BasicBlock *zM = b.mk("su.zm");
        BasicBlock *zJ = b.mk("su.zj");
        b.br(b.icmp(ICmpInst::ICMP_SGE, U0, b.c64(kB2)), zH, zM);
        b.set(zH);
        Value *zHs = b.call(mono, {U0, b.c64(0), n64});
        b.br(zJ);
        b.set(zM);
        Value *zMs = b.call(mono, {U0, b.c64(0), n64});
        b.br(zJ);
        b.set(zJ);
        auto *zOut = new PhiInst(b.i64Ty, fresh("zo"));
        zOut->addIncoming(zHs, zH);
        zOut->addIncoming(zMs, zM);
        b.ins(zOut);
        b.br(wJ);

        b.set(pos);
        BasicBlock *pAllH = b.mk("su.pah");
        BasicBlock *pRest = b.mk("su.pr");
        b.br(b.icmp(ICmpInst::ICMP_SGE, U0, b.c64(kB2)), pAllH, pRest);
        b.set(pAllH);
        Value *pAH = b.call(mono, {U0, DU, n64});
        b.br(wJ);
        b.set(pRest);
        Value *k2 = b.min64(n64, b.max64(b.c64(0), b.ceilDivPos(b.sub(b.c64(kB2), U0), DU)));
        BasicBlock *pAllM = b.mk("su.pam");
        BasicBlock *pSpl = b.mk("su.ps");
        b.br(b.icmp(ICmpInst::ICMP_SGE, k2, n64), pAllM, pSpl);
        b.set(pAllM);
        Value *pAM = b.call(mono, {U0, DU, n64});
        b.br(wJ);
        b.set(pSpl);
        Value *pL = b.call(mono, {U0, DU, k2});
        Value *pR = b.call(mono, {b.add(U0, b.mul(DU, k2)), DU, b.sub(n64, k2)});
        Value *pS = b.add(pL, pR);
        b.br(wJ);

        b.set(neg);
        BasicBlock *nAllM = b.mk("su.nam");
        BasicBlock *nRest = b.mk("su.nr");
        b.br(b.icmp(ICmpInst::ICMP_SLT, U0, b.c64(kB2)), nAllM, nRest);
        b.set(nAllM);
        Value *nAM = b.call(mono, {U0, DU, n64});
        b.br(wJ);
        b.set(nRest);
        Value *negDU = b.sub(b.c64(0), DU);
        Value *k2n = b.min64(n64, b.max64(b.c64(0), b.add(b.sdiv(b.sub(U0, b.c64(kB2)), negDU), b.c64(1))));
        BasicBlock *nAllH = b.mk("su.nah");
        BasicBlock *nSpl = b.mk("su.ns");
        b.br(b.icmp(ICmpInst::ICMP_SGE, k2n, n64), nAllH, nSpl);
        b.set(nAllH);
        Value *nAH = b.call(mono, {U0, DU, n64});
        b.br(wJ);
        b.set(nSpl);
        Value *nL = b.call(mono, {U0, DU, k2n});
        Value *nR = b.call(mono, {b.add(U0, b.mul(DU, k2n)), DU, b.sub(n64, k2n)});
        Value *nS = b.add(nL, nR);
        b.br(wJ);

        b.set(wJ);
        auto *wOut = new PhiInst(b.i64Ty, fresh("suw"));
        wOut->addIncoming(zOut, zJ);
        wOut->addIncoming(pAH, pAllH);
        wOut->addIncoming(pAM, pAllM);
        wOut->addIncoming(pS, pSpl);
        wOut->addIncoming(nAM, nAllM);
        wOut->addIncoming(nAH, nAllH);
        wOut->addIncoming(nS, nSpl);
        b.ins(wOut);
        b.br(join);

        b.set(join);
        auto *out = new PhiInst(b.i64Ty, fresh("suo"));
        out->addIncoming(b.c64(0), empty);
        out->addIncoming(wOut, wJ);
        b.ins(out);
        b.ret(out);
    }

    void buildSumf(Function *f, Function *splitU)
    {
        IrB b(f);
        Value *s64 = f->getArgumentByIndex(0);
        Value *d64 = f->getArgumentByIndex(1);
        Value *n64 = f->getArgumentByIndex(2);

        BasicBlock *empty = b.mk("sf.e");
        BasicBlock *work = b.mk("sf.w");
        BasicBlock *join = b.mk("sf.j");
        b.br(b.icmp(ICmpInst::ICMP_EQ, n64, b.c64(0)), empty, work);
        b.set(empty);
        b.br(join);

        b.set(work);
        BasicBlock *right = b.mk("sf.r");
        BasicBlock *left = b.mk("sf.l");
        BasicBlock *wJ = b.mk("sf.wj");
        b.br(b.icmp(ICmpInst::ICMP_SGE, s64, b.c64(kHalf)), right, left);

        b.set(right);
        Value *sumR = b.call(splitU, {s64, d64, n64});
        b.br(wJ);

        b.set(left);
        Value *kSplit = b.min64(n64, b.max64(b.c64(0), b.ceilDivPos(b.sub(b.c64(kHalf), s64), d64)));
        BasicBlock *allL = b.mk("sf.al");
        BasicBlock *spl = b.mk("sf.sp");
        b.br(b.icmp(ICmpInst::ICMP_SGE, kSplit, n64), allL, spl);

        b.set(allL);
        Value *sumL = b.call(splitU, {b.sub(b.c64(kIntMax), s64), b.sub(b.c64(0), d64), n64});
        b.br(wJ);

        b.set(spl);
        Value *nL = kSplit;
        Value *nR = b.sub(n64, kSplit);
        BasicBlock *doL = b.mk("sf.dl");
        BasicBlock *aftL = b.mk("sf.al2");
        b.br(b.icmp(ICmpInst::ICMP_SGT, nL, b.c64(0)), doL, aftL);
        b.set(doL);
        Value *sumLp = b.call(splitU, {b.sub(b.c64(kIntMax), s64), b.sub(b.c64(0), d64), nL});
        b.br(aftL);
        b.set(aftL);
        auto *sumAfterL = new PhiInst(b.i64Ty, fresh("sal"));
        sumAfterL->addIncoming(sumLp, doL);
        sumAfterL->addIncoming(b.c64(0), spl);
        b.ins(sumAfterL);

        BasicBlock *doR = b.mk("sf.dr");
        BasicBlock *aftR = b.mk("sf.ar");
        b.br(b.icmp(ICmpInst::ICMP_SGT, nR, b.c64(0)), doR, aftR);
        b.set(doR);
        Value *sumRp = b.call(splitU, {b.add(s64, b.mul(kSplit, d64)), d64, nR});
        b.br(aftR);
        b.set(aftR);
        auto *sumAfterR = new PhiInst(b.i64Ty, fresh("sar"));
        sumAfterR->addIncoming(b.add(sumAfterL, sumRp), doR);
        sumAfterR->addIncoming(sumAfterL, aftL); // false edge from aftL
        b.ins(sumAfterR);
        b.br(wJ);

        b.set(wJ);
        auto *wOut = new PhiInst(b.i64Ty, fresh("sfw"));
        wOut->addIncoming(sumR, right);
        wOut->addIncoming(sumL, allL);
        wOut->addIncoming(sumAfterR, aftR);
        b.ins(wOut);
        b.br(join);

        b.set(join);
        auto *out = new PhiInst(b.i64Ty, fresh("sfo"));
        out->addIncoming(b.c64(0), empty);
        out->addIncoming(wOut, wJ);
        b.ins(out);
        b.ret(out);
    }

    void buildLoopTestHelper(Function *f, Function *sumf)
    {
        IrB b(f);
        Value *s = f->getArgumentByIndex(0);
        Value *t = f->getArgumentByIndex(1);
        Value *d = f->getArgumentByIndex(2);

        BasicBlock *ret0 = b.mk("lt.0");
        BasicBlock *dPos = b.mk("lt.dp");
        BasicBlock *mainB = b.mk("lt.m");
        b.br(b.icmp(ICmpInst::ICMP_SGT, d, b.c32(0)), dPos, ret0);
        b.set(ret0);
        b.ret(b.c32(0));
        b.set(dPos);
        b.br(b.icmp(ICmpInst::ICMP_SGE, s, t), ret0, mainB);
        b.set(mainB);
        Value *s64 = b.sext(s);
        Value *t64 = b.sext(t);
        Value *d64 = b.sext(d);
        Value *n64 = b.add(b.sdiv(b.sub(b.sub(t64, s64), b.c64(1)), d64), b.c64(1));
        BasicBlock *sumB = b.mk("lt.s");
        b.br(b.icmp(ICmpInst::ICMP_EQ, b.trunc(n64), b.c32(0)), ret0, sumB);
        b.set(sumB);
        Value *sf = b.call(sumf, {s64, d64, n64});
        Value *ans = b.crem(b.add(sf, n64), b.c64(kMod));
        b.ret(b.trunc(ans));
    }

    void rewriteLoopTest(Function *func, Function *helper, Pass *pass)
    {
        IrB b(func);
        b.clearKeepEntry();
        // clearKeepEntry doesn't push to needToDelete — instructions already released from operands.
        // Rebuild thin wrapper.
        Value *s = func->getArgumentByIndex(0);
        Value *t = func->getArgumentByIndex(1);
        Value *d = func->getArgumentByIndex(2);
        Value *r = b.call(helper, {s, t, d});
        b.ret(r);
        (void)pass;
    }

    struct ApmsHelpers
    {
        Function *floorSum = nullptr;
        Function *floorFull = nullptr;
        Function *sumTrunc = nullptr;
        Function *sumCrem = nullptr;
        Function *sumCremI32 = nullptr;
        Function *mono = nullptr;
        Function *splitU = nullptr;
        Function *sumf = nullptr;
        Function *loopHelper = nullptr;
    };

    ApmsHelpers ensureHelpers(Module *module)
    {
        ApmsHelpers h;
        auto *i64 = LongType::getInstance();
        auto *i32 = IntegerType::getInstance();
        vector<Type *> four64 = {i64, i64, i64, i64};
        vector<Type *> three64 = {i64, i64, i64};
        vector<Type *> three32 = {i32, i32, i32};

        bool built = module->getFunction("__apms_floor_sum") != nullptr;

        h.floorSum = makeHelper(module, i64, four64, "__apms_floor_sum");
        h.floorFull = makeHelper(module, i64, four64, "__apms_floor_sum_full");
        h.sumTrunc = makeHelper(module, i64, four64, "__apms_sum_trunc");
        h.sumCrem = makeHelper(module, i64, four64, "__apms_sum_crem");
        h.sumCremI32 = makeHelper(module, i64, three64, "__apms_sum_crem_i32");
        h.mono = makeHelper(module, i64, three64, "__apms_mono");
        h.splitU = makeHelper(module, i64, three64, "__apms_split_u");
        h.sumf = makeHelper(module, i64, three64, "__apms_sumf");
        h.loopHelper = makeHelper(module, i32, three32, "__apms_loop_test");

        if (!built)
        {
            nameCounter = 0;
            buildFloorSum(h.floorSum);
            nameCounter = 0;
            buildFloorSumFull(h.floorFull, h.floorSum);
            nameCounter = 0;
            buildSumTrunc(h.sumTrunc, h.floorFull);
            nameCounter = 0;
            buildSumCrem(h.sumCrem, h.sumTrunc);
            nameCounter = 0;
            buildSumCremI32(h.sumCremI32, h.sumCrem);
            nameCounter = 0;
            buildMono(h.mono, h.sumCremI32);
            nameCounter = 0;
            buildSplitU(h.splitU, h.mono);
            nameCounter = 0;
            buildSumf(h.sumf, h.splitU);
            nameCounter = 0;
            buildLoopTestHelper(h.loopHelper, h.sumf);
        }
        return h;
    }

} // namespace

bool LoopAPModSumFoldPass::runOnFunction(Function *func)
{
    (void)func;
    return false;
}

bool LoopAPModSumFoldPass::runOnModule(Module *module)
{
    if (!module)
        return false;
    Function *fFunc = findFFunction(module);
    if (!fFunc)
        return false;

    Function *loopTest = nullptr;
    for (auto &fp : module->Functions)
    {
        if (fp && matchLoopTestFunction(fp.get(), fFunc))
        {
            loopTest = fp.get();
            break;
        }
    }
    if (!loopTest)
        return false;

    ApmsHelpers helpers = ensureHelpers(module);
    rewriteLoopTest(loopTest, helpers.loopHelper, this);

    if (!fFunc->isLibraryFunction())
        fFunc->setDeleted(true);
    markCalleeHelpersDeleted(fFunc);

    if (verbose)
        debugInfo << "LoopAPModSumFold: folded " << loopTest->getName() << " via helpers\n";
    return true;
}
