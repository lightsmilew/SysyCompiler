#include "LoopFusionPass.h"
#include "ControlFlowAnalysis.h"
#include <algorithm>
#include <functional>
#include <limits>
#include <queue>
#include <set>
#include <unordered_map>
#include <unordered_set>

using namespace std;
using namespace optimization;

namespace
{
// 将 from 上指向 oldSucc 的分支改为 newSucc，并同步前驱/后继关系
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

bool preheaderIsTrivialBranch(BasicBlock *bb)
{
    if (!bb)
        return false;
    auto &insts = bb->getInstructions();
    if (insts.empty())
        return false;
    if (insts.size() == 1)
        return insts.back()->isTerminator();
    for (size_t i = 0; i + 1 < insts.size(); ++i)
    {
        if (insts[i]->hasResult())
            return false;
    }
    return insts.back()->isTerminator();
}

bool headerIsSimpleCanonical(BasicBlock *header)
{
    if (!header)
        return false;
    auto &insts = header->getInstructions();
    if (insts.size() < 3)
        return false;

    bool seenCmp = false;
    for (size_t i = 0; i < insts.size(); ++i)
    {
        Instruction *inst = insts[i].get();
        if (!inst)
            return false;
        if (dynamic_cast<PhiInst *>(inst))
            continue;
        if (auto *icmp = dynamic_cast<ICmpInst *>(inst))
        {
            (void)icmp;
            if (seenCmp)
                return false;
            seenCmp = true;
            continue;
        }
        if (i + 1 == insts.size() && inst->isTerminator())
            continue;
        return false;
    }
    return seenCmp;
}

bool isClonablePreheaderInst(Instruction *inst)
{
    if (!inst)
        return false;
    if (inst->isTerminator())
        return true;
    if (dynamic_cast<CopyInst *>(inst) || dynamic_cast<BinaryOperator *>(inst) ||
        dynamic_cast<ICmpInst *>(inst) || dynamic_cast<CastInst *>(inst) ||
        dynamic_cast<GetElementPtrInst *>(inst))
        return true;
    if (auto *call = dynamic_cast<CallInst *>(inst))
    {
        Function *func = call->getCalledFunction();
        if (!func)
            return false;
        const string &name = func->getName();
        if (name.find("sysy") != string::npos || name == "starttime" || name == "stoptime")
            return true;
    }
    return false;
}

Value *stripCopyChain(Value *v)
{
    while (auto *cpy = dynamic_cast<CopyInst *>(v))
    {
        Value *src = cpy->getSource();
        if (!src || src == v)
            break;
        v = src;
    }
    return v;
}

bool initValueEquivalent(Value *a, Value *b)
{
    if (a == b)
        return true;
    a = stripCopyChain(a);
    b = stripCopyChain(b);
    if (a == b)
        return true;
    auto *ca = dynamic_cast<ConstantInt *>(a);
    auto *cb = dynamic_cast<ConstantInt *>(b);
    if (ca && cb)
        return ca->Value == cb->Value;
    return false;
}

Value *resolveMappedValue(Value *v, const std::unordered_map<Value *, Value *> &vm)
{
    Value *cur = v;
    for (int guard = 0; guard < 32 && cur; ++guard)
    {
        auto it = vm.find(cur);
        if (it == vm.end())
            break;
        if (it->second == cur)
            break;
        cur = it->second;
    }
    return cur;
}

void removePhiIncomingFromBlock(PhiInst *phi, BasicBlock *block)
{
    if (!phi || !block)
        return;
    for (int idx = static_cast<int>(phi->getNumIncomingValues()) - 1; idx >= 0; --idx)
    {
        if (phi->getIncomingBlock(static_cast<unsigned>(idx)) == block)
            phi->removeIncoming(static_cast<unsigned>(idx));
    }
}

void applyValueMapToBlocks(const vector<BasicBlock *> &blocks,
                           const std::unordered_map<Value *, Value *> &vm)
{
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (BasicBlock *bb : blocks)
        {
            if (!bb)
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                Instruction *inst = instPtr.get();
                if (!inst)
                    continue;
                if (auto *phi = dynamic_cast<PhiInst *>(inst))
                {
                    for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
                    {
                        Value *iv = phi->getIncomingValue(k);
                        Value *mapped = resolveMappedValue(iv, vm);
                        if (mapped != iv)
                        {
                            phi->setIncomingValue(k, mapped);
                            changed = true;
                        }
                    }
                }
                for (size_t k = 0; k < inst->getNumOperands(); ++k)
                {
                    Value *op = inst->getOperandByIndex(k);
                    Value *mapped = resolveMappedValue(op, vm);
                    if (mapped != op)
                    {
                        inst->setOperandByIndex(k, mapped);
                        changed = true;
                    }
                }
            }
        }
    }
}

void expandInductionCopyMap(const Loop &loop, PhiInst * /*oldInd*/,
                            std::unordered_map<Value *, Value *> &vm)
{
    bool changed = true;
    while (changed)
    {
        changed = false;
        for (BasicBlock *bb : loop.blocks)
        {
            if (!bb)
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
                if (!cpy || vm.count(cpy))
                    continue;
                Value *src = resolveMappedValue(cpy->getSource(), vm);
                if (src != cpy->getSource())
                {
                    vm[cpy] = src;
                    changed = true;
                }
            }
        }
    }
}

void remapSecondLoopInduction(const Loop &secondLoop, PhiInst *secondInd, PhiInst *firstInd,
                              std::unordered_map<Value *, Value *> &valueMap)
{
    valueMap[secondInd] = firstInd;
    expandInductionCopyMap(secondLoop, secondInd, valueMap);
    applyValueMapToBlocks(secondLoop.blocks, valueMap);
}

void transferLatchPhiIncoming(PhiInst *firstInd, PhiInst *secondInd, BasicBlock *firstLatch,
                            BasicBlock *secondLatch, BasicBlock *stepIncomingBlock,
                            const std::unordered_map<Value *, Value *> &vm)
{
    removePhiIncomingFromBlock(firstInd, firstLatch);
    removePhiIncomingFromBlock(firstInd, secondLatch);

    for (unsigned k = 0; k < secondInd->getNumIncomingValues(); ++k)
    {
        if (secondInd->getIncomingBlock(k) != secondLatch)
            continue;
        Value *stepVal = resolveMappedValue(secondInd->getIncomingValue(k), vm);
        firstInd->addIncoming(stepVal, stepIncomingBlock);
        return;
    }
}

bool isOrderingSensitiveIo(const CallInst *call)
{
    if (!call)
        return false;
    Function *func = call->getCalledFunction();
    if (!func)
        return false;
    const string &name = func->getName();
    static const set<string> kIoOps = {"getarray",  "getfarray", "getint",   "getch",
                                       "getfloat",  "putarray",  "putfarray", "putint",
                                       "putch",     "putfloat"};
    return kIoOps.count(name) > 0;
}

bool valueReferencesPhi(Value *v, PhiInst *phi)
{
    v = stripCopyChain(v);
    return v == phi;
}

bool isInductionBumpInst(Instruction *inst, PhiInst *indPhi)
{
    auto *bin = dynamic_cast<BinaryOperator *>(inst);
    if (!bin || bin->getOpcode() != Opcode::Add)
        return false;
    auto *cR = dynamic_cast<ConstantInt *>(stripCopyChain(bin->getRHS()));
    auto *cL = dynamic_cast<ConstantInt *>(stripCopyChain(bin->getLHS()));
    if (cR && cR->Value == 1 && valueReferencesPhi(bin->getLHS(), indPhi))
        return true;
    if (cL && cL->Value == 1 && valueReferencesPhi(bin->getRHS(), indPhi))
        return true;
    return false;
}

Instruction *findInductionBumpInBlock(BasicBlock *bb, PhiInst *indPhi)
{
    if (!bb || !indPhi)
        return nullptr;
    auto &insts = bb->getInstructions();
    for (int idx = static_cast<int>(insts.size()) - 2; idx >= 0; --idx)
    {
        Instruction *inst = insts[static_cast<size_t>(idx)].get();
        if (isInductionBumpInst(inst, indPhi))
            return inst;
    }
    return nullptr;
}

bool phiDefinedInBlock(PhiInst *phi, BasicBlock *bb)
{
    if (!phi || !bb)
        return false;
    for (auto &instPtr : bb->getInstructions())
    {
        if (instPtr.get() == phi)
            return true;
    }
    return false;
}

PhiInst *findLocalInductionPhiInHeader(BasicBlock *header, BasicBlock *latch)
{
    if (!header || !latch)
        return nullptr;
    for (auto &instPtr : header->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi)
            break;
        if (phi->getNumIncomingValues() != 2)
            continue;
        Value *stepV = nullptr;
        for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
        {
            if (phi->getIncomingBlock(k) == latch)
                stepV = phi->getIncomingValue(k);
        }
        auto *stepOp = dynamic_cast<BinaryOperator *>(stepV);
        if (!stepOp || stepOp->getOpcode() != Opcode::Add)
            continue;
        if (isInductionBumpInst(stepOp, phi))
            return phi;
    }
    return nullptr;
}

void refineLoopInductionPhi(BasicBlock *header, BasicBlock *latch, PhiInst *&indPhi)
{
    PhiInst *local = findLocalInductionPhiInHeader(header, latch);
    if (!local)
        return;
    if (!indPhi || !phiDefinedInBlock(indPhi, header))
        indPhi = local;
}

void setPhiIncomingForBlock(PhiInst *phi, BasicBlock *from, Value *val)
{
    if (!phi || !from || !val)
        return;
    for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
    {
        if (phi->getIncomingBlock(k) == from)
        {
            phi->setIncomingValue(k, val);
            return;
        }
    }
    phi->addIncoming(val, from);
}

ConstantInt *zeroI32Constant()
{
    return new ConstantInt(IntegerType::getInstance(), 0);
}

Value *findOuterBumpValueInExit(BasicBlock *exit, Value *outerIv)
{
    if (!exit || !outerIv)
        return nullptr;
    outerIv = stripCopyChain(outerIv);
    for (auto &instPtr : exit->getInstructions())
    {
        auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get());
        if (!bin || (bin->getOpcode() != Opcode::Add && bin->getOpcode() != Opcode::Sub))
            continue;
        if (stripCopyChain(bin->getLHS()) == outerIv || stripCopyChain(bin->getRHS()) == outerIv)
            return bin;
    }
    return nullptr;
}

PhiInst *getOrCreateHeaderInductionPhi(BasicBlock *header, BasicBlock *latch, Value *indHint)
{
    if (!header)
        return nullptr;
    PhiInst *ind = findLocalInductionPhiInHeader(header, latch);
    if (ind)
        return ind;

    Value *iv = indHint;
    if (!iv)
    {
        for (auto &instPtr : header->getInstructions())
        {
            if (auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get()))
            {
                iv = icmp->getLHS();
                break;
            }
        }
    }
    if (!iv)
        return nullptr;

    auto *phi = new PhiInst(iv->getType(), iv->getName() + ".fused");
    auto &insts = header->getInstructions();
    size_t insertPos = 0;
    for (size_t i = 0; i < insts.size(); ++i)
    {
        if (dynamic_cast<PhiInst *>(insts[i].get()))
            insertPos = i + 1;
        else
            break;
    }
    insts.insert(insts.begin() + static_cast<long>(insertPos),
                 std::unique_ptr<Instruction>(phi));
    ind = phi;

    for (auto &instPtr : header->getInstructions())
    {
        if (auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get()))
        {
            if (stripCopyChain(icmp->getLHS()) == stripCopyChain(iv))
                icmp->setOperandByIndex(0, ind);
        }
    }
    return ind;
}

void repairFullScanSecondInnerHeaderPhi(BasicBlock *firstHeader, BasicBlock *secondHeader,
                                        BasicBlock *secondLatch, BasicBlock *secondPreheader,
                                        Value *indHint)
{
    PhiInst *ind = getOrCreateHeaderInductionPhi(secondHeader, secondLatch, indHint);
    if (!ind || !firstHeader)
        return;

    if (secondPreheader)
        removePhiIncomingFromBlock(ind, secondPreheader);
    setPhiIncomingForBlock(ind, firstHeader, zeroI32Constant());

    if (secondLatch)
    {
        Instruction *bump = findInductionBumpInBlock(secondLatch, ind);
        if (bump)
            setPhiIncomingForBlock(ind, secondLatch, bump);
    }
}

vector<BasicBlock *> collectBlocksUntilExit(BasicBlock *header, BasicBlock *loopExit)
{
    vector<BasicBlock *> region;
    if (!header)
        return region;
    std::unordered_set<BasicBlock *> visited;
    std::queue<BasicBlock *> work;
    work.push(header);
    while (!work.empty())
    {
        BasicBlock *bb = work.front();
        work.pop();
        if (!bb || !visited.insert(bb).second)
            continue;
        region.push_back(bb);
        if (bb == loopExit)
            continue;
        for (BasicBlock *succ : bb->getSuccessors())
        {
            if (succ && visited.count(succ) == 0)
                work.push(succ);
        }
    }
    return region;
}

void replaceValueInBlocks(const vector<BasicBlock *> &blocks, Value *from, Value *to)
{
    if (!from || !to || stripCopyChain(from) == stripCopyChain(to))
        return;
    for (BasicBlock *bb : blocks)
    {
        if (!bb)
            continue;
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (!inst)
                continue;
            for (size_t k = 0; k < inst->getNumOperands(); ++k)
            {
                if (stripCopyChain(inst->getOperandByIndex(k)) == stripCopyChain(from))
                    inst->setOperandByIndex(k, to);
            }
        }
    }
}

