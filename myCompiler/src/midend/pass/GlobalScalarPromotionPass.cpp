#include "GlobalScalarPromotionPass.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
using namespace std;
using namespace optimization;

namespace
{
static int nameCounter = 0;

static string freshName(const string &prefix)
{
    return prefix + "_" + to_string(nameCounter++);
}

static Value *stripCopy(Value *v)
{
    while (auto *cpy = dynamic_cast<CopyInst *>(v))
        v = cpy->getSource();
    return v;
}

static bool sameValue(Value *a, Value *b)
{
    return stripCopy(a) == stripCopy(b);
}

static BasicBlock *getLoopLatch(const Loop &loop)
{
    BasicBlock *header = loop.header;
    if (!header)
        return nullptr;
    BasicBlock *latch = nullptr;
    for (auto *pred : header->getPredecessors())
    {
        if (loop.containsBlock(pred) && pred != loop.getPreheader())
        {
            if (latch)
                return nullptr;
            latch = pred;
        }
    }
    return latch;
}

static GlobalVariable *resolveScalarGlobal(Value *ptr)
{
    ptr = stripCopy(ptr);
    while (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr))
        ptr = gep->getPointerOperand();
    auto *gv = dynamic_cast<GlobalVariable *>(ptr);
    if (!gv || gv->isArray())
        return nullptr;
    return gv;
}

static set<GlobalVariable *> collectCandidateGlobals(Function *func)
{
    set<GlobalVariable *> globals;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            Value *ptr = nullptr;
            if (auto *load = dynamic_cast<LoadInst *>(instPtr.get()))
                ptr = load->getPointer();
            else if (auto *store = dynamic_cast<StoreInst *>(instPtr.get()))
                ptr = store->getPointer();
            else
                continue;
            if (auto *gv = resolveScalarGlobal(ptr))
                globals.insert(gv);
        }
    }
    return globals;
}

static bool valueUsesLoadOfGlobal(Value *v, GlobalVariable *gv, unordered_set<Value *> &visited)
{
    v = stripCopy(v);
    if (!visited.insert(v).second)
        return false;
    if (auto *load = dynamic_cast<LoadInst *>(v))
        return resolveScalarGlobal(load->getPointer()) == gv;
    if (auto *bin = dynamic_cast<BinaryOperator *>(v))
        return valueUsesLoadOfGlobal(bin->getLHS(), gv, visited) ||
               valueUsesLoadOfGlobal(bin->getRHS(), gv, visited);
    return false;
}

static bool loopStoresDeriveFromGlobalLoad(const Loop &loop, GlobalVariable *gv)
{
    for (auto *bb : loop.blocks)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            auto *store = dynamic_cast<StoreInst *>(instPtr.get());
            if (!store)
                continue;
            if (resolveScalarGlobal(store->getPointer()) != gv)
                continue;
            unordered_set<Value *> visited;
            if (!valueUsesLoadOfGlobal(store->getValueToStore(), gv, visited))
                return false;
        }
    }
    return true;
}

static void replaceLoadsOfGlobalInLoop(const Loop &loop, GlobalVariable *gv, Value *replacement)
{
    for (auto *bb : loop.blocks)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            if (auto *load = dynamic_cast<LoadInst *>(instPtr.get()))
            {
                if (resolveScalarGlobal(load->getPointer()) == gv)
                    load->replaceAllUsesWith(replacement);
            }
        }
    }
}

static Value *findExistingPreheaderLoad(BasicBlock *preheader, GlobalVariable *gv)
{
    Value *lastLoad = nullptr;
    for (auto &instPtr : preheader->getInstructions())
    {
        if (auto *load = dynamic_cast<LoadInst *>(instPtr.get()))
        {
            if (resolveScalarGlobal(load->getPointer()) == gv)
                lastLoad = load;
        }
    }
    return lastLoad;
}

static bool exitBlockIsOnlyGlobalEpilogue(BasicBlock *bb, GlobalVariable *gv)
{
    for (auto &instPtr : bb->getInstructions())
    {
        if (instPtr->isTerminator())
            break;
        if (dynamic_cast<CallInst *>(instPtr.get()))
            return false;
        if (auto *store = dynamic_cast<StoreInst *>(instPtr.get()))
        {
            if (resolveScalarGlobal(store->getPointer()) != gv)
                return false;
        }
    }
    return true;
}

