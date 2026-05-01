#include "PolyhedralLoopPass.h"
#include "ControlFlowAnalysis.h"
#include <set>

using namespace std;
using namespace optimization;

namespace
{
bool replaceBranchTarget(BasicBlock *from, BasicBlock *oldSucc, BasicBlock *newSucc)
{
    if (!from || !oldSucc || !newSucc)
        return false;
    auto *br = dynamic_cast<BranchInst *>(from->getTerminator());
    if (!br)
        return false;

    bool changed = false;
    if (br->isConditional())
    {
        if (br->getTrueBlock() == oldSucc)
        {
            br->setTrueBlock(newSucc);
            changed = true;
        }
        if (br->getFalseBlock() == oldSucc)
        {
            br->setFalseBlock(newSucc);
            changed = true;
        }
    }
    else if (br->getTrueBlock() == oldSucc)
    {
        br->setTrueBlock(newSucc);
        changed = true;
    }

    if (!changed)
        return false;

    from->removeSuccessor(oldSucc);
    oldSucc->removePredecessor(from);
    from->addSuccessor(newSucc);
    newSucc->addPredecessor(from);
    return true;
}

bool blockHasPhi(BasicBlock *bb)
{
    if (!bb)
        return false;
    for (auto &instPtr : bb->getInstructions())
        if (dynamic_cast<PhiInst *>(instPtr.get()))
            return true;
    return false;
}
} // namespace

bool PolyhedralLoopOptimizePass::tryParseAffine1D(Value *v, Affine1D &out)
{
    out = {};
    if (!v)
        return false;
    if (auto *phi = dynamic_cast<PhiInst *>(v))
    {
        out.phi = phi;
        out.offset = 0;
        out.valid = true;
        return true;
    }
    if (auto *bin = dynamic_cast<BinaryOperator *>(v))
    {
        if (bin->getOpcode() == Opcode::Add)
        {
            Value *a = bin->getLHS();
            Value *b = bin->getRHS();
            auto *ca = dynamic_cast<ConstantInt *>(a);
            auto *cb = dynamic_cast<ConstantInt *>(b);
            Affine1D inner{};
            if (ca && tryParseAffine1D(b, inner))
            {
                out.phi = inner.phi;
                out.offset = inner.offset + ca->Value;
                out.valid = true;
                return true;
            }
            if (cb && tryParseAffine1D(a, inner))
            {
                out.phi = inner.phi;
                out.offset = inner.offset + cb->Value;
                out.valid = true;
                return true;
            }
        }
        else if (bin->getOpcode() == Opcode::Sub)
        {
            auto *cb = dynamic_cast<ConstantInt *>(bin->getRHS());
            Affine1D inner{};
            if (cb && tryParseAffine1D(bin->getLHS(), inner))
            {
                out.phi = inner.phi;
                out.offset = inner.offset - cb->Value;
                out.valid = true;
                return true;
            }
        }
    }
    return false;
}

bool PolyhedralLoopOptimizePass::tryGet2DIndices(GetElementPtrInst *gep, Affine1D &row, Affine1D &col)
{
    vector<Value *> idx = gep->getIndices();
    if (idx.size() < 2)
        return false;
    return tryParseAffine1D(idx[idx.size() - 2], row) && tryParseAffine1D(idx.back(), col);
}

void PolyhedralLoopOptimizePass::findNestedLoopPairs(const vector<Loop> &loops,
                                                     vector<pair<const Loop *, const Loop *>> &out)
{
    set<pair<size_t, size_t>> seen;
    for (size_t oi = 0; oi < loops.size(); ++oi)
    {
        const Loop &O = loops[oi];
        if (!O.header)
            continue;
        for (size_t ii = 0; ii < loops.size(); ++ii)
        {
            if (oi == ii)
                continue;
            const Loop &I = loops[ii];
            if (!I.header)
                continue;
            if (!O.containsBlock(I.header))
                continue;
            if (I.blocks.size() > O.blocks.size())
                continue;
            if (!seen.insert({oi, ii}).second)
                continue;
            out.emplace_back(&loops[oi], &loops[ii]);
        }
    }
}

static Value *stripCast(Value *v)
{
    while (auto *castInst = dynamic_cast<CastInst *>(v))
        v = castInst->getOperands()[0];
    return v;
}

