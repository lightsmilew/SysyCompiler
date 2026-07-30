#include "FunctionPass.h"
using namespace std;
using namespace optimization;

namespace
{
// TRE 后会标记 tailRecursionEliminated
static bool isTailRecursionEliminatedFunction(Function *func)
{
    return func && func->isTailRecursionEliminated();
}
} // namespace

// 函数内联
bool FunctionInliningPass::runOnFunction(Function *caller)
{
    bool changed = false;
    bool localChanged;
    do
    {
        localChanged = false;
        // 这里是因为每次run之后可能会修改basicBlock，所以要重新获取
        auto &bbs = caller->getBasicBlocks();
        for (auto &bbPtr : bbs)
        {
            BasicBlock *bb = bbPtr.get();
            if (!bb)
                continue;
            auto &insts = bb->getInstructions();
            for (auto it = insts.begin(); it != insts.end();)
            {
                if (auto *call = dynamic_cast<CallInst *>(it->get()))
                {
                    Function *callee = call->getCalledFunction();
                    if (shouldInline(callee, caller))
                    {
                        auto insertPos = it - insts.begin();
                        inlineAt(call, caller, bb, insertPos);
                        it = insts.begin() + insertPos;
                        call->removeThisFromOperands();
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                        callee->setDeleted(true);
                        changed = true;
                        localChanged = true;
                        // debug
                        if (verbose)
                            verifyCFG(caller);
                        break; // 退出当前基本块循环，重新获取bbs
                    }
                    else
                    {
                        ++it; // 如果不内联，继续下一个指令
                    }
                }
                else
                {
                    ++it;
                }
            }
            if (localChanged)
                break; // 只要有内联，重新获取bbs
        }
    } while (localChanged);
    // 更新函数的循环信息，用于是否适合内联判断
    // caller->setLoops(ControlFlowAnalysis::findLoops(caller));
    return changed;
}
// 判断是否适合内联
bool FunctionInliningPass::shouldInline(Function *callee, Function *caller)
{
    if (!callee || callee->isLibraryFunction())
        return false;
    // AP mod-sum closed-form helpers: keep outlined to avoid CFG explosion after inlining.
    if (callee->getName().rfind("__apms_", 0) == 0)
        return false;
    if (inlineMainCalleeLayer)
    {
        if (caller->getName() != "main")
            return false;
        if (!isTailRecursionEliminatedFunction(callee))
            return false;
    }
    if (callee->isRecursive())
        return false;
    // 新增：如果只有一个基本块，且所有指令都是算术运算（不含控制流/调用/副作用），也允许内联,此时不考虑指令大小
    if (callee->getBasicBlocks().size() == 1)
    {
        auto &insts = callee->getBasicBlocks()[0]->getInstructions();
        bool onlyArithmetic = true;
        for (auto &instPtr : insts)
        {
            Instruction *inst = instPtr.get();
            // 只允许二元算术运算和return
            if (!(inst->isBinaryOp() || inst->getOpcode() == Opcode::Ret))
            {
                onlyArithmetic = false;
                break;
            }
        }
        if (onlyArithmetic)
            return true;
    }
    // // 原来的复杂判断
    // auto loops = callee->getLoops();
    // auto size_loops = loops.size();
    // if (size_loops == 1 && loops[0].blocks.size() <= 2)
    //     return true;                               // 如果只有一个循环且循环体只有一个基本块，允许内联
    // size_loops = size_loops == 0 ? 1 : size_loops; // 避免除0
    // auto basicBlockCount = callee->getBasicBlocks().size();
    // // 不内联递归/库函数/过大函数/控制流复杂
    // if (callee->getInstructionCount() > 64 || basicBlockCount / size_loops > 5)
    //     return false;
    // return true;
    return callee->getInstructionCount() <= 200; // 指令数不超过200条允许内联
}
// 内联实现（支持多基本块，正确处理多分支return）
int FunctionInliningPass::inlineAt(CallInst *call, Function *caller, BasicBlock *bb, size_t insertPos)
{
    Function *callee = call->getCalledFunction();
    if (!callee)
        return 0;

    // 参数映射
    std::unordered_map<Value *, Value *> valueMap;
    auto &params = callee->getArguments();
    const auto &args = call->getArguments();
    string suffix = getsuffix(callee->getName());
    std::vector<std::unique_ptr<Instruction>> tailRecParamInits;
    for (size_t i = 0; i < params.size(); ++i)
    {
        if (callee->isTailRecursionEliminated() && !params[i]->getType()->isPointerTy())
        {
            auto initCopy = std::make_unique<CopyInst>(args[i], params[i]->getName() + suffix);
            valueMap[params[i].get()] = initCopy.get();
            tailRecParamInits.push_back(std::move(initCopy));
        }
        else
        {
            valueMap[params[i].get()] = args[i];
        }
    }
    int num = 0;

    // 复制所有基本块，建立映射
    std::unordered_map<BasicBlock *, BasicBlock *> bbMap;
    std::vector<BasicBlock *> calleeBBs;
    for (auto &bbCallee : callee->getBasicBlocks())
    {
        auto *newBB = new BasicBlock(bbCallee->getName() + "_" + suffix, caller);
        bbMap[bbCallee.get()] = newBB;
        calleeBBs.push_back(bbCallee.get());
    }
    // === 新增：只处理单基本块的情况 ===
    if (callee->getBasicBlocks().size() == 1)
    {
        BasicBlock *calleeBB = callee->getBasicBlocks()[0].get();
        std::vector<std::unique_ptr<Instruction>> newInsts;
        Value *retVal = nullptr;
        for (auto &instCallee : calleeBB->getInstructions())
        {
            if (instCallee.get()->getOpcode() == Opcode::Ret)
            {
                auto *retInst = dynamic_cast<ReturnInst *>(instCallee.get());
                if (retInst && retInst->getReturnValue())
                {
                    // 记录返回值
                    retVal = retInst->getReturnValue();
                    if (valueMap.count(retVal))
                        retVal = valueMap[retVal];
                }
                continue; // 不插入return指令
            }
            Instruction *newInst = instCallee->cloneWithRename(valueMap, suffix);
            // 替换操作数为映射后的
            for (size_t i = 0; i < newInst->getOperands().size(); ++i)
            {
                Value *op = newInst->getOperands()[i];
                if (valueMap.count(op))
                    newInst->setOperandByIndex(i, valueMap[op]);
            }
            valueMap[instCallee.get()] = newInst;
            newInsts.push_back(std::unique_ptr<Instruction>(newInst));
            num++;
        }
        // 插入到调用点后
        auto &insts = bb->getInstructions();
        insts.insert(insts.begin() + insertPos + 1,
                     std::make_move_iterator(newInsts.begin()),
                     std::make_move_iterator(newInsts.end()));
        // 替换call的所有use
        if (call->hasReturnValue() && retVal)
            call->replaceAllUsesWith(retVal);
        return num;
    }
    // 复制指令，建立value映射
    for (auto *bbCallee : calleeBBs)
    {
        BasicBlock *newBB = bbMap[bbCallee];
        for (auto &instCallee : bbCallee->getInstructions())
        {
            Instruction *newInst = instCallee->cloneWithRename(valueMap, suffix);
            // 替换操作数为映射后的
            for (size_t i = 0; i < newInst->getOperands().size(); ++i)
            {
                Value *op = newInst->getOperands()[i];
                if (valueMap.count(op))
                    newInst->setOperandByIndex(i, valueMap[op]);
            }
            valueMap[instCallee.get()] = newInst;
            newBB->addInstruction(std::unique_ptr<Instruction>(newInst));
            num++;
        }
    }
    // phi输入有可能在后面才定义，操作完后重新遍历一遍替换phi输入
    for (auto *bbCallee : calleeBBs)
    {
        BasicBlock *newBB = bbMap[bbCallee];
        for (auto &instPtr : newBB->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
            {
                for (size_t i = 0; i < phi->getNumOperands(); ++i)
                {
                    Value *operand = phi->getOperandByIndex(i);
                    if (valueMap.count(operand))
                    {
                        phi->setOperandByIndex(i, valueMap[operand]);
                    }
                }
            }
        }
    }

    // 修正控制流（Br、Phi等指向新BB）
    for (auto *bbCallee : calleeBBs)
    {
        BasicBlock *newBB = bbMap[bbCallee];
        auto &insts = newBB->getInstructions();
        for (auto &instPtr : insts)
        {
            Instruction *inst = instPtr.get();
            // 修正Br指令
            if (auto *br = dynamic_cast<BranchInst *>(inst))
            {
                if (br->TrueBlock && bbMap.count(br->TrueBlock))
                    br->TrueBlock = bbMap[br->TrueBlock];
                if (br->FalseBlock && bbMap.count(br->FalseBlock))
                    br->FalseBlock = bbMap[br->FalseBlock];
            }
            // 修正Phi指令
            if (auto *phi = dynamic_cast<PhiInst *>(inst))
            {
                for (size_t i = 0; i < phi->IncomingValues.size(); ++i)
                {
                    if (bbMap.count(phi->IncomingValues[i]))
                    {
                        // 更新基本块的user
                        phi->IncomingValues[i]->removeUser(phi);
                        phi->IncomingValues[i] = bbMap[phi->IncomingValues[i]];
                        phi->IncomingValues[i]->addUser(phi);
                    }
                }
            }
        }
    }

    // 复制前驱后继关系
    for (auto *bbCallee : calleeBBs)
    {
        BasicBlock *oldBB = bbCallee;
        BasicBlock *newBB = bbMap[oldBB];
        // 复制前驱
        for (auto *pred : oldBB->getPredecessors())
        {
            if (bbMap.count(pred))
                newBB->addPredecessor(bbMap[pred]);
        }
        // 复制后继
        for (auto *succ : oldBB->getSuccessors())
        {
            if (bbMap.count(succ))
                newBB->addSuccessor(bbMap[succ]);
        }
    }

    // 拆分调用点所在基本块
    auto &insts = bb->getInstructions();
    std::vector<std::unique_ptr<Instruction>> afterCallInsts;
    for (size_t i = insertPos + 1; i < insts.size(); ++i)
    {
        afterCallInsts.push_back(std::move(insts[i]));
    }
    // 记录需要删除的指令
    for (size_t i = insertPos + 1; i < insts.size(); ++i)
    {
        if (i < insts.size())
        {
            needToDelete.push_back(insts[i].release());
        }
    }
    insts.erase(insts.begin() + insertPos + 1, insts.end());

    // 新建call后基本块
    auto *afterBB = new BasicBlock(bb->getName() + "_" + "_after" + suffix, caller);
    for (auto &instPtr : afterCallInsts)
    {
        afterBB->addInstruction(std::move(instPtr));
    }
    // 把原来的后继关系转移到after块，本身只保留到entry_inli的前驱后继关系
    for (auto *succ : bb->getSuccessors())
    {
        bb->removeSuccessor(succ);
        succ->removePredecessor(bb);
        succ->addPredecessor(afterBB);
        afterBB->addSuccessor(succ);
    }
    // 原来phi指令的输入也要替换成after块
    for (auto inst : bb->getUsers())
    {
        if (auto *phi = dynamic_cast<PhiInst *>(inst))
        {
            for (size_t i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                if (phi->getIncomingBlock(i) == bb)
                {
                    phi->setIncomingBlock(i, afterBB);
                }
            }
        }
    }

    // 在调用点插入跳转到内联入口块
    auto *entryBB = bbMap[callee->getEntryBlock()];
    for (auto &initCopy : tailRecParamInits)
        bb->addInstruction(std::move(initCopy));
    bb->addInstruction(std::make_unique<BranchInst>(entryBB));
    entryBB->addPredecessor(bb);
    bb->addSuccessor(entryBB);

    // 所有内联体内Return替换为跳转到afterBB，并处理返回值
    bool hasReturnValue = call->hasReturnValue();
    std::vector<std::pair<BasicBlock *, Value *>> retPairs;
    for (auto *bbCallee : calleeBBs)
    {
        BasicBlock *newBB = bbMap[bbCallee];
        auto &insts = newBB->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            if (auto *ret = dynamic_cast<ReturnInst *>(it->get()))
            {
                if (hasReturnValue && ret->getReturnValue())
                {
                    retPairs.push_back({newBB, ret->getReturnValue()});
                }
                // 从操作数中移除自己
                ret->removeThisFromOperands();
                needToDelete.push_back(it->release()); // 记录需要删除的指令
                it = insts.erase(it);
                newBB->addInstruction(std::make_unique<BranchInst>(afterBB));
                newBB->addSuccessor(afterBB);
                afterBB->addPredecessor(newBB);
            }
            else
            {
                ++it;
            }
        }
    }

    // 多分支return用phi合并
    if (hasReturnValue && !retPairs.empty())
    {
        Value *phiVal = nullptr;
        if (retPairs.size() == 1)
        {
            phiVal = retPairs[0].second;
        }
        else
        {
            auto *phi = new PhiInst(call->getType(), call->getName() + suffix);
            for (auto &[fromBB, val] : retPairs)
            {
                phi->addIncoming(val, fromBB);
            }
            afterBB->insert(std::unique_ptr<Instruction>(phi), 0);
            phiVal = phi;
            num++;
        }
        call->replaceAllUsesWith(phiVal);
    }

    // 插入新基本块到caller
    auto &bbs = caller->getBasicBlocks();
    auto bbIt = std::find_if(bbs.begin(), bbs.end(),
                             [bb](const std::unique_ptr<BasicBlock> &ptr)
                             { return ptr.get() == bb; });
    ++bbIt;
    // 先收集所有新块
    std::vector<std::unique_ptr<BasicBlock>> newBBPtrs;
    for (auto *bbCallee : calleeBBs)
    {
        newBBPtrs.push_back(std::unique_ptr<BasicBlock>(bbMap[bbCallee]));
    }
    newBBPtrs.push_back(std::unique_ptr<BasicBlock>(afterBB));
    bbs.insert(bbIt, std::make_move_iterator(newBBPtrs.begin()), std::make_move_iterator(newBBPtrs.end()));

    return num;
}
void FunctionInliningPass::verifyCFG(Function *func)
{
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        // 检查后继
        for (auto *succ : bb->getSuccessors())
        {
            bool found = false;
            for (auto *pred : succ->getPredecessors())
            {
                if (pred == bb)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                debugInfo << "CFG Error: " << bb->getName()
                          << " is successor of " << succ->getName()
                          << " but not in its predecessors.\n";
            }
        }
        // 检查前驱
        for (auto *pred : bb->getPredecessors())
        {
            bool found = false;
            for (auto *succ : pred->getSuccessors())
            {
                if (succ == bb)
                {
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                debugInfo << "CFG Error: " << bb->getName()
                          << " is predecessor of " << pred->getName()
                          << " but not in its successors.\n";
            }
        }
    }
}
bool TailRecursionEliminationPass::runOnFunction(Function *func)
{
    bool changed = false;
    if (func->isLibraryFunction() || func->getName() == "main")
        return false;
    auto &bbs = func->getBasicBlocks();
    if (bbs.empty())
        return false;

    BasicBlock *entryBB = func->getEntryBlock();
    auto exitBBs = func->getExitBlocks();
    for (auto bb : exitBBs)
    {
        if (bb->getInstructions().size() < 2)
            continue;
        auto *ret = dynamic_cast<ReturnInst *>(bb->getInstructions().back().get());
        auto *call = dynamic_cast<CallInst *>(bb->getInstructions()[bb->getInstructions().size() - 2].get());
        if (!ret || !call || ret->getReturnValue() != call || call->getCalledFunction() != func)
            continue;
        if (call->getIntArguments().size() > 8 || call->getFloatArguments().size() > 8)
            continue;
        auto &params = func->getArguments();
        const auto &args = call->getArguments();
        if (params.size() != args.size())
            continue;
        bool skipBlock = false;
        for (const auto &param : params)
        {
            if (param->getType()->isPointerTy())
            {
                if (verbose)
                {
                    debugInfo << "TailRecursionEliminationPass: Skipping tail recursion elimination for function "
                              << func->getName() << " at block " << bb->getName()
                              << " due to pointer parameter " << param->getName() << "\n";
                }
                skipBlock = true;
                break;
            }
        }
        if (skipBlock)
            continue;

        for (size_t i = 0; i < params.size(); ++i)
        {
            Value *param = params[i].get();
            Value *arg = args[i];
            auto *copy = new CopyInst(arg, param->getName());
            bb->insert(std::unique_ptr<Instruction>(copy), bb->getInstructions().size() - 2);
        }
        if (verbose)
        {
            debugInfo << "TailRecursionEliminationPass: Eliminating tail recursion in function " << func->getName()
                      << " at block " << bb->getName() << "\n";
        }
        call->removeThisFromOperands();
        ret->removeThisFromOperands();
        needToDelete.push_back(call);
        needToDelete.push_back(ret);
        bb->getInstructions().erase(std::remove_if(bb->getInstructions().begin(), bb->getInstructions().end(),
                                                   [&](const std::unique_ptr<Instruction> &inst)
                                                   {
                                                       return inst.get() == call || inst.get() == ret;
                                                   }),
                                    bb->getInstructions().end());
        bb->addInstruction(std::make_unique<BranchInst>(entryBB));
        entryBB->addPredecessor(bb);
        bb->addSuccessor(entryBB);
        changed = true;
    }
    if (changed)
        func->setTailRecursionEliminated(true);
    return changed;
}