void repairSkippedOuterSuccessorEntry(BasicBlock *junctionExit, BasicBlock *skippedOuterExit,
                                      BasicBlock *entry, BasicBlock *loopExit)
{
    if (!junctionExit || !skippedOuterExit || !entry)
        return;

    Value *staleIv = nullptr;
    ICmpInst *entryCmp = nullptr;
    for (auto &instPtr : entry->getInstructions())
    {
        if (auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get()))
        {
            entryCmp = icmp;
            staleIv = icmp->getLHS();
            break;
        }
        if (!instPtr->isTerminator() && !dynamic_cast<PhiInst *>(instPtr.get()))
            break;
    }

    auto &entryInsts = entry->getInstructions();
    PhiInst *entryPhi = new PhiInst(IntegerType::getInstance(), "fused.outer.succ");
    entryInsts.insert(entryInsts.begin(), std::unique_ptr<Instruction>(entryPhi));
    entryPhi->addIncoming(zeroI32Constant(), junctionExit);

    if (staleIv && loopExit)
    {
        vector<BasicBlock *> region = collectBlocksUntilExit(entry, loopExit);
        replaceValueInBlocks(region, staleIv, entryPhi);
    }

    if (entryCmp)
        entryCmp->setOperandByIndex(0, entryPhi);

    if (loopExit)
    {
        for (BasicBlock *pred : loopExit->getPredecessors())
        {
            if (!pred || pred == entry)
                continue;
            Value *bump = findOuterBumpValueInExit(pred, entryPhi);
            if (bump)
            {
                entryPhi->addIncoming(bump, pred);
                break;
            }
        }
    }
}

void redirectHeaderLoopExit(BasicBlock *header, BasicBlock *body, BasicBlock *newExit)
{
    auto *hBr = dynamic_cast<BranchInst *>(header ? header->getTerminator() : nullptr);
    if (!hBr || !hBr->isConditional() || !body || !newExit)
        return;
    if (hBr->getTrueBlock() == body)
    {
        if (hBr->getFalseBlock() != newExit)
            (void)replaceBranchTarget(header, hBr->getFalseBlock(), newExit);
    }
    else if (hBr->getFalseBlock() == body)
    {
        if (hBr->getTrueBlock() != newExit)
            (void)replaceBranchTarget(header, hBr->getTrueBlock(), newExit);
    }
}

void forceHeaderLoopExitTo(BasicBlock *header, BasicBlock *body, BasicBlock *unifiedExit)
{
    auto *hBr = dynamic_cast<BranchInst *>(header ? header->getTerminator() : nullptr);
    if (!hBr || !hBr->isConditional() || !body || !unifiedExit)
        return;
    if (hBr->getTrueBlock() == body)
    {
        if (hBr->getFalseBlock() != unifiedExit)
            (void)replaceBranchTarget(header, hBr->getFalseBlock(), unifiedExit);
    }
    else if (hBr->getFalseBlock() == body)
    {
        if (hBr->getTrueBlock() != unifiedExit)
            (void)replaceBranchTarget(header, hBr->getTrueBlock(), unifiedExit);
    }
}

void retargetExitPhiPredecessor(BasicBlock *exitBb, BasicBlock *oldPred, BasicBlock *newPred)
{
    if (!exitBb || !oldPred || !newPred || oldPred == newPred)
        return;
    for (auto &instPtr : exitBb->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi)
            continue;
        for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
        {
            if (phi->getIncomingBlock(k) == oldPred)
                phi->setIncomingBlock(k, newPred);
        }
    }
}

bool spliceSecondExitIntoFirstExit(BasicBlock *firstExit, BasicBlock *secondExit, BasicBlock *loopHeader,
                                   std::unordered_map<Value *, Value *> &valueMap)
{
    if (!firstExit || !secondExit || !loopHeader || firstExit == secondExit)
        return false;
    auto &destInsts = firstExit->getInstructions();
    if (destInsts.empty())
        return false;

    size_t insertPos = destInsts.size() - 1;
    for (auto &instPtr : secondExit->getInstructions())
    {
        Instruction *inst = instPtr.get();
        if (!inst || inst->isTerminator())
            continue;

        Instruction *cloned = inst->clone();
        if (auto *phi = dynamic_cast<PhiInst *>(cloned))
        {
            for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
            {
                phi->setIncomingBlock(k, loopHeader);
                Value *iv = resolveMappedValue(phi->getIncomingValue(k), valueMap);
                phi->setIncomingValue(k, iv);
            }
        }
        for (size_t k = 0; k < cloned->getNumOperands(); ++k)
        {
            Value *op = cloned->getOperandByIndex(k);
            auto it = valueMap.find(op);
            if (it != valueMap.end())
                cloned->setOperandByIndex(k, it->second);
        }
        valueMap[inst] = cloned;
        destInsts.insert(destInsts.begin() + static_cast<long>(insertPos++),
                         unique_ptr<Instruction>(cloned));
    }
    return true;
}

void setUnconditionalBranchTarget(BasicBlock *from, BasicBlock *target)
{
    if (!from || !target)
        return;
    auto *br = dynamic_cast<BranchInst *>(from->getTerminator());
    if (!br || br->isConditional())
        return;
    if (br->getTrueBlock() != target)
        replaceBranchTarget(from, br->getTrueBlock(), target);
}

bool mergeSecondHeaderPhisIntoFirst(BasicBlock *firstHeader, BasicBlock *secondHeader,
                                    BasicBlock *secondPreheader, BasicBlock *firstPreheader,
                                    BasicBlock *firstLatch, BasicBlock *secondLatch,
                                    PhiInst *secondIndPhi, PhiInst *firstIndPhi,
                                    std::unordered_map<Value *, Value *> &valueMap)
{
    if (!firstHeader || !secondHeader)
        return false;

    PhiInst *secondLocalInd =
        findLocalInductionPhiInHeader(secondHeader, secondLatch);

    auto &headerInsts = firstHeader->getInstructions();
    size_t insertPos = headerInsts.size();
    for (size_t i = 0; i < headerInsts.size(); ++i)
    {
        if (!dynamic_cast<PhiInst *>(headerInsts[i].get()))
        {
            insertPos = i;
            break;
        }
    }

    for (int idx = static_cast<int>(secondHeader->getInstructions().size()) - 1; idx >= 0; --idx)
    {
        auto *phi = dynamic_cast<PhiInst *>(secondHeader->getInstructions()[static_cast<size_t>(idx)].get());
        if (!phi || phi == secondIndPhi || phi == firstIndPhi)
            continue;
        if (secondLocalInd && phi == secondLocalInd)
            continue;

        auto *mergedPhi = dynamic_cast<PhiInst *>(phi->clone());
        for (unsigned k = 0; k < mergedPhi->getNumIncomingValues(); ++k)
        {
            BasicBlock *from = mergedPhi->getIncomingBlock(k);
            if (from == secondLatch)
                mergedPhi->setIncomingBlock(k, firstLatch);
            else if (from == secondPreheader)
                mergedPhi->setIncomingBlock(k, firstPreheader);
            Value *iv = resolveMappedValue(mergedPhi->getIncomingValue(k), valueMap);
            mergedPhi->setIncomingValue(k, iv);
        }
        valueMap[phi] = mergedPhi;
        headerInsts.insert(headerInsts.begin() + static_cast<long>(insertPos++),
                           unique_ptr<Instruction>(mergedPhi));
    }

    applyValueMapToBlocks({secondHeader}, valueMap);
    return true;
}

bool spliceSecondBodyIntoFirst(BasicBlock *firstBody, BasicBlock *secondBody, BasicBlock *firstHeader,
                               BasicBlock *secondHeader, PhiInst *secondIndPhi, PhiInst *firstIndPhi,
                               std::unordered_map<Value *, Value *> &valueMap)
{
    if (!firstBody || !secondBody || !firstHeader)
        return false;

    auto &bodyInsts = firstBody->getInstructions();
    if (bodyInsts.empty())
        return false;

    for (int idx = static_cast<int>(bodyInsts.size()) - 2; idx >= 0; --idx)
    {
        Instruction *inst = bodyInsts[static_cast<size_t>(idx)].get();
        if (inst && isInductionBumpInst(inst, firstIndPhi))
        {
            bodyInsts.erase(bodyInsts.begin() + idx);
            break;
        }
    }

    size_t insertPos = bodyInsts.size() - 1;
    for (auto &instPtr : secondBody->getInstructions())
    {
        Instruction *inst = instPtr.get();
        if (!inst || inst->isTerminator())
            continue;

        Instruction *cloned = inst->clone();
        for (size_t k = 0; k < cloned->getNumOperands(); ++k)
        {
            Value *op = cloned->getOperandByIndex(k);
            auto it = valueMap.find(op);
            if (it != valueMap.end())
                cloned->setOperandByIndex(k, it->second);
        }
        valueMap[inst] = cloned;
        bodyInsts.insert(bodyInsts.begin() + static_cast<long>(insertPos++),
                         unique_ptr<Instruction>(cloned));
    }

    applyValueMapToBlocks({firstBody, secondBody, secondHeader}, valueMap);

    auto *bodyBr = dynamic_cast<BranchInst *>(firstBody->getTerminator());
    if (!bodyBr)
        return false;
    if (bodyBr->isConditional())
        return false;
    if (bodyBr->getTrueBlock() != firstHeader)
    {
        if (!replaceBranchTarget(firstBody, bodyBr->getTrueBlock(), firstHeader))
            bodyBr->setTrueBlock(firstHeader);
    }
    return true;
}

bool valueMatchesInduction(Value *v, Value *ind)
{
    if (!v || !ind)
        return false;
    return stripCopyChain(v) == stripCopyChain(ind);
}

// 多维 GEP 中除内层归纳变量外的非常数下标（典型为外层行下标）
Value *getLoopRowIndexValue(const Loop &loop, Value *innerInd)
{
    if (!innerInd)
        return nullptr;
    innerInd = stripCopyChain(innerInd);
    Value *row = nullptr;
    for (BasicBlock *bb : loop.blocks)
    {
        if (!bb)
            continue;
        for (auto &instPtr : bb->getInstructions())
        {
            auto *gep = dynamic_cast<GetElementPtrInst *>(instPtr.get());
            if (!gep)
                continue;
            for (Value *idx : gep->getIndices())
            {
                Value *v = stripCopyChain(idx);
                if (dynamic_cast<ConstantInt *>(v))
                    continue;
                if (v == innerInd)
                    continue;
                if (!row)
                    row = idx;
                else if (stripCopyChain(row) != v)
                    return nullptr;
            }
        }
    }
    return row;
}

// 1 = SLT/LT half, 2 = SGE/GE half, 0 = unknown
bool extractIndHalfGuard(ICmpInst *icmp, Value *ind, int &halfKind, Value *&boundOut)
{
    if (!icmp || !ind)
        return false;
    ind = stripCopyChain(ind);
    Value *lhs = stripCopyChain(icmp->getLHS());
    Value *rhs = stripCopyChain(icmp->getRHS());
    Value *bound = nullptr;
    if (lhs == ind)
        bound = icmp->getRHS();
    else if (rhs == ind)
        bound = icmp->getLHS();
    else
        return false;

    switch (icmp->getPredicate())
    {
    case ICmpInst::ICMP_SLT:
    case ICmpInst::ICMP_SLE:
        // ind on LHS: ind <(=) bound (lower); bound on LHS: ind >(=) bound (upper)
        halfKind = (lhs == ind) ? 1 : 2;
        break;
    case ICmpInst::ICMP_SGE:
    case ICmpInst::ICMP_SGT:
        halfKind = (lhs == ind) ? 2 : 1;
        break;
    default:
        return false;
    }
    boundOut = bound;
    return true;
}

bool extractPhiHalfGuard(ICmpInst *icmp, PhiInst *phi, int &halfKind, Value *&boundOut)
{
    return extractIndHalfGuard(icmp, phi, halfKind, boundOut);
}

bool blockHasPhiHalfGuard(BasicBlock *bb, PhiInst *phi, int &halfKind, Value *&boundOut)
{
    if (!bb)
        return false;
    for (auto &instPtr : bb->getInstructions())
    {
        auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get());
        if (!icmp)
            continue;
        if (extractPhiHalfGuard(icmp, phi, halfKind, boundOut))
            return true;
    }
    return false;
}

bool blockHasHalfGuardForInd(BasicBlock *bb, Value *ind, int &halfKind, Value *&boundOut)
{
    if (!bb || !ind)
        return false;
    for (auto &instPtr : bb->getInstructions())
    {
        auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get());
        if (!icmp)
            continue;
        if (extractIndHalfGuard(icmp, ind, halfKind, boundOut))
            return true;
    }
    return false;
}

// slt/sge partition on any induction phi/copy（融合后 header phi 与分支 guard 可能不一致）
bool blockHasHalfGuard(BasicBlock *bb, int &halfKind, Value *&boundOut)
{
    if (!bb)
        return false;
    for (auto &instPtr : bb->getInstructions())
    {
        auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get());
        if (!icmp)
            continue;
        Value *lhs = stripCopyChain(icmp->getLHS());
        Value *rhs = stripCopyChain(icmp->getRHS());
        if (!dynamic_cast<PhiInst *>(lhs) && !dynamic_cast<PhiInst *>(rhs) &&
            !dynamic_cast<CopyInst *>(icmp->getLHS()) && !dynamic_cast<CopyInst *>(icmp->getRHS()))
            continue;
        if (extractIndHalfGuard(icmp, icmp->getLHS(), halfKind, boundOut))
            return true;
        if (extractIndHalfGuard(icmp, icmp->getRHS(), halfKind, boundOut))
            return true;
    }
    return false;
}

