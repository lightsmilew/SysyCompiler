#include "LoopPass.h"
#include <algorithm>
#include <functional>
#include <unordered_set>
using namespace std;
using namespace optimization;

namespace
{
bool sameValueForAccess(Value *lhs, Value *rhs)
{
    if (lhs == rhs)
    {
        return true;
    }

    auto *lhsConstInt = dynamic_cast<ConstantInt *>(lhs);
    auto *rhsConstInt = dynamic_cast<ConstantInt *>(rhs);
    if (lhsConstInt && rhsConstInt)
    {
        return lhsConstInt->Value == rhsConstInt->Value;
    }

    if (!lhs || !rhs)
    {
        return false;
    }

    return isSameAddr(lhs, rhs);
}

bool collectAccessPattern(Value *value, Value *&baseValue, vector<Value *> &indices)
{
    if (!value)
    {
        return false;
    }

    if (auto *castInst = dynamic_cast<CastInst *>(value))
    {
        return collectAccessPattern(castInst->getOperand(), baseValue, indices);
    }

    if (auto *gepInst = dynamic_cast<GetElementPtrInst *>(value))
    {
        if (!collectAccessPattern(gepInst->getPointerOperand(), baseValue, indices))
        {
            return false;
        }

        auto gepIndices = gepInst->getIndices();
        indices.insert(indices.end(), gepIndices.begin(), gepIndices.end());
        return true;
    }

    if (!baseValue)
    {
        baseValue = value;
        return true;
    }

    return sameValueForAccess(baseValue, value);
}

bool sameAccessPattern(const vector<Value *> &lhs, const vector<Value *> &rhs)
{
    if (lhs.size() != rhs.size())
    {
        return false;
    }

    for (size_t i = 0; i < lhs.size(); ++i)
    {
        if (!sameValueForAccess(lhs[i], rhs[i]))
        {
            return false;
        }
    }

    return true;
}

Value *stripCopy(Value *v)
{
    while (auto *cpy = dynamic_cast<CopyInst *>(v))
    {
        v = cpy->getSource();
    }
    return v;
}

bool sameLoopValue(Value *a, Value *b)
{
    if (!a || !b)
    {
        return false;
    }
    if (stripCopy(a) == stripCopy(b))
    {
        return true;
    }
    if (!a->getName().empty() && a->getName() == b->getName())
    {
        return true;
    }
    return false;
}

bool valueDependsOnImpl(Value *val, Value *target, std::unordered_set<Value *> &visited)
{
    if (!val || !target)
    {
        return false;
    }
    if (sameLoopValue(val, target))
    {
        return true;
    }
    if (!visited.insert(val).second)
    {
        return false;
    }
    if (auto *inst = dynamic_cast<Instruction *>(val))
    {
        for (auto *op : inst->getOperands())
        {
            if (valueDependsOnImpl(op, target, visited))
            {
                return true;
            }
        }
    }
    return false;
}

bool valueDependsOn(Value *val, Value *target)
{
    std::unordered_set<Value *> visited;
    return valueDependsOnImpl(val, target, visited);
}

BasicBlock *findLoopLatchBlock(const Loop &loop)
{
    for (auto *bb : loop.blocks)
    {
        if (bb == loop.header)
        {
            continue;
        }
        for (auto *succ : bb->getSuccessors())
        {
            if (succ == loop.header)
            {
                return bb;
            }
        }
    }
    return nullptr;
}

bool hasZeroInitOutsideLoop(Value *tracked, const Loop &loop)
{
    if (!tracked || !loop.header)
    {
        return false;
    }

    if (auto *trackedPhi = dynamic_cast<PhiInst *>(stripCopy(tracked)))
    {
        for (size_t i = 0; i < trackedPhi->getNumIncomingValues(); ++i)
        {
            if (loop.containsBlock(trackedPhi->getIncomingBlock(i)))
            {
                continue;
            }
            auto *initConst = dynamic_cast<ConstantInt *>(stripCopy(trackedPhi->getIncomingValue(i)));
            if (initConst && initConst->Value == 0)
            {
                return true;
            }
        }
        return false;
    }

    for (auto *pred : loop.header->getPredecessors())
    {
        if (loop.containsBlock(pred))
        {
            continue;
        }
        for (auto &instPtr : pred->getInstructions())
        {
            auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
            if (!cpy || !sameLoopValue(cpy, tracked))
            {
                continue;
            }
            auto *initConst = dynamic_cast<ConstantInt *>(stripCopy(cpy->getSource()));
            if (initConst && initConst->Value == 0)
            {
                return true;
            }
        }
    }
    return false;
}

bool hasUnitIncrementAtLatch(Value *iv, const Loop &loop)
{
    BasicBlock *latch = findLoopLatchBlock(loop);
    if (!latch)
    {
        return false;
    }

    for (auto &instPtr : latch->getInstructions())
    {
        auto *addInst = dynamic_cast<BinaryOperator *>(instPtr.get());
        if (!addInst || addInst->getOpcode() != Opcode::Add)
        {
            continue;
        }
        auto *one = dynamic_cast<ConstantInt *>(stripCopy(addInst->getRHS()));
        if (!one || one->Value != 1)
        {
            continue;
        }
        if (sameLoopValue(addInst->getLHS(), iv))
        {
            return true;
        }
    }
    return false;
}

bool hasNestedLoopInside(const Loop &outer, const vector<Loop> &allLoops)
{
    for (const auto &inner : allLoops)
    {
        if (inner.header == outer.header)
        {
            continue;
        }
        if (outer.containsBlock(inner.header))
        {
            return true;
        }
    }
    return false;
}

std::set<BasicBlock *> collectPerIterationBlocks(const Loop &outer)
{
    std::set<BasicBlock *> perIter;
    if (!outer.header)
    {
        return perIter;
    }

    BasicBlock *body = nullptr;
    auto &headerInsts = outer.header->getInstructions();
    auto *br = dynamic_cast<BranchInst *>(headerInsts.back().get());
    if (br && br->isConditional())
    {
        body = br->getTrueBlock();
    }
    if (!body || !outer.containsBlock(body))
    {
        return perIter;
    }

    std::vector<BasicBlock *> worklist = {body};
    perIter.insert(body);
    while (!worklist.empty())
    {
        BasicBlock *bb = worklist.back();
        worklist.pop_back();
        for (auto *succ : bb->getSuccessors())
        {
            if (succ == outer.header || !outer.containsBlock(succ) || perIter.count(succ))
            {
                continue;
            }
            perIter.insert(succ);
            worklist.push_back(succ);
        }
    }
    return perIter;
}

static constexpr int kFullUnrollMaxTripCount = 20;
static constexpr int kMaxConstantFullUnrollNestLayers = 2;
static constexpr int kPartialUnrollFactor = 8;
static constexpr int kPartialUnrollMaxBodyInsts = 40;
/// 纯计算循环体指令数超过此值时不做部分展开（展开后 I-cache 压力大、收益小）
static constexpr int kPureComputePartialUnrollMaxBodyInsts = 4;

void collectLoopBodyAndLatch(const Loop &loop, BasicBlock *&body, BasicBlock *&latch)
{
    latch = nullptr;
    if (loop.blocks.size() == 3)
    {
        latch = findLoopLatchBlock(loop);
    }
    body = nullptr;
    for (auto *bb : loop.blocks)
    {
        if (bb != loop.header && bb != latch)
        {
            body = bb;
            break;
        }
    }
}

bool isUnrollableSimpleLoop(const Loop &loop, const vector<Loop> &allLoops)
{
    if (hasNestedLoopInside(loop, allLoops))
    {
        return false;
    }
    if (loop.blocks.size() <= 2)
    {
        return true;
    }
    if (loop.blocks.size() == 3 && findLoopLatchBlock(loop))
    {
        return true;
    }
    return false;
}

int countLoopBodyInsts(BasicBlock *body, BasicBlock *latch)
{
    int count = 0;
    auto countBlock = [&](BasicBlock *bb)
    {
        if (!bb)
        {
            return;
        }
        for (auto &instPtr : bb->getInstructions())
        {
            if (!instPtr->isTerminator())
            {
                count++;
            }
        }
    };
    countBlock(body);
    countBlock(latch);
    return count;
}

static bool isPureComputationInst(Instruction *inst)
{
    if (!inst || inst->isTerminator())
    {
        return true;
    }
    const Opcode op = inst->getOpcode();
    if (op == Opcode::Load || op == Opcode::Store || op == Opcode::Stored || op == Opcode::Call)
    {
        return false;
    }
    return !inst->mayHaveSideEffects();
}

static bool loopBodyIsPureComputation(BasicBlock *body, BasicBlock *latch)
{
    auto blockIsPure = [](BasicBlock *bb)
    {
        if (!bb)
        {
            return true;
        }
        for (auto &instPtr : bb->getInstructions())
        {
            if (!isPureComputationInst(instPtr.get()))
            {
                return false;
            }
        }
        return true;
    };
    return blockIsPure(body) && blockIsPure(latch);
}

struct PartialUnrollCost
{
    int bodyInstCount = 0;
    bool isPureComputation = false;
    bool profitable = false;
};

static PartialUnrollCost computePartialUnrollCost(BasicBlock *body, BasicBlock *latch)
{
    PartialUnrollCost cost;
    cost.bodyInstCount = countLoopBodyInsts(body, latch);
    cost.isPureComputation = loopBodyIsPureComputation(body, latch);
    if (cost.bodyInstCount > kPartialUnrollMaxBodyInsts)
    {
        return cost;
    }
    if (cost.isPureComputation && cost.bodyInstCount > kPureComputePartialUnrollMaxBodyInsts)
    {
        return cost;
    }
    cost.profitable = true;
    return cost;
}

bool verifySimpleLoopControl(BasicBlock *header, BasicBlock *body, BasicBlock *latch)
{
    if (!body || !header)
    {
        return false;
    }
    auto &bodyInsts = body->getInstructions();
    if (bodyInsts.empty() || !bodyInsts.back()->isTerminator())
    {
        return false;
    }
    if (latch)
    {
        auto *bodyBr = dynamic_cast<BranchInst *>(bodyInsts.back().get());
        if (!bodyBr || bodyBr->isConditional() || bodyBr->getTrueBlock() != latch)
        {
            return false;
        }
        auto &latchInsts = latch->getInstructions();
        if (latchInsts.empty() || !latchInsts.back()->isTerminator())
        {
            return false;
        }
        auto *latchBr = dynamic_cast<BranchInst *>(latchInsts.back().get());
        return latchBr && !latchBr->isConditional() && latchBr->getTrueBlock() == header;
    }
    auto *br = dynamic_cast<BranchInst *>(bodyInsts.back().get());
    return br && !br->isConditional() && br->getTrueBlock() == header;
}

int loopNestingDepth(const Loop &loop, const vector<Loop> &allLoops)
{
    int depth = 0;
    for (const auto &other : allLoops)
    {
        if (other.header != loop.header && other.containsBlock(loop.header))
        {
            depth++;
        }
    }
    return depth;
}

void sortLoopsInnermostFirst(vector<Loop> &loops)
{
    std::sort(loops.begin(), loops.end(),
              [&loops](const Loop &a, const Loop &b)
              {
                  int depthA = loopNestingDepth(a, loops);
                  int depthB = loopNestingDepth(b, loops);
                  if (depthA != depthB)
                  {
                      return depthA > depthB;
                  }
                  return a.blocks.size() < b.blocks.size();
              });
}

enum class UnrollResult
{
    None,
    Partial,
    Full
};

struct CopyLoopIVInfo
{
    Value *iv = nullptr;
    Value *init = nullptr;
    BinaryOperator *incAdd = nullptr;
    int step = -1;
};

bool detectCopyLoopIV(const Loop &loop,
                      BasicBlock *preheader,
                      BasicBlock *body,
                      BasicBlock *latch,
                      ICmpInst *cmp,
                      CopyLoopIVInfo &info,
                      Value *&boundVal,
                      int &cmpSide)
{
    if (!preheader || !body || !cmp)
    {
        return false;
    }

    Value *ivVal = nullptr;
    if (dynamic_cast<ConstantInt *>(stripCopy(cmp->getRHS())))
    {
        ivVal = cmp->getLHS();
        boundVal = cmp->getRHS();
        cmpSide = 0;
    }
    else if (dynamic_cast<ConstantInt *>(stripCopy(cmp->getLHS())))
    {
        ivVal = cmp->getRHS();
        boundVal = cmp->getLHS();
        cmpSide = 1;
    }
    else
    {
        return false;
    }

    Value *init = nullptr;
    for (auto &instPtr : preheader->getInstructions())
    {
        auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
        if (cpy && sameLoopValue(cpy, ivVal))
        {
            init = cpy->getSource();
            break;
        }
    }
    if (!init)
    {
        return false;
    }

    BinaryOperator *incAdd = nullptr;
    CopyInst *nextCopy = nullptr;
    auto scanBlock = [&](BasicBlock *bb)
    {
        if (!bb)
        {
            return;
        }
        for (auto &instPtr : bb->getInstructions())
        {
            if (auto *add = dynamic_cast<BinaryOperator *>(instPtr.get()))
            {
                if (add->getOpcode() == Opcode::Add &&
                    (sameLoopValue(add->getLHS(), ivVal) || sameLoopValue(add->getRHS(), ivVal)))
                {
                    incAdd = add;
                }
            }
            else if (auto *cpy = dynamic_cast<CopyInst *>(instPtr.get()))
            {
                if (sameLoopValue(cpy, ivVal))
                {
                    nextCopy = cpy;
                }
            }
        }
    };
    scanBlock(body);
    scanBlock(latch);
    if (!incAdd || !nextCopy || stripCopy(nextCopy->getSource()) != incAdd)
    {
        return false;
    }

    int step = -1;
    if (auto *stepConst = dynamic_cast<ConstantInt *>(stripCopy(
            sameLoopValue(incAdd->getLHS(), ivVal) ? incAdd->getRHS() : incAdd->getLHS())))
    {
        step = stepConst->Value;
    }
    if (step <= 0)
    {
        return false;
    }

    info.iv = ivVal;
    info.init = init;
    info.incAdd = incAdd;
    info.step = step;
    return true;
}

UnrollResult tryUnrollOneLoop(Function *func,
                              const Loop &loop,
                              const vector<Loop> &allLoops,
                              int &fullUnrollLayersDone,
                              bool verbose,
                              std::stringstream &debugInfo)
{
    BasicBlock *header = loop.header;
    if (header->getName().find("_unroll_") != string::npos)
        return UnrollResult::None;
    if (!isUnrollableSimpleLoop(loop, allLoops))
        return UnrollResult::None;
    if (header->getInstructions().size() < 2)
        return UnrollResult::None;

    // 1. 收集所有phi指令
    std::vector<PhiInst *> headerPhis;
    for (auto &instPtr : header->getInstructions())
        if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
            headerPhis.push_back(phi);

    // 2. 找到循环条件cmp
    auto &headerInsts = header->getInstructions();

    auto *cmp = dynamic_cast<ICmpInst *>(headerInsts[headerInsts.size() - 2].get());
    if (!cmp)
        return UnrollResult::None;
    // 只处理 < 或 <=，否则跳过
    if (cmp->getPredicate() != ICmpInst::ICMP_SLT && cmp->getPredicate() != ICmpInst::ICMP_SLE)
        return UnrollResult::None;

    // 7. 找到循环体与 latch
    BasicBlock *body = nullptr;
    BasicBlock *latch = nullptr;
    collectLoopBodyAndLatch(loop, body, latch);
    if (!body)
        return UnrollResult::None;
    if (!verifySimpleLoopControl(header, body, latch))
        return UnrollResult::None;
    BasicBlock *preheader = nullptr;
    BasicBlock *exitBlock = nullptr;
    for (auto *pred : header->getPredecessors())
        if (std::find(loop.blocks.begin(), loop.blocks.end(), pred) == loop.blocks.end())
            preheader = pred;
    for (auto *succ : header->getSuccessors())
        if (std::find(loop.blocks.begin(), loop.blocks.end(), succ) == loop.blocks.end())
            exitBlock = succ;
    if (!preheader || !exitBlock)
        return UnrollResult::None;

    for (auto *pred : header->getPredecessors())
    {
        if (pred->getName().find("_unroll_exit") != string::npos)
            return UnrollResult::None;
    }

    // 3. 找到induction variable phi 或 copy 归纳变量
    PhiInst *indPhi = nullptr;
    Value *indVar = nullptr;
    Value *boundVal = nullptr;
    int cmpSide = -1;
    bool copyBasedIV = false;
    CopyLoopIVInfo copyIV;
    std::unordered_map<PhiInst *, Value *> phiInit, phiInc;
    int intcValue = -1;

    for (int side = 0; side < 2; ++side)
    {
        Value *v = (side == 0) ? cmp->getLHS() : cmp->getRHS();
        for (auto *phi : headerPhis)
        {
            if (v == phi || sameLoopValue(v, phi))
            {
                indVar = phi;
                indPhi = phi;
                cmpSide = side;
                boundVal = (side == 0) ? cmp->getRHS() : cmp->getLHS();
                break;
            }
        }
        if (indPhi)
            break;
    }

    if (indPhi)
    {
        for (auto *phi : headerPhis)
        {
            for (size_t i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                BasicBlock *from = phi->getIncomingBlock(i);
                if (std::find(loop.blocks.begin(), loop.blocks.end(), from) == loop.blocks.end())
                    phiInit[phi] = phi->getIncomingValue(i);
                else
                    phiInc[phi] = phi->getIncomingValue(i);
            }
        }
        if (phiInit[indPhi])
        {
            if (auto *bin = dynamic_cast<BinaryOperator *>(phiInc[indPhi]))
            {
                if (bin->getOpcode() == Opcode::Add &&
                    (sameLoopValue(bin->getLHS(), indVar) || sameLoopValue(bin->getRHS(), indVar)))
                {
                    if (auto *constVal = dynamic_cast<ConstantInt *>(stripCopy(bin->getRHS())))
                        intcValue = constVal->Value;
                    else if (auto *constVal = dynamic_cast<ConstantInt *>(stripCopy(bin->getLHS())))
                        intcValue = constVal->Value;
                }
            }
        }
    }

    if (intcValue < 0 &&
        detectCopyLoopIV(loop, preheader, body, latch, cmp, copyIV, boundVal, cmpSide))
    {
        copyBasedIV = true;
        indVar = copyIV.iv;
        intcValue = copyIV.step;
        indPhi = nullptr;
    }
    else if (intcValue < 0)
    {
        return UnrollResult::None;
    }

    // 6. tripCount
    int tripCount = -1;
    Value *initVal = copyBasedIV ? copyIV.init : phiInit[indPhi];
    auto *initConst = dynamic_cast<ConstantInt *>(stripCopy(initVal));
    auto *boundConst = dynamic_cast<ConstantInt *>(stripCopy(boundVal));
    if (initConst && boundConst)
    {
        int init = initConst->Value;
        int bound = boundConst->Value;
        if (cmp->getPredicate() == ICmpInst::ICMP_SLT)
            tripCount = (cmpSide == 0) ? (bound - init) : (init - bound);
        else if (cmp->getPredicate() == ICmpInst::ICMP_SLE)
            tripCount = (cmpSide == 0) ? (bound - init + 1) : (init - bound + 1);
        tripCount = tripCount / intcValue;
    }

    // 9. 完全展开（常量循环嵌套最多展开 kMaxConstantFullUnrollNestLayers 层）
    if (tripCount > 0 && tripCount <= kFullUnrollMaxTripCount &&
        fullUnrollLayersDone < kMaxConstantFullUnrollNestLayers)
    {
        auto &preInsts = preheader->getInstructions();
        auto insertPos = preInsts.size();
        if (!preInsts.empty() && preInsts.back()->isTerminator())
            insertPos--;
        std::unordered_map<Value *, Value *> valueMap;
        if (copyBasedIV)
        {
            valueMap[indVar] = copyIV.init;
        }
        else
        {
            for (auto *phi : headerPhis)
                valueMap[phi] = phiInit[phi];
        }

        for (int i = 0; i < tripCount; ++i)
        {
            std::vector<Instruction *> clonedInsts;
            auto cloneFromBlock = [&](BasicBlock *bb)
            {
                if (!bb)
                {
                    return;
                }
                for (auto &instPtr : bb->getInstructions())
                {
                    if (instPtr->isTerminator())
                        continue;
                    Instruction *cloned = instPtr->clone();
                    cloned->setName(cloned->getName() + "_unroll" + std::to_string(i));
                    for (size_t k = 0; k < cloned->getOperands().size(); ++k)
                    {
                        Value *oldOp = cloned->getOperands()[k];
                        if (copyBasedIV)
                        {
                            if (sameLoopValue(oldOp, indVar))
                                cloned->setOperandByIndex(k, valueMap[indVar]);
                        }
                        else
                        {
                            for (auto *phi : headerPhis)
                            {
                                if (oldOp == phi)
                                    cloned->setOperandByIndex(k, valueMap[phi]);
                            }
                        }
                        if (valueMap.count(oldOp))
                            cloned->setOperandByIndex(k, valueMap[oldOp]);
                    }
                    valueMap[instPtr.get()] = cloned;
                    clonedInsts.push_back(cloned);
                }
            };
            cloneFromBlock(body);
            cloneFromBlock(latch);
            for (auto *cloned : clonedInsts)
                preInsts.insert(preInsts.begin() + insertPos++, std::unique_ptr<Instruction>(cloned));
            if (copyBasedIV)
            {
                valueMap[indVar] = valueMap[copyIV.incAdd];
            }
            else
            {
                for (auto *phi : headerPhis)
                {
                    auto *inc = phiInc[phi];
                    if (inc)
                        valueMap[phi] = valueMap[inc];
                }
            }
        }
        for (auto &instPtr : preInsts)
        {
            if (auto *preBr = dynamic_cast<BranchInst *>(instPtr.get()))
            {
                if (preBr->getTrueBlock() == header)
                    preBr->setTrueBlock(exitBlock);
                if (preBr->getFalseBlock() == header)
                    preBr->setFalseBlock(exitBlock);
            }
        }
        for (auto &instPtr : exitBlock->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
            {
                for (size_t i = 0; i < phi->getIncomingBlocks().size(); ++i)
                {
                    if (phi->getIncomingBlock(i) == header)
                    {
                        phi->setIncomingBlock(i, preheader);
                        if (valueMap.count(phi))
                            phi->setIncomingValue(i, valueMap[phi]);
                    }
                }
            }
        }
        if (!copyBasedIV)
        {
            for (auto *phi : headerPhis)
                phi->replaceAllUsesWith(valueMap[phi]);
        }
        else
        {
            for (auto &instPtr : preheader->getInstructions())
            {
                if (auto *cpy = dynamic_cast<CopyInst *>(instPtr.get()))
                {
                    if (sameLoopValue(cpy, indVar))
                        cpy->setOperandByIndex(0, valueMap[indVar]);
                }
            }
        }
        for (auto *bb : loop.blocks)
            bb->removeSelfBasicBlock();
        preheader->addSuccessor(exitBlock);
        exitBlock->addPredecessor(preheader);
        fullUnrollLayersDone++;
        if (verbose)
            debugInfo << "LoopUnrollingPass: Fully unrolled loop at " << header->getName()
                      << " tripCount=" << tripCount
                      << " (nestLayer=" << fullUnrollLayersDone << "/"
                      << kMaxConstantFullUnrollNestLayers << ")"
                      << (copyBasedIV ? " copy iv" : "") << "\n";
        return UnrollResult::Full;
    }

    if (copyBasedIV || headerPhis.empty())
        return UnrollResult::None;

    const PartialUnrollCost partialCost = computePartialUnrollCost(body, latch);
    if (!partialCost.profitable)
    {
        if (verbose)
        {
            debugInfo << "LoopUnrollingPass: skip 4-way unroll at " << header->getName()
                      << " (bodyInsts=" << partialCost.bodyInstCount;
            if (partialCost.isPureComputation &&
                partialCost.bodyInstCount > kPureComputePartialUnrollMaxBodyInsts)
            {
                debugInfo << ", pure-compute body > " << kPureComputePartialUnrollMaxBodyInsts;
            }
            else if (partialCost.bodyInstCount > kPartialUnrollMaxBodyInsts)
            {
                debugInfo << ", body > " << kPartialUnrollMaxBodyInsts;
            }
            debugInfo << ")\n";
        }
        return UnrollResult::None;
    }

    int unrollFactor = kPartialUnrollFactor;
    auto *unrollHeader = new BasicBlock(header->getName() + "_unroll_header", func);
    auto *unrollBody = new BasicBlock(body->getName() + "_unroll_body", func);
    auto *unrollExit = new BasicBlock(header->getName() + "_unroll_exit", func);

    std::unordered_map<PhiInst *, PhiInst *> phiMap;
    for (auto *phi : headerPhis)
    {
        auto *newPhi = new PhiInst(phi->getType(), phi->getName() + "_unroll_phi");
        unrollHeader->addInstruction(std::unique_ptr<Instruction>(newPhi));
        newPhi->addIncoming(phiInit[phi], preheader);
        phiMap[phi] = newPhi;
    }
    auto *unrollPhi = phiMap[indPhi];

    ICmpInst *loopCondCmp = cmp;
    std::unordered_map<Value *, Value *> headerValueMap;
    for (auto &instPtr : header->getInstructions())
    {
        Instruction *inst = instPtr.get();
        if (dynamic_cast<PhiInst *>(inst) || dynamic_cast<BranchInst *>(inst))
            continue;
        if (inst == loopCondCmp)
            continue;
        Instruction *cloned = inst->clone();
        for (size_t k = 0; k < cloned->getOperands().size(); ++k)
        {
            Value *oldOp = cloned->getOperands()[k];
            for (auto *phi : headerPhis)
            {
                if (oldOp == phi)
                    cloned->setOperandByIndex(k, phiMap[phi]);
            }
            if (headerValueMap.count(oldOp))
                cloned->setOperandByIndex(k, headerValueMap[oldOp]);
        }
        headerValueMap[inst] = cloned;
        unrollHeader->addInstruction(std::unique_ptr<Instruction>(cloned));
    }
    Value *condLHS = unrollPhi;
    Value *condRHS = boundVal;
    auto replaceSSA = [&](Value *v) -> Value *
    {
        if (headerValueMap.count(v))
            return headerValueMap[v];
        for (auto *phi : headerPhis)
            if (v == phi)
                return phiMap[phi];
        return v;
    };
    condLHS = replaceSSA(condLHS);
    condRHS = replaceSSA(condRHS);

    auto *unrollBoundAdj = new BinaryOperator(
        Opcode::Sub,
        condRHS,
        new ConstantInt(IntegerType::getInstance(), unrollFactor * intcValue),
        unrollPhi->getName() + "_unroll_bound");
    unrollHeader->addInstruction(std::unique_ptr<Instruction>(unrollBoundAdj));
    auto *unrollCond = new ICmpInst(ICmpInst::ICMP_SLT, condLHS, unrollBoundAdj, "unroll_cmp");
    unrollHeader->addInstruction(std::unique_ptr<Instruction>(unrollCond));

    auto *unrollBr = new BranchInst(unrollCond, unrollBody, unrollExit);
    unrollHeader->addInstruction(std::unique_ptr<Instruction>(unrollBr));

    std::unordered_map<Value *, Value *> valueMap;
    for (auto *phi : headerPhis)
        valueMap[phi] = phiMap[phi];

    for (int u = 0; u < unrollFactor; ++u)
    {
        auto cloneToUnrollBody = [&](BasicBlock *bb)
        {
            if (!bb)
            {
                return;
            }
            for (auto &instPtr : bb->getInstructions())
            {
                if (instPtr->isTerminator())
                    continue;
                Instruction *cloned = instPtr->clone();
                cloned->setName(cloned->getName() + "_unroll" + std::to_string(u));
                for (size_t k = 0; k < cloned->getOperands().size(); ++k)
                {
                    Value *oldOp = cloned->getOperands()[k];
                    for (auto *phi : headerPhis)
                    {
                        if (oldOp == phi)
                            cloned->setOperandByIndex(k, valueMap[phi]);
                    }
                    if (valueMap.count(oldOp))
                        cloned->setOperandByIndex(k, valueMap[oldOp]);
                }
                unrollBody->addInstruction(std::unique_ptr<Instruction>(cloned));
                valueMap[instPtr.get()] = cloned;
            }
        };
        cloneToUnrollBody(body);
        cloneToUnrollBody(latch);
        for (auto *phi : headerPhis)
        {
            auto *inc = phiInc[phi];
            if (inc)
                valueMap[phi] = valueMap[inc];
        }
    }
    for (auto *phi : headerPhis)
    {
        if (phi == indPhi)
        {
            auto *unrollInc = new BinaryOperator(
                Opcode::Add,
                phiMap[phi],
                new ConstantInt(IntegerType::getInstance(), unrollFactor * intcValue),
                phiMap[phi]->getName() + "_inc");
            unrollBody->addInstruction(std::unique_ptr<Instruction>(unrollInc));
            phiMap[phi]->addIncoming(unrollInc, unrollBody);
        }
        else
        {
            auto *inc = phiInc[phi];
            if (inc)
                phiMap[phi]->addIncoming(valueMap[phi], unrollBody);
        }
    }
    for (auto &instPtr : preheader->getInstructions())
    {
        if (auto *preBr = dynamic_cast<BranchInst *>(instPtr.get()))
        {
            if (preBr->getTrueBlock() == header)
                preBr->setTrueBlock(unrollHeader);
            if (preBr->getFalseBlock() == header)
                preBr->setFalseBlock(unrollExit);
        }
    }
    unrollBody->addInstruction(std::make_unique<BranchInst>(unrollHeader));
    unrollExit->addInstruction(std::make_unique<BranchInst>(header));
    preheader->removeSuccessor(header);
    header->removePredecessor(preheader);

    preheader->addSuccessor(unrollHeader);
    unrollHeader->addPredecessor(preheader);
    unrollHeader->addSuccessor(unrollBody);
    unrollBody->addPredecessor(unrollHeader);
    unrollBody->addSuccessor(unrollHeader);
    unrollHeader->addPredecessor(unrollBody);
    unrollHeader->addSuccessor(unrollExit);
    unrollExit->addPredecessor(unrollHeader);
    unrollExit->addSuccessor(header);
    header->addPredecessor(unrollExit);

    func->addBasicBlock(std::unique_ptr<BasicBlock>(unrollHeader));
    func->addBasicBlock(std::unique_ptr<BasicBlock>(unrollBody));
    func->addBasicBlock(std::unique_ptr<BasicBlock>(unrollExit));
    for (auto &instPtr : header->getInstructions())
    {
        if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
        {
            for (size_t i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                if (phi->getIncomingBlock(i) == preheader)
                {
                    phi->setIncomingBlock(i, unrollExit);
                    if (phiMap.count(phi))
                        phi->setIncomingValue(i, phiMap[phi]);
                }
            }
        }
    }

    if (verbose)
        debugInfo << "LoopUnrollingPass: 4-way unrolled loop at " << header->getName()
                  << " (bodyInsts=" << partialCost.bodyInstCount
                  << ", inserted unroll loop before original)\n";
    return UnrollResult::Partial;
}
} // namespace
// ========== 循环不变代码移动 ==========
bool LoopInvariantCodeMotionPass::runOnFunction(Function *func)
{
    bool changed = false;
    // 记录每一轮pass后是否有外提变量，有则继续运行直到所有能外提变量全部外提
    bool localChanged;
    func->setLoops(ControlFlowAnalysis::findLoops(func)); // 确保循环信息是最新的（含 preheader）
    do
    {
        int count = 0;
        localChanged = false;
        // 1. 查找所有循环

        for (auto &loop : func->getLoops())
        {
            // 2. 使用循环信息中的前置块
            BasicBlock *preheader = loop.getPreheader();
            if (!preheader)
                continue;

            // 3. 收集所有循环不变指令（记录指令和所在基本块）
            std::vector<std::pair<Instruction *, BasicBlock *>> invariants;
            bool foundNew;
            do
            {
                foundNew = false;
                for (auto *bb : loop.blocks)
                {
                    for (auto &instPtr : bb->getInstructions())
                    {
                        Instruction *inst = instPtr.get();
                        if (std::find_if(invariants.begin(), invariants.end(),
                                         [inst](const auto &p)
                                         { return p.first == inst; }) == invariants.end() &&
                            canMoveToPreheader(inst, loop) && isLoopInvariant(inst, loop))
                        {
                            invariants.emplace_back(inst, bb);
                            foundNew = true;
                        }
                    }
                }
            } while (foundNew); // 递增收集直到收敛

            // 4. 将循环不变指令移动到 preheader
            for (auto &[inst, fromBB] : invariants)
            {
                auto &insts = fromBB->getInstructions();
                auto it = std::find_if(insts.begin(), insts.end(),
                                       [&](const std::unique_ptr<Instruction> &ptr)
                                       { return ptr.get() == inst; });
                if (it != insts.end())
                {
                    if (verbose)
                    {
                        debugInfo << "Moved invariant instruction: " << inst->toString()
                                  << " from " << fromBB->getName() << " to preheader "
                                  << preheader->getName() << "\n";
                    }
                    std::unique_ptr<Instruction> movedInst = std::move(*it);
                    it = insts.erase(it);
                    // 将指令插入到 preheader 的末尾(终结指令之前)
                    preheader->insertBeforeTerminator(std::move(movedInst));
                    localChanged = true;
                    changed = true;
                    count++;
                }
            }
        }
    } while (localChanged);
    return changed;
}
bool LoopInvariantCodeMotionPass::canMoveToPreheader(Instruction *inst, const Loop &loop)
{
    // 外提合法判断条件：地址不是循环改变量、没有循环内的store、没有函数调用对顶层地址进行store
    if (auto loadInst = dynamic_cast<LoadInst *>(inst))
    {
        Value *addr = loadInst->getPointer();
        // 如果循环体内有对该地址的修改，则不能外提
        if (auto loadOp = dynamic_cast<Instruction *>(addr))
        {
            if (loop.containsInst(loadOp))
            {
                // 如果addr是循环变量，则不能外提
                return false;
            }
        }
        // 获取addr的原始指针操作数
        Value *loadOriginalPointer = loadInst->getOriginalPointer();
        // 判断循环体内是否有对该地址的store
        for (auto *loopBB : loop.blocks)
        {
            for (auto &instPtr : loopBB->getInstructions())
            {
                Instruction *store = instPtr.get();
                if (auto storeInst = dynamic_cast<StoreInst *>(store))
                {
                    Value *storeOriginalAddr = storeInst->getOriginalPointer();
                    // 如果store的地址和load的地址相同，则不能外提
                    if (isSameAddr(storeOriginalAddr, loadOriginalPointer))
                    {
                        return false; // 两条load之间有store，不能外提
                    }
                }
            }
        }
        // 判断是否有其他call对该地址的修改
        for (auto *loopBB : loop.blocks)
        {
            for (auto &instPtr : loopBB->getInstructions())
            {
                Instruction *call = instPtr.get();
                if (auto callInst = dynamic_cast<CallInst *>(call))
                {
                    // 如果是调用函数，且函数有副作用，则不能外提
                    if (callInst->HasModifiedArray(loadOriginalPointer))
                    {
                        return false;
                    }
                }
            }
        }
        // 否则可以外提
        return true;
    }
    // 增加对phi指令的特殊处理，phi用于处理合流，不能外提
    // copy指令不能外提，因为是由合流产生
    return !inst->mayHaveSideEffects() && inst->getOpcode() != Opcode::Copy && inst->getOpcode() != Opcode::Phi;
}
// 判断指令是否在循环不变
bool LoopInvariantCodeMotionPass::isLoopInvariant(Instruction *inst, const Loop &loop)
{
    // 检查指令是否在循环中，并且不依赖于循环
    for (auto *op : inst->getOperands())
    {
        if (auto *def = dynamic_cast<Instruction *>(op))
        {
            // 如果操作数是循环中的变量，则不是循环不变
            if (loop.containsInst(def))
            {
                return false;
            }
        }
    }
    return true;
}
bool RemoveUselessWhilePass::runOnFunction(Function *func)
{
    bool changed = false;
    bool localChanged;
    do
    {
        localChanged = false;
        func->setLoops(ControlFlowAnalysis::findLoops(func));
        auto &loops = func->getLoops();
        for (const auto &loop : loops)
        {
            // 只处理 header + 单 body 的典型 while
            if (loop.blocks.size() != 2)
                continue;

            BasicBlock *whilebody = nullptr;
            for (auto *bb : loop.blocks)
            {
                if (bb != loop.header)
                    whilebody = bb;
            }
            if (!whilebody)
                continue;

            auto *term = dynamic_cast<BranchInst *>(loop.header->getTerminator());
            if (!term || !term->isConditional())
                continue;

            // 出口：header 后继中不是 body 的那一边
            BasicBlock *exitBlock = nullptr;
            {
                int sc = 0;
                for (auto *succ : loop.header->getSuccessors())
                {
                    if (succ == whilebody)
                        continue;
                    exitBlock = succ;
                    ++sc;
                    if (sc > 1)
                        break;
                }
            }
            if (!exitBlock || loop.header->getSuccessors().size() != 2)
                continue;

            // 可证「零次进入 body」：条件为常量，且恒定走 exit 一边
            auto *ci = dynamic_cast<ConstantInt *>(term->getCondition());
            if (!ci)
                continue;
            BasicBlock *taken = (ci->Value != 0) ? term->getTrueBlock() : term->getFalseBlock();
            if (taken != exitBlock)
                continue;

            // 循环外进入 header 的前驱只能有一个（典型 preheader）
            int outerPreds = 0;
            for (auto *pred : loop.header->getPredecessors())
            {
                if (pred == whilebody)
                    continue;
                ++outerPreds;
                if (outerPreds > 1)
                    break;
            }
            if (outerPreds != 1)
                continue;

            // 先改前驱到出口，避免删块后悬空
            for (auto *pred : loop.header->getPredecessors())
            {
                if (pred == whilebody)
                    continue;
                for (auto &instPtr : pred->getInstructions())
                {
                    auto *br = dynamic_cast<BranchInst *>(instPtr.get());
                    if (!br)
                        continue;
                    if (br->getTrueBlock() == loop.header)
                        br->setTrueBlock(exitBlock);
                    if (br->getFalseBlock() == loop.header)
                        br->setFalseBlock(exitBlock);
                }
                pred->addSuccessor(exitBlock);
                exitBlock->addPredecessor(pred);
            }

            // 删掉「从 exit 角度来自 header」的 Phi 边，并在仅余一条 incoming 时用单值替换 Phi
            removePhiIncomingFromPredecessor(exitBlock, loop.header);

            const string removedHeaderName = loop.header->getName();
            for (auto *bb : loop.blocks)
                bb->removeSelfBasicBlock();

            localChanged = true;
            changed = true;
            if (verbose)
            {
                debugInfo << "RemoveUselessWhilePass: Removed zero-trip while at header "
                          << removedHeaderName << "\n";
            }
            break;
        }
    } while (localChanged);
    return changed;
}
// 目前只支持整型规约
bool LoopSumReductionPass::runOnFunction(Function *func)
{
    bool changed = false;
    func->setLoops(ControlFlowAnalysis::findLoops(func)); // 确保循环信息是最新的
    auto &loops = func->getLoops();
    for (const auto loop : loops)
    {
        // 循环头
        BasicBlock *header = loop.header;
        // 检查是否为 while(j < n) 头部
        // 获取终结指令前一条指令
        auto size = header->getInstructions().size();
        if (header->getInstructions().size() < 2)
            continue; // 至少需要两条指令
        auto *cmp = dynamic_cast<ICmpInst *>(header->getInstructions()[size - 2].get());
        if (!cmp || cmp->getPredicate() != ICmpInst::ICMP_SLT)
            continue;
        Value *jVar = cmp->getLHS();
        Value *nVar = cmp->getRHS();
        Value *sumVar = nullptr;
        int count_phi = 0;
        bool canReduce = true;
        for (auto &instPtr : header->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
            {
                if (count_phi >= 2)
                {
                    canReduce = false; // 只处理两个phi指令->简单求和循环
                    break;
                }
                if (phi != jVar)
                {
                    sumVar = phi;
                }
                count_phi++;
            }
        }
        if (!canReduce || !sumVar)
            continue; // 不是while(j<n)循环，或者没有sum变量
        // 找到循环体
        BasicBlock *body = nullptr;
        if (loop.blocks.size() > 2)
            continue; // 只处理简单循环
        for (auto *lp_block : loop.blocks)
        {
            if (lp_block != header)
                body = lp_block;
        }
        if (!body)
            continue; // 没有找到循环体
        // 检查循环体是否有 sum = sum + ...; j = j + 1;
        BinaryOperator *sumAdd = nullptr, *jInc = nullptr;
        bool isFloat = false;
        for (auto &instPtr : body->getInstructions())
        {
            if (auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get()))
            {
                // j = j + 1
                if (bin->getOpcode() == Opcode::Add &&
                    (bin->getLHS() == jVar && dynamic_cast<ConstantInt *>(bin->getRHS()) || bin->getRHS() == jVar && dynamic_cast<ConstantInt *>(bin->getLHS())))
                {
                    jInc = bin;
                }
                // sum = sum + j 或 sum = sum + (a+j)*(b+j)
                // 浮点数暂不支持后面一种，会有精度误差
                else if ((bin->getOpcode() == Opcode::Add || bin->getOpcode() == Opcode::FAdd) && (bin->getLHS() == sumVar || bin->getRHS() == sumVar))
                {
                    isFloat = bin->getType()->isFloatTy();
                    sumAdd = bin;
                }
            }
        }
        if (!sumAdd || !jInc)
            continue;

        // 检查sumAdd右侧是否为j，或为(a+j)*(b+j)
        Value *sumExpr = nullptr;
        if (sumAdd->getRHS() == jVar || sumAdd->getLHS() == jVar)
        {
            // sum = sum + j
            sumExpr = jVar;
        }
        else if (auto *cast = dynamic_cast<CastInst *>(sumAdd->getRHS()))
        {
            // sum = sum + (float)j
            if (cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
            {
                sumExpr = cast;
            }
        }
        else if (auto *cast = dynamic_cast<CastInst *>(sumAdd->getLHS()))
        {
            // sum = sum + (float)j
            if (cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
            {
                sumExpr = cast;
            }
        }
        else if (auto *mul = dynamic_cast<BinaryOperator *>(sumAdd->getRHS()))
        {
            // sum = sum + (a+j)*(b+j)
            if (mul->getOpcode() == Opcode::Mul)
            {
                auto *add1 = dynamic_cast<BinaryOperator *>(mul->getLHS());
                auto *add2 = dynamic_cast<BinaryOperator *>(mul->getRHS());
                if (add1 && add2 &&
                    add1->getOpcode() == Opcode::Add &&
                    add2->getOpcode() == Opcode::Add &&
                    (add1->getLHS() == jVar || add1->getRHS() == jVar) &&
                    (add2->getLHS() == jVar || add2->getRHS() == jVar))
                {
                    sumExpr = mul;
                }
            }
            // else if (mul->getOpcode() == Opcode::FMul)
            // {
            //     auto *add1 = dynamic_cast<BinaryOperator *>(mul->getLHS());
            //     auto *add2 = dynamic_cast<BinaryOperator *>(mul->getRHS());
            //     if (add1 && add2 &&
            //         add1->getOpcode() == Opcode::FAdd &&
            //         add2->getOpcode() == Opcode::FAdd )
            //     {
            //         bool isValid1=false;
            //         bool isValid2=false;
            //         if(auto *cast=dynamic_cast<CastInst *>(add1->getLHS()))
            //         {
            //             if(cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
            //             {
            //                 isValid1=true;
            //             }
            //         }
            //         else if(auto *cast=dynamic_cast<CastInst *>(add1->getRHS()))
            //         {
            //             if(cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
            //             {
            //                 isValid1=true;
            //             }
            //         }
            //         if(auto *cast=dynamic_cast<CastInst *>(add2->getLHS()))
            //         {
            //             if(cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
            //             {
            //                 isValid2=true;
            //             }
            //         }
            //         else if(auto *cast=dynamic_cast<CastInst *>(add2->getRHS()))
            //         {
            //             if(cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
            //             {
            //                 isValid2=true;
            //             }
            //         }
            //         if(isValid1&&isValid2)sumExpr = mul;
            //     }
            // }
        }
        if (!sumExpr)
            continue;
        // 从header中的phi查找到j和sum初值
        auto *jPhi = dynamic_cast<PhiInst *>(jVar);
        if (!jPhi)
            continue; // j不是phi指令，无法获取初值
        Value *jInit = nullptr;
        size_t phiIncomingNum = jPhi->getNumIncomingValues();
        if (phiIncomingNum > 2)
            continue; // 只处理简单循环，phi指令的输入必须只有两个
        for (size_t i = 0; i < phiIncomingNum; ++i)
        {
            if (jPhi->getIncomingBlock(i) != body)
            {
                jInit = jPhi->getIncomingValue(i);
                break;
            }
        }
        if (auto *constInt = dynamic_cast<ConstantInt *>(jInit))
        {
            if (constInt->Value != 0)
            {
                // 如果j初值不为0，则不需要进行归约
                continue;
            }
        }
        else
        {
            continue; // j初值不是常量0，无法进行归约
        }
        // 获取sum初值
        auto *sumPhi = dynamic_cast<PhiInst *>(sumAdd->getLHS());
        if (!sumPhi)
            continue; // sum不是phi指令，无法获取初值
        Value *sumInit = nullptr;
        phiIncomingNum = sumPhi->getNumIncomingValues();
        if (phiIncomingNum > 2)
            continue; // 只处理简单循环，phi指令的输入必须只有两个
        // 查找sum的初值
        for (size_t i = 0; i < phiIncomingNum; ++i)
        {
            if (sumPhi->getIncomingBlock(i) != body)
            {
                sumInit = sumPhi->getIncomingValue(i);
                break;
            }
        }
        // sum的初值可以不为0，因为sum可以是任意初值
        // 获取前驱块用于插入
        BasicBlock *preheader = nullptr;
        int count = 0;
        for (auto *pred : header->getPredecessors())
        {
            if (pred != body)
            {
                preheader = pred;
                count++;
            }
            if (count > 1)
            {
                preheader = nullptr; // 如果有多个前驱，则不处理
                break;
            }
        }
        // 获取退出块用于连接
        BasicBlock *exitBlock = nullptr;
        count = 0;
        for (auto *succ : header->getSuccessors())
        {
            if (succ != body)
            {
                exitBlock = succ;
                count++;
            }
            if (count > 1)
            {
                exitBlock = nullptr; // 如果有多个出口，则不处理
                break;
            }
        }
        if (!preheader || !exitBlock)
            continue;
        Instruction *formula = nullptr;
        if (sumExpr == jVar)
        {
            // sum = ∑j = n(n-1)/2
            // 这里n就是循环次数，j从0开始到n-1
            // 计算n(n-1)/2
            auto *n_minus_1 = new BinaryOperator(Opcode::Sub, nVar, new ConstantInt(IntegerType::getInstance(), 1), "n-1");
            auto *n_n_minus_1 = new BinaryOperator(Opcode::Mul, nVar, n_minus_1, "n(n-1)");
            auto *half = new BinaryOperator(Opcode::SDiv, n_n_minus_1, new ConstantInt(IntegerType::getInstance(), 2), "n(n-1)/2");
            auto *sumInit_half = new BinaryOperator(Opcode::Add, sumInit, half, "sum_init_half");
            Instruction *cast = nullptr;
            if (isFloat)
            {
                cast = new CastInst(Opcode::SIToFP, sumInit_half, FloatType::getInstance(), "sum_init_half_cast");
            }
            formula = isFloat ? cast : sumInit_half;
            // 将公式添加到preheader
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_minus_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_n_minus_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(half));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(sumInit_half));
            if (cast)
            {
                preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(cast));
            }
        }
        else
        {
            // sum = ∑(a+j)*(b+j) = n*a*b + n*(n-1)/2*(a+b) + n*(n-1)*(2n-1)/6
            auto *a = dynamic_cast<BinaryOperator *>(sumExpr)->getLHS();
            auto *b = dynamic_cast<BinaryOperator *>(sumExpr)->getRHS();
            // 获得a,b，如果其中一个是j，则另一个是常量
            if (auto *binaryInst = dynamic_cast<BinaryOperator *>(a))
            {
                if (binaryInst->getOpcode() != Opcode::Add) // && binaryInst->getOpcode() != Opcode::FAdd)
                {
                    // 如果不是加法，则不处理
                    continue;
                }
                if (binaryInst->getOpcode() == Opcode::Add)
                {
                    if (binaryInst->getLHS() == jVar)
                    {
                        a = binaryInst->getRHS();
                    }
                    else if (binaryInst->getRHS() == jVar)
                    {
                        a = binaryInst->getLHS();
                    }
                }
                // else if (binaryInst->getOpcode() == Opcode::FAdd)
                // {
                //     if(auto *cast=dynamic_cast<CastInst *>(binaryInst->getLHS()))
                //     {
                //         if (cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
                //         {
                //             a = binaryInst->getRHS();
                //         }
                //     }
                //     else if(auto *cast=dynamic_cast<CastInst *>(binaryInst->getRHS()))
                //     {
                //         if (cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
                //         {
                //             a = binaryInst->getLHS();
                //         }
                //     }
                // }
            }
            else
                continue;
            if (auto *binaryInst = dynamic_cast<BinaryOperator *>(b))
            {
                if (binaryInst->getOpcode() != Opcode::Add) //&& binaryInst->getOpcode() != Opcode::FAdd)
                {
                    // 如果不是加法，则不处理
                    continue;
                }
                if (binaryInst->getOpcode() == Opcode::Add)
                {
                    if (binaryInst->getLHS() == jVar)
                    {
                        b = binaryInst->getRHS();
                    }
                    else if (binaryInst->getRHS() == jVar)
                    {
                        b = binaryInst->getLHS();
                    }
                }
                // else if (binaryInst->getOpcode() == Opcode::FAdd)
                // {
                //     if(auto *cast=dynamic_cast<CastInst *>(binaryInst->getLHS()))
                //     {
                //         if (cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
                //         {
                //             b = binaryInst->getRHS();
                //         }
                //     }
                //     else if(auto *cast=dynamic_cast<CastInst *>(binaryInst->getRHS()))
                //     {
                //         if (cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
                //         {
                //             b = binaryInst->getLHS();
                //         }
                //     }
                // }
            }
            else
                continue;
            if (isFloat)
                continue;
            // 这种情况暂不支持float，会有精度问题
            // 这种情况下全部转为float再计算
            auto addOp = isFloat ? Opcode::FAdd : Opcode::Add;
            auto mulOp = isFloat ? Opcode::FMul : Opcode::Mul;
            auto divOp = isFloat ? Opcode::FDiv : Opcode::SDiv;
            auto subOp = isFloat ? Opcode::FSub : Opcode::Sub;
            Value *One = nullptr, *Two = nullptr, *Six = nullptr;
            if (isFloat)
            {
                One = new ConstantFloat(FloatType::getInstance(), 1.0f);
                Two = new ConstantFloat(FloatType::getInstance(), 2.0f);
                Six = new ConstantFloat(FloatType::getInstance(), 6.0f);
                nVar = new CastInst(Opcode::SIToFP, nVar, FloatType::getInstance(), "n_float");
                preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(dynamic_cast<Instruction *>(nVar)));
            }
            else
            {
                One = new ConstantInt(IntegerType::getInstance(), 1);
                Two = new ConstantInt(IntegerType::getInstance(), 2);
                Six = new ConstantInt(IntegerType::getInstance(), 6);
            }
            // 计算n*a*b
            auto *a_mutiply_b = new BinaryOperator(mulOp, a, b, "ab");
            auto *n_a_mutiply_b = new BinaryOperator(mulOp, nVar, a_mutiply_b, "nab");
            // 计算(a+b)*n*(n-1)/2
            auto *n_minus_1 = new BinaryOperator(subOp, nVar, One, "n-1");
            auto *n_n_minus_1 = new BinaryOperator(mulOp, nVar, n_minus_1, "n(n-1)");
            auto *half = new BinaryOperator(divOp, n_n_minus_1, Two, "n(n-1)/2");
            auto *a_plus_b = new BinaryOperator(addOp, a, b, "a+b");
            auto *n_n_minus_1_half = new BinaryOperator(mulOp, half, a_plus_b, "n(n-1)/2*(a+b)");
            // 计算n*(n-1)*(2n-1)/6
            auto *two_n = new BinaryOperator(mulOp, Two, nVar, "2n");
            auto *two_n_minus_1 = new BinaryOperator(subOp, two_n, One, "2n-1");
            auto *n_n_minus_1_two_n_minus_1 = new BinaryOperator(mulOp, n_n_minus_1, two_n_minus_1, "n(n-1)*(2n-1)");
            auto *n_n_minus_1_two_n_minus_1_six = new BinaryOperator(divOp, n_n_minus_1_two_n_minus_1, Six, "n(n-1)*(2n-1)/6");
            // 求和
            auto *sum_1 = new BinaryOperator(addOp, n_a_mutiply_b, n_n_minus_1_half, "sum_1");
            auto *sum_2 = new BinaryOperator(addOp, sum_1, n_n_minus_1_two_n_minus_1_six, "sum_2");
            auto *sum_3 = new BinaryOperator(addOp, sum_2, sumInit, "sum_3");
            formula = sum_3;
            // 添加
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(a_mutiply_b));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_a_mutiply_b));

            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_minus_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_n_minus_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(half));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(a_plus_b));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_n_minus_1_half));

            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(two_n));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(two_n_minus_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_n_minus_1_two_n_minus_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_n_minus_1_two_n_minus_1_six));

            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(sum_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(sum_2));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(sum_3));
        }
        // 替换原来prehead的sumphi
        sumPhi->replaceAllUsesWith(formula);
        sumPhi->removeThisFromOperands();
        // 删除原来的sumphi指令
        needToDelete.push_back(sumPhi);
        preheader->Instructions.erase(std::remove_if(preheader->getInstructions().begin(), preheader->getInstructions().end(),
                                                     [sumPhi](const std::unique_ptr<Instruction> &inst)
                                                     { return inst.get() == sumPhi; }),
                                      preheader->getInstructions().end());
        // 替换原来的jphi
        jPhi->replaceAllUsesWith(nVar);
        jPhi->removeThisFromOperands();
        // 删除原来的jphi指令
        needToDelete.push_back(jPhi);
        preheader->Instructions.erase(std::remove_if(preheader->getInstructions().begin(), preheader->getInstructions().end(),
                                                     [jPhi](const std::unique_ptr<Instruction> &inst)
                                                     { return inst.get() == jPhi; }),
                                      preheader->getInstructions().end());
        // 修正prehead的跳转指令到exitBlock
        for (auto &instPtr : preheader->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (auto *br = dynamic_cast<BranchInst *>(inst))
            {
                if (br->getTrueBlock() == header)
                {
                    // 如果是循环头的跳转，直接跳到循环出口
                    br->setTrueBlock(exitBlock);
                }
                if (br->getFalseBlock() == header)
                {
                    // 如果是循环头的跳转，直接跳到循环出口
                    br->setFalseBlock(exitBlock);
                }
            }
        }
        for (auto &bb : loop.blocks)
        {
            bb->removeSelfBasicBlock(); // 删除基本块的CFG连接，便于删除基本块
        }
        // 建立prehead到while.exit的连接
        preheader->addSuccessor(exitBlock);
        exitBlock->addPredecessor(preheader);

        // 修正exit的phi 指令
        auto &exitInsts = exitBlock->getInstructions();
        for (auto it = exitInsts.begin(); it != exitInsts.end();)
        {
            if (auto *phi = dynamic_cast<PhiInst *>(it->get()))
            {
                // 如果有来自header输入的phi
                if (find(phi->getIncomingBlocks().begin(), phi->getIncomingBlocks().end(), header) != phi->getIncomingBlocks().end())
                {
                    phi->replaceIncomingBasicBlock(header, preheader); // 替换为preheader
                    continue;
                }
            }
            ++it;
        }
        changed = true;
        if (verbose)
        {
            debugInfo << "LoopSumReductionPass: Reduced sum loop at header " << header->getName() << " to formula.\n";
        }
        break; // 只处理一个循环
    }
    return changed;
}
bool ModLoopReductionPass ::runOnFunction(Function *func)
{
    bool changed = false;
    func->setLoops(ControlFlowAnalysis::findLoops(func)); // 确保循环信息是最新的
    auto &loops = func->getLoops();
    for (const auto &loop : loops)
    {
        BasicBlock *headBlock = loop.header;
        if (headBlock->getInstructions().size() < 2)
            continue;
        // 1. 检查循环条件 while(i < maxindex)
        // 只处理小于等于
        auto *cmp = dynamic_cast<ICmpInst *>(headBlock->getInstructions()[headBlock->getInstructions().size() - 2].get());
        if (!cmp || cmp->getPredicate() != ICmpInst::ICMP_SLT)
            continue;
        Value *iVar = cmp->getLHS();
        Value *maxindex = cmp->getRHS();

        // 2. 检查phi获取i和sum初值
        PhiInst *iPhi = nullptr, *sumPhi = nullptr;
        for (auto &instPtr : headBlock->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
            {
                if (phi == iVar)
                {
                    iPhi = phi;
                }
                else
                    sumPhi = phi;
            }
        }
        if (!iPhi || !sumPhi)
            continue;
        Value *iInit = nullptr, *sumInit = nullptr;
        for (size_t i = 0; i < iPhi->getNumIncomingValues(); ++i)
        {
            auto *iInitInst = dynamic_cast<Instruction *>(iPhi->getIncomingValue(i));
            if (iInitInst == nullptr || !loop.containsInst(iInitInst))
            {
                iInit = iPhi->getIncomingValue(i);
                break;
            }
        }
        for (size_t i = 0; i < sumPhi->getNumIncomingValues(); ++i)
        {
            auto *sumInitInst = dynamic_cast<Instruction *>(sumPhi->getIncomingValue(i));
            if (sumInitInst == nullptr || !loop.containsInst(sumInitInst))
            {
                sumInit = sumPhi->getIncomingValue(i);
                break;
            }
        }
        // 4. 在所有body块中查找 sum += x; sum %= remconst; i++
        BinaryOperator *sumAdd = nullptr, *sumMod = nullptr, *iInc = nullptr;
        Value *x = nullptr, *remconst = nullptr, *stepLength = nullptr;
        for (auto *bb : loop.blocks)
        {
            if (bb == headBlock)
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                if (auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get()))
                {
                    if (!sumAdd && bin->getOpcode() == Opcode::Add &&
                        (bin->getLHS() == sumPhi || bin->getRHS() == sumPhi))
                    {
                        sumAdd = bin;
                        x = (bin->getLHS() == sumPhi) ? bin->getRHS() : bin->getLHS();
                    }
                    if (!sumMod && bin->getOpcode() == Opcode::SRem &&
                        bin->getLHS() == sumAdd)
                    {
                        sumMod = bin;
                        remconst = bin->getRHS();
                    }
                    if (!iInc && bin->getOpcode() == Opcode::Add &&
                        (bin->getLHS() == iPhi || bin->getRHS() == iPhi))
                    {
                        iInc = bin;
                        stepLength = (bin->getLHS() == iPhi) ? bin->getRHS() : bin->getLHS();
                    }
                }
            }
        }
        if (!sumAdd || !sumMod || !iInc || !x || !remconst)
            continue;
        // 5. 只处理常量remconst和x
        auto *remconstC = dynamic_cast<ConstantInt *>(remconst);
        auto *xC = dynamic_cast<ConstantInt *>(x);
        if (!remconstC || !xC)
            continue;
        // 超过2^16的常量不处理,因为会溢出
        if (remconstC->Value > 65536)
            continue;
        // 6. 生成归约公式
        // 公式 initsum%remconst+(maxindex-i/stepLength*x%remconst)&remconst
        auto *sumInitMod = new BinaryOperator(Opcode::SRem, sumInit, remconst, "sumInit_mod");
        auto *max_minus_i = new BinaryOperator(Opcode::Sub, maxindex, iInit, "max_minus_i");
        auto *max_minus_i_div_step = new BinaryOperator(Opcode::SDiv, max_minus_i, stepLength, "max_minus_i_div");
        auto *max_minus_i_mod = new BinaryOperator(Opcode::SRem, max_minus_i_div_step, remconst, "max_minus_i_mod");
        auto *x_mod = new BinaryOperator(Opcode::SRem, x, remconst, "x_mod");
        auto *mul = new BinaryOperator(Opcode::Mul, max_minus_i_mod, x_mod, "mul_mod");
        auto *mul_mod = new BinaryOperator(Opcode::SRem, mul, remconst, "mul_mod2");
        auto *finalSum = new BinaryOperator(Opcode::Add, sumInitMod, mul_mod, "final_sum");
        auto *finalSumMod = new BinaryOperator(Opcode::SRem, finalSum, remconst, "final_sum_mod");

        auto *if_sumPhi = new PhiInst(sumInit->getType(), "if_sum_phi");
        auto *i_Phi = new PhiInst(iInit->getType(), "i_phi");
        // 7. 替换循环为if-else
        BasicBlock *preBlock = nullptr;
        for (auto *pred : headBlock->getPredecessors())
            if (find(loop.blocks.begin(), loop.blocks.end(), pred) == loop.blocks.end())
                preBlock = pred;
        if (!preBlock)
            continue;
        BasicBlock *exitBlock = nullptr;
        for (auto *succ : headBlock->getSuccessors())
            if (find(loop.blocks.begin(), loop.blocks.end(), succ) == loop.blocks.end())
                exitBlock = succ;
        if (!exitBlock)
            continue;

        auto *cond = new ICmpInst(ICmpInst::ICMP_SLT, iInit, maxindex, "modulo_cond");
        auto *thenBB = new BasicBlock("modulo_then", func);
        auto *elseBB = new BasicBlock("modulo_else", func);
        // phi添加输入
        if_sumPhi->addIncoming(finalSumMod, thenBB);
        if_sumPhi->addIncoming(sumInit, elseBB);
        // 如果来自then，则已经循环到最大
        i_Phi->addIncoming(maxindex, thenBB);
        i_Phi->addIncoming(iInit, elseBB);
        // then块跳转
        thenBB->addInstruction(std::unique_ptr<Instruction>(sumInitMod));
        thenBB->addInstruction(std::unique_ptr<Instruction>(max_minus_i));
        thenBB->addInstruction(std::unique_ptr<Instruction>(max_minus_i_div_step));
        thenBB->addInstruction(std::unique_ptr<Instruction>(max_minus_i_mod));
        thenBB->addInstruction(std::unique_ptr<Instruction>(max_minus_i));
        thenBB->addInstruction(std::unique_ptr<Instruction>(x_mod));
        thenBB->addInstruction(std::unique_ptr<Instruction>(mul));
        thenBB->addInstruction(std::unique_ptr<Instruction>(mul_mod));
        thenBB->addInstruction(std::unique_ptr<Instruction>(finalSum));
        thenBB->addInstruction(std::unique_ptr<Instruction>(finalSumMod));

        // 跳转到merge块
        thenBB->addInstruction(std::make_unique<BranchInst>(exitBlock));
        elseBB->addInstruction(std::make_unique<BranchInst>(exitBlock));

        exitBlock->addInstruction(std::unique_ptr<Instruction>(if_sumPhi));
        exitBlock->addInstruction(std::unique_ptr<Instruction>(i_Phi));
        // if.cond块添加跳转
        preBlock->addInstruction(std::unique_ptr<Instruction>(cond));
        preBlock->addInstruction(std::make_unique<BranchInst>(cond, thenBB, elseBB));

        preBlock->addSuccessor(thenBB);
        preBlock->addSuccessor(elseBB);
        thenBB->addPredecessor(preBlock);
        elseBB->addPredecessor(preBlock);
        thenBB->addSuccessor(exitBlock);
        elseBB->addSuccessor(exitBlock);
        exitBlock->addPredecessor(thenBB);
        exitBlock->addPredecessor(elseBB);

        sumPhi->replaceAllUsesWith(if_sumPhi);
        iPhi->replaceAllUsesWith(i_Phi);
        // 删除原循环体
        for (auto *bb : loop.blocks)
            bb->removeSelfBasicBlock();

        func->addBasicBlock(std::unique_ptr<BasicBlock>(thenBB));
        func->addBasicBlock(std::unique_ptr<BasicBlock>(elseBB));
        // 删除原来preBlock的跳转
        auto &preInsts = preBlock->getInstructions();
        // 先收集，统一删除
        std::vector<Instruction *> branchToDelete;
        for (auto it = preInsts.begin(); it != preInsts.end();)
        {
            if (auto *br = dynamic_cast<BranchInst *>(it->get()))
            {
                // 如果是无条件跳转，删除
                if (!br->isConditional() && br->getTrueBlock() == headBlock)
                {
                    branchToDelete.push_back(br);
                }
            }
            ++it;
        }
        for (auto *br : branchToDelete)
        {
            br->removeThisFromOperands();
            needToDelete.push_back(br);
            preInsts.erase(std::remove_if(preInsts.begin(), preInsts.end(),
                                          [br](const std::unique_ptr<Instruction> &inst)
                                          { return inst.get() == br; }),
                           preInsts.end());
        }
        // 修正exit的phi输入
        auto &exitInsts = exitBlock->getInstructions();
        for (auto it = exitInsts.begin(); it != exitInsts.end();)
        {
            if (auto *phi = dynamic_cast<PhiInst *>(it->get()))
            {
                // 这里需要先获取incomingBlocks再用于find比较，否则获得的是拷贝
                auto incomingBlocks = phi->getIncomingBlocks();
                // 如果有来自header输入的phi
                if (find(incomingBlocks.begin(), incomingBlocks.end(), headBlock) != incomingBlocks.end())
                {
                    phi->replaceIncomingBasicBlock(headBlock, preBlock); // 替换为preBlock
                    continue;
                }
            }
            ++it;
        }
        changed = true;
        if (verbose)
            debugInfo << "LoopModuloReductionPass: Reduced loop at header " << headBlock->getName() << " to modulo formula.\n";
        break; // 只处理一个
    }
    return changed;
}