static Value *getWritebackValue(BasicBlock *bb, Value *startCur)
{
    if (!bb || !startCur)
        return startCur;
    Value *cur = startCur;
    for (auto &instPtr : bb->getInstructions())
    {
        Instruction *inst = instPtr.get();
        if (dynamic_cast<CallInst *>(inst) || dynamic_cast<StoreInst *>(inst))
            break;
        if (cur != startCur)
        {
            for (Value *op : inst->getOperands())
            {
                if (op == cur)
                    return cur;
            }
        }
        if (auto *bin = dynamic_cast<BinaryOperator *>(inst))
        {
            if (bin->getLHS() == cur || bin->getRHS() == cur)
                cur = bin;
        }
    }
    return cur;
}

static bool branchesToBlock(BasicBlock *from, BasicBlock *to)
{
    auto *br = dynamic_cast<BranchInst *>(from->getTerminator());
    if (!br)
        return false;
    if (br->getTrueBlock() == to)
        return true;
    if (br->isConditional() && br->getFalseBlock() == to)
        return true;
    return false;
}

static void insertAtBlockStartAfterPhis(BasicBlock *bb, Instruction *inst)
{
    auto &insts = bb->getInstructions();
    size_t pos = 0;
    while (pos < insts.size() && dynamic_cast<PhiInst *>(insts[pos].get()))
        ++pos;
    bb->insert(unique_ptr<Instruction>(inst), static_cast<unsigned>(pos));
}

static void insertBeforeTerminator(BasicBlock *bb, Instruction *inst)
{
    auto &insts = bb->getInstructions();
    unsigned insertAt = static_cast<unsigned>(insts.size());
    for (unsigned i = 0; i < insts.size(); ++i)
    {
        if (insts[i]->isTerminator())
            insertAt = i;
    }
    bb->insert(unique_ptr<Instruction>(inst), insertAt);
}

// Value of @gv after executing the preheader (last store wins; ignores gp_* temps).
static Value *getPreheaderEndScalar(BasicBlock *preheader, GlobalVariable *gv)
{
    Value *cur = nullptr;
    for (auto &instPtr : preheader->getInstructions())
    {
        Instruction *inst = instPtr.get();
        if (inst->getName().find("gp_") == 0)
            continue;
        if (auto *load = dynamic_cast<LoadInst *>(inst))
        {
            if (resolveScalarGlobal(load->getPointer()) == gv)
                cur = load;
        }
        else if (auto *store = dynamic_cast<StoreInst *>(inst))
        {
            if (resolveScalarGlobal(store->getPointer()) == gv)
                cur = store->getValueToStore();
        }
    }
    return cur;
}

static void insertGlobalWritebacks(GlobalVariable *gv,
                                   const unordered_map<BasicBlock *, Value *> &funcVal,
                                   const unordered_map<BasicBlock *, Value *> &loopOutVal,
                                   const Loop &loop,
                                   const set<BasicBlock *> &loopExitTargets)
{
    (void)funcVal;
    for (BasicBlock *exitBB : loopExitTargets)
    {
        Value *startCur = nullptr;
        for (auto *bb : loop.blocks)
        {
            if (!branchesToBlock(bb, exitBB))
                continue;
            auto lit = loopOutVal.find(bb);
            if (lit != loopOutVal.end())
            {
                startCur = lit->second;
                break;
            }
        }
        if (!startCur)
        {
            if (auto fit = funcVal.find(exitBB); fit != funcVal.end())
                startCur = fit->second;
        }
        if (!startCur)
            continue;

        Value *val = exitBlockIsOnlyGlobalEpilogue(exitBB, gv)
                         ? getWritebackValue(exitBB, startCur)
                         : startCur;
        auto &insts = exitBB->getInstructions();
        unsigned insertAt = static_cast<unsigned>(insts.size());
        for (unsigned i = 0; i < insts.size(); ++i)
        {
            if (insts[i]->isTerminator())
                insertAt = i;
        }
        exitBB->insert(make_unique<StoreInst>(val, gv), insertAt);
    }
}

static void insertPhiIntoBlock(BasicBlock *header, PhiInst *phi)
{
    auto &insts = header->getInstructions();
    size_t pos = 0;
    while (pos < insts.size() && dynamic_cast<PhiInst *>(insts[pos].get()))
        ++pos;
    insts.insert(insts.begin() + static_cast<long>(pos), unique_ptr<Instruction>(phi));
}