bool collectIoHalfGuardsImpl(const Loop &loop, Value *ind, vector<pair<int, Value *>> &guards)
{
    guards.clear();
    if (!ind)
        return true;

    for (BasicBlock *bb : loop.blocks)
    {
        if (!bb)
            continue;
        for (auto &instPtr : bb->getInstructions())
        {
            auto *call = dynamic_cast<CallInst *>(instPtr.get());
            if (!call || !isOrderingSensitiveIo(call))
                continue;

            int halfKind = 0;
            Value *bound = nullptr;
            if (blockHasHalfGuardForInd(bb, ind, halfKind, bound))
            {
                guards.push_back({halfKind, bound});
                continue;
            }

            bool found = false;
            for (BasicBlock *pred : bb->getPredecessors())
            {
                if (!loop.containsBlock(pred))
                    continue;
                if (blockHasHalfGuardForInd(pred, ind, halfKind, bound))
                {
                    guards.push_back({halfKind, bound});
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
    }
    return true;
}

bool collectStoreHalfGuardsImpl(const Loop &loop, Value *ind, vector<pair<int, Value *>> &guards)
{
    guards.clear();
    if (!ind)
        return true;

    for (BasicBlock *bb : loop.blocks)
    {
        if (!bb)
            continue;
        for (auto &instPtr : bb->getInstructions())
        {
            if (!dynamic_cast<StoreInst *>(instPtr.get()))
                continue;

            int halfKind = 0;
            Value *bound = nullptr;
            if (blockHasHalfGuardForInd(bb, ind, halfKind, bound))
            {
                guards.push_back({halfKind, bound});
                continue;
            }

            bool found = false;
            for (BasicBlock *pred : bb->getPredecessors())
            {
                if (!loop.containsBlock(pred))
                    continue;
                if (blockHasHalfGuardForInd(pred, ind, halfKind, bound))
                {
                    guards.push_back({halfKind, bound});
                    found = true;
                    break;
                }
            }
            if (!found)
                return false;
        }
    }
    return true;
}

bool boundsEquivalent(Value *a, Value *b)
{
    if (a == b)
        return true;
    if (initValueEquivalent(a, b))
        return true;
    a = stripCopyChain(a);
    b = stripCopyChain(b);
    if (a == b || initValueEquivalent(a, b))
        return true;

    auto *ba = dynamic_cast<BinaryOperator *>(a);
    auto *bb = dynamic_cast<BinaryOperator *>(b);
    if (!ba || !bb || ba->getOpcode() != bb->getOpcode())
        return false;
    return boundsEquivalent(ba->getLHS(), bb->getLHS()) &&
           boundsEquivalent(ba->getRHS(), bb->getRHS());
}

bool findEntryHalfGuard(BasicBlock *entry, Value *outerInd, int &halfKind, Value *&boundOut,
                        int maxDepth = 4)
{
    if (!entry || !outerInd)
        return false;
    outerInd = stripCopyChain(outerInd);
    std::vector<std::pair<BasicBlock *, int>> work;
    std::set<BasicBlock *> seen;
    work.push_back({entry, 0});
    while (!work.empty())
    {
        auto [bb, depth] = work.back();
        work.pop_back();
        if (!bb || seen.count(bb))
            continue;
        seen.insert(bb);
        if (blockHasHalfGuardForInd(bb, outerInd, halfKind, boundOut))
            return true;
        if (depth >= maxDepth)
            continue;
        for (BasicBlock *pred : bb->getPredecessors())
            work.push_back({pred, depth + 1});
    }
    return false;
}

// glue-body-merge 会把两个循环体无条件拼在一起；若二者由互补的半区 guard 分别保护则不可融合
bool bodyMergeGluePartitionSafe(const Loop &firstLoop, const Loop &secondLoop,
                                BasicBlock *firstPreheader, BasicBlock *secondPreheader,
                                PhiInst *firstIndPhi, PhiInst *secondIndPhi)
{
    Value *outerRow = getLoopRowIndexValue(firstLoop, firstIndPhi);
    if (!outerRow)
        outerRow = getLoopRowIndexValue(secondLoop, secondIndPhi);
    if (!outerRow)
        return true;

    int k1 = 0;
    int k2 = 0;
    Value *b1 = nullptr;
    Value *b2 = nullptr;
    const bool has1 = findEntryHalfGuard(firstPreheader, outerRow, k1, b1);
    const bool has2 = findEntryHalfGuard(secondPreheader, outerRow, k2, b2);
    if (!has1 && !has2)
        return true;
    if (has1 != has2)
        return false;
    if (k1 != k2 && boundsEquivalent(b1, b2))
        return false;
    return true;
}

bool phiHasIncomingFrom(PhiInst *phi, BasicBlock *from)
{
    if (!phi || !from)
        return false;
    for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
    {
        if (phi->getIncomingBlock(k) == from)
            return true;
    }
    return false;
}

Value *phiIncomingFromBlock(PhiInst *phi, BasicBlock *from)
{
    if (!phi || !from)
        return nullptr;
    for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
    {
        if (phi->getIncomingBlock(k) == from)
            return phi->getIncomingValue(k);
    }
    return nullptr;
}

bool instHasUseOutsideLoops(Instruction *inst, const Loop &a, const Loop &b, const Loop *c = nullptr,
                            const Loop *d = nullptr)
{
    if (!inst)
        return false;
    for (auto *user : inst->getUsers())
    {
        auto *userInst = dynamic_cast<Instruction *>(user);
        if (!userInst)
            continue;
        if (a.containsInst(userInst) || b.containsInst(userInst))
            continue;
        if (c && c->containsInst(userInst))
            continue;
        if (d && d->containsInst(userInst))
            continue;
        return true;
    }
    return false;
}

bool instructionInBlock(Instruction *inst, BasicBlock *bb)
{
    if (!inst || !bb)
        return false;
    for (auto &instPtr : bb->getInstructions())
    {
        if (instPtr.get() == inst)
            return true;
    }
    return false;
}

bool instHasUseOutsideLoopExceptExit(Instruction *inst, const Loop &loop, BasicBlock *exit)
{
    if (!inst)
        return false;
    for (auto *user : inst->getUsers())
    {
        auto *userInst = dynamic_cast<Instruction *>(user);
        if (!userInst)
            continue;
        if (loop.containsInst(userInst))
            continue;
        if (exit && instructionInBlock(userInst, exit))
            continue;
        return true;
    }
    return false;
}

void retargetSecondLoopPhiLatches(const Loop &secondLoop, PhiInst *secondIndPhi,
                                  BasicBlock *secondLatch, BasicBlock *firstLatch)
{
    if (!firstLatch || !secondLatch)
        return;
    for (BasicBlock *bb : secondLoop.blocks)
    {
        if (!bb)
            continue;
        for (auto &instPtr : bb->getInstructions())
        {
            auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
            if (!phi || phi == secondIndPhi)
                continue;
            for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
            {
                if (phi->getIncomingBlock(k) == secondLatch)
                    phi->setIncomingBlock(k, firstLatch);
            }
        }
    }
}

bool blockIsConditionalLoopHeader(BasicBlock *bb)
{
    if (!bb)
        return false;
    auto *br = dynamic_cast<BranchInst *>(bb->getTerminator());
    if (!br || !br->isConditional())
        return false;
    for (auto &instPtr : bb->getInstructions())
    {
        if (dynamic_cast<PhiInst *>(instPtr.get()))
            return true;
    }
    return false;
}

bool gluePathLeadsToHeader(const vector<BasicBlock *> &glue, BasicBlock *target, int maxHops = 12)
{
    if (glue.empty() || !target)
        return false;
    queue<BasicBlock *> q;
    unordered_set<BasicBlock *> visited;
    for (BasicBlock *bb : glue)
    {
        if (bb)
            visited.insert(bb);
    }
    if (BasicBlock *tail = glue.back())
        q.push(tail);
    while (!q.empty())
    {
        BasicBlock *cur = q.front();
        q.pop();
        if (cur == target)
            return true;
        if (static_cast<int>(visited.size()) > maxHops + static_cast<int>(glue.size()))
            break;
        auto *br = dynamic_cast<BranchInst *>(cur->getTerminator());
        if (!br)
            continue;
        auto enqueue = [&](BasicBlock *succ) {
            if (!succ || visited.count(succ))
                return;
            visited.insert(succ);
            q.push(succ);
        };
        if (br->isConditional())
        {
            enqueue(br->getTrueBlock());
            enqueue(br->getFalseBlock());
        }
        else
        {
            enqueue(br->getTrueBlock());
        }
    }
    return false;
}

// glue 首块为外层 while 头、路径通向 second.header：应保留 glue 串联，禁止 body-merge 断开
bool gluePathIsWrapperOuterLoop(const vector<BasicBlock *> &glueBlocks, BasicBlock *secondHeader)
{
    if (glueBlocks.empty() || !secondHeader)
        return false;
    if (!blockIsConditionalLoopHeader(glueBlocks.front()))
        return false;
    return gluePathLeadsToHeader(glueBlocks, secondHeader);
}

bool glueBlocksSafe(const vector<BasicBlock *> &glue, PhiInst *innerIndPhi)
{
    for (BasicBlock *bb : glue)
    {
        if (!bb)
            return false;
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (!inst || inst->isTerminator())
                continue;
            if (dynamic_cast<StoreInst *>(inst))
                return false;
            if (auto *call = dynamic_cast<CallInst *>(inst))
            {
                if (isOrderingSensitiveIo(call) || call->ifHasSideEffects())
                    return false;
            }
            if (!innerIndPhi)
                continue;
            if (auto *bin = dynamic_cast<BinaryOperator *>(inst))
            {
                if (bin->getOpcode() != Opcode::Add && bin->getOpcode() != Opcode::Sub)
                    continue;
                if (valueReferencesPhi(bin->getLHS(), innerIndPhi) ||
                    valueReferencesPhi(bin->getRHS(), innerIndPhi))
                    return false;
            }
        }
    }
    return true;
}

bool exitHasOuterInductionBump(BasicBlock *exit, PhiInst *innerIndPhi)
{
    if (!exit || !innerIndPhi)
        return false;
    for (auto &instPtr : exit->getInstructions())
    {
        auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get());
        if (!bin || (bin->getOpcode() != Opcode::Add && bin->getOpcode() != Opcode::Sub))
            continue;
        if (valueReferencesPhi(bin->getLHS(), innerIndPhi) ||
            valueReferencesPhi(bin->getRHS(), innerIndPhi))
            continue;
        if (dynamic_cast<PhiInst *>(stripCopyChain(bin->getLHS())) ||
            dynamic_cast<PhiInst *>(stripCopyChain(bin->getRHS())))
            return true;
    }
    return false;
}

void removeOuterInductionBumpInExit(BasicBlock *exit, PhiInst *innerIndPhi)
{
    if (!exit || !innerIndPhi)
        return;
    auto &insts = exit->getInstructions();
    for (size_t idx = 0; idx < insts.size(); ++idx)
    {
        auto *bin = dynamic_cast<BinaryOperator *>(insts[idx].get());
        if (!bin || (bin->getOpcode() != Opcode::Add && bin->getOpcode() != Opcode::Sub))
            continue;
        if (valueReferencesPhi(bin->getLHS(), innerIndPhi) ||
            valueReferencesPhi(bin->getRHS(), innerIndPhi))
            continue;
        if (!dynamic_cast<PhiInst *>(stripCopyChain(bin->getLHS())) &&
            !dynamic_cast<PhiInst *>(stripCopyChain(bin->getRHS())))
            continue;
        insts.erase(insts.begin() + static_cast<long>(idx));
        return;
    }
}

void clearBlockExceptTerminator(BasicBlock *bb)
{
    if (!bb)
        return;
    auto &insts = bb->getInstructions();
    for (int idx = static_cast<int>(insts.size()) - 2; idx >= 0; --idx)
        insts.erase(insts.begin() + idx);
}

bool stripOuterBumpFromExit(BasicBlock *exit, PhiInst *innerIndPhi)
{
    if (!exit || !innerIndPhi)
        return false;
    auto &insts = exit->getInstructions();
    for (size_t idx = 0; idx < insts.size(); ++idx)
    {
        auto *bin = dynamic_cast<BinaryOperator *>(insts[idx].get());
        if (!bin || (bin->getOpcode() != Opcode::Add && bin->getOpcode() != Opcode::Sub))
            continue;
        if (valueReferencesPhi(bin->getLHS(), innerIndPhi) ||
            valueReferencesPhi(bin->getRHS(), innerIndPhi))
            continue;
        if (!dynamic_cast<PhiInst *>(stripCopyChain(bin->getLHS())) &&
            !dynamic_cast<PhiInst *>(stripCopyChain(bin->getRHS())))
            continue;

        Value *preBump = bin->getLHS();
        if (auto *cpy = dynamic_cast<CopyInst *>(insts.size() > idx + 1 ? insts[idx + 1].get() : nullptr))
        {
            if (cpy->getSource() == bin)
            {
                if (stripCopyChain(preBump) == stripCopyChain(cpy))
                    preBump = bin->getRHS();
                cpy->replaceAllUsesWith(preBump);
                insts.erase(insts.begin() + static_cast<long>(idx + 1));
                insts.erase(insts.begin() + static_cast<long>(idx));
                return true;
            }
        }
        bin->replaceAllUsesWith(preBump);
        insts.erase(insts.begin() + static_cast<long>(idx));
        return true;
    }
    return false;
}

void unifyOuterInductionInExitBump(BasicBlock *exit, Value *fromIv, Value *toIv)
{
    if (!exit || !fromIv || !toIv || stripCopyChain(fromIv) == stripCopyChain(toIv))
        return;
    fromIv = stripCopyChain(fromIv);
    toIv = stripCopyChain(toIv);
    auto &insts = exit->getInstructions();
    for (size_t idx = 0; idx < insts.size(); ++idx)
    {
        auto *bin = dynamic_cast<BinaryOperator *>(insts[idx].get());
        if (!bin || (bin->getOpcode() != Opcode::Add && bin->getOpcode() != Opcode::Sub))
            continue;
        bool touched = false;
        for (size_t k = 0; k < bin->getNumOperands(); ++k)
        {
            if (stripCopyChain(bin->getOperandByIndex(k)) == fromIv)
            {
                bin->setOperandByIndex(k, toIv);
                touched = true;
            }
        }
        if (!touched)
            continue;

        if (idx + 1 < insts.size())
        {
            if (auto *cpy = dynamic_cast<CopyInst *>(insts[idx + 1].get()))
            {
                if (cpy->getSource() == bin && stripCopyChain(cpy) == fromIv)
                    insts.erase(insts.begin() + static_cast<long>(idx + 1));
            }
        }

        string copyName = toIv->getName().empty() ? "outer_iv" : toIv->getName();
        insts.insert(insts.begin() + static_cast<long>(idx) + 1,
                     unique_ptr<Instruction>(new CopyInst(bin, copyName)));
        return;
    }
}

void remapValueInBlockList(const vector<BasicBlock *> &blocks, Value *from, Value *to)
{
    if (!from || !to)
        return;
    from = stripCopyChain(from);
    to = stripCopyChain(to);
    for (BasicBlock *bb : blocks)
    {
        if (!bb)
            continue;
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (!inst)
                continue;
            for (size_t k = 0; k < inst->getNumOperands(); ++k)
            {
                Value *op = inst->getOperandByIndex(k);
                if (stripCopyChain(op) == from)
                    inst->setOperandByIndex(k, to);
            }
        }
    }
}