bool PolyhedralLoopOptimizePass::addLoopTag(BasicBlock *bb, const string &tag)
{
    if (!bb)
        return false;
    const string oldName = bb->getName();
    if (oldName.find(tag) != string::npos)
        return false;
    bb->setName(oldName + tag);
    return true;
}

bool PolyhedralLoopOptimizePass::matchCanonicalLoopShape(const Loop *loop, CanonicalLoopShape &out) const
{
    out = {};
    if (!loop || !loop->header)
        return false;
    out.header = loop->header;

    // preheader: unique predecessor outside loop
    for (BasicBlock *pred : out.header->getPredecessors())
    {
        if (!loop->containsBlock(pred))
        {
            if (out.preheader)
                return false;
            out.preheader = pred;
        }
    }
    if (!out.preheader)
        return false;

    // header terminator must be conditional branch: body / exit
    auto *hBr = dynamic_cast<BranchInst *>(out.header->getTerminator());
    if (!hBr || !hBr->isConditional())
        return false;
    BasicBlock *succA = hBr->getTrueBlock();
    BasicBlock *succB = hBr->getFalseBlock();
    const bool aIn = loop->containsBlock(succA);
    const bool bIn = loop->containsBlock(succB);
    if (aIn == bIn)
        return false;
    out.body = aIn ? succA : succB;
    out.exit = aIn ? succB : succA;

    // latch: predecessor of header in loop (excluding header self-edge patterns unsupported)
    for (BasicBlock *pred : out.header->getPredecessors())
    {
        if (loop->containsBlock(pred) && pred != out.header)
        {
            if (out.latch)
                return false;
            out.latch = pred;
        }
    }
    if (!out.latch)
        return false;

    // induction phi + compare + step
    auto *icmp = dynamic_cast<ICmpInst *>(hBr->getCondition());
    if (!icmp)
        return false;
    out.bound = nullptr;
    out.indPhi = nullptr;
    for (Value *side : {icmp->getLHS(), icmp->getRHS()})
    {
        if (auto *p = dynamic_cast<PhiInst *>(side))
            out.indPhi = p;
    }
    if (!out.indPhi)
        return false;
    out.bound = (icmp->getLHS() == out.indPhi) ? icmp->getRHS() : icmp->getLHS();

    if (out.indPhi->getNumIncomingValues() != 2)
        return false;
    Value *stepV = nullptr;
    for (unsigned i = 0; i < out.indPhi->getNumIncomingValues(); ++i)
    {
        if (out.indPhi->getIncomingBlock(i) == out.latch)
            stepV = out.indPhi->getIncomingValue(i);
    }
    auto *stepOp = dynamic_cast<BinaryOperator *>(stepV);
    if (!stepOp || (stepOp->getOpcode() != Opcode::Add && stepOp->getOpcode() != Opcode::Sub))
        return false;
    Value *lhs = stepOp->getLHS();
    Value *rhs = stepOp->getRHS();
    auto *cL = dynamic_cast<ConstantInt *>(lhs);
    auto *cR = dynamic_cast<ConstantInt *>(rhs);
    if (lhs != out.indPhi && rhs != out.indPhi)
        return false;
    if (!cL && !cR)
        return false;
    int step = 0;
    if (stepOp->getOpcode() == Opcode::Add)
    {
        step = cL ? cL->Value : cR->Value;
    }
    else
    {
        // phi - c
        if (!cR || lhs != out.indPhi)
            return false;
        step = -cR->Value;
    }
    if (step == 0)
        return false;
    out.step = step;
    out.isInc = step > 0;
    out.valid = true;
    return true;
}