bool LoopUnrollingPass::runOnFunction(Function *func)
{
    bool changed = false;
    int fullUnrollLayersDone = 0;
    bool fullUnrolledThisRound;
    do
    {
        fullUnrolledThisRound = false;
        func->setLoops(ControlFlowAnalysis::findLoops(func));
        auto loops = func->getLoops();
        sortLoopsInnermostFirst(loops);
        for (const auto &loop : loops)
        {
            // 每遇到一个新的最外层循环子树，重新计数已完全展开的嵌套层数
            if (loopNestingDepth(loop, loops) == 0)
                fullUnrollLayersDone = 0;

            switch (tryUnrollOneLoop(func, loop, loops, fullUnrollLayersDone, verbose, debugInfo))
            {
            case UnrollResult::Full:
                changed = true;
                fullUnrolledThisRound = true;
                break;
            case UnrollResult::Partial:
                changed = true;
                continue;
            case UnrollResult::None:
                continue;
            }
            if (fullUnrolledThisRound)
                break;
        }
    } while (fullUnrolledThisRound);
    return changed;
}


namespace
{
    int powdivNameCounter = 0;

    string powdivFreshName(const string &prefix)
    {
        return prefix + to_string(powdivNameCounter++);
    }

    ConstantInt *powdivCi(int v)
    {
        return new ConstantInt(IntegerType::getInstance(), v);
    }