static Value *findExistingPostLoopPhi(BasicBlock *bb)
{
    for (auto &instPtr : bb->getInstructions())
    {
        if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
        {
            if (phi->getName().find("gp_post_phi") != string::npos)
                return phi;
        }
        else
        {
            break;
        }
    }
    return nullptr;
}

static Value *materializeMergedValue(BasicBlock *bb, const vector<pair<BasicBlock *, Value *>> &predVals)
{
    if (predVals.empty())
        return nullptr;
    if (Value *existing = findExistingPostLoopPhi(bb))
        return existing;
    Value *first = predVals[0].second;
    bool allSame = true;
    for (size_t i = 1; i < predVals.size(); ++i)
    {
        if (!sameValue(first, predVals[i].second))
        {
            allSame = false;
            break;
        }
    }
    if (allSame)
        return first;

    auto *phi = new PhiInst(IntegerType::getInstance(), freshName("gp_post_phi"));
    insertPhiIntoBlock(bb, phi);
    for (const auto &pv : predVals)
        phi->addIncoming(pv.second, pv.first);
    return phi;
}

static void insertStoreBeforeTerminator(BasicBlock *bb, Value *val, GlobalVariable *gv)
{
    auto &insts = bb->getInstructions();
    unsigned insertAt = static_cast<unsigned>(insts.size());
    for (unsigned i = 0; i < insts.size(); ++i)
    {
        if (insts[i]->isTerminator())
            insertAt = i;
    }
    bb->insert(make_unique<StoreInst>(val, gv), insertAt);
}

static void insertStoreAfterPhis(BasicBlock *bb, Value *val, GlobalVariable *gv)
{
    auto &insts = bb->getInstructions();
    unsigned insertAt = 0;
    while (insertAt < insts.size() && dynamic_cast<PhiInst *>(insts[insertAt].get()))
        ++insertAt;
    bb->insert(make_unique<StoreInst>(val, gv), insertAt);
}

static void replaceLoadsOfGlobalInBlocks(Function *func, GlobalVariable *gv,
                                         const unordered_map<BasicBlock *, Value *> &valMap)
{
    for (const auto &entry : valMap)
    {
        BasicBlock *bb = entry.first;
        Value *val = entry.second;
        if (!bb || !val)
            continue;
        for (auto &instPtr : bb->getInstructions())
        {
            if (auto *load = dynamic_cast<LoadInst *>(instPtr.get()))
            {
                if (resolveScalarGlobal(load->getPointer()) == gv)
                    load->replaceAllUsesWith(val);
            }
        }
    }
}

static bool instructionStillInFunction(Function *func, Instruction *inst)
{
    if (!inst)
        return false;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            if (instPtr.get() == inst)
                return true;
        }
    }
    return false;
}

static bool isSafeFlushValue(Function *func, Value *val)
{
    if (!val)
        return false;
    if (dynamic_cast<Constant *>(val) || dynamic_cast<GlobalVariable *>(val) ||
        dynamic_cast<Argument *>(val))
        return true;
    if (auto *inst = dynamic_cast<Instruction *>(val))
        return instructionStillInFunction(func, inst);
    return false;
}

static void flushGlobalStores(Function *func, GlobalVariable *gv, const Loop &loop,
                              const unordered_map<BasicBlock *, Value *> &funcVal)
{
    for (const auto &entry : funcVal)
    {
        if (loop.containsBlock(entry.first))
            continue;
        if (!isSafeFlushValue(func, entry.second))
            continue;

        bool hasGpPostPhi = false;
        for (auto &instPtr : entry.first->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
            {
                if (phi->getName().find("gp_post_phi") != string::npos)
                    hasGpPostPhi = true;
            }
            else
            {
                break;
            }
        }
        if (!hasGpPostPhi)
            continue;

        insertStoreAfterPhis(entry.first, entry.second, gv);
    }
}

static set<BasicBlock *> collectLoopExitTargets(const Loop &loop)
{
    set<BasicBlock *> targets;
    for (auto *bb : loop.blocks)
    {
        auto *br = dynamic_cast<BranchInst *>(bb->getTerminator());
        if (!br)
            continue;
        if (br->getTrueBlock() && !loop.containsBlock(br->getTrueBlock()))
            targets.insert(br->getTrueBlock());
        if (br->isConditional() && br->getFalseBlock() && !loop.containsBlock(br->getFalseBlock()))
            targets.insert(br->getFalseBlock());
    }
    if (targets.empty() && loop.header)
    {
        auto *br = dynamic_cast<BranchInst *>(loop.header->getTerminator());
        if (br && br->isConditional())
        {
            if (br->getFalseBlock() && !loop.containsBlock(br->getFalseBlock()))
                targets.insert(br->getFalseBlock());
            if (br->getTrueBlock() && !loop.containsBlock(br->getTrueBlock()))
                targets.insert(br->getTrueBlock());
        }
    }
    return targets;
}
} // namespace