bool PolyhedralLoopOptimizePass::isPerfectTwoLevelNest(const CanonicalLoopShape &outer, const CanonicalLoopShape &inner)
{
    if (!outer.valid || !inner.valid)
        return false;
    // perfect-nest skeleton (while form):
    // outer.body -> inner.header (no side path)
    // inner.exit == outer.latch
    // inner.preheader == outer.body
    if (inner.preheader != outer.body)
        return false;
    if (inner.exit != outer.latch)
        return false;

    auto *obr = dynamic_cast<BranchInst *>(outer.body->getTerminator());
    if (!obr || obr->isConditional())
        return false;
    if (obr->getTrueBlock() != inner.header)
        return false;

    // outer.body should mostly be "inner init + branch".
    // Keep conservative: allow phi/copy/init/arithmetic/cast and terminator only.
    for (auto &instPtr : outer.body->getInstructions())
    {
        Instruction *inst = instPtr.get();
        if (inst->isTerminator())
            continue;
        if (dynamic_cast<PhiInst *>(inst) || dynamic_cast<CopyInst *>(inst) ||
            dynamic_cast<BinaryOperator *>(inst) || dynamic_cast<CastInst *>(inst) ||
            dynamic_cast<ICmpInst *>(inst))
            continue;
        return false;
    }
    return true;
}

bool PolyhedralLoopOptimizePass::buildInterchangePlan(const CanonicalLoopShape &outer, const CanonicalLoopShape &inner,
                                                      InterchangePlan &plan)
{
    plan = {};
    if (!outer.valid || !inner.valid)
        return false;
    if (outer.indPhi->getNumIncomingValues() != 2 || inner.indPhi->getNumIncomingValues() != 2)
        return false;

    for (unsigned i = 0; i < outer.indPhi->getNumIncomingValues(); ++i)
    {
        if (outer.indPhi->getIncomingBlock(i) == outer.preheader)
            plan.outerInit = outer.indPhi->getIncomingValue(i);
        if (outer.indPhi->getIncomingBlock(i) == outer.latch)
            plan.outerStepInst = dynamic_cast<Instruction *>(outer.indPhi->getIncomingValue(i));
    }
    for (unsigned i = 0; i < inner.indPhi->getNumIncomingValues(); ++i)
    {
        if (inner.indPhi->getIncomingBlock(i) == inner.preheader)
            plan.innerInit = inner.indPhi->getIncomingValue(i);
        if (inner.indPhi->getIncomingBlock(i) == inner.latch)
            plan.innerStepInst = dynamic_cast<Instruction *>(inner.indPhi->getIncomingValue(i));
    }
    if (!plan.outerInit || !plan.innerInit || !plan.outerStepInst || !plan.innerStepInst)
        return false;

    // Conservative: inner preheader should provide induction init directly (copy/const/arg are allowed).
    auto isInitLike = [](Value *v) -> bool {
        return dynamic_cast<ConstantInt *>(v) || dynamic_cast<Argument *>(v) ||
               dynamic_cast<CopyInst *>(v) || dynamic_cast<BinaryOperator *>(v) ||
               dynamic_cast<PhiInst *>(v);
    };
    if (!isInitLike(plan.outerInit) || !isInitLike(plan.innerInit))
        return false;

    plan.valid = true;
    return true;
}

int PolyhedralLoopOptimizePass::extractStepConst(PhiInst *phi, Instruction *stepInst, bool &ok)
{
    ok = false;
    auto *bin = dynamic_cast<BinaryOperator *>(stepInst);
    if (!bin)
        return 0;
    if (bin->getOpcode() == Opcode::Add)
    {
        if (bin->getLHS() == phi)
        {
            if (auto *c = dynamic_cast<ConstantInt *>(bin->getRHS()))
            {
                ok = true;
                return c->Value;
            }
        }
        if (bin->getRHS() == phi)
        {
            if (auto *c = dynamic_cast<ConstantInt *>(bin->getLHS()))
            {
                ok = true;
                return c->Value;
            }
        }
    }
    if (bin->getOpcode() == Opcode::Sub && bin->getLHS() == phi)
    {
        if (auto *c = dynamic_cast<ConstantInt *>(bin->getRHS()))
        {
            ok = true;
            return -c->Value;
        }
    }
    return 0;
}

void PolyhedralLoopOptimizePass::swapPhiUsesInBlock(BasicBlock *bb, PhiInst *a, PhiInst *b)
{
    if (!bb || !a || !b || a == b)
        return;
    for (auto &instPtr : bb->getInstructions())
    {
        Instruction *inst = instPtr.get();
        if (dynamic_cast<PhiInst *>(inst))
            continue;
        for (unsigned i = 0; i < inst->getNumOperands(); ++i)
        {
            Value *op = inst->getOperandByIndex(i);
            if (op == a)
                inst->setOperandByIndex(i, b);
            else if (op == b)
                inst->setOperandByIndex(i, a);
        }
    }
}