bool findGluePath(BasicBlock *from, BasicBlock *to, int maxDepth, vector<BasicBlock *> &midBlocks,
                  BasicBlock **exitSuccToRetarget)
{
    midBlocks.clear();
    if (exitSuccToRetarget)
        *exitSuccToRetarget = nullptr;
    if (!from || !to)
        return false;
    if (from == to)
        return true;

    queue<BasicBlock *> q;
    unordered_map<BasicBlock *, BasicBlock *> parent;
    unordered_set<BasicBlock *> visited;
    q.push(from);
    visited.insert(from);
    parent[from] = nullptr;

    while (!q.empty())
    {
        BasicBlock *cur = q.front();
        q.pop();
        if (cur == to)
        {
            for (BasicBlock *p = to; p && p != from; p = parent[p])
                midBlocks.push_back(p);
            reverse(midBlocks.begin(), midBlocks.end());
            if (exitSuccToRetarget)
            {
                if (!midBlocks.empty())
                    *exitSuccToRetarget = midBlocks.front();
                else
                    *exitSuccToRetarget = to;
            }
            return true;
        }

        int depth = 0;
        for (BasicBlock *p = cur; p && p != from; p = parent[p])
            ++depth;
        if (depth >= maxDepth)
            continue;

        auto *term = dynamic_cast<BranchInst *>(cur->getTerminator());
        if (!term)
            continue;

        auto enqueue = [&](BasicBlock *succ) {
            if (!succ || visited.count(succ))
                return;
            visited.insert(succ);
            parent[succ] = cur;
            q.push(succ);
        };

        if (term->isConditional())
        {
            enqueue(term->getTrueBlock());
            enqueue(term->getFalseBlock());
        }
        else
        {
            enqueue(term->getTrueBlock());
        }
    }
    return false;
}

BasicBlock *getUnconditionalBranchTarget(BasicBlock *bb)
{
    if (!bb)
        return nullptr;
    auto *br = dynamic_cast<BranchInst *>(bb->getTerminator());
    if (!br || br->isConditional())
        return nullptr;
    return br->getTrueBlock();
}

bool isBodyMergeDisconnectTarget(BasicBlock *target, const Loop &secondLoop, BasicBlock *secondHeader,
                                 BasicBlock *secondBody, BasicBlock *secondPreheader,
                                 BasicBlock *secondExit, const vector<BasicBlock *> &glueBlocks)
{
    if (!target)
        return false;
    if (secondLoop.containsBlock(target))
        return true;
    if (target == secondHeader || target == secondBody || target == secondPreheader || target == secondExit)
        return true;
    for (BasicBlock *glue : glueBlocks)
    {
        if (target == glue)
            return true;
    }
    return false;
}

BasicBlock *resolveBodyMergeEpilogue(BasicBlock *secondExit, BasicBlock *firstExit, const Loop &secondLoop,
                                   BasicBlock *secondHeader, BasicBlock *secondBody,
                                   BasicBlock *secondPreheader,
                                   const vector<BasicBlock *> &glueBlocks)
{
    const auto bad = [&](BasicBlock *b) {
        return isBodyMergeDisconnectTarget(b, secondLoop, secondHeader, secondBody, secondPreheader,
                                           secondExit, glueBlocks);
    };
    const auto walkFrom = [&](BasicBlock *start) -> BasicBlock * {
        BasicBlock *cur = start;
        for (int depth = 0; depth < 12 && cur; ++depth)
        {
            auto *br = dynamic_cast<BranchInst *>(cur->getTerminator());
            if (!br)
                break;
            if (br->isConditional())
            {
                if (br->getTrueBlock() && !bad(br->getTrueBlock()))
                    return br->getTrueBlock();
                if (br->getFalseBlock() && !bad(br->getFalseBlock()))
                    return br->getFalseBlock();
            }
            else if (br->getTrueBlock() && !bad(br->getTrueBlock()))
            {
                return br->getTrueBlock();
            }
            BasicBlock *next = br->isConditional() ? br->getTrueBlock() : br->getTrueBlock();
            if (!next || next == cur)
                break;
            cur = next;
        }
        return nullptr;
    };
    if (BasicBlock *ep = walkFrom(secondExit))
        return ep;
    return walkFrom(firstExit);
}

void retargetTerminatorAwayFrom(BasicBlock *from,
                                const std::function<bool(BasicBlock *)> &shouldRetarget,
                                BasicBlock *target)
{
    if (!from || !target)
        return;
    auto *br = dynamic_cast<BranchInst *>(from->getTerminator());
    if (!br)
        return;
    if (br->isConditional())
    {
        if (br->getTrueBlock() && shouldRetarget(br->getTrueBlock()))
            (void)replaceBranchTarget(from, br->getTrueBlock(), target);
        if (br->getFalseBlock() && shouldRetarget(br->getFalseBlock()))
            (void)replaceBranchTarget(from, br->getFalseBlock(), target);
    }
    else if (br->getTrueBlock() && shouldRetarget(br->getTrueBlock()))
    {
        (void)replaceBranchTarget(from, br->getTrueBlock(), target);
    }
}

void retargetExternalPredecessorsOfBlocks(const vector<BasicBlock *> &deadBlocks, BasicBlock *liveTarget)
{
    if (!liveTarget || deadBlocks.empty())
        return;
    unordered_set<BasicBlock *> deadSet(deadBlocks.begin(), deadBlocks.end());
    const auto targetsDead = [&](BasicBlock *t) { return t && deadSet.count(t); };
    unordered_set<BasicBlock *> visitedPreds;
    for (BasicBlock *dead : deadBlocks)
    {
        if (!dead)
            continue;
        vector<BasicBlock *> preds(dead->getPredecessors());
        for (BasicBlock *pred : preds)
        {
            if (!pred || !visitedPreds.insert(pred).second)
                continue;
            retargetTerminatorAwayFrom(pred, targetsDead, liveTarget);
        }
    }
}

void forceUnconditionalBranchTo(BasicBlock *from, BasicBlock *target)
{
    if (!from || !target)
        return;
    auto *br = dynamic_cast<BranchInst *>(from->getTerminator());
    if (!br)
        return;
    if (!br->isConditional() && br->getTrueBlock() == target)
        return;

    if (br->isConditional())
    {
        if (br->getTrueBlock())
        {
            from->removeSuccessor(br->getTrueBlock());
            br->getTrueBlock()->removePredecessor(from);
        }
        if (br->getFalseBlock())
        {
            from->removeSuccessor(br->getFalseBlock());
            br->getFalseBlock()->removePredecessor(from);
        }
    }
    else if (br->getTrueBlock())
    {
        from->removeSuccessor(br->getTrueBlock());
        br->getTrueBlock()->removePredecessor(from);
    }

    auto &insts = from->getInstructions();
    if (!insts.empty() && insts.back()->isTerminator())
    {
        insts.back()->removeThisFromOperands();
        insts.pop_back();
    }
    from->addSuccessor(target);
    target->addPredecessor(from);
    from->addInstruction(std::unique_ptr<Instruction>(new BranchInst(target)));
}

bool blockBranchesTo(BasicBlock *from, BasicBlock *to)
{
    if (!from || !to)
        return false;
    auto *br = dynamic_cast<BranchInst *>(from->getTerminator());
    if (!br)
        return false;
    if (br->isConditional())
        return br->getTrueBlock() == to || br->getFalseBlock() == to;
    return br->getTrueBlock() == to;
}

void finalizeGlueWrapperFusion(BasicBlock *firstHeader, BasicBlock *firstExit, BasicBlock *secondExit,
                               const vector<BasicBlock *> &glueBlocks)
{
    if (glueBlocks.empty())
        return;
    BasicBlock *outerHeader = glueBlocks.front();
    if (secondExit && firstHeader && !blockBranchesTo(secondExit, firstHeader))
    {
        BasicBlock *succ = getUnconditionalBranchTarget(secondExit);
        if (succ && succ != firstHeader)
            (void)replaceBranchTarget(secondExit, succ, firstHeader);
    }
    if (firstExit && outerHeader && !blockBranchesTo(firstExit, outerHeader))
    {
        auto *exitBr = dynamic_cast<BranchInst *>(firstExit->getTerminator());
        if (exitBr && !exitBr->isConditional())
        {
            BasicBlock *succ = exitBr->getTrueBlock();
            if (succ && succ != outerHeader)
                (void)replaceBranchTarget(firstExit, succ, outerHeader);
        }
    }
}

void finalizeSharedBoundaryFusion(BasicBlock *firstHeader, BasicBlock *firstBody, BasicBlock *firstExit,
                                  BasicBlock *firstLatch, BasicBlock *secondHeader, BasicBlock *secondExit,
                                  const Loop &secondLoop,
                                  std::unordered_map<Value *, Value *> &valueMap)
{
    if (!firstHeader || !firstBody || !secondHeader || !secondExit)
        return;

    // 统一出口：header 假分支直达 second.exit，不再经 first.exit 再“启动”第二循环
    forceHeaderLoopExitTo(firstHeader, firstBody, secondExit);

    // 旧路径 first.exit -> second.header 会绕过串联的 second.body，改指向统一出口
    if (firstExit && blockBranchesTo(firstExit, secondHeader))
        (void)replaceBranchTarget(firstExit, secondHeader, secondExit);

    applyValueMapToBlocks(secondLoop.blocks, valueMap);

    const auto retargetSecondHeaderPreds = [&](BasicBlock *from) {
        if (!from || from == secondHeader)
            return;
        auto *br = dynamic_cast<BranchInst *>(from->getTerminator());
        if (!br)
            return;
        if (br->isConditional())
        {
            if (br->getTrueBlock() == secondHeader)
                (void)replaceBranchTarget(from, secondHeader, firstHeader);
            if (br->getFalseBlock() == secondHeader)
                (void)replaceBranchTarget(from, secondHeader, firstHeader);
        }
        else if (br->getTrueBlock() == secondHeader)
        {
            (void)replaceBranchTarget(from, secondHeader, firstHeader);
        }
    };

    for (BasicBlock *bb : secondLoop.blocks)
        retargetSecondHeaderPreds(bb);
    retargetSecondHeaderPreds(firstLatch);
    retargetSecondHeaderPreds(firstExit);

    if (firstExit && firstExit != secondExit)
        retargetExitPhiPredecessor(secondExit, firstExit, firstHeader);
}

void severSharedBoundaryDeadBlocks(Function *func, BasicBlock *firstExit, BasicBlock *secondHeader,
                                   BasicBlock *secondExit)
{
    if (!func || !secondHeader || !secondExit)
        return;

    std::unordered_set<BasicBlock *> deadBlocks;
    deadBlocks.insert(secondHeader);
    if (firstExit && firstExit != secondExit)
        deadBlocks.insert(firstExit);

    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        if (!bb || deadBlocks.count(bb))
            continue;
        auto *br = dynamic_cast<BranchInst *>(bb->getTerminator());
        if (!br)
            continue;
        if (br->isConditional())
        {
            if (br->getTrueBlock() && deadBlocks.count(br->getTrueBlock()))
                (void)replaceBranchTarget(bb, br->getTrueBlock(), secondExit);
            if (br->getFalseBlock() && deadBlocks.count(br->getFalseBlock()))
                (void)replaceBranchTarget(bb, br->getFalseBlock(), secondExit);
        }
        else if (br->getTrueBlock() && deadBlocks.count(br->getTrueBlock()))
        {
            (void)replaceBranchTarget(bb, br->getTrueBlock(), secondExit);
        }
    }

    for (BasicBlock *dead : deadBlocks)
    {
        auto &insts = dead->getInstructions();
        for (int idx = static_cast<int>(insts.size()) - 2; idx >= 0; --idx)
        {
            auto *call = dynamic_cast<CallInst *>(insts[static_cast<size_t>(idx)].get());
            if (!call || !call->getCalledFunction())
                continue;
            const string &callee = call->getCalledFunction()->getName();
            if (callee == "_sysy_starttime" || callee == "_sysy_stoptime")
                insts.erase(insts.begin() + idx);
        }
    }

    for (BasicBlock *dead : deadBlocks)
    {
        vector<BasicBlock *> preds(dead->getPredecessors());
        for (BasicBlock *pred : preds)
        {
            if (!pred)
                continue;
            auto *pbr = dynamic_cast<BranchInst *>(pred->getTerminator());
            if (!pbr)
                continue;
            if (pbr->isConditional())
            {
                if (pbr->getTrueBlock() == dead)
                    (void)replaceBranchTarget(pred, dead, secondExit);
                if (pbr->getFalseBlock() == dead)
                    (void)replaceBranchTarget(pred, dead, secondExit);
            }
            else if (pbr->getTrueBlock() == dead)
            {
                (void)replaceBranchTarget(pred, dead, secondExit);
            }
        }
    }
}