GlobalVariable *GlobalScalarPromotionPass::getScalarGlobal(Value *ptr)
{
    return resolveScalarGlobal(ptr);
}

bool GlobalScalarPromotionPass::isDirectAccessToGlobal(Value *ptr, GlobalVariable *gv)
{
    return getScalarGlobal(ptr) == gv;
}

bool GlobalScalarPromotionPass::loopMayModifyViaCall(const Loop &loop, GlobalVariable *gv)
{
    for (auto *bb : loop.blocks)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            auto *call = dynamic_cast<CallInst *>(instPtr.get());
            if (!call)
                continue;
            if (call->HasModifiedArray(gv))
                return true;
        }
    }
    return false;
}

bool GlobalScalarPromotionPass::hasStoreToGlobal(const Loop &loop, GlobalVariable *gv)
{
    for (auto *bb : loop.blocks)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            if (auto *store = dynamic_cast<StoreInst *>(instPtr.get()))
            {
                if (isDirectAccessToGlobal(store->getPointer(), gv))
                    return true;
            }
        }
    }
    return false;
}

bool GlobalScalarPromotionPass::hasLoadToGlobal(const Loop &loop, GlobalVariable *gv)
{
    for (auto *bb : loop.blocks)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            if (auto *load = dynamic_cast<LoadInst *>(instPtr.get()))
            {
                if (isDirectAccessToGlobal(load->getPointer(), gv))
                    return true;
            }
        }
    }
    return false;
}

void GlobalScalarPromotionPass::insertPhiAtHeader(BasicBlock *header, PhiInst *phi)
{
    auto &insts = header->getInstructions();
    size_t pos = 0;
    while (pos < insts.size() && dynamic_cast<PhiInst *>(insts[pos].get()))
        ++pos;
    insts.insert(insts.begin() + static_cast<long>(pos), unique_ptr<Instruction>(phi));
}

void GlobalScalarPromotionPass::insertBeforeTerminator(BasicBlock *bb, Instruction *inst)
{
    bb->insertBeforeTerminator(unique_ptr<Instruction>(inst));
}

bool GlobalScalarPromotionPass::branchesTo(BasicBlock *from, BasicBlock *to)
{
    return branchesToBlock(from, to);
}

bool GlobalScalarPromotionPass::propagateGlobalOutsideLoop(
    Function *func, GlobalVariable *gv, const Loop &loop,
    const unordered_map<BasicBlock *, Value *> &loopOutVal,
    const set<BasicBlock *> &loopExitTargets)
{
    unordered_map<BasicBlock *, Value *> funcVal;
    bool progress = true;
    int iterLimit = static_cast<int>(func->getBasicBlocks().size()) + 4;

    while (progress && iterLimit-- > 0)
    {
        progress = false;
        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            if (loop.containsBlock(bb))
                continue;
            if (bb == loop.getPreheader())
                continue;
            if (loopExitTargets.count(bb))
                continue;

            Value *inVal = nullptr;
            const auto &preds = bb->getPredecessors();
            if (preds.empty())
            {
                inVal = findExistingPreheaderLoad(bb, gv);
                if (!inVal)
                    continue;
            }
            else
            {
                vector<pair<BasicBlock *, Value *>> predVals;
                size_t expectedPreds = 0;
                for (auto *pred : preds)
                {
                    if (!branchesTo(pred, bb))
                        continue;
                    expectedPreds++;
                    Value *pv = nullptr;
                    if (loop.containsBlock(pred))
                    {
                        auto lit = loopOutVal.find(pred);
                        if (lit == loopOutVal.end())
                        {
                            predVals.clear();
                            break;
                        }
                        pv = lit->second;
                    }
                    else
                    {
                        auto fit = funcVal.find(pred);
                        if (fit == funcVal.end())
                        {
                            predVals.clear();
                            break;
                        }
                        pv = fit->second;
                    }
                    predVals.emplace_back(pred, pv);
                }
                if (predVals.size() != expectedPreds)
                    continue;
                if (predVals.empty())
                    continue;
                inVal = materializeMergedValue(bb, predVals);
                if (!inVal)
                    continue;
            }

            Value *newOut = applyBlock(gv, bb, inVal);
            auto it = funcVal.find(bb);
            if (it == funcVal.end() || !sameValue(it->second, newOut))
            {
                funcVal[bb] = newOut;
                progress = true;
            }
        }
    }

    replaceLoadsOfGlobalInBlocks(func, gv, funcVal);
    flushGlobalStores(func, gv, loop, funcVal);
    return true;
}