bool PolyhedralLoopOptimizePass::tryInterchangeToExposeParallel(const Loop *outer, const Loop *inner,
                                                                const vector<pair<int, int>> &depVectors,
                                                                PhiInst *outerPhi, PhiInst *innerPhi)
{
    // Stage-1 executable gate:
    // only proceed on canonical perfect nests and only when dependence suggests
    // interchange could expose a DOALL dimension.
    CanonicalLoopShape oShape{}, iShape{};
    if (!matchCanonicalLoopShape(outer, oShape) || !matchCanonicalLoopShape(inner, iShape))
        return false;
    if (!oShape.valid || !iShape.valid)
        return false;
    if (!isPerfectTwoLevelNest(oShape, iShape))
        return false;
    if (!outerPhi || !innerPhi || oShape.indPhi != outerPhi || iShape.indPhi != innerPhi)
        return false;
    InterchangePlan plan{};
    if (!buildInterchangePlan(oShape, iShape, plan) || !plan.valid)
        return false;

    bool carryOuter = false;
    bool carryInner = false;
    for (auto [dOuter, dInner] : depVectors)
    {
        carryOuter |= (dOuter != 0);
        carryInner |= (dInner != 0);
    }
    // Need transformation only when current inner carries deps.
    if (!carryInner)
        return false;
    // Require lex legality after swap to keep conservative.
    if (!interchangeLegalLex(depVectors))
        return false;

    // Stage-2: execute restricted interchange rewrite on canonical perfect nest.
    bool okOuterStep = false, okInnerStep = false;
    int outerStepC = extractStepConst(outerPhi, plan.outerStepInst, okOuterStep);
    int innerStepC = extractStepConst(innerPhi, plan.innerStepInst, okInnerStep);
    if (!okOuterStep || !okInnerStep)
        return false;

    // 1) swap loop-carried values at phi boundaries
    // outer loop becomes old-inner iterator
    for (unsigned i = 0; i < outerPhi->getNumIncomingValues(); ++i)
    {
        if (outerPhi->getIncomingBlock(i) == oShape.preheader)
            outerPhi->setIncomingValue(i, plan.innerInit);
    }
    // inner loop becomes old-outer iterator
    for (unsigned i = 0; i < innerPhi->getNumIncomingValues(); ++i)
    {
        if (innerPhi->getIncomingBlock(i) == iShape.preheader)
            innerPhi->setIncomingValue(i, plan.outerInit);
    }

    // create new carried steps at corresponding latches
    auto *newOuterStep = new BinaryOperator(
        Opcode::Add,
        outerPhi,
        new ConstantInt(IntegerType::getInstance(), innerStepC),
        outerPhi->getName() + "_poly_inter_step");
    oShape.latch->insertBeforeTerminator(unique_ptr<Instruction>(newOuterStep));
    auto *newInnerStep = new BinaryOperator(
        Opcode::Add,
        innerPhi,
        new ConstantInt(IntegerType::getInstance(), outerStepC),
        innerPhi->getName() + "_poly_inter_step");
    iShape.latch->insertBeforeTerminator(unique_ptr<Instruction>(newInnerStep));
    for (unsigned i = 0; i < outerPhi->getNumIncomingValues(); ++i)
    {
        if (outerPhi->getIncomingBlock(i) == oShape.latch)
            outerPhi->setIncomingValue(i, newOuterStep);
    }
    for (unsigned i = 0; i < innerPhi->getNumIncomingValues(); ++i)
    {
        if (innerPhi->getIncomingBlock(i) == iShape.latch)
            innerPhi->setIncomingValue(i, newInnerStep);
    }

    // 2) swap compare bounds
    auto *oCmp = dynamic_cast<ICmpInst *>(dynamic_cast<BranchInst *>(oShape.header->getTerminator())->getCondition());
    auto *iCmp = dynamic_cast<ICmpInst *>(dynamic_cast<BranchInst *>(iShape.header->getTerminator())->getCondition());
    if (!oCmp || !iCmp)
        return false;
    for (unsigned i = 0; i < oCmp->getNumOperands(); ++i)
    {
        Value *op = oCmp->getOperandByIndex(i);
        if (op == outerPhi)
            oCmp->setOperandByIndex(i, innerPhi);
        else if (op == oShape.bound)
            oCmp->setOperandByIndex(i, iShape.bound);
    }
    for (unsigned i = 0; i < iCmp->getNumOperands(); ++i)
    {
        Value *op = iCmp->getOperandByIndex(i);
        if (op == innerPhi)
            iCmp->setOperandByIndex(i, outerPhi);
        else if (op == iShape.bound)
            iCmp->setOperandByIndex(i, oShape.bound);
    }

    // 3) swap variable usage in inner loop computations
    for (BasicBlock *bb : inner->blocks)
    {
        if (bb == iShape.header)
            continue;
        swapPhiUsesInBlock(bb, outerPhi, innerPhi);
    }

    bool changed = false;
    changed |= addLoopTag(oShape.header, ".poly.interchange.outer");
    changed |= addLoopTag(iShape.header, ".poly.interchange.inner");
    changed |= addLoopTag(iShape.body, ".poly.interchange.body");
    changed |= addLoopTag(iShape.exit, ".poly.interchange.latch");
    changed |= addLoopTag(oShape.preheader, ".poly.interchange.applied");
    if (verbose && changed)
        debugInfo << "  rewrite: perfect-nest interchange applied (restricted SSA/CFG-safe form)\n";
    return changed;
}