void redirectBranchesToAnyInLoop(BasicBlock *from, const Loop &secondLoop, BasicBlock *target)
{
    if (!from || !target)
        return;
    auto *br = dynamic_cast<BranchInst *>(from->getTerminator());
    if (!br)
        return;
    if (br->isConditional())
    {
        if (br->getTrueBlock() && secondLoop.containsBlock(br->getTrueBlock()))
            (void)replaceBranchTarget(from, br->getTrueBlock(), target);
        if (br->getFalseBlock() && secondLoop.containsBlock(br->getFalseBlock()))
            (void)replaceBranchTarget(from, br->getFalseBlock(), target);
    }
    else if (br->getTrueBlock() && secondLoop.containsBlock(br->getTrueBlock()))
        (void)replaceBranchTarget(from, br->getTrueBlock(), target);
}

void fullyDisconnectBodyMergedSecondLoop(const Loop &secondLoop, BasicBlock *firstHeader,
                                         BasicBlock *firstBody, BasicBlock *firstExit,
                                         BasicBlock *secondHeader, BasicBlock *secondBody,
                                         BasicBlock *secondPreheader, BasicBlock *secondExit,
                                         const vector<BasicBlock *> &glueBlocks,
                                         BasicBlock *epilogue)
{
    if (!epilogue)
        return;

    const auto shouldRetarget = [&](BasicBlock *t) {
        return isBodyMergeDisconnectTarget(t, secondLoop, secondHeader, secondBody, secondPreheader,
                                           secondExit, glueBlocks);
    };

    if (firstHeader && secondExit && blockBranchesTo(firstHeader, secondExit))
        (void)replaceBranchTarget(firstHeader, secondExit, firstExit);
    forceHeaderLoopExitTo(firstHeader, firstBody, firstExit);
    retargetTerminatorAwayFrom(firstHeader, shouldRetarget, firstExit);
    if (firstHeader && firstBody && firstExit)
    {
        auto *hBr = dynamic_cast<BranchInst *>(firstHeader->getTerminator());
        if (hBr && hBr->isConditional())
        {
            BasicBlock *exitArm = hBr->getTrueBlock() == firstBody ? hBr->getFalseBlock() : hBr->getTrueBlock();
            if (shouldRetarget(exitArm))
                forceHeaderLoopExitTo(firstHeader, firstBody, firstExit);
        }
    }

    if (firstExit)
    {
        redirectBranchesToAnyInLoop(firstExit, secondLoop, epilogue);
        retargetTerminatorAwayFrom(firstExit, shouldRetarget, epilogue);
        auto *exitBr = dynamic_cast<BranchInst *>(firstExit->getTerminator());
        if (!exitBr || exitBr->isConditional() || exitBr->getTrueBlock() != epilogue)
            forceUnconditionalBranchTo(firstExit, epilogue);
    }

    for (BasicBlock *bb : glueBlocks)
    {
        redirectBranchesToAnyInLoop(bb, secondLoop, epilogue);
        retargetTerminatorAwayFrom(bb, shouldRetarget, epilogue);
    }
    if (secondPreheader)
    {
        redirectBranchesToAnyInLoop(secondPreheader, secondLoop, epilogue);
        retargetTerminatorAwayFrom(secondPreheader, shouldRetarget, epilogue);
    }

    // 仅断开 second 环路与 glue 内部边；勿把 while.exit.36 等外部入口改到 epilogue
    if (!glueBlocks.empty() && firstHeader)
        retargetExternalPredecessorsOfBlocks(glueBlocks, firstHeader);

    for (BasicBlock *loopBb : secondLoop.blocks)
    {
        if (!loopBb)
            continue;
        vector<BasicBlock *> preds(loopBb->getPredecessors());
        for (BasicBlock *pred : preds)
        {
            if (pred && !secondLoop.containsBlock(pred))
            {
                redirectBranchesToAnyInLoop(pred, secondLoop, epilogue);
                retargetTerminatorAwayFrom(pred, shouldRetarget, epilogue);
            }
        }
    }

    for (BasicBlock *loopBb : secondLoop.blocks)
    {
        if (!loopBb || loopBb == epilogue)
            continue;
        auto *br = dynamic_cast<BranchInst *>(loopBb->getTerminator());
        if (!br)
            continue;
        if (br->isConditional())
        {
            if (br->getTrueBlock() && br->getTrueBlock() != epilogue)
                (void)replaceBranchTarget(loopBb, br->getTrueBlock(), epilogue);
            if (br->getFalseBlock() && br->getFalseBlock() != epilogue)
                (void)replaceBranchTarget(loopBb, br->getFalseBlock(), epilogue);
        }
        else if (br->getTrueBlock() && br->getTrueBlock() != epilogue)
            (void)replaceBranchTarget(loopBb, br->getTrueBlock(), epilogue);
    }

    if (secondExit && secondExit != epilogue)
        forceUnconditionalBranchTo(secondExit, epilogue);

    if (!glueBlocks.empty() && firstHeader)
        retargetExternalPredecessorsOfBlocks(glueBlocks, firstHeader);
}

const Loop *findMinimalContainingLoop(const vector<Loop> &loops, BasicBlock *innerHeader)
{
    const Loop *best = nullptr;
    size_t bestSize = SIZE_MAX;
    for (const Loop &cand : loops)
    {
        if (!cand.header || cand.header == innerHeader || !cand.containsBlock(innerHeader))
            continue;
        if (cand.blocks.size() >= bestSize)
            continue;
        bestSize = cand.blocks.size();
        best = &cand;
    }
    return best;
}

bool isTrivialWrapperBody(BasicBlock *body)
{
    if (!body)
        return false;
    for (auto &instPtr : body->getInstructions())
    {
        Instruction *inst = instPtr.get();
        if (!inst || inst->isTerminator())
            continue;
        return false;
    }
    auto *br = dynamic_cast<BranchInst *>(body->getTerminator());
    return br && !br->isConditional();
}

bool wrapperBodyLeadsToInnerHeader(BasicBlock *body, BasicBlock *innerHeader)
{
    if (!isTrivialWrapperBody(body) || !innerHeader)
        return false;

    BasicBlock *tgt = getUnconditionalBranchTarget(body);
    if (!tgt)
        return false;
    if (tgt == innerHeader)
        return true;

    auto *tbr = dynamic_cast<BranchInst *>(tgt->getTerminator());
    if (!tbr || !tbr->isConditional())
        return false;
    return tbr->getTrueBlock() == innerHeader || tbr->getFalseBlock() == innerHeader;
}

} // namespace

bool LoopFusionPass::fusionInductionDimensionsCompatible(
    const Loop &firstLoop, const Loop &secondLoop, const CanonicalLoopShape &first,
    const CanonicalLoopShape &second, FusionLinkKind linkKind,
    const SequentialSiblingGlueInfo *seqGlue)
{
    Value *i1 = first.indPhi;
    Value *i2 = second.indPhi;
    if (valueMatchesInduction(i1, i2))
        return true;

    if (linkKind == FusionLinkKind::DirectAdjacent && second.preheader == first.exit)
        return true;

    Value *r1 = getLoopRowIndexValue(firstLoop, i1);
    Value *r2 = getLoopRowIndexValue(secondLoop, i2);

    if (seqGlue && seqGlue->active && linkKind == FusionLinkKind::GlueToSecondHeader)
    {
        Value *o1 = headerInductionValue(seqGlue->outerFirst);
        if (!o1)
            o1 = seqGlue->outerFirst.indPhi;
        Value *o2 = headerInductionValue(seqGlue->outerSecond);
        if (!o2)
            o2 = seqGlue->outerSecond.indPhi;
        if (o1 && o2)
        {
            if (r1 && stripCopyChain(r1) != stripCopyChain(o1))
                return false;
            if (r2 && stripCopyChain(r2) != stripCopyChain(o2))
                return false;
            return true;
        }
    }

    if (r1 && r2 && stripCopyChain(r1) == stripCopyChain(r2))
        return true;

    if (!r1 && !r2)
        return true;

    if (linkKind == FusionLinkKind::GlueToSecondHeader)
    {
        if (r2 && !valueMatchesInduction(r2, i1))
            return false;
        if (r1 && !r2)
            return false;
        return true;
    }

    if (r2 && valueMatchesInduction(r2, i1))
        return true;
    if (!r1 && !r2)
        return true;
    if (r1 && r2 && stripCopyChain(r1) == stripCopyChain(r2))
        return true;
    return false;
}

Value *LoopFusionPass::loopEntryInitValue(const CanonicalLoopShape &shape)
{
    if (!shape.valid || !shape.preheader)
        return nullptr;
    if (shape.indPhi)
    {
        for (unsigned k = 0; k < shape.indPhi->getNumIncomingValues(); ++k)
        {
            if (shape.indPhi->getIncomingBlock(k) == shape.preheader)
                return shape.indPhi->getIncomingValue(k);
        }
    }
    Value *iv = headerInductionValue(shape);
    if (!iv)
        iv = shape.indPhi;
    if (!iv)
        return nullptr;
    for (auto &instPtr : shape.preheader->getInstructions())
    {
        auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
        if (!cpy)
            continue;
        auto *initConst = dynamic_cast<ConstantInt *>(stripCopyChain(cpy->getSource()));
        if (!initConst || initConst->Value != 0)
            continue;
        if (cpy == iv || stripCopyChain(cpy) == stripCopyChain(iv))
            return cpy->getSource();
    }
    for (auto &instPtr : shape.preheader->getInstructions())
    {
        auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
        if (!cpy)
            continue;
        auto *initConst = dynamic_cast<ConstantInt *>(stripCopyChain(cpy->getSource()));
        if (initConst && initConst->Value == 0)
            return cpy->getSource();
    }
    return nullptr;
}

Value *LoopFusionPass::headerInductionValue(const CanonicalLoopShape &shape)
{
    if (!shape.header)
        return nullptr;
    auto *hBr = dynamic_cast<BranchInst *>(shape.header->getTerminator());
    if (!hBr || !hBr->isConditional())
        return nullptr;
    auto *icmp = dynamic_cast<ICmpInst *>(hBr->getCondition());
    if (!icmp)
        return nullptr;
    if (shape.indPhi)
    {
        if (valueMatchesInduction(icmp->getLHS(), shape.indPhi))
            return icmp->getLHS();
        if (valueMatchesInduction(icmp->getRHS(), shape.indPhi))
            return icmp->getRHS();
    }
    for (Value *side : {icmp->getLHS(), icmp->getRHS()})
    {
        if (dynamic_cast<PhiInst *>(side) || dynamic_cast<CopyInst *>(side))
            return side;
    }
    return nullptr;
}

bool blockExitsIntoLoop(const Loop &targetLoop, BasicBlock *bb)
{
    if (!bb)
        return false;
    auto *br = dynamic_cast<BranchInst *>(bb->getTerminator());
    if (!br)
        return false;
    auto check = [&](BasicBlock *succ) {
        return succ && targetLoop.containsBlock(succ);
    };
    if (br->isConditional())
        return check(br->getTrueBlock()) || check(br->getFalseBlock());
    return check(br->getTrueBlock());
}

bool fusionPathAlreadyConnected(BasicBlock *exit, const Loop &secondLoop, BasicBlock *secondHeader)
{
    if (!exit || !secondHeader)
        return false;
    if (blockExitsIntoLoop(secondLoop, exit))
        return true;
    auto *br = dynamic_cast<BranchInst *>(exit->getTerminator());
    if (!br)
        return false;
    vector<BasicBlock *> succs;
    if (br->isConditional())
    {
        succs.push_back(br->getTrueBlock());
        succs.push_back(br->getFalseBlock());
    }
    else
        succs.push_back(br->getTrueBlock());
    for (BasicBlock *succ : succs)
    {
        if (!succ)
            continue;
        if (blockExitsIntoLoop(secondLoop, succ))
            return true;
        if (blockBranchesTo(succ, secondHeader))
            return true;
    }
    return false;
}

void LoopFusionPass::linkSharedBoundaryHalfBound(BasicBlock *entry, BasicBlock *sharedExit,
                                                 const Loop &secondLoop,
                                                 std::unordered_map<Value *, Value *> &valueMap)
{
    if (!entry || !sharedExit)
        return;

    Value *exitHalf = nullptr;
    for (int idx = static_cast<int>(sharedExit->getInstructions().size()) - 2; idx >= 0; --idx)
    {
        auto *sra = dynamic_cast<BinaryOperator *>(sharedExit->getInstructions()[static_cast<size_t>(idx)].get());
        if (sra && sra->getOpcode() == Opcode::Sra)
        {
            exitHalf = sra;
            break;
        }
    }
    Value *entryHalf = nullptr;
    for (auto &instPtr : entry->getInstructions())
    {
        auto *sra = dynamic_cast<BinaryOperator *>(instPtr.get());
        if (sra && sra->getOpcode() == Opcode::Sra)
        {
            entryHalf = sra;
            break;
        }
    }
    if (!exitHalf || !entryHalf)
        return;

    valueMap[exitHalf] = entryHalf;
    applyValueMapToBlocks(secondLoop.blocks, valueMap);
}

void LoopFusionPass::stripSharedBoundarySecondInit(BasicBlock *junctionExit,
                                                   const CanonicalLoopShape &second)
{
    if (!junctionExit || !second.valid)
        return;
    Value *secondIv = headerInductionValue(second);
    if (!secondIv)
        secondIv = second.indPhi;
    if (!secondIv)
        return;

    auto &insts = junctionExit->getInstructions();
    for (int idx = static_cast<int>(insts.size()) - 2; idx >= 0; --idx)
    {
        auto *cpy = dynamic_cast<CopyInst *>(insts[static_cast<size_t>(idx)].get());
        if (!cpy)
            continue;
        auto *initConst = dynamic_cast<ConstantInt *>(stripCopyChain(cpy->getSource()));
        if (!initConst || initConst->Value != 0)
            continue;
        if (stripCopyChain(cpy) != stripCopyChain(secondIv))
            continue;
        insts.erase(insts.begin() + idx);
    }
}