Value *GlobalScalarPromotionPass::mergePredValues(const vector<Value *> &vals)
{
    if (vals.empty())
        return nullptr;
    Value *first = vals[0];
    for (size_t i = 1; i < vals.size(); ++i)
    {
        if (!sameValue(first, vals[i]))
            return nullptr;
    }
    return first;
}

static Value *simulateBlock(GlobalVariable *gv, BasicBlock *bb, Value *in)
{
    Value *cur = in;
    for (auto &instPtr : bb->getInstructions())
    {
        Instruction *inst = instPtr.get();
        if (auto *load = dynamic_cast<LoadInst *>(inst))
        {
            if (!resolveScalarGlobal(load->getPointer()) || resolveScalarGlobal(load->getPointer()) != gv)
                continue;
            continue;
        }
        if (auto *store = dynamic_cast<StoreInst *>(inst))
        {
            if (!resolveScalarGlobal(store->getPointer()) || resolveScalarGlobal(store->getPointer()) != gv)
                continue;
            cur = store->getValueToStore();
            continue;
        }
        if (auto *bin = dynamic_cast<BinaryOperator *>(inst))
        {
            Opcode op = bin->getOpcode();
            if ((op == Opcode::Add || op == Opcode::Sub || op == Opcode::SRem || op == Opcode::Mul) &&
                (bin->getLHS() == cur || bin->getRHS() == cur))
                cur = bin;
        }
    }
    return cur;
}

Value *GlobalScalarPromotionPass::applyBlock(GlobalVariable *gv, BasicBlock *bb, Value *in)
{
    Value *cur = in;
    auto &insts = bb->getInstructions();
    for (auto it = insts.begin(); it != insts.end();)
    {
        Instruction *inst = it->get();
        if (auto *load = dynamic_cast<LoadInst *>(inst))
        {
            if (!isDirectAccessToGlobal(load->getPointer(), gv))
            {
                ++it;
                continue;
            }
            if (sameValue(load, in))
            {
                ++it;
                continue;
            }
            Value *replacement = sameValue(cur, load) ? in : cur;
            load->replaceAllUsesWith(replacement);
            if (sameValue(cur, load))
                cur = in;
            load->removeThisFromOperands();
            it = insts.erase(it);
            continue;
        }
        if (auto *store = dynamic_cast<StoreInst *>(inst))
        {
            if (!isDirectAccessToGlobal(store->getPointer(), gv))
            {
                ++it;
                continue;
            }
            cur = store->getValueToStore();
            store->removeThisFromOperands();
            it = insts.erase(it);
            continue;
        }
        if (auto *bin = dynamic_cast<BinaryOperator *>(inst))
        {
            Opcode op = bin->getOpcode();
            if ((op == Opcode::Add || op == Opcode::Sub || op == Opcode::SRem || op == Opcode::Mul) &&
                (bin->getLHS() == cur || bin->getRHS() == cur))
                cur = bin;
        }
        ++it;
    }
    return cur;
}