PhiInst *PolyhedralLoopOptimizePass::findInductionPhi(BasicBlock *header)
{
    if (!header)
        return nullptr;
    auto *term = dynamic_cast<BranchInst *>(header->getTerminator());
    if (!term || !term->isConditional())
        return nullptr;
    auto *icmp = dynamic_cast<ICmpInst *>(term->getCondition());
    if (!icmp)
        return nullptr;
    for (Value *side : {icmp->getLHS(), icmp->getRHS()})
    {
        if (auto *p = dynamic_cast<PhiInst *>(side))
            return p;
        Affine1D af{};
        if (tryParseAffine1D(side, af) && af.valid && af.phi)
            return dynamic_cast<PhiInst *>(af.phi);
    }
    return nullptr;
}

bool PolyhedralLoopOptimizePass::interchangeLegalLex(const vector<pair<int, int>> &depVectors)
{
    auto lexPositive = [](int a, int b) -> bool {
        if (a != 0)
            return a > 0;
        if (b != 0)
            return b > 0;
        return true;
    };
    // 原嵌套按 (外行,内列)；交换后迭代字典序对应向量 (Δ_col, Δ_outer)，须保持正向
    for (auto [dr, dc] : depVectors)
    {
        if (!lexPositive(dc, dr))
            return false;
    }
    return true;
}

void PolyhedralLoopOptimizePass::collectDepsVerbose(const Loop *outer, const Loop *inner,
                                                     vector<pair<int, int>> &depVectors)
{
    depVectors.clear();

    auto considerPair = [&](GetElementPtrInst *gepS, Affine1D sr, Affine1D sc,
                            GetElementPtrInst *gepL, Affine1D lr, Affine1D lc) {
        Value *baseS = stripCast(gepS->getPointerOperand());
        Value *baseL = stripCast(gepL->getPointerOperand());
        if (baseS != baseL)
            return;

        if (sr.phi != lr.phi || sc.phi != lc.phi || !sr.phi || !sc.phi)
            return;

        int dr = sr.offset - lr.offset;
        int dc = sc.offset - lc.offset;
        depVectors.emplace_back(dr, dc);
    };

    for (BasicBlock *bb : inner->blocks)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            auto *store = dynamic_cast<StoreInst *>(inst);
            if (!store)
                continue;
            auto *gepS = dynamic_cast<GetElementPtrInst *>(stripCast(store->getPointer()));
            if (!gepS)
                continue;
            Affine1D sr{}, sc{};
            if (!tryGet2DIndices(gepS, sr, sc))
                continue;

            for (auto &otherPtr : bb->getInstructions())
            {
                auto *load = dynamic_cast<LoadInst *>(otherPtr.get());
                if (!load)
                    continue;
                auto *gepL = dynamic_cast<GetElementPtrInst *>(stripCast(load->getPointer()));
                if (!gepL)
                    continue;
                Affine1D lr{}, lc{};
                if (!tryGet2DIndices(gepL, lr, lc))
                    continue;
                considerPair(gepS, sr, sc, gepL, lr, lc);
            }
        }
    }

    const bool hadDeps = !depVectors.empty();
    if (verbose && hadDeps)
    {
        debugInfo << "[PolyhedralLoopOptimize] nested loops outer_header=" << outer->header->getName()
                  << " inner_header=" << inner->header->getName() << "\n";

        bool depAlongOuter = false;
        bool depAlongInner = false;
        for (auto [dr, dc] : depVectors)
        {
            debugInfo << "  approx dependence delta (store index - load index): (" << dr << ", " << dc << ")\n";
            if (dr != 0)
                depAlongOuter = true;
            if (dc != 0)
                depAlongInner = true;
        }

        debugInfo << "  heuristic: dependence along outer dimension (first index delta nonzero): "
                  << (depAlongOuter ? "yes" : "no") << "\n";
        debugInfo << "  heuristic: dependence along inner dimension (second index delta nonzero): "
                  << (depAlongInner ? "yes" : "no") << "\n";
        if (!depAlongOuter && depAlongInner)
            debugInfo << "  (notes) outer-i loop may be parallelizable if inner carries all forward deps "
                         "(cf. diagonal skew / wavefront)\n";
    }
}