void LoopFusionPass::stripSkippedOuterLoopEntryInit(BasicBlock *junctionExit,
                                                    const CanonicalLoopShape &skippedOuter)
{
    if (!junctionExit || !skippedOuter.valid)
        return;
    Value *skippedIv = headerInductionValue(skippedOuter);
    if (!skippedIv)
        skippedIv = skippedOuter.indPhi;
    if (!skippedIv)
        return;

    auto &insts = junctionExit->getInstructions();
    for (int idx = static_cast<int>(insts.size()) - 2; idx >= 0; --idx)
    {
        auto *cpy = dynamic_cast<CopyInst *>(insts[static_cast<size_t>(idx)].get());
        if (!cpy)
            continue;
        auto *initConst = dynamic_cast<ConstantInt *>(stripCopyChain(cpy->getSource()));
        if (!initConst || initConst->Value != 0)
            continue;
        if (stripCopyChain(cpy) != stripCopyChain(skippedIv))
            continue;
        insts.erase(insts.begin() + idx);
    }
}

bool LoopFusionPass::spliceExitInitsBeforeBranch(BasicBlock *dest, BasicBlock *source)
{
    if (!dest || !source || dest == source)
        return true;
    auto &destInsts = dest->getInstructions();
    if (destInsts.empty())
        return false;
    size_t insertPos = destInsts.size() - 1;
    for (auto &instPtr : source->getInstructions())
    {
        Instruction *inst = instPtr.get();
        if (!inst || inst->isTerminator())
            continue;
        if (dynamic_cast<PhiInst *>(inst))
        {
            Instruction *cloned = inst->clone();
            destInsts.insert(destInsts.begin() + static_cast<long>(insertPos++),
                             unique_ptr<Instruction>(cloned));
            continue;
        }
        if (!isClonablePreheaderInst(inst))
            return false;
        Instruction *cloned = inst->clone();
        destInsts.insert(destInsts.begin() + static_cast<long>(insertPos++),
                         unique_ptr<Instruction>(cloned));
    }
    return true;
}

void LoopFusionPass::retargetSkippedOuterPhiInits(BasicBlock *junctionExit, BasicBlock *skippedOuterExit,
                                                  BasicBlock *afterSecondOuter)
{
    if (!junctionExit || !skippedOuterExit || !afterSecondOuter)
        return;
    for (auto &instPtr : afterSecondOuter->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi)
            break;
        for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
        {
            if (phi->getIncomingBlock(static_cast<unsigned>(k)) == skippedOuterExit)
                phi->setIncomingBlock(static_cast<unsigned>(k), junctionExit);
        }
    }
}

bool LoopFusionPass::partitionHalfFusionSafe(const Loop &firstLoop, const Loop &secondLoop,
                                             const CanonicalLoopShape &first,
                                             const CanonicalLoopShape &second)
{
    vector<pair<int, Value *>> g1, g2;
    if (!collectStoreHalfGuardsImpl(firstLoop, headerInductionValue(first), g1))
        return false;
    if (!collectStoreHalfGuardsImpl(secondLoop, headerInductionValue(second), g2))
        return false;
    if (g1.empty() || g2.empty())
        return false;

    Value *refBound = g1.front().second;
    auto allSameKind = [&](const vector<pair<int, Value *>> &guards, int kind) {
        for (const auto &g : guards)
        {
            if (g.first != kind || !boundsEquivalent(g.second, refBound))
                return false;
        }
        return true;
    };
    return (allSameKind(g1, 1) && allSameKind(g2, 2)) || (allSameKind(g1, 2) && allSameKind(g2, 1));
}

bool LoopFusionPass::ioInterleaveFusionSafe(const Loop &firstLoop, const Loop &secondLoop,
                                            const CanonicalLoopShape &first,
                                            const CanonicalLoopShape &second)
{
    vector<pair<int, Value *>> g1, g2;
    if (!collectIoHalfGuardsImpl(firstLoop, headerInductionValue(first), g1))
        return false;
    if (!collectIoHalfGuardsImpl(secondLoop, headerInductionValue(second), g2))
        return false;

    const bool io1 = !g1.empty();
    const bool io2 = !g2.empty();
    // 无 IO 时不受交错读写约束（供 validateFuseLoopPair 通过 store 等循环）
    if (!io1 && !io2)
        return true;
    if (!io1 || !io2)
        return false;

    Value *refBound = g1.front().second;
    auto allSameKind = [&](const vector<pair<int, Value *>> &guards, int kind) {
        for (const auto &g : guards)
        {
            if (g.first != kind || !boundsEquivalent(g.second, refBound))
                return false;
        }
        return true;
    };
    return (allSameKind(g1, 1) && allSameKind(g2, 2)) || (allSameKind(g1, 2) && allSameKind(g2, 1));
}

bool LoopFusionPass::matchFusionPartnerLoopShape(const Loop *loop, CanonicalLoopShape &out)
{
    out = {};
    if (!loop || !loop->header)
        return false;
    out.header = loop->header;

    for (BasicBlock *pred : out.header->getPredecessors())
    {
        if (!loop->containsBlock(pred))
            out.preheader = pred;
    }
    if (!out.preheader)
        return false;

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

    for (BasicBlock *pred : out.header->getPredecessors())
    {
        if (loop->containsBlock(pred) && pred != out.header)
            out.latch = pred;
    }
    if (!out.latch)
        return false;

    auto *icmp = dynamic_cast<ICmpInst *>(hBr->getCondition());
    if (!icmp)
        return false;
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
        step = cL ? cL->Value : cR->Value;
    else
    {
        if (!cR || lhs != out.indPhi)
            return false;
        step = -cR->Value;
    }
    if (step == 0)
        return false;
    out.step = step;
    out.isInc = step > 0;
    refineLoopInductionPhi(out.header, out.latch, out.indPhi);
    out.valid = true;
    return true;
}

bool LoopFusionPass::isWrapperLoopToInnerHeader(const CanonicalLoopShape &shape)
{
    if (!shape.valid || !shape.body)
        return false;
    for (auto &instPtr : shape.body->getInstructions())
    {
        Instruction *inst = instPtr.get();
        if (!inst || inst->isTerminator())
            continue;
        return false;
    }
    auto *br = dynamic_cast<BranchInst *>(shape.body->getTerminator());
    return br && !br->isConditional() && br->getTrueBlock() != shape.header &&
           br->getTrueBlock() != shape.exit;
}

// 识别「外层顺序相邻、内层同界」的兄弟结构：first 内层结束经 outerSecond.body 进入 second 内层
bool LoopFusionPass::findSequentialInnerGluePath(const vector<Loop> &allLoops,
                                                 const Loop &firstInnerLoop,
                                                 const Loop &secondInnerLoop,
                                                 const CanonicalLoopShape &first,
                                                 const CanonicalLoopShape &second,
                                                 SequentialSiblingGlueInfo &seqGlue,
                                                 vector<BasicBlock *> &glueBlocks,
                                                 BasicBlock *&glueExitFrom,
                                                 BasicBlock *&glueExitOldSucc) const
{
    seqGlue = {};
    glueBlocks.clear();
    glueExitFrom = nullptr;
    glueExitOldSucc = nullptr;

    if (!first.valid || !second.valid || !first.header || !second.header || !first.exit)
        return false;

    const Loop *outerFirstLoop = findMinimalContainingLoop(allLoops, first.header);
    const Loop *outerSecondLoop = findMinimalContainingLoop(allLoops, second.header);
    if (!outerFirstLoop || !outerSecondLoop || outerFirstLoop == outerSecondLoop)
        return false;

    CanonicalLoopShape outerFirst{};
    CanonicalLoopShape outerSecond{};
    if (!matchCanonicalLoopShape(outerFirstLoop, outerFirst) || !outerFirst.valid)
        return false;
    if (!matchCanonicalLoopShape(outerSecondLoop, outerSecond) || !outerSecond.valid)
        return false;

    if (!outerFirst.exit || !outerSecond.preheader || outerFirst.exit != outerSecond.preheader)
        return false;
    if (!wrapperBodyLeadsToInnerHeader(outerFirst.body, first.header))
        return false;
    if (!wrapperBodyLeadsToInnerHeader(outerSecond.body, second.header))
        return false;
    if (!blockBranchesTo(first.exit, outerFirst.header))
        return false;
    if (!exitHasOuterInductionBump(first.exit, first.indPhi))
        return false;
    if (first.step != second.step || !boundsEquivalent(first.bound, second.bound))
        return false;
    if (outerFirst.step != outerSecond.step || !boundsEquivalent(outerFirst.bound, outerSecond.bound))
        return false;

    Value *outerFirstInit = nullptr;
    for (unsigned k = 0; k < outerFirst.indPhi->getNumIncomingValues(); ++k)
    {
        if (outerFirst.indPhi->getIncomingBlock(k) == outerFirst.preheader)
        {
            outerFirstInit = outerFirst.indPhi->getIncomingValue(k);
            break;
        }
    }
    Value *outerSecondInit = nullptr;
    for (unsigned k = 0; k < outerSecond.indPhi->getNumIncomingValues(); ++k)
    {
        if (outerSecond.indPhi->getIncomingBlock(k) == outerSecond.preheader)
        {
            outerSecondInit = outerSecond.indPhi->getIncomingValue(k);
            break;
        }
    }
    if (!outerFirstInit || !outerSecondInit || !initValueEquivalent(outerFirstInit, outerSecondInit))
        return false;

    seqGlue.active = true;
    seqGlue.outerFirst = outerFirst;
    seqGlue.outerSecond = outerSecond;
    seqGlue.outerFirstLoop = outerFirstLoop;
    seqGlue.outerSecondLoop = outerSecondLoop;

    glueBlocks = {outerSecond.body};
    glueExitFrom = first.exit;
    glueExitOldSucc = outerFirst.header;
    return true;
}

// outer 是否真包含 inner（不同 header，且 inner 的 header 在 outer 的循环体内）
bool loopProperlyContains(const Loop &outer, const Loop &inner)
{
    if (!outer.header || !inner.header || outer.header == inner.header)
        return false;
    return find(outer.blocks.begin(), outer.blocks.end(), inner.header) != outer.blocks.end();
}

const Loop *findImmediateParentLoop(const Loop &inner, const vector<Loop> &allLoops)
{
    const Loop *parent = nullptr;
    size_t minBlocks = numeric_limits<size_t>::max();
    for (const Loop &cand : allLoops)
    {
        if (!loopProperlyContains(cand, inner))
            continue;
        if (cand.blocks.size() < minBlocks)
        {
            minBlocks = cand.blocks.size();
            parent = &cand;
        }
    }
    return parent;
}

bool loopsSameImmediateNestingLevel(const Loop &first, const Loop &second, const vector<Loop> &allLoops)
{
    const Loop *p1 = findImmediateParentLoop(first, allLoops);
    const Loop *p2 = findImmediateParentLoop(second, allLoops);
    if (p1 == p2)
        return true;
    if (p1 && p2 && p1->header == p2->header)
        return true;
    return false;
}