bool GlobalScalarPromotionPass::promoteGlobalInLoop(Function *func, Loop &loop, GlobalVariable *gv)
{
    (void)func;
    if (!loop.header || loop.blocks.size() < 2)
        return false;
    if (loop.header->getName().find("_unroll_") != string::npos)
        return false;
    if (!hasStoreToGlobal(loop, gv) || !hasLoadToGlobal(loop, gv))
        return false;
    if (!loopStoresDeriveFromGlobalLoad(loop, gv))
        return false;
    if (loopMayModifyViaCall(loop, gv))
        return false;

    loop.computePreheader();
    BasicBlock *preheader = loop.getPreheader();
    if (!preheader)
        return false;

    Value *preheaderScalar = getPreheaderEndScalar(preheader, gv);
    LoadInst *preLoad = nullptr;
    if (!preheaderScalar)
    {
        preLoad = new LoadInst(gv, freshName("gp_load"));
        insertBeforeTerminator(preheader, preLoad);
        preheaderScalar = preLoad;
    }

    auto *promotedPhi = new PhiInst(IntegerType::getInstance(), freshName("gp_phi"));
    insertPhiAtHeader(loop.header, promotedPhi);
    for (auto *pred : loop.header->getPredecessors())
    {
        if (!loop.containsBlock(pred))
            promotedPhi->addIncoming(preheaderScalar, pred);
    }

    unordered_map<BasicBlock *, Value *> outVal;
    bool progress = true;
    int iterLimit = static_cast<int>(loop.blocks.size()) + 4;

    while (progress && iterLimit-- > 0)
    {
        progress = false;
        for (auto *bb : loop.blocks)
        {
            Value *inVal = nullptr;
            if (bb == loop.header)
            {
                inVal = promotedPhi;
            }
            else
            {
                vector<Value *> predsIn;
                for (auto *pred : bb->getPredecessors())
                {
                    if (!loop.containsBlock(pred))
                        continue;
                    auto pit = outVal.find(pred);
                    if (pit == outVal.end())
                    {
                        predsIn.clear();
                        break;
                    }
                    predsIn.push_back(pit->second);
                }
                if (predsIn.empty())
                    continue;
                inVal = mergePredValues(predsIn);
                if (!inVal)
                    return false;
            }

            Value *newOut = simulateBlock(gv, bb, inVal);
            auto it = outVal.find(bb);
            if (it == outVal.end() || !sameValue(it->second, newOut))
            {
                outVal[bb] = newOut;
                progress = true;
            }
        }
    }

    BasicBlock *latch = getLoopLatch(loop);
    if (!latch)
        return false;
    auto latchIt = outVal.find(latch);
    if (latchIt == outVal.end())
        return false;

    for (auto *bb : loop.blocks)
    {
        Value *inVal = nullptr;
        if (bb == loop.header)
            inVal = promotedPhi;
        else
        {
            vector<Value *> predsIn;
            for (auto *pred : bb->getPredecessors())
            {
                if (!loop.containsBlock(pred))
                    continue;
                auto pit = outVal.find(pred);
                if (pit == outVal.end())
                {
                    predsIn.clear();
                    break;
                }
                predsIn.push_back(pit->second);
            }
            if (predsIn.empty())
                continue;
            inVal = mergePredValues(predsIn);
            if (!inVal)
                return false;
        }
        applyBlock(gv, bb, inVal);
    }

    promotedPhi->addIncoming(latchIt->second, latch);
    replaceLoadsOfGlobalInLoop(loop, gv, promotedPhi);

    set<BasicBlock *> loopExitTargets = collectLoopExitTargets(loop);
    for (BasicBlock *exitBB : loopExitTargets)
    {
        Value *wbVal = latchIt->second;
        for (auto *bb : loop.blocks)
        {
            if (!branchesToBlock(bb, exitBB))
                continue;
            if (auto it = outVal.find(bb); it != outVal.end())
            {
                wbVal = it->second;
                break;
            }
        }
        if (exitBlockIsOnlyGlobalEpilogue(exitBB, gv))
            wbVal = getWritebackValue(exitBB, wbVal);
        insertStoreAfterPhis(exitBB, wbVal, gv);
    }

    propagateGlobalOutsideLoop(func, gv, loop, outVal, loopExitTargets);

    if (verbose)
    {
        debugInfo << "GlobalScalarPromotion: promoted @" << gv->getName() << " in loop "
                  << loop.header->getName() << "\n";
    }
    return true;
}

bool GlobalScalarPromotionPass::runOnFunction(Function *func)
{
    bool changed = false;
    func->setLoops(ControlFlowAnalysis::findLoops(func));
    auto &loops = func->getLoops();

    set<GlobalVariable *> globals = collectCandidateGlobals(func);
    if (globals.empty())
        return false;

    vector<size_t> order(loops.size());
    for (size_t i = 0; i < loops.size(); ++i)
        order[i] = i;
    sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return loops[a].blocks.size() < loops[b].blocks.size();
    });

    for (auto *gv : globals)
    {
        for (size_t idx : order)
            changed |= promoteGlobalInLoop(func, loops[idx], gv);
    }

    return changed;
}