bool PolyhedralLoopOptimizePass::maybeOptimizeNest(const Loop *outer, const Loop *inner)
{
    vector<pair<int, int>> depVectors;
    collectDepsVerbose(outer, inner, depVectors);

    if (depVectors.empty())
        return false;

    PhiInst *outerPhi = findInductionPhi(outer->header);
    PhiInst *innerPhi = findInductionPhi(inner->header);
    if (!outerPhi || !innerPhi)
        return false;

    bool changed = tryInterchangeToExposeParallel(outer, inner, depVectors, outerPhi, innerPhi);

    if (verbose && !interchangeLegalLex(depVectors))
        debugInfo << "  interchange hint: skipped (dependence not lex-forward after dimension swap)\n";
    else if (verbose && changed)
        debugInfo << "  interchange hint: rewrite committed\n";

    (void)innerPhi;
    (void)outerPhi;
    return changed;
}

bool PolyhedralLoopOptimizePass::runOnFunction(Function *func)
{
    bool changed = false;
    bool localChanged = false;
    do
    {
        localChanged = false;
        func->setLoops(ControlFlowAnalysis::findLoops(func));
        const auto loops = func->getLoops();
        if (loops.empty())
            return changed;

        vector<pair<const Loop *, const Loop *>> pairs;
        findNestedLoopPairs(loops, pairs);

        for (auto &pr : pairs)
        {
            if (maybeOptimizeNest(pr.first, pr.second))
            {
                changed = true;
                localChanged = true;
                break;
            }
        }
        if (localChanged)
            continue;

        for (const auto &L : loops)
        {
            if (tryTileLoop(func, &L))
            {
                changed = true;
                localChanged = true;
                break;
            }
        }
        if (localChanged)
            continue;

        if (tryFuseAdjacentLoops(func, loops))
        {
            changed = true;
            localChanged = true;
        }
    } while (localChanged);

    return changed;
}