bool LoopFusionPass::validateFuseLoopPair(const Loop &firstLoop, const Loop &secondLoop,
                                                      const CanonicalLoopShape &first,
                                                      const CanonicalLoopShape &second, FusionLinkKind linkKind,
                                                      string &rejectReason, const vector<Loop> &allLoops,
                                                      const SequentialSiblingGlueInfo *seqGlue)
{
    rejectReason.clear();

    // 仅融合同一嵌套层的循环；sequential-inner glue 由专用路径处理不同外层下的兄弟内层
    if (!(seqGlue && seqGlue->active) && !loopsSameImmediateNestingLevel(firstLoop, secondLoop, allLoops))
    {
        rejectReason = "loops are not at the same nesting level";
        return false;
    }

    // 共享边界：first.exit 即 second 的 preheader（两段 while 首尾相接）
    if (linkKind == FusionLinkKind::DirectAdjacent && second.preheader == first.exit)
    {
        // 单侧 wrapper 仍拒绝；双侧 wrapper（仅 br 到内层）可合并外层归纳
        const bool firstWrap = isWrapperLoopToInnerHeader(first);
        const bool secondWrap = isWrapperLoopToInnerHeader(second);
        if (firstWrap != secondWrap)
        {
            rejectReason = "shared-boundary fusion not safe for wrapper outer loops";
            return false;
        }
        if (isWrapperLoopToInnerHeader(first) && isWrapperLoopToInnerHeader(second))
        {
            rejectReason = "shared-boundary cannot fuse two wrapper outer loops";
            return false;
        }
        const bool ioHalf = LoopFusionPass::ioInterleaveFusionSafe(firstLoop, secondLoop, first, second);
        const bool storeHalf = LoopFusionPass::partitionHalfFusionSafe(firstLoop, secondLoop, first, second);
        if (!ioHalf && !storeHalf)
        {
            rejectReason = "shared-boundary loops lack complementary partition guards";
            return false;
        }
    }

    if (linkKind == FusionLinkKind::GlueToSecondHeader && !(seqGlue && seqGlue->active) &&
        second.preheader != first.exit)
    {
        if (!bodyMergeGluePartitionSafe(firstLoop, secondLoop, first.preheader, second.preheader,
                                        first.indPhi, second.indPhi))
        {
            rejectReason = "glue body-merge would fuse complementary partition-guarded loops";
            return false;
        }
    }

    if (linkKind == FusionLinkKind::GlueToSecondHeader && seqGlue && seqGlue->active)
    {
        if (seqGlue->outerFirst.header &&
            blockBranchesTo(first.exit, seqGlue->outerFirst.header))
        {
            rejectReason =
                "sequential-inner full-scan loops (first inner exit bumps outer) are not supported";
            return false;
        }

        auto shapeBodyHasCompute = [](const CanonicalLoopShape &s) {
            if (!s.body)
                return false;
            for (auto &instPtr : s.body->getInstructions())
            {
                Instruction *inst = instPtr.get();
                if (!inst || inst->isTerminator() || dynamic_cast<PhiInst *>(inst))
                    continue;
                return true;
            }
            return false;
        };
        if (isWrapperLoopToInnerHeader(second) || !shapeBodyHasCompute(second))
        {
            rejectReason = "glue fusion target is not a sibling inner loop body";
            return false;
        }
        if (!seqGlue && isWrapperLoopToInnerHeader(first) && first.indPhi != second.indPhi)
        {
            rejectReason = "glue fusion from wrapper outer loop to inner loop";
            return false;
        }
    }

    if (blockHasPhi(first.exit))
    {
        for (auto &instPtr : first.exit->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (!inst)
                continue;
            auto *phi = dynamic_cast<PhiInst *>(inst);
            if (!phi)
                continue;
            for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
            {
                Value *iv = phi->getIncomingValue(k);
                if (!iv)
                    continue;
                if (auto *defInst = dynamic_cast<Instruction *>(iv))
                {
                    if (secondLoop.containsInst(defInst))
                    {
                        rejectReason = "first exit phi depends on second loop";
                        return false;
                    }
                }
            }
        }
    }

    if (blockHasPhi(second.exit))
    {
        for (auto &instPtr : second.exit->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (!inst)
                continue;
            auto *phi = dynamic_cast<PhiInst *>(inst);
            if (!phi)
                continue;
            if (phi->getNumIncomingValues() != 1 || phi->getIncomingBlock(0) != second.header)
            {
                rejectReason = "second exit phi not safely rewritable";
                return false;
            }
        }
    }

    if (first.step != second.step || !boundsEquivalent(first.bound, second.bound))
    {
        rejectReason = "step/bound mismatch";
        return false;
    }

    Value *firstInit = LoopFusionPass::loopEntryInitValue(first);
    Value *secondInit = phiIncomingFromBlock(second.indPhi, first.exit);
    if (!secondInit)
        secondInit = LoopFusionPass::loopEntryInitValue(second);
    if (!firstInit || !secondInit || !initValueEquivalent(secondInit, firstInit))
    {
        rejectReason = "init mismatch";
        return false;
    }

    if (!LoopFusionPass::ioInterleaveFusionSafe(firstLoop, secondLoop, first, second))
    {
        rejectReason = "ordering-sensitive IO not mutually exclusive on induction index";
        return false;
    }

    if (!LoopFusionPass::fusionInductionDimensionsCompatible(firstLoop, secondLoop, first, second,
                                                             linkKind, seqGlue))
    {
        rejectReason = "fusion pairs different loop dimensions (outer/inner index mismatch)";
        return false;
    }

    if (seqGlue && seqGlue->active)
    {
        if (!exitHasOuterInductionBump(second.exit, second.indPhi))
        {
            rejectReason = "sequential inner fusion requires outer bump on second inner exit";
            return false;
        }
    }

    BasicBlock *effectiveSecondPreheader = second.preheader;
    if (phiHasIncomingFrom(second.indPhi, first.exit))
        effectiveSecondPreheader = first.exit;

    for (BasicBlock *bb : secondLoop.blocks)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (!inst)
                continue;
            if (seqGlue && seqGlue->active && seqGlue->outerFirstLoop && seqGlue->outerSecondLoop)
            {
                if (instHasUseOutsideLoops(inst, secondLoop, firstLoop, seqGlue->outerFirstLoop,
                                           seqGlue->outerSecondLoop))
                {
                    rejectReason = "second loop has external use";
                    return false;
                }
            }
            else if (instHasUseOutsideLoopExceptExit(inst, secondLoop, second.exit))
            {
                rejectReason = "second loop has external use";
                return false;
            }
        }
    }

    if (effectiveSecondPreheader)
    {
        for (auto &instPtr : effectiveSecondPreheader->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (!inst || inst->isTerminator() || dynamic_cast<PhiInst *>(inst))
                continue;
            if (!isClonablePreheaderInst(inst) || inst->mayHaveSideEffects())
            {
                rejectReason = "preheader has non-clonable or side-effect instruction";
                return false;
            }
        }
    }

    return true;
}

bool LoopFusionPass::attemptFuseLoopPair(const Loop &firstLoop, const Loop &secondLoop,
                                                     CanonicalLoopShape &first, CanonicalLoopShape &second,
                                                     FusionLinkKind linkKind,
                                                     const vector<BasicBlock *> &glueBlocks,
                                                     BasicBlock *glueExitFrom, BasicBlock *glueExitOldSucc,
                                                     string &rejectReason, const vector<Loop> &allLoops,
                                                     const SequentialSiblingGlueInfo *seqGlue)
{
    rejectReason.clear();
    std::unordered_map<Value *, Value *> valueMap;
    const bool sequentialInnerGlue = seqGlue && seqGlue->active;
    PhiInst *secondLocalInd =
        findLocalInductionPhiInHeader(second.header, second.latch);
    PhiInst *secondCarriedInd = secondLocalInd ? secondLocalInd : second.indPhi;

    auto rejectFusion = [&](const string &reason) {
        rejectReason = reason;
    };

    if (!LoopFusionPass::validateFuseLoopPair(firstLoop, secondLoop, first, second, linkKind, rejectReason,
                                              allLoops, seqGlue))
        return false;

    const bool sharedBoundaryAdjacent =
        linkKind == FusionLinkKind::DirectAdjacent && second.preheader == first.exit;
    const bool glueWrapperOuter =
        linkKind == FusionLinkKind::GlueToSecondHeader &&
        gluePathIsWrapperOuterLoop(glueBlocks, second.header);

    if (!sequentialInnerGlue && !glueWrapperOuter && secondCarriedInd &&
        secondCarriedInd != first.indPhi)
        valueMap[secondCarriedInd] = first.indPhi;

    Value *outerFirstIv = nullptr;
    Value *outerSecondIv = nullptr;
    if (seqGlue && seqGlue->active)
    {
        outerFirstIv = headerInductionValue(seqGlue->outerFirst);
        if (!outerFirstIv)
            outerFirstIv = seqGlue->outerFirst.indPhi;
        outerSecondIv = headerInductionValue(seqGlue->outerSecond);
        if (!outerSecondIv)
            outerSecondIv = seqGlue->outerSecond.indPhi;
        if (!outerFirstIv || !outerSecondIv)
        {
            rejectFusion("sequential inner fusion missing outer induction value");
            return false;
        }
    }

    if (!sequentialInnerGlue && !glueWrapperOuter && blockHasPhi(second.exit))
    {
        for (auto &instPtr : second.exit->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (!inst)
                continue;
            auto *phi = dynamic_cast<PhiInst *>(inst);
            if (!phi || phi->getIncomingValue(0) != second.indPhi)
                continue;
            phi->setIncomingBlock(0, first.exit);
            phi->setIncomingValue(0, first.indPhi);
        }
    }

    BasicBlock *effectiveSecondPreheader = second.preheader;
    if (phiHasIncomingFrom(second.indPhi, first.exit))
        effectiveSecondPreheader = first.exit;

    if (linkKind == FusionLinkKind::GlueToSecondHeader &&
        exitHasOuterInductionBump(first.exit, first.indPhi))
    {
        if (seqGlue && seqGlue->active && !stripOuterBumpFromExit(first.exit, first.indPhi))
        {
            rejectFusion("failed to strip premature outer induction bump on first inner exit");
            return false;
        }
        else
        {
            Instruction *bumpInst = nullptr;
            auto &exitInsts = first.exit->getInstructions();
            for (auto &instPtr : exitInsts)
            {
                auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get());
                if (!bin || (bin->getOpcode() != Opcode::Add && bin->getOpcode() != Opcode::Sub))
                    continue;
                if (valueReferencesPhi(bin->getLHS(), first.indPhi) ||
                    valueReferencesPhi(bin->getRHS(), first.indPhi))
                    continue;
                if (dynamic_cast<PhiInst *>(stripCopyChain(bin->getLHS())) ||
                    dynamic_cast<PhiInst *>(stripCopyChain(bin->getRHS())))
                {
                    bumpInst = bin;
                    break;
                }
            }
            if (bumpInst && bumpInst->getUsers().empty())
                removeOuterInductionBumpInExit(first.exit, first.indPhi);
        }
    }

    if (seqGlue && seqGlue->active && outerSecondIv && outerFirstIv)
        valueMap[outerSecondIv] = outerFirstIv;

    bool safe = true;
    auto &preInsts = first.preheader->getInstructions();
    size_t preInsertPos = preInsts.size();
    if (!preInsts.empty() && preInsts.back()->isTerminator())
        --preInsertPos;
    if (effectiveSecondPreheader && !sequentialInnerGlue && !sharedBoundaryAdjacent)
    {
        for (auto &instPtr : effectiveSecondPreheader->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (!inst)
            {
                safe = false;
                break;
            }
            if (inst->isTerminator())
                continue;
            if (dynamic_cast<PhiInst *>(inst))
                continue;
            if (effectiveSecondPreheader == first.exit)
                continue;
            if (!isClonablePreheaderInst(inst) || inst->mayHaveSideEffects())
            {
                safe = false;
                break;
            }

            Instruction *cloned = inst->clone();
            cloned->setName(inst->getName() + ".fused.pre");
            for (size_t k = 0; k < cloned->getNumOperands(); ++k)
            {
                Value *oldOp = cloned->getOperandByIndex(k);
                auto it = valueMap.find(oldOp);
                if (it != valueMap.end())
                    cloned->setOperandByIndex(k, it->second);
            }
            valueMap[inst] = cloned;
            preInsts.insert(preInsts.begin() + preInsertPos++, unique_ptr<Instruction>(cloned));
        }
    }
    if (!safe)
    {
        rejectFusion("preheader has non-clonable or side-effect instruction");
        return false;
    }

    const bool bodyMergeGlue = !sequentialInnerGlue && linkKind == FusionLinkKind::GlueToSecondHeader &&
                               !sharedBoundaryAdjacent && !glueWrapperOuter;

    if (!sequentialInnerGlue)
    {
        if (bodyMergeGlue)
        {
            if (!mergeSecondHeaderPhisIntoFirst(first.header, second.header, second.preheader,
                                                first.preheader, first.latch, second.latch,
                                                second.indPhi, first.indPhi, valueMap))
            {
                rejectFusion("failed to merge second loop header phis into first");
                return false;
            }
        }
        // glue 外层包装：min/store 各自保留内层归纳变量，禁止映射到 first.indPhi
        if (!glueWrapperOuter)
        {
            remapSecondLoopInduction(secondLoop, secondCarriedInd, first.indPhi, valueMap);
            if (!bodyMergeGlue)
                retargetSecondLoopPhiLatches(secondLoop, secondCarriedInd, second.latch, first.latch);
        }
        if (sharedBoundaryAdjacent)
        {
            LoopFusionPass::linkSharedBoundaryHalfBound(first.preheader, first.exit, secondLoop, valueMap);
            transferLatchPhiIncoming(first.indPhi, second.indPhi, first.latch, second.latch, second.latch,
                                   valueMap);
        }
    }
    // sequentialInnerGlue：内层 j 归纳变量保持独立，仅由下方 outer iv 映射处理外层 i
    if (sequentialInnerGlue && seqGlue->outerSecondLoop && outerFirstIv && outerSecondIv)
    {
        vector<BasicBlock *> remapBlocks;
        for (BasicBlock *bb : seqGlue->outerSecondLoop->blocks)
        {
            if (bb && bb != second.exit)
                remapBlocks.push_back(bb);
        }
        if (seqGlue->outerSecond.body && seqGlue->outerSecond.body != second.exit)
            remapBlocks.push_back(seqGlue->outerSecond.body);
        remapValueInBlockList(remapBlocks, outerSecondIv, outerFirstIv);
        if (second.exit)
            unifyOuterInductionInExitBump(second.exit, outerSecondIv, outerFirstIv);
    }

    // 共享边界相邻：同一轮迭代内串联两个循环体（IO 两半 / store 两半），出口落到 second.exit
    if (sharedBoundaryAdjacent)
    {
        LoopFusionPass::stripSharedBoundarySecondInit(first.exit, second);
        if (!replaceBranchTarget(second.latch, second.header, first.header))
        {
            rejectFusion("failed to rewrite shared-boundary second latch to first header");
            return false;
        }
        if (first.latch && second.body &&
            !replaceBranchTarget(first.latch, first.header, second.body))
        {
            rejectFusion("failed to chain shared-boundary first latch to second body");
            return false;
        }
        finalizeSharedBoundaryFusion(first.header, first.body, first.exit, first.latch, second.header,
                                     second.exit, secondLoop, valueMap);
        Function *func = first.header ? first.header->Parent : nullptr;
        if (func)
            severSharedBoundaryDeadBlocks(func, first.exit, second.header, second.exit);
    }
    // 顺序兄弟内层：同一外层行上先跑 first 内层 j 循环再跑 second（如 C=2A+3B 再变换 C）
    else if (sequentialInnerGlue)
    {
        const bool innerFullScan =
            seqGlue && seqGlue->active && seqGlue->outerFirst.header &&
            blockBranchesTo(first.exit, seqGlue->outerFirst.header);
        if (innerFullScan)
        {
            if (blockBranchesTo(first.header, first.exit) &&
                !replaceBranchTarget(first.header, first.exit, second.header))
            {
                rejectFusion("failed to chain full-scan first inner exit to second inner header");
                return false;
            }
            repairFullScanSecondInnerHeaderPhi(first.header, second.header, second.latch,
                                               second.preheader, secondCarriedInd);
        }
        else
        {
            if (!replaceBranchTarget(second.latch, second.header, first.header))
            {
                rejectFusion("failed to chain sequential-inner second latch to first header");
                return false;
            }
            if (first.latch && second.body &&
                !replaceBranchTarget(first.latch, first.header, second.body))
            {
                rejectFusion("failed to chain sequential-inner first latch to second body");
                return false;
            }
        }
    }
    else if (bodyMergeGlue)
    {
        if (!spliceSecondBodyIntoFirst(first.body, second.body, first.header, second.header,
                                       secondCarriedInd, first.indPhi, valueMap))
        {
            rejectFusion("failed to splice second loop body into first");
            return false;
        }
        removePhiIncomingFromBlock(first.indPhi, first.latch);
        removePhiIncomingFromBlock(first.indPhi, second.latch);
        if (Instruction *bump = findInductionBumpInBlock(first.body, first.indPhi))
            setPhiIncomingForBlock(first.indPhi, first.latch, bump);
        applyValueMapToBlocks({first.header}, valueMap);

        BasicBlock *epilogue = resolveBodyMergeEpilogue(second.exit, first.exit, secondLoop, second.header,
                                                        second.body, second.preheader, glueBlocks);
        if (!epilogue)
            epilogue = getUnconditionalBranchTarget(second.exit);
        if (!epilogue)
            epilogue = getUnconditionalBranchTarget(first.exit);
        applyValueMapToBlocks({second.exit}, valueMap);
        if (!spliceSecondExitIntoFirstExit(first.exit, second.exit, first.header, valueMap))
        {
            rejectFusion("failed to splice second loop exit into first exit");
            return false;
        }
        if (epilogue)
        {
            fullyDisconnectBodyMergedSecondLoop(secondLoop, first.header, first.body, first.exit,
                                                second.header, second.body, second.preheader,
                                                second.exit, glueBlocks, epilogue);
            applyValueMapToBlocks({first.exit, epilogue}, valueMap);
        }
    }
    else if (glueWrapperOuter)
    {
        finalizeGlueWrapperFusion(first.header, first.exit, second.exit, glueBlocks);
    }
    else if (!sequentialInnerGlue)
    {
        if (!replaceBranchTarget(first.latch, first.header, second.header))
        {
            rejectFusion("failed to rewrite first latch to second header");
            return false;
        }
        if (!replaceBranchTarget(second.latch, second.header, first.header))
        {
            rejectFusion("failed to rewrite second latch to first header");
            return false;
        }
        transferLatchPhiIncoming(first.indPhi, second.indPhi, first.latch, second.latch, second.latch,
                               valueMap);
    }

    if (sharedBoundaryAdjacent)
    {
        forceHeaderLoopExitTo(first.header, first.body, second.exit);
    }
    else if (!sequentialInnerGlue && second.preheader != first.exit && !glueWrapperOuter)
    {
        if (!bodyMergeGlue)
            (void)LoopFusionPass::spliceExitInitsBeforeBranch(second.exit, first.exit);
        if (!bodyMergeGlue)
        {
            if (!replaceBranchTarget(first.header, first.exit, second.exit))
            {
                auto *hBr = dynamic_cast<BranchInst *>(first.header->getTerminator());
                if (!hBr || !hBr->isConditional())
                {
                    rejectFusion("failed to rewrite first header exit to second exit");
                    return false;
                }
                if (hBr->getFalseBlock() == first.exit)
                    hBr->setFalseBlock(second.exit);
                else if (hBr->getTrueBlock() == first.exit)
                    hBr->setTrueBlock(second.exit);
                else
                {
                    rejectFusion("failed to rewrite first header exit to second exit");
                    return false;
                }
            }
        }
    }
    else if (glueExitFrom && glueExitOldSucc && !sequentialInnerGlue && !bodyMergeGlue && !glueWrapperOuter)
    {
        BasicBlock *glueTarget = second.header;
        if (blockBranchesTo(glueExitFrom, glueExitOldSucc) &&
            !replaceBranchTarget(glueExitFrom, glueExitOldSucc, glueTarget))
        {
            rejectFusion("failed to retarget glue exit to second header");
            return false;
        }
    }

    // 合并两个外层壳循环：second 外层完成时直接接到 first 外层之后的代码
    if (seqGlue && seqGlue->active)
    {
        if (seqGlue->outerSecond.header && second.exit &&
            !replaceBranchTarget(second.exit, seqGlue->outerSecond.header, seqGlue->outerFirst.header))
        {
            rejectFusion("failed to retarget second inner exit to first outer header");
            return false;
        }
        if (seqGlue->outerSecond.exit && seqGlue->outerFirst.exit)
        {
            BasicBlock *afterSecondOuter = getUnconditionalBranchTarget(seqGlue->outerSecond.exit);
            if (!afterSecondOuter)
                afterSecondOuter = seqGlue->outerSecond.exit;
            LoopFusionPass::stripSkippedOuterLoopEntryInit(seqGlue->outerFirst.exit,
                                                           seqGlue->outerSecond);
            BasicBlock *thirdLoopExit = nullptr;
            for (BasicBlock *succ : afterSecondOuter->getSuccessors())
            {
                if (succ && succ != afterSecondOuter)
                {
                    auto *br = dynamic_cast<BranchInst *>(afterSecondOuter->getTerminator());
                    if (br && !br->isConditional() && br->getTrueBlock() == succ)
                    {
                        thirdLoopExit = succ;
                        break;
                    }
                }
            }
            if (!thirdLoopExit)
            {
                auto *hBr = dynamic_cast<BranchInst *>(afterSecondOuter->getTerminator());
                if (hBr && hBr->isConditional())
                    thirdLoopExit = hBr->getFalseBlock();
            }
            repairSkippedOuterSuccessorEntry(seqGlue->outerFirst.exit, seqGlue->outerSecond.exit,
                                             afterSecondOuter, thirdLoopExit);
            bool redirected = false;
            if (seqGlue->outerSecond.exit && seqGlue->outerSecond.exit != seqGlue->outerFirst.exit)
                redirected |= replaceBranchTarget(seqGlue->outerFirst.exit, seqGlue->outerSecond.exit,
                                                  afterSecondOuter);
            if (seqGlue->outerSecond.header)
                redirected |= replaceBranchTarget(seqGlue->outerFirst.exit, seqGlue->outerSecond.header,
                                                  afterSecondOuter);
            if (!redirected)
            {
                rejectFusion("failed to skip redundant second outer loop on completion");
                return false;
            }
        }

        // 内层合并：按 j 交错 vs 先扫完 first 再扫 second 分别处理
        if (sequentialInnerGlue && first.header && first.exit && second.exit && seqGlue)
        {
            const bool innerFullScan =
                seqGlue->outerFirst.header &&
                blockBranchesTo(first.exit, seqGlue->outerFirst.header);
            if (!innerFullScan &&
                !replaceBranchTarget(first.header, first.exit, second.exit))
            {
                rejectFusion("failed to route fused inner loop exit through second inner exit");
                return false;
            }
            if (seqGlue->outerFirst.header)
            {
                Value *outerIv = headerInductionValue(seqGlue->outerFirst);
                if (!outerIv)
                    outerIv = seqGlue->outerFirst.indPhi;
                Value *bumpVal = outerIv ? findOuterBumpValueInExit(second.exit, outerIv) : nullptr;
                for (auto &instPtr : seqGlue->outerFirst.header->getInstructions())
                {
                    auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
                    if (!phi)
                        continue;
                    for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
                    {
                        if (phi->getIncomingBlock(k) == first.exit)
                        {
                            phi->setIncomingBlock(k, second.exit);
                            if (bumpVal)
                                phi->setIncomingValue(k, bumpVal);
                        }
                    }
                }
            }
        }
    }

    recordFusion(first, second, linkKind, sharedBoundaryAdjacent, sequentialInnerGlue, glueBlocks,
                 glueWrapperOuter);
    return true;
}