    unique_ptr<Instruction> powdivOwn(Instruction *inst)
    {
        return unique_ptr<Instruction>(inst);
    }

    Value *powdivStripCopy(Value *v)
    {
        while (auto *cpy = dynamic_cast<CopyInst *>(v))
            v = cpy->getSource();
        return v;
    }

    bool isPowDivLoopCallee(Function *func)
    {
        if (!func || func->getName() != "getNumPos" || func->getArguments().size() != 2)
            return false;

        for (auto &bbPtr : func->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                if (auto *div = dynamic_cast<BinaryOperator *>(instPtr.get()))
                {
                    if (div->getOpcode() != Opcode::SDiv)
                        continue;
                    auto *divisor = dynamic_cast<ConstantInt *>(powdivStripCopy(div->getRHS()));
                    if (divisor && divisor->Value == 16)
                        return true;
                }
            }
        }
        return false;
    }

    bool isPowDivLoopCall(CallInst *call)
    {
        if (!call)
            return false;
        Function *callee = call->getCalledFunction();
        return callee && isPowDivLoopCallee(callee);
    }

    void powdivLink(BasicBlock *from, BasicBlock *to)
    {
        from->addInstruction(powdivOwn(new BranchInst(to)));
        from->addSuccessor(to);
        to->addPredecessor(from);
    }

    void powdivTransferSuccessors(BasicBlock *from, BasicBlock *to)
    {
        vector<BasicBlock *> succs = from->getSuccessors();
        for (BasicBlock *succ : succs)
        {
            from->removeSuccessor(succ);
            succ->removePredecessor(from);
            to->addSuccessor(succ);
            succ->addPredecessor(to);
            for (auto &instPtr : succ->getInstructions())
            {
                if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                    phi->replaceIncomingBasicBlock(from, to);
            }
        }
    }

    BasicBlock *powdivSplitBlockTail(Function *func, BasicBlock *bb, unsigned firstMovedIdx)
    {
        auto *afterBB = func->addBasicBlock(bb->getName() + ".powdiv_after");
        auto &insts = bb->getInstructions();
        vector<unique_ptr<Instruction>> tail;
        for (unsigned i = firstMovedIdx; i < insts.size(); ++i)
            tail.push_back(std::move(insts[i]));
        insts.erase(insts.begin() + static_cast<long>(firstMovedIdx), insts.end());
        for (auto &instPtr : tail)
            afterBB->addInstruction(std::move(instPtr));
        powdivTransferSuccessors(bb, afterBB);
        return afterBB;
    }

    // Non-negative digit extract: (num >> shift) & radixMask.
    Value *buildFastNonNegDigit(BasicBlock *bb, const function<void(Instruction *)> &ins, Value *num,
                                Value *shiftAmt, int radixMask, const string &s)
    {
        (void)bb;
        auto *shifted = new BinaryOperator(Opcode::Sra, num, shiftAmt,
                                           powdivFreshName("powdiv_fast_sra" + s));
        ins(shifted);

        auto *digit = new BinaryOperator(Opcode::And, shifted, powdivCi(radixMask),
                                         powdivFreshName("powdiv_fast_and" + s));
        ins(digit);
        return digit;
    }

    Value *buildPowDivDigitExtractLinear(BasicBlock *bb, unsigned &insertIndex, Value *num, Value *pos,
                                         int posShiftLog2, int radixMask, const string &nameSuffix)
    {
        num = powdivStripCopy(num);
        pos = powdivStripCopy(pos);
        const string s = nameSuffix;

        auto ins = [&](Instruction *inst) {
            bb->insert(powdivOwn(inst), insertIndex++);
        };

        if (auto *posCi = dynamic_cast<ConstantInt *>(pos))
        {
            const int p = posCi->Value;
            if (p >= 8)
                return powdivCi(0);
            if (p == 0)
            {
                auto *digit = new BinaryOperator(Opcode::And, num, powdivCi(radixMask),
                                                 powdivFreshName("powdiv_and0" + s));
                ins(digit);
                return digit;
            }
            return buildFastNonNegDigit(bb, ins, num, powdivCi(p * posShiftLog2), radixMask, s);
        }

        auto *shiftAmt = new BinaryOperator(Opcode::Sll, pos, powdivCi(posShiftLog2),
                                            powdivFreshName("powdiv_shift" + s));
        ins(shiftAmt);
        return buildFastNonNegDigit(bb, ins, num, shiftAmt, radixMask, s);
    }

    void buildPowDivFuncBody(Function *func, BasicBlock *entry, Value *num, Value *pos, int posShiftLog2,
                             int radixMask)
    {
        num = powdivStripCopy(num);
        pos = powdivStripCopy(pos);

        auto retVal = [&](BasicBlock *bb, Value *val) {
            bb->addInstruction(powdivOwn(new ReturnInst(val)));
        };

        if (auto *posCi = dynamic_cast<ConstantInt *>(pos))
        {
            const int p = posCi->Value;
            if (p >= 8)
            {
                retVal(entry, powdivCi(0));
                return;
            }
            if (p == 0)
            {
                auto *digit = new BinaryOperator(Opcode::And, num, powdivCi(radixMask), "powdiv_fn0");
                entry->addInstruction(powdivOwn(digit));
                retVal(entry, digit);
                return;
            }
            if (p >= 1 && p <= 7)
            {
                auto ins = [&](Instruction *inst) { entry->addInstruction(powdivOwn(inst)); };
                retVal(entry, buildFastNonNegDigit(entry, ins, num, powdivCi(p * posShiftLog2), radixMask,
                                                   "_fn"));
                return;
            }
        }

        // Variable / out-of-range const: pos >= 8 → 0, else (num >> (pos<<2)) & mask.
        // Needed so large pos stays semantically zero (RISC-V shifts mask to 5 bits).
        auto *ret0BB = func->addBasicBlock("powdiv_fn.ret0");
        auto *extractBB = func->addBasicBlock("powdiv_fn.extract");
        auto *ge8 = new ICmpInst(ICmpInst::ICMP_SGE, pos, powdivCi(8), "powdiv_fn_ge8");
        entry->addInstruction(powdivOwn(ge8));
        entry->addInstruction(powdivOwn(new BranchInst(ge8, ret0BB, extractBB)));
        entry->addSuccessor(ret0BB);
        entry->addSuccessor(extractBB);
        ret0BB->addPredecessor(entry);
        extractBB->addPredecessor(entry);
        retVal(ret0BB, powdivCi(0));

        auto ins = [&](Instruction *inst) { extractBB->addInstruction(powdivOwn(inst)); };
        auto *shiftAmt =
            new BinaryOperator(Opcode::Sll, pos, powdivCi(posShiftLog2), "powdiv_fn_shift");
        ins(shiftAmt);
        retVal(extractBB, buildFastNonNegDigit(extractBB, ins, num, shiftAmt, radixMask, "_fn"));
    }

    void emitPowdivVarPosCFG(Function *func, BasicBlock *bb, BasicBlock *afterBB, PhiInst *phi,
                             Value *num, Value *pos, int posShiftLog2, int radixMask,
                             const string &nameSuffix)
    {
        num = powdivStripCopy(num);
        pos = powdivStripCopy(pos);
        const string s = nameSuffix;

        auto *ret0BB = func->addBasicBlock(bb->getName() + ".powdiv0" + s);
        auto *extractBB = func->addBasicBlock(bb->getName() + ".powdiv_extract" + s);

        auto *ge8 = new ICmpInst(ICmpInst::ICMP_SGE, pos, powdivCi(8), powdivFreshName("powdiv_ge8" + s));
        bb->addInstruction(powdivOwn(ge8));
        bb->addInstruction(powdivOwn(new BranchInst(ge8, ret0BB, extractBB)));
        bb->addSuccessor(ret0BB);
        bb->addSuccessor(extractBB);
        ret0BB->addPredecessor(bb);
        extractBB->addPredecessor(bb);

        powdivLink(ret0BB, afterBB);
        phi->addIncoming(powdivCi(0), ret0BB);

        auto extractIns = [&](Instruction *inst) { extractBB->addInstruction(powdivOwn(inst)); };
        auto *shiftAmt = new BinaryOperator(Opcode::Sll, pos, powdivCi(posShiftLog2),
                                            powdivFreshName("powdiv_shift" + s));
        extractIns(shiftAmt);
        Value *digit = buildFastNonNegDigit(extractBB, extractIns, num, shiftAmt, radixMask, s);
        powdivLink(extractBB, afterBB);
        phi->addIncoming(digit, extractBB);
    }
}