// 保守 tiling：只处理 canonical loop, step == 1, 且退出块没有 phi，避免复杂 SSA 迁移
bool PolyhedralLoopOptimizePass::tryTileLoop(Function *func, const Loop *loop, int tileSize)
{
    if (!func || !loop || !loop->header)
        return false;
    CanonicalLoopShape shape{};
    if (!matchCanonicalLoopShape(loop, shape) || !shape.valid)
        return false;
    if (shape.step != 1)
        return false;
    if (blockHasPhi(shape.exit))
        return false;

    auto *headerBr = dynamic_cast<BranchInst *>(shape.header->getTerminator());
    auto *headerCmp = headerBr ? dynamic_cast<ICmpInst *>(headerBr->getCondition()) : nullptr;
    if (!headerBr || !headerBr->isConditional() || !headerCmp)
        return false;

    Value *initValue = nullptr;
    for (unsigned i = 0; i < shape.indPhi->getNumIncomingValues(); ++i)
    {
        if (shape.indPhi->getIncomingBlock(i) == shape.preheader)
        {
            initValue = shape.indPhi->getIncomingValue(i);
            break;
        }
    }
    if (!initValue)
        return false;

    // 插入 tile loop 的新基本块
    auto *tileHeader = new BasicBlock(shape.header->getName() + ".tile_header", func);
    auto *tileBody = new BasicBlock(shape.header->getName() + ".tile_body", func);
    auto *tileLatch = new BasicBlock(shape.header->getName() + ".tile_latch", func);

    auto *tilePhi = new PhiInst(shape.indPhi->getType(), shape.indPhi->getName() + ".tile");
    tileHeader->addInstruction(unique_ptr<Instruction>(tilePhi));
    tilePhi->addIncoming(initValue, shape.preheader);

    auto *tileCmp = new ICmpInst(ICmpInst::ICMP_SLT, tilePhi, shape.bound, shape.header->getName() + ".tile_cmp");
    tileHeader->addInstruction(unique_ptr<Instruction>(tileCmp));
    tileHeader->addInstruction(unique_ptr<Instruction>(new BranchInst(tileCmp, tileBody, shape.exit)));

    auto *tileEndCand = new BinaryOperator(
        Opcode::Add, tilePhi, new ConstantInt(IntegerType::getInstance(), tileSize),
        shape.header->getName() + ".tile_endcand");
    tileBody->addInstruction(unique_ptr<Instruction>(tileEndCand));
    auto *tileEndCmp = new ICmpInst(ICmpInst::ICMP_SLT, tileEndCand, shape.bound, shape.header->getName() + ".tile_endcmp");
    tileBody->addInstruction(unique_ptr<Instruction>(tileEndCmp));
    auto *tileEnd = new SelectInst(tileEndCmp, tileEndCand, shape.bound, shape.header->getName() + ".tile_end");
    tileBody->addInstruction(unique_ptr<Instruction>(tileEnd));
    tileBody->addInstruction(unique_ptr<Instruction>(new BranchInst(shape.header)));

    auto *tileNext = new BinaryOperator(
        Opcode::Add, tilePhi, new ConstantInt(IntegerType::getInstance(), tileSize),
        shape.header->getName() + ".tile_next");
    tileLatch->addInstruction(unique_ptr<Instruction>(tileNext));
    tileLatch->addInstruction(unique_ptr<Instruction>(new BranchInst(tileHeader)));
    tilePhi->addIncoming(tileNext, tileLatch);

    // 让原 preheader 进入 tile loop
    if (!replaceBranchTarget(shape.preheader, shape.header, tileHeader))
    {
        return false;
    }

    // 原 header 的退出边改为 tileLatch，进入下一 tile
    if (!replaceBranchTarget(shape.header, shape.exit, tileLatch))
    {
        return false;
    }

    // header compare 的边界改成 tileEnd；induction phi 的 preheader incoming 改成 tilePhi
    for (unsigned i = 0; i < shape.indPhi->getNumIncomingValues(); ++i)
    {
        if (shape.indPhi->getIncomingBlock(i) == shape.preheader)
        {
            shape.indPhi->setIncomingBlock(i, tileBody);
            shape.indPhi->setIncomingValue(i, tilePhi);
            break;
        }
    }

    for (unsigned i = 0; i < headerCmp->getNumOperands(); ++i)
    {
        if (headerCmp->getOperandByIndex(i) == shape.bound)
        {
            headerCmp->setOperandByIndex(i, tileEnd);
        }
    }

    // tile body / latch 的 CFG 边
    tileBody->addSuccessor(shape.header);
    shape.header->addPredecessor(tileBody);
    tileLatch->addSuccessor(tileHeader);
    tileHeader->addPredecessor(tileLatch);

    func->addBasicBlock(unique_ptr<BasicBlock>(tileHeader));
    func->addBasicBlock(unique_ptr<BasicBlock>(tileBody));
    func->addBasicBlock(unique_ptr<BasicBlock>(tileLatch));

    if (verbose)
    {
        debugInfo << "PolyhedralLoopOptimize: tiled loop " << shape.header->getName()
                  << " with tileSize=" << tileSize << "\n";
    }
    return true;
}