void LoopFusionPass::recordFusion(const CanonicalLoopShape &first, const CanonicalLoopShape &second,
                                  FusionLinkKind linkKind, bool sharedBoundaryAdjacent,
                                  bool sequentialInnerGlue, const vector<BasicBlock *> &glueBlocks,
                                  bool glueWrapperOuter)
{
    const char *kind = "adjacent";
    if (sequentialInnerGlue)
        kind = "sequential-sibling-inner";
    else if (sharedBoundaryAdjacent)
        kind = "shared-boundary-adjacent";
    else if (glueWrapperOuter)
        kind = "glue-wrapper";
    else if (linkKind == FusionLinkKind::GlueToSecondHeader)
        kind = "glue-body-merged";

    debugInfo << "fused " << kind << " loops ";
    if (first.header)
        debugInfo << first.header->getName();
    else
        debugInfo << "?";
    debugInfo << " + ";
    if (second.header)
        debugInfo << second.header->getName();
    else
        debugInfo << "?";

    if (linkKind == FusionLinkKind::GlueToSecondHeader && !glueBlocks.empty())
    {
        debugInfo << " via ";
        for (size_t gi = 0; gi < glueBlocks.size(); ++gi)
        {
            if (gi)
                debugInfo << "->";
            if (glueBlocks[gi])
                debugInfo << glueBlocks[gi]->getName();
        }
    }
    debugInfo << "\n";
}

bool LoopFusionPass::matchCanonicalLoopShape(const Loop *loop, CanonicalLoopShape &out) const
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
    refineLoopInductionPhi(out.header, out.latch, out.indPhi);
    out.valid = true;
    return true;
}

bool LoopFusionPass::runOnFunction(Function *func)
{
    if (!func)
        return false;

    debugInfo.str("");
    debugInfo.clear();
    //检查指令数量是否过多，过多则分析复杂度太高，不进行融合
    if (func->getInstructionCount() > 3000)
        return false;
    // 每轮至多融合一对，然后重新建循环信息，直到无法再融
    bool changed = false;
    for (int round = 0; round < 32; ++round)
    {
        func->setLoops(ControlFlowAnalysis::findLoops(func));
        const auto loops = func->getLoops();
        if (loops.empty())
            break;
        if (!tryFuseAdjacentLoops(func, loops))
            break;
        changed = true;
    }
    return changed;
}

bool LoopFusionPass::tryFuseAdjacentLoops(Function *func, const vector<Loop> &loops)
{
    if (!func)
        return false;

    const auto tryPair = [&](size_t i, size_t j, bool seqGluePassOnly) -> bool {
        if (i == j)
            return false;

        CanonicalLoopShape first{};
        if (!matchCanonicalLoopShape(&loops[i], first) || !first.valid)
        {
            if (!LoopFusionPass::matchFusionPartnerLoopShape(&loops[i], first))
                return false;
        }
        if (!LoopFusionPass::loopEntryInitValue(first))
            return false;

        CanonicalLoopShape second{};
        if (!matchCanonicalLoopShape(&loops[j], second) || !second.valid)
        {
            if (!LoopFusionPass::matchFusionPartnerLoopShape(&loops[j], second))
                return false;
        }

        FusionLinkKind linkKind = FusionLinkKind::DirectAdjacent;
        vector<BasicBlock *> glueBlocks;
        BasicBlock *glueExitFrom = nullptr;
        BasicBlock *glueExitOldSucc = nullptr;
        SequentialSiblingGlueInfo seqGlue{};

        const bool seqGlueFound = findSequentialInnerGluePath(loops, loops[i], loops[j], first, second,
                                                              seqGlue, glueBlocks, glueExitFrom,
                                                              glueExitOldSucc);
        if (seqGluePassOnly)
        {
            if (!seqGlueFound)
                return false;
            linkKind = FusionLinkKind::GlueToSecondHeader;
        }
        else if (seqGlueFound)
        {
            return false;
        }

        const bool sharedBoundaryPair = second.preheader == first.exit;
        if (!seqGluePassOnly)
        {
            if (sharedBoundaryPair && !blockBranchesTo(first.exit, second.header))
                return false;
            if (blockBranchesTo(first.exit, second.header) && !sharedBoundaryPair)
                return false;
            if (fusionPathAlreadyConnected(first.exit, loops[j], second.header) && !sharedBoundaryPair)
                return false;
            if (first.latch && blockBranchesTo(first.latch, second.header) && !sharedBoundaryPair)
                return false;

            const bool phiLinked = phiHasIncomingFrom(second.indPhi, first.exit);
            if (second.preheader != first.exit && !phiLinked)
            {
                if (findGluePath(first.exit, second.header, 4, glueBlocks, &glueExitOldSucc))
                {
                    if (!glueBlocksSafe(glueBlocks, first.indPhi))
                        return false;
                    linkKind = FusionLinkKind::GlueToSecondHeader;
                    glueExitFrom = first.exit;
                }
                else
                    return false;
            }

            if (first.latch == first.body && linkKind == FusionLinkKind::DirectAdjacent &&
                !sharedBoundaryPair)
                return false;
        }

        string rejectReason;
        const SequentialSiblingGlueInfo *seqGluePtr = seqGlue.active ? &seqGlue : nullptr;
        if (!LoopFusionPass::validateFuseLoopPair(loops[i], loops[j], first, second, linkKind, rejectReason,
                                                  loops, seqGluePtr))
            return false;

        return attemptFuseLoopPair(loops[i], loops[j], first, second, linkKind, glueBlocks, glueExitFrom,
                                   glueExitOldSucc, rejectReason, loops, seqGluePtr);
    };

    for (size_t i = 0; i < loops.size(); ++i)
        for (size_t j = 0; j < loops.size(); ++j)
            if (tryPair(i, j, true))
                return true;

    for (size_t i = 0; i < loops.size(); ++i)
        for (size_t j = 0; j < loops.size(); ++j)
            if (tryPair(i, j, false))
                return true;

    return false;
}