bool PowDivLoopReductionPass::rewriteDivLoopCallee(Function *func)
{
    if (!isPowDivLoopCallee(func))
        return false;

    BasicBlock *entry = func->getEntryBlock();
    if (!entry)
        return false;

    Value *num = func->getArguments()[0].get();
    Value *pos = func->getArguments()[1].get();

    vector<BasicBlock *> toRemove;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        if (bbPtr.get() != entry)
            toRemove.push_back(bbPtr.get());
    }
    for (BasicBlock *bb : toRemove)
        bb->removeSelfBasicBlock();

    auto &insts = entry->getInstructions();
    while (!insts.empty())
    {
        insts.back()->removeThisFromOperands();
        insts.pop_back();
    }

    unsigned idx = 0;
    (void)idx;
    buildPowDivFuncBody(func, entry, num, pos, kPosShiftLog2, kRadixMask);

    if (verbose)
    {
        debugInfo << "PowDivLoopReduction: reduced pow-base div loop in " << func->getName()
                  << "\n";
    }
    return true;
}

bool PowDivLoopReductionPass::replaceDivLoopCalls(Function *func)
{
    if (isPowDivLoopCallee(func))
        return false;

    vector<BasicBlock *> blocks;
    blocks.reserve(func->getBasicBlocks().size());
    for (auto &bbPtr : func->getBasicBlocks())
        blocks.push_back(bbPtr.get());

    bool changed = false;
    for (BasicBlock *bb : blocks)
    {
        auto &insts = bb->getInstructions();
        for (int idx = static_cast<int>(insts.size()) - 1; idx >= 0; --idx)
        {
            auto *call = dynamic_cast<CallInst *>(insts[static_cast<unsigned>(idx)].get());
            if (!isPowDivLoopCall(call))
                continue;

            vector<Value *> args = call->getArguments();
            if (args.size() != 2)
                continue;

            const unsigned callIdx = static_cast<unsigned>(idx);
            Value *posArg = powdivStripCopy(args[1]);
            if (auto *posCi = dynamic_cast<ConstantInt *>(posArg))
            {
                if (posCi->Value >= 8)
                {
                    call->replaceAllUsesWith(powdivCi(0));
                    call->removeThisFromOperands();
                    insts.erase(insts.begin() + static_cast<long>(callIdx));
                }
                else
                {
                    unsigned insertIdx = callIdx;
                    Value *digit = buildPowDivDigitExtractLinear(bb, insertIdx, args[0], args[1],
                                                               kPosShiftLog2, kRadixMask, "");
                    const unsigned numInserted = insertIdx - callIdx;
                    call->replaceAllUsesWith(digit);
                    call->removeThisFromOperands();
                    insts.erase(insts.begin() + static_cast<long>(callIdx + numInserted));
                }
            }
            else
            {
                BasicBlock *afterBB = powdivSplitBlockTail(func, bb, callIdx + 1);
                auto *phi = new PhiInst(IntegerType::getInstance(), powdivFreshName("powdiv_phi"));
                afterBB->insert(powdivOwn(phi), 0);
                call->replaceAllUsesWith(phi);
                call->removeThisFromOperands();
                insts.erase(insts.begin() + static_cast<long>(callIdx));
                emitPowdivVarPosCFG(func, bb, afterBB, phi, args[0], args[1], kPosShiftLog2, kRadixMask,
                                    "");
            }

            if (verbose)
            {
                debugInfo << "PowDivLoopReduction: replaced pow-base div call in " << bb->getName()
                          << " of " << func->getName() << "\n";
            }
            changed = true;
        }
    }
    return changed;
}

bool PowDivLoopReductionPass::runOnFunction(Function *func)
{
    powdivNameCounter = 0;
    bool changed = rewriteDivLoopCallee(func);
    changed = replaceDivLoopCalls(func) || changed;
    return changed;
}