// 保守 fusion：两个相邻 canonical loop，要求 trip shape 一致、第二个 loop 无外部依赖
bool PolyhedralLoopOptimizePass::tryFuseAdjacentLoops(Function *func, const vector<Loop> &loops)
{
    if (!func)
        return false;

    for (size_t i = 0; i < loops.size(); ++i)
    {
        CanonicalLoopShape first{}, second{};
        if (!matchCanonicalLoopShape(&loops[i], first) || !first.valid)
            continue;
        if (blockHasPhi(first.exit))
            continue;

        Value *firstInit = nullptr;
        for (unsigned k = 0; k < first.indPhi->getNumIncomingValues(); ++k)
        {
            if (first.indPhi->getIncomingBlock(k) == first.preheader)
            {
                firstInit = first.indPhi->getIncomingValue(k);
                break;
            }
        }
        if (!firstInit)
            continue;

        for (size_t j = 0; j < loops.size(); ++j)
        {
            if (i == j)
                continue;

            if (!matchCanonicalLoopShape(&loops[j], second) || !second.valid)
                continue;
            if (second.preheader != first.exit)
                continue;
            if (blockHasPhi(second.exit))
                continue;
            if (first.step != second.step || first.bound != second.bound)
                continue;

            Value *secondInit = nullptr;
            for (unsigned k = 0; k < second.indPhi->getNumIncomingValues(); ++k)
            {
                if (second.indPhi->getIncomingBlock(k) == second.preheader)
                {
                    secondInit = second.indPhi->getIncomingValue(k);
                    break;
                }
            }
            if (!secondInit || secondInit != firstInit)
                continue;

            // 第二个 loop 不能有任何对 loop 外的使用
            bool safe = true;
            for (BasicBlock *bb : loops[j].blocks)
            {
                for (auto &instPtr : bb->getInstructions())
                {
                    Instruction *inst = instPtr.get();
                    if (!inst)
                        continue;
                    if (inst->hasExternalUse(loops[j]))
                    {
                        safe = false;
                        break;
                    }
                }
                if (!safe)
                    break;
            }
            if (!safe)
                continue;

            // 把第二个 loop 的 body 指令克隆到第一个 loop body 中
            std::unordered_map<Value *, Value *> valueMap;
            valueMap[second.indPhi] = first.indPhi;
            auto &dstInsts = first.body->getInstructions();
            auto insertPos = dstInsts.size();
            if (!dstInsts.empty() && dstInsts.back()->isTerminator())
                --insertPos;

            for (auto &instPtr : second.body->getInstructions())
            {
                Instruction *inst = instPtr.get();
                if (!inst || inst->isTerminator())
                    continue;
                if (dynamic_cast<PhiInst *>(inst))
                    continue;

                Instruction *cloned = inst->clone();
                cloned->setName(inst->getName() + ".fused");
                for (size_t k = 0; k < cloned->getNumOperands(); ++k)
                {
                    Value *oldOp = cloned->getOperandByIndex(k);
                    auto it = valueMap.find(oldOp);
                    if (it != valueMap.end())
                        cloned->setOperandByIndex(k, it->second);
                }
                valueMap[inst] = cloned;
                dstInsts.insert(dstInsts.begin() + insertPos++, unique_ptr<Instruction>(cloned));
            }

            // 原 loop1 的退出块直接跳到 second 的 exit
            if (!replaceBranchTarget(first.header, first.exit, second.exit))
                continue;

            // 删除 second loop 的块：preheader/header/body
            vector<BasicBlock *> toRemove;
            toRemove.push_back(second.preheader);
            for (BasicBlock *bb : loops[j].blocks)
                toRemove.push_back(bb);
            for (BasicBlock *bb : toRemove)
            {
                if (bb)
                    bb->removeSelfBasicBlock();
            }

            auto &bbs = func->getBasicBlocks();
            for (auto it = bbs.begin(); it != bbs.end();)
            {
                BasicBlock *bb = it->get();
                if (find(toRemove.begin(), toRemove.end(), bb) != toRemove.end())
                {
                    needToDelete.push_back(it->release());
                    it = bbs.erase(it);
                }
                else
                {
                    ++it;
                }
            }

            if (verbose)
            {
                debugInfo << "PolyhedralLoopOptimize: fused adjacent loops "
                          << first.header->getName() << " and " << second.header->getName() << "\n";
            }
            return true;
        }
    }
    return false;
}
