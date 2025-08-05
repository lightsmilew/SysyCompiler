#include "OptimizationPasses.h"
#include <iostream>
#include <stack>

using namespace std;
using namespace optimization;
// ========== PassManager 实现 ==========
void PassManager::addPass(std::unique_ptr<Pass> pass)
{
    passes.push_back(std::move(pass));
}

bool PassManager::runOnModule(Module *module)
{
    bool changed = false;
    // 初始化循环信息
    initializeLoops(module);
    // 先对每个 pass，依次作用于所有函数
    for (auto &pass : passes)
    {
        for (auto &func : module->Functions)
        {
            if (!func->isLibraryFunction())
            {
                changed |= pass->runOnFunction(func.get());
            }
        }
        // 先不删除用于调试
        // 如果是函数内联pass，则在内联后删除内联的函数
        if (dynamic_cast<FunctionInliningPass *>(pass.get()))
        {
            module->Functions.erase(
                std::remove_if(
                    module->Functions.begin(),
                    module->Functions.end(),
                    [](const auto &func)
                    { return func->isDeletedFunction(); }),
                module->Functions.end());
        }
    }
    return changed;
}
void PassManager::setVerbose(bool v)
{
    verbose = v;
    for (auto &pass : passes)
    {
        pass->verbose = v;
    }
}
void PassManager::initializeLoops(Module *module)
{
    for (auto &func : module->Functions)
    {
        if (func->isLibraryFunction())
            continue; // 跳过库函数
        func->setLoops(ControlFlowAnalysis::findLoops(func.get()));
        if (verbose)
        {
            debugInfo << "Function: " << func->getName() << "\n";
            for (const auto &loop : func->getLoops())
            {
                debugInfo << "  Loop Header: " << loop.header->getName() << "\n";
                debugInfo << "  Blocks: ";
                for (const auto &block : loop.blocks)
                {
                    debugInfo << block->getName() << " ";
                }
                debugInfo << "\n";
            }
        }
    }
}
std::string PassManager::toString() const
{
    std::stringstream ss;
    for (const auto &pass : passes)
    {
        ss << pass->getName() << ": \n"
           << pass->toString() << "\n";
    }
    if (verbose)
    {
        ss << "Debug Info:\n"
           << debugInfo.str();
    }
    return ss.str();
}
// ========== 死代码消除 ==========
bool DeadCodeEliminationPass::runOnFunction(Function *func)
{
    // 先删除不可达基本块（前驱为空且不是入口块）
    auto &bbs = func->getBasicBlocks();

    if (!bbs.empty())
    {
        BasicBlock *entry = bbs[0].get();
        // 先收集所有将要删除的不可达基本块
        std::vector<BasicBlock *> toDelete;
        for (auto &bbPtr : bbs)
        {
            BasicBlock *bb = bbPtr.get();
            if (bb != entry && bb->getPredecessors().empty())
            {
                toDelete.push_back(bb);
            }
        }
        // 对所有 phi 指令，移除对将要删除块的引用
        for (auto &bbPtr : bbs)
        {
            BasicBlock *bb = bbPtr.get();
            auto &insts = bb->getInstructions();
            for (auto &instPtr : insts)
            {
                if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                {
                    for (BasicBlock *delBB : toDelete)
                    {
                        unsigned index = phi->getIndexByBasicBlock(delBB);
                        if (index == -1)
                            continue; // 如果没有这个前驱块，跳过
                        // 删除对应的前驱块和值
                        phi->removeIncoming(index);
                    }
                    // 如果只剩一个incoming，直接替换
                    if (phi->getNumIncomingValues() == 1)
                    {
                        Value *incomingValue = phi->getIncomingValue(0);
                        phi->replaceAllUsesWith(incomingValue);
                        // 标记phi待删除（可加入needToDelete，或直接删除）
                    }
                }
            }
        }
        for (auto it = bbs.begin(); it != bbs.end();)
        {
            BasicBlock *bb = it->get();
            if (bb != entry && bb->getPredecessors().empty())
            {
                // 从后继中删除自身
                for (auto *succ : bb->getSuccessors())
                {
                    succ->removePredecessor(bb);
                }
                // 这里不能直接删除，把它放到needToDelete中,否则内存空间释放了
                needToDelete.push_back(it->release());
                // 从基本块列表中删除
                it = bbs.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    std::unordered_set<Instruction *> liveInsts;
    markLiveInstructions(func, liveInsts);

    bool changed = false;
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            if (liveInsts.count(inst) == 0)
            {
                inst->removeThisFromOperands(); // 从操作数中移除自己
                needToDelete.push_back(it->release());
                it = insts.erase(it);
                changed = true;
                if (verbose)
                {
                    debugInfo << "Dead instruction: " << inst->toString() << "\n";
                }
            }
            else
            {
                ++it;
            }
        }
    }
    return changed;
}

// 标记所有关键指令和其依赖为活跃
void DeadCodeEliminationPass::markLiveInstructions(Function *func, std::unordered_set<Instruction *> &liveInsts)
{
    std::stack<Instruction *> worklist;
    // 1. 标记终结指令、store、call等有副作用的指令为活跃
    for (auto &bb : func->getBasicBlocks())
    {
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            // 如果是关键指令
            if (isInstructionCritical(inst))
            {
                liveInsts.insert(inst);
                worklist.push(inst);
            }
        }
    }
    // 2. 反向传播依赖
    while (!worklist.empty())
    {
        Instruction *inst = worklist.top();
        worklist.pop();
        for (auto *op : inst->getOperands())
        {
            if (auto *def = dynamic_cast<Instruction *>(op))
            {
                if (liveInsts.insert(def).second)
                {
                    worklist.push(def);
                }
            }
        }
    }
}

// 判断指令是否为关键指令
bool DeadCodeEliminationPass::isInstructionCritical(Instruction *inst)
{
    return inst->mayHaveSideEffects();
}
// ========== 公共子表达式消除 ==========
bool CommonSubexpressionEliminationPass::runOnFunction(Function *func)
{
    std::unordered_map<BasicBlock *, BasicBlock *> idom;
    idom = ControlFlowAnalysis::analyze(func);
    bool changed = false;
    bool localChanged;
    // 递归消除
    do
    {
        exprMap.clear();
        localChanged = false;
        for (auto &bb : func->getBasicBlocks())
        {
            auto &insts = bb->getInstructions();
            for (auto it = insts.begin(); it != insts.end();)
            {
                Instruction *inst = it->get();
                if (!canBeCommonSubexpression(inst, bb.get()))
                {
                    ++it;
                    continue;
                }
                auto key = getExpressionKey(inst);
                auto found = exprMap.find(key);
                if (found != exprMap.end())
                {
                    // 只有原表达式所在基本块支配当前基本块时才可消除
                    BasicBlock *defBB = found->second.second;
                    Instruction *defInst = found->second.first;
                    //  load特判
                    //  如果查到的load在本load之前的块则跳过(load仅支持同基本块消除)
                    //  或者如果操作数有load指令，则不消除
                    if (inst->getOpcode() == Opcode::Load)
                    {
                        if (defBB != bb.get())
                        {
                            // 如果不是同一个基本块，则跳过
                            ++it;
                            continue;
                        }
                        // 否则判断中间是否有store指令进行修改
                        if (!CanLoadCSE(inst, found->second.first, bb.get()))
                        {
                            // 如果不能消除，则更新exprMap
                            exprMap[key] = {inst, bb.get()};
                            ++it;
                            continue;
                        }
                    }
                    // 判断表达式操作数是否有load，如果有load，且defBB!=bb，则不消除
                    // 此时表示该load指令的地址有可能被跨块修改，保守起见不进行消除
                    bool CanNotCSEWithLoadOperand = false;
                    for (auto *op : inst->getOperands())
                    {
                        if (auto *loadInst = dynamic_cast<LoadInst *>(op))
                        {
                            if (defBB != bb.get())
                            {
                                CanNotCSEWithLoadOperand = true;
                                break;
                            }
                            else
                            {

                                // 判断是否有store对该地址进行修改
                                Value *addr = loadInst->getOriginalPointer();
                                int pos1 = bb->getInstructionOrder(defInst);
                                int pos2 = bb->getInstructionOrder(inst);
                                // 检查两条指令之间是否有store指令修改了地址
                                if (pos1 > pos2)
                                {
                                    // 如果defInst在inst之后，则不消除
                                    CanNotCSEWithLoadOperand = true;
                                    break;
                                }
                                auto &insts = bb->getInstructions();
                                for (int i = pos1 + 1; i < pos2; i++)
                                {
                                    if (auto *storeInst = dynamic_cast<StoreInst *>(insts[i].get()))
                                    {
                                        if (isSameAddr(storeInst->getOriginalPointer(), addr))
                                        {
                                            CanNotCSEWithLoadOperand = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                    if (CanNotCSEWithLoadOperand)
                    {
                        // 不过可以更新exprMap，为后续可能的消除做准备
                        exprMap[key] = {inst, bb.get()};
                        ++it;
                        continue;
                    }
                    // 进入消除过程
                    if (defBB == bb.get() || ControlFlowAnalysis::dominates(idom, defBB, bb.get()))
                    {
                        inst->replaceAllUsesWith(found->second.first);
                        if (verbose)
                        {
                            debugInfo << inst->toString() << " replaced with "
                                      << found->second.first->toString() << " in "
                                      << bb->getName() << endl;
                        }
                        // 从操作数的user列表中移除自己
                        inst->removeThisFromOperands();
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                        localChanged = true;
                        changed = true;
                        continue;
                    }
                }
                exprMap[key] = {inst, bb.get()};
                ++it;
            }
        }
    } while (localChanged);
    return changed;
}
// 为每个指令生成唯一的表达式键
std::pair<std::string, std::vector<std::string>> CommonSubexpressionEliminationPass::getExpressionKey(Instruction *inst)
{
    std::vector<std::string> ops;
    // 如果是getelementptr指令，特殊处理，补的0不需要添加
    if (auto *gep = dynamic_cast<GetElementPtrInst *>(inst))
    {
        for (int i = 0; i < gep->getNumOperands() - gep->num_addedzero; i++)
        {
            auto *op = gep->getOperandByIndex(i);
            if (auto *ci = dynamic_cast<ConstantInt *>(op))
            {
                ops.push_back("int:" + std::to_string(ci->Value));
            }
            else if (auto *cf = dynamic_cast<ConstantFloat *>(op))
            {
                ops.push_back("float:" + std::to_string(cf->Value));
            }
            else
            {
                ops.push_back("var:" + op->getName());
            }
        }
        return {inst->getOpcodeName(), ops};
    }
    for (auto *op : inst->getOperands())
    {
        if (auto *ci = dynamic_cast<ConstantInt *>(op))
        {
            ops.push_back("int:" + std::to_string(ci->Value));
        }
        else if (auto *cf = dynamic_cast<ConstantFloat *>(op))
        {
            ops.push_back("float:" + std::to_string(cf->Value));
        }
        else
        {
            ops.push_back("var:" + op->getName());
        }
    }
    return {inst->getOpcodeName(), ops};
}

// 判断指令是否可以作为公共子表达式
bool CommonSubexpressionEliminationPass::canBeCommonSubexpression(Instruction *inst, BasicBlock *bb)
{
    // 如果有phi作为操作数，不做CSE，因为此时变量依赖合流，不同位置的值可能不一样
    for (auto *v : inst->getOperands())
    {
        if (dynamic_cast<PhiInst *>(v))
        {
            return false;
        }
    }
    // 处理无副作用的二元运算、getelementptr、load以及无副作用的call
    // 不包括Store Ret Br
    return (inst->isBinaryOp() ||
            inst->getOpcode() == Opcode::GetElementPtr ||
            inst->getOpcode() == Opcode::Load ||
            (inst->getOpcode() == Opcode::Call && !dynamic_cast<CallInst *>(inst)->ifHasSideEffects()));
}
// 修改load指令CSE处理，跨基本块暂时不做，难度太高
// load需要支持，只做基本块内替换，如果第一个load后面没有store则可以替换，替换后更新哈希表load的指令
bool CommonSubexpressionEliminationPass::CanLoadCSE(Instruction *inst, Instruction *map_inst, BasicBlock *bb)
{
    // 只允许同一基本块内的load做CSE，且store和load之间没有其他store
    auto *loadInst = dynamic_cast<LoadInst *>(inst);
    auto *mapLoadInst = dynamic_cast<LoadInst *>(map_inst);
    if (!loadInst || !mapLoadInst)
        return false;
    Value *addr = loadInst->getOriginalPointer();
    if (!addr)
        return false;
    // std::string addrName =normalizeName(addr->getName());
    int pos1 = bb->getInstructionOrder(map_inst);
    int pos2 = bb->getInstructionOrder(inst);
    if (pos1 == -1 || pos2 == -1 || pos1 >= pos2)
        return false; // map_inst必须在inst之前
    auto &insts = bb->getInstructions();
    // 检查load之前是否有store，load之后不能有store
    for (int i = pos1 + 1; i < pos2; ++i)
    {
        if (auto *store = dynamic_cast<StoreInst *>(insts[i].get()))
        {
            if (isSameAddr(store->getOriginalPointer(), addr))
            {
                return false; // 两条load之间有store，不能CSE
            }
        }
    }
    // 没有store，可以CSE
    return true;
}

// 哈希函数，用于表达式键的哈希表
std::size_t CommonSubexpressionEliminationPass::ExpressionHash::operator()(const std::pair<std::string, std::vector<std::string>> &expr) const
{
    std::size_t h = std::hash<std::string>()(expr.first);
    for (const auto &s : expr.second)
        h ^= std::hash<std::string>()(s) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}

// ========== 循环不变代码移动 ==========
bool LoopInvariantCodeMotionPass::runOnFunction(Function *func)
{
    bool changed = false;
    // 记录每一轮pass后是否有外提变量，有则继续运行直到所有能外提变量全部外提
    bool localChanged;
    do
    {
        int count = 0;
        localChanged = false;
        // 1. 查找所有循环
        auto loops = func->getLoops();
        for (auto &loop : loops)
        {
            // 2. 找到循环的前置块（preheader）
            BasicBlock *preheader = findPreheader(loop);
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
            if (loop.contains(loadOp))
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
    ;
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
            if (loop.contains(def))
            {
                return false;
            }
        }
    }
    return true;
}
// 查找循环头的前驱块
BasicBlock *LoopInvariantCodeMotionPass::findPreheader(const Loop &loop)
{
    BasicBlock *header = loop.header;
    BasicBlock *preheader = nullptr;
    int count = 0;
    for (auto *pred : header->getPredecessors())
    {
        // preheader 必须不在循环体内
        if (std::find(loop.blocks.begin(), loop.blocks.end(), pred) == loop.blocks.end())
        {
            preheader = pred;
            ++count;
        }
    }
    // 必须只有一个循环外前驱才安全
    if (count == 1)
        return preheader;
    return nullptr;
}
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
                    if (shouldInline(callee))
                    {
                        auto insertPos = it - insts.begin();
                        inlineAt(call, caller, bb, insertPos);
                        call->removeThisFromOperands();
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                        callee->setDeleted(true); // 标记为已删除
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
    // 更新函数的循环信息
    caller->setLoops(ControlFlowAnalysis::findLoops(caller));
    if (verbose)
    {
        debugInfo << "Function: " << caller->getName() << "\n";
        for (const auto &loop : caller->getLoops())
        {
            debugInfo << "  Loop Header: " << loop.header->getName() << "\n";
            debugInfo << "  Blocks: ";
            for (const auto &block : loop.blocks)
            {
                debugInfo << block->getName() << " ";
            }
            debugInfo << "\n";
        }
    }
    return changed;
}
// 判断是否适合内联
bool FunctionInliningPass::shouldInline(Function *callee)
{
    if (callee->isLibraryFunction() || callee->isRecursive())
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
    auto size_loops = callee->getLoops().size();
    size_loops = size_loops == 0 ? 1 : size_loops; // 避免除0
    auto basicBlockCount = callee->getBasicBlocks().size();
    // 不内联递归/库函数/过大函数/控制流复杂
    if (callee->getInstructionCount() > 64 || basicBlockCount / size_loops > 5)
        return false;
    return true;
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
    for (size_t i = 0; i < params.size(); ++i)
        valueMap[params[i].get()] = args[i];
    int num = 0;
    string suffix = getsuffix(callee->getName());

    // 复制所有基本块，建立映射
    std::unordered_map<BasicBlock *, BasicBlock *> bbMap;
    std::vector<BasicBlock *> calleeBBs;
    for (auto &bbCallee : callee->getBasicBlocks())
    {
        auto *newBB = new BasicBlock(bbCallee->getName() + "_" + suffix, caller);
        bbMap[bbCallee.get()] = newBB;
        calleeBBs.push_back(bbCallee.get());
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
                        phi->IncomingValues[i] = bbMap[phi->IncomingValues[i]];
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
// 常量折叠实现
bool ConstantFoldingPass::runOnFunction(Function *func)
{
    bool changed = false;
    bool localChanged;
    do
    {
        localChanged = false;
        for (auto &bb : func->getBasicBlocks())
        {
            auto &insts = bb->getInstructions();
            for (auto it = insts.begin(); it != insts.end();)
            {
                Instruction *inst = it->get();
                // 只处理二元运算且无副作用
                if (inst && inst->isBinaryOp())
                {
                    auto binaryOperator = dynamic_cast<BinaryOperator *>(inst);
                    if (!binaryOperator)
                    {
                        ++it;
                        continue;
                    }
                    Value *lhs = binaryOperator->getLHS();
                    Value *rhs = binaryOperator->getRHS();

                    // int常量折叠
                    if (auto *ci1 = dynamic_cast<ConstantInt *>(lhs))
                    {
                        if (auto *ci2 = dynamic_cast<ConstantInt *>(rhs))
                        {
                            int result = 0;
                            switch (inst->getOpcode())
                            {
                            case Opcode::Add:
                                result = ci1->Value + ci2->Value;
                                break;
                            case Opcode::Sub:
                                result = ci1->Value - ci2->Value;
                                break;
                            case Opcode::Mul:
                                result = ci1->Value * ci2->Value;
                                break;
                            case Opcode::SDiv:
                                result = ci2->Value != 0 ? ci1->Value / ci2->Value : 0;
                                break;
                            case Opcode::SRem:
                                result = ci2->Value != 0 ? ci1->Value % ci2->Value : 0;
                                break;
                            default:
                                throw std::runtime_error("Unsupported opcode for constant folding");
                            }
                            auto constVal = new ConstantInt(IntegerType::getInstance(), result);
                            inst->replaceAllUsesWith(constVal);
                            if (verbose)
                            {
                                debugInfo << "Constant folding: " << inst->getOpcodeName() << " "
                                          << ci1->Value << " and " << ci2->Value
                                          << " to " << result << "\n";
                            }
                            // 还要打印输出
                            inst->removeThisFromOperands();
                            needToDelete.push_back(it->release());
                            it = insts.erase(it);
                            localChanged = true;
                            changed = true;
                            continue;
                        }
                    }
                    // float常量折叠
                    if (auto *cf1 = dynamic_cast<ConstantFloat *>(lhs))
                    {
                        if (auto *cf2 = dynamic_cast<ConstantFloat *>(rhs))
                        {
                            float result = 0;
                            switch (inst->getOpcode())
                            {
                            case Opcode::FAdd:
                                result = cf1->Value + cf2->Value;
                                break;
                            case Opcode::FSub:
                                result = cf1->Value - cf2->Value;
                                break;
                            case Opcode::FMul:
                                result = cf1->Value * cf2->Value;
                                break;
                            case Opcode::FDiv:
                                result = cf2->Value != 0.0f ? cf1->Value / cf2->Value : 0.0f;
                                break;
                            default:
                                throw std::runtime_error("Unsupported opcode for constant folding");
                            }
                            auto constVal = new ConstantFloat(FloatType::getInstance(), result);
                            inst->replaceAllUsesWith(constVal);
                            if (verbose)
                            {
                                debugInfo << "Constant folding: " << inst->getOpcodeName() << " "
                                          << cf1->Value << " and " << cf2->Value
                                          << " to " << result << "\n";
                            }
                            // 还要打印输出
                            inst->removeThisFromOperands();
                            needToDelete.push_back(it->release());
                            it = insts.erase(it);
                            localChanged = true;
                            changed = true;
                            continue;
                        }
                    }
                }
                // int比较指令
                if (inst && inst->getOpcode() == Opcode::ICmp)
                {
                    auto *icmp = dynamic_cast<ICmpInst *>(inst);
                    auto *ci1 = dynamic_cast<ConstantInt *>(icmp->getLHS());
                    auto *ci2 = dynamic_cast<ConstantInt *>(icmp->getRHS());
                    if (ci1 && ci2)
                    {
                        int result = 0;
                        switch (icmp->getPredicate())
                        {
                        case ICmpInst::ICMP_EQ:
                            result = (ci1->Value == ci2->Value);
                            break;
                        case ICmpInst::ICMP_NE:
                            result = (ci1->Value != ci2->Value);
                            break;
                        case ICmpInst::ICMP_SLT:
                            result = (ci1->Value < ci2->Value);
                            break;
                        case ICmpInst::ICMP_SLE:
                            result = (ci1->Value <= ci2->Value);
                            break;
                        case ICmpInst::ICMP_SGT:
                            result = (ci1->Value > ci2->Value);
                            break;
                        case ICmpInst::ICMP_SGE:
                            result = (ci1->Value >= ci2->Value);
                            break;
                        }
                        auto constVal = new ConstantInt(IntegerType::getInstance(), result);
                        inst->replaceAllUsesWith(constVal);
                        if (verbose)
                        {
                            debugInfo << "Constant folding: " << inst->getOpcodeName() << " "
                                      << ci1->Value << " and " << ci2->Value
                                      << " to " << result << "\n";
                        }
                        // 还要打印输出
                        inst->removeThisFromOperands();
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                        localChanged = true;
                        changed = true;
                        continue;
                    }
                }
                // float比较指令
                if (inst && inst->getOpcode() == Opcode::FCmp)
                {
                    auto *fcmp = dynamic_cast<FCmpInst *>(inst);
                    auto *cf1 = dynamic_cast<ConstantFloat *>(fcmp->getLHS());
                    auto *cf2 = dynamic_cast<ConstantFloat *>(fcmp->getRHS());
                    if (cf1 && cf2)
                    {
                        int result = 0;
                        switch (fcmp->getPredicate())
                        {
                        case FCmpInst::FCMP_OEQ:
                            result = (cf1->Value == cf2->Value);
                            break;
                        case FCmpInst::FCMP_ONE:
                            result = (cf1->Value != cf2->Value);
                            break;
                        case FCmpInst::FCMP_OLT:
                            result = (cf1->Value < cf2->Value);
                            break;
                        case FCmpInst::FCMP_OLE:
                            result = (cf1->Value <= cf2->Value);
                            break;
                        case FCmpInst::FCMP_OGT:
                            result = (cf1->Value > cf2->Value);
                            break;
                        case FCmpInst::FCMP_OGE:
                            result = (cf1->Value >= cf2->Value);
                            break;
                        }
                        auto constVal = new ConstantInt(IntegerType::getInstance(), result);
                        inst->replaceAllUsesWith(constVal);
                        if (verbose)
                        {
                            debugInfo << "Constant folding: " << inst->getOpcodeName() << " "
                                      << cf1->Value << " and " << cf2->Value
                                      << " to " << result << "\n";
                        }
                        // 还要打印输出
                        inst->removeThisFromOperands();
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                        localChanged = true;
                        changed = true;
                        continue;
                    }
                }
                ++it;
            }
        }
    } while (localChanged);
    return changed;
}
// phi消除
bool PhiEliminationPass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            auto *phi = dynamic_cast<PhiInst *>(inst);
            if (!phi)
            {
                ++it;
                continue;
            }
            // 1. 只有一个输入，直接替换
            if (phi->getNumIncomingValues() == 1)
            {
                Value *incomingValue = phi->getIncomingValue(0);
                phi->removeThisFromOperands();
                phi->replaceAllUsesWith(incomingValue);
                needToDelete.push_back(it->release());
                it = insts.erase(it);
                changed = true;
                continue;
            }
            // 如果所有输入都相同，直接替换，不需要产生copy指令
            bool allSame = true;
            Value *firstVal = phi->getIncomingValue(0);
            for (size_t i = 1; i < phi->getNumIncomingValues(); ++i)
            {
                if (phi->getIncomingValue(i) != firstVal)
                {
                    allSame = false;
                    break;
                }
            }
            if (allSame)
            {
                phi->removeThisFromOperands();
                phi->replaceAllUsesWith(firstVal);
                needToDelete.push_back(it->release());
                it = insts.erase(it);
                changed = true;
                continue;
            }
            vector<Value *> operands;
            // 2. 多输入phi，插入move/copy到前驱块末尾
            for (size_t i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                BasicBlock *pred = phi->getIncomingBlock(i);
                Value *val = phi->getIncomingValue(i);
                // 在终结指令前插入
                auto rawPtr = new CopyInst(val, phi->getName());
                std::unique_ptr<Instruction> copyInst(rawPtr);
                pred->insertBeforeTerminator(std::move(copyInst));
            }
            // 从基本块中删除原来指令，phi对应value仍然保留
            // 从所有phi的操作数中删除自己
            phi->removeThisFromOperands();
            needToDelete.push_back(it->release());
            it = insts.erase(it);
            changed = true;
        }
    }
    // 遍历所有基本块的所有指令，如果替换后的copy指令源操作数名字与目的操作数名字相同则删除
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            if (!inst)
            {
                std::cerr << "inst is nullptr!" << std::endl;
                continue;
            }
            if (auto *copy = dynamic_cast<CopyInst *>(inst))
            {
                if (copy->getSource()->getName() == copy->getDest()->getName())
                {
                    // 删除无效的copy指令
                    copy->removeThisFromOperands();
                    needToDelete.push_back(it->release());
                    it = insts.erase(it);
                    changed = true;
                    continue;
                }
            }
            ++it;
        }
    }
    return changed;
}

// 活跃变量分析 Pass 实现
// 修改 runOnFunction 实现
bool LiveVariableAnalysisPass::runOnFunction(Function *func)
{
    liveIn.clear();
    liveOut.clear();

    auto &bbs = func->getBasicBlocks();
    for (auto &bbPtr : bbs)
    {
        BasicBlock *bb = bbPtr.get();
        liveIn[bb] = {};
        liveOut[bb] = {};
    }

    // 正确收集每个基本块的def和use集合
    std::unordered_map<BasicBlock *, std::set<std::string>> defMap, useMap;
    for (auto &bbPtr : bbs)
    {
        BasicBlock *bb = bbPtr.get();
        std::set<std::string> def, use;
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            // 处理操作数
            if (auto *call = dynamic_cast<CallInst *>(inst))
            {
                for (size_t i = 1; i < call->getNumOperands(); ++i)
                {
                    Value *op = call->getOperandByIndex(i);
                    if (dynamic_cast<Constant *>(op))
                        continue;
                    std::string name = op->getName();
                    if (def.count(name) == 0 && use.count(name) == 0)
                        use.insert(name);
                }
            }
            else
            {
                for (auto *op : inst->getOperands())
                {
                    if (dynamic_cast<Constant *>(op))
                        continue;
                    std::string name = op->getName();
                    if (def.count(name) == 0 && use.count(name) == 0)
                        use.insert(name);
                }
            }
            // 处理定义
            if (inst->hasResult())
                def.insert(inst->getName());
        }
        defMap[bb] = def;
        useMap[bb] = use;
    }

    if (verbose)
    {
        debugInfo << func->getName() << " Def/Use Sets:\n";
        for (auto &bbPtr : bbs)
        {
            BasicBlock *bb = bbPtr.get();
            debugInfo << "BB: " << bb->getName() << "\n";
            debugInfo << "  Def: ";
            for (auto &v : defMap[bb])
                debugInfo << v << " ";
            debugInfo << "\n  Use: ";
            for (auto &v : useMap[bb])
                debugInfo << v << " ";
            debugInfo << "\n";
        }
    }

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (auto it = bbs.rbegin(); it != bbs.rend(); ++it)
        {
            BasicBlock *bb = it->get();
            std::set<std::string> oldIn = liveIn[bb];
            std::set<std::string> oldOut = liveOut[bb];

            // liveOut[bb] = 并集(succ的liveIn)
            liveOut[bb].clear();
            for (auto *succ : bb->getSuccessors())
                liveOut[bb].insert(liveIn[succ].begin(), liveIn[succ].end());

            // liveIn[bb] = use[bb] ∪ (liveOut[bb] - def[bb])
            liveIn[bb] = useMap[bb];
            for (auto &v : liveOut[bb])
            {
                if (defMap[bb].count(v) == 0)
                    liveIn[bb].insert(v);
            }

            if (liveIn[bb] != oldIn || liveOut[bb] != oldOut)
                changed = true;
        }
    }
    for (auto &bbPtr : bbs)
    {
        BasicBlock *bb = bbPtr.get();
        bb->setLiveIn(liveIn[bb]);
        bb->setLiveOut(liveOut[bb]);
    }
    if (verbose)
    {
        debugInfo << func->getName() << " Live Variable Analysis:\n";
        debugInfo << "  Total Basic Blocks: " << bbs.size() << "\n";
        for (auto &bbPtr : bbs)
        {
            BasicBlock *bb = bbPtr.get();
            debugInfo << "BB: " << bb->getName() << "\n";
            debugInfo << "  liveIn: ";
            for (auto &v : liveIn[bb])
                debugInfo << v << " ";
            debugInfo << "\n  liveOut: ";
            for (auto &v : liveOut[bb])
                debugInfo << v << " ";
            debugInfo << "\n";
        }
    }
    return false;
}
bool GEPExpansionPass ::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            if (auto *gep = dynamic_cast<GetElementPtrInst *>(inst))
            {
                // 取消限制，可以增加循环不变量外提优化
                auto indices = gep->getIndices();
                vector<unique_ptr<Instruction>> newgepInsts;
                auto pointer = gep->getPointerOperand();
                std::string basename = gep->getName();
                int size = indices.size() - gep->num_addedzero;
                for (int i = 0; i < size; i++)
                {
                    auto newgep = std::make_unique<GetElementPtrInst>(pointer, vector<Value *>{indices[i]}, basename + "_gep" + std::to_string(i));
                    newgepInsts.push_back(std::move(newgep));
                    // 更新指针操作数
                    pointer = newgepInsts.back().get();
                }
                // 插入新GEP指令到当前基本块
                it = insts.insert(it, std::make_move_iterator(newgepInsts.begin()), std::make_move_iterator(newgepInsts.end()));
                // 跳过新插入的GEP
                std::advance(it, size);
                Instruction *lastNewGEP = prev(it, 1)->get(); // 获取最后一个新插入的GEP指令
                // 替换原GEP的所有使用
                gep->replaceAllUsesWith(lastNewGEP);
                // 删除原来的GEP指令
                gep->removeThisFromOperands();
                needToDelete.push_back(it->release());
                it = insts.erase(it);
                changed = true;
                if (verbose)
                {
                    debugInfo << "GEP Expansion: Replaced GEP " << gep->getName() << " with "
                              << indices.size() << " new GEP instructions in " << bb->getName() << "\n";
                }
            }
            else
            {
                ++it; // 如果不是GEP，继续下一个指令
            }
        }
    }
    return changed;
}
bool AddChainReductionPass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        // 用下标逆序遍历，避免迭代器失效
        for (int i = insts.size() - 1; i >= 0; --i)
        {
            Instruction *inst = insts[i].get();
            if (inst && inst->getOpcode() == Opcode::Add)
            {
                Value *lhs = inst->getOperands()[0];
                Value *rhs = inst->getOperands()[1];
                int chainLen = 1;
                Value *base = nullptr;
                Instruction *cur = inst;
                std::vector<Instruction *> chainInsts = {cur};
                while (auto *prevAdd = dynamic_cast<BinaryOperator *>(lhs))
                {
                    if (prevAdd->getOpcode() != Opcode::Add)
                        break;
                    Value *prevLhs = prevAdd->getOperands()[0];
                    Value *prevRhs = prevAdd->getOperands()[1];
                    if (prevLhs == rhs && prevRhs == rhs)
                    {
                        chainLen++;
                        chainInsts.push_back(prevAdd);
                        lhs = prevLhs;
                    }
                    else if (prevRhs == rhs)
                    {
                        chainLen++;
                        chainInsts.push_back(prevAdd);
                        lhs = prevLhs;
                    }
                    else
                    {
                        break;
                    }
                }
                if (chainLen > 1 && rhs)
                {
                    base = rhs;
                    auto *mulInst = new BinaryOperator(Opcode::Mul, base, new ConstantInt(IntegerType::getInstance(), chainLen + 1), inst->getName() + "_mul");
                    // 在链式加法最后一条指令的后面插入
                    insts.insert(insts.begin() + i + 1, std::unique_ptr<Instruction>(mulInst));
                    inst->removeThisFromOperands();
                    inst->replaceAllUsesWith(mulInst);
                    needToDelete.push_back(insts[i].release());
                    insts.erase(insts.begin() + i);
                    changed = true;
                    // 删除链上的所有 add
                    for (auto *chainInst : chainInsts)
                    {
                        if (chainInst != inst)
                        {
                            auto chainIt = std::find_if(insts.begin(), insts.end(),
                                                        [chainInst](const std::unique_ptr<Instruction> &ptr)
                                                        { return ptr.get() == chainInst; });
                            if (chainIt != insts.end())
                            {
                                chainInst->removeThisFromOperands();
                                needToDelete.push_back(chainIt->release());
                                insts.erase(chainIt);
                            }
                        }
                    }
                }
            }
        }
    }
    return changed;
}
bool StrengthReductionPass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        // 用下标逆序遍历，避免迭代器失效
        for (int i = insts.size() - 1; i >= 0; --i)
        {
            Instruction *inst = insts[i].get();
            if (inst && inst->getOpcode() == Opcode::Mul)
            {
                Value *lhs = inst->getOperands()[0];
                Value *rhs = inst->getOperands()[1];
                if (auto *constInt = dynamic_cast<ConstantInt *>(rhs))
                {
                    if (constInt->Value == 0)
                    {
                        // 乘以0，直接替换为0
                        auto *zero = new ConstantInt(IntegerType::getInstance(), 0);
                        inst->replaceAllUsesWith(zero);
                        inst->removeThisFromOperands();
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "Strength Reduction: Replaced Mul with 0 in " << bb->getName() << "\n";
                        }
                        continue;
                    }
                    else if (constInt->Value != 0 && (constInt->Value & (constInt->Value - 1)) == 0)
                    {
                        // 2的幂，直接左移
                        int shift = 0;
                        int val = constInt->Value;
                        while (val > 1)
                        {
                            val >>= 1;
                            shift++;
                        }
                        // 替换为左移操作
                        auto *shlInst = new BinaryOperator(Opcode::Sll, lhs, new ConstantInt(IntegerType::getInstance(), shift), inst->getName() + "_sll");
                        inst->removeThisFromOperands();
                        inst->replaceAllUsesWith(shlInst);
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(shlInst));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "Strength Reduction: Replaced Mul with Sll for " << constInt->Value
                                      << " in " << bb->getName() << "\n";
                        }
                    }
                    // 不需要处理不是2的幂次方情况，因为会降低性能
                }
            }
            else if (inst && inst->getOpcode() == Opcode::SDiv)
            {
                Value *lhs = inst->getOperands()[0];
                Value *rhs = inst->getOperands()[1];
                if (auto *constInt = dynamic_cast<ConstantInt *>(rhs))
                {
                    if (constInt->Value != 0 && (constInt->Value & (constInt->Value - 1)) == 0)
                    {
                        // 2的幂，直接算数右移
                        int shift = 0;
                        int val = constInt->Value;
                        while (val > 1)
                        {
                            val >>= 1;
                            shift++;
                        }
                        // 替换为右移操作
                        // 负数除法需要加掩码
                        auto *zero = new ConstantInt(IntegerType::getInstance(), 0);
                        auto *mask = new ConstantInt(IntegerType::getInstance(), (1 << shift) - 1);
                        auto *signedDiv = new BinaryOperator(Opcode::Sra, lhs, new ConstantInt(IntegerType::getInstance(), 31), inst->getName() + "_signedDiv");
                        auto *addand = new BinaryOperator(Opcode::And, signedDiv, mask, inst->getName() + "_addand");
                        auto *lhsAdj = new BinaryOperator(Opcode::Add, lhs, addand, inst->getName() + "_lhsAdj");
                        auto *sraInst = new BinaryOperator(Opcode::Sra, lhsAdj, new ConstantInt(IntegerType::getInstance(), shift), inst->getName() + "_sra");
                        inst->removeThisFromOperands();
                        inst->replaceAllUsesWith(sraInst);
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sraInst));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhsAdj));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(addand));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(signedDiv));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "Strength Reduction: Replaced SDiv with Sra for " << constInt->Value
                                      << " in " << bb->getName() << "\n";
                        }
                    }
                }
            }
        }
    }
    return changed;
}
bool GEPToBitCastPass::runOnFunction(Function *func)
{
    bool changed = false;
    // gep展开经过公共子表达式消除后可以强度削弱->查看是否有多余的GEP指令（比如indices全为0的情况）
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            if (auto *gep = dynamic_cast<GetElementPtrInst *>(inst))
            {
                // 检查是否所有索引都是0
                bool allZero = true;
                for (auto *index : gep->getIndices())
                {
                    if (auto *constInt = dynamic_cast<ConstantInt *>(index))
                    {
                        if (constInt->Value != 0)
                        {
                            allZero = false;
                            break;
                        }
                    }
                    else
                    {
                        allZero = false;
                    }
                }
                if (allZero)
                {
                    // 类型转换：插入BitCast指令
                    auto *ptrOperand = gep->getPointerOperand();
                    auto *targetType = gep->getType(); // GEP的结果类型
                    auto *castInst = new CastInst(Opcode::BitCast, ptrOperand, targetType, gep->getName() + "_bitcast");
                    // 在GEP指令前面插入BitCast指令
                    it = insts.insert(it, std::unique_ptr<Instruction>(castInst));
                    gep->removeThisFromOperands();
                    gep->replaceAllUsesWith(castInst);
                    ++it;
                    // 删除原来的GEP指令（此时it指向gep，castInst在gep前面）
                    needToDelete.push_back(it->release());
                    it = insts.erase(it);
                    changed = true;
                    if (verbose)
                    {
                        debugInfo << "GEP to BitCast: Replaced GEP " << gep->getName() << " with BitCast in "
                                  << bb->getName() << "\n";
                    }
                }
                else
                {
                    ++it;
                }
            }
            else
            {
                ++it; // 如果不是GEP，继续下一个指令
            }
        }
    }
    // 替换完再扫描一遍bitcast，如果类型相同直接删除,用操作数替换
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            if (auto *bitCast = dynamic_cast<CastInst *>(inst))
            {
                if (bitCast->getOpcode() == Opcode::BitCast)
                {
                    // 检查源类型和目标类型是否相同
                    Type *srcType = bitCast->getOperand()->getType();
                    Type *destType = bitCast->getType();
                    if (destType->isTypeEqual(destType, srcType))
                    {
                        // 删除无效的BitCast指令
                        bitCast->removeThisFromOperands();
                        bitCast->replaceAllUsesWith(bitCast->getOperand());
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "Removed redundant BitCast: " << bitCast->getName() << " in " << bb->getName() << "\n";
                        }
                    }
                    else
                    {
                        ++it; // 如果不是冗余的BitCast，继续下一个指令
                    }
                }
                else
                {
                    ++it; // 如果不是BitCast，继续下一个指令
                }
            }
            else
            {
                ++it; // 如果不是BitCast，继续下一个指令
            }
        }
    }
    return changed;
}
bool ArrayEliminationPass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        for (size_t i = 0; i < insts.size(); ++i)
        {
            auto *storeInst = dynamic_cast<StoreInst *>(insts[i].get());
            if (!storeInst)
                continue;
            auto *gep_store = dynamic_cast<GetElementPtrInst *>(storeInst->getPointer());
            if (!gep_store || gep_store->getIndices().size() != 1)
                continue;
            Value *arr = gep_store->getPointerOperand();
            Value *idx_store = gep_store->getIndices()[0];
            bool isSimple = false;
            bool needTypeCast = false;
            Value *A = nullptr;
            Opcode binOpcode;
            auto *bin = dynamic_cast<BinaryOperator *>(storeInst->getValueToStore());
            if (bin)
            {
                if (bin->getOpcode() == Opcode::Add || bin->getOpcode() == Opcode::FAdd)
                {
                    binOpcode = bin->getOpcode();
                    auto *lhs_sitofp = dynamic_cast<CastInst *>(bin->getLHS());
                    auto *rhs_sitofp = dynamic_cast<CastInst *>(bin->getRHS());
                    if (lhs_sitofp && lhs_sitofp->getOpcode() == Opcode::SIToFP && lhs_sitofp->getOperand() == idx_store)
                    {
                        // a[j] = A + (float)j
                        A = bin->getRHS();
                        needTypeCast = true;
                        isSimple = true;
                    }
                    else if (rhs_sitofp && rhs_sitofp->getOpcode() == Opcode::SIToFP && rhs_sitofp->getOperand() == idx_store)
                    {
                        // a[j] = (float)j + A
                        A = bin->getLHS();
                        needTypeCast = true;
                        isSimple = true;
                    }
                }
            }
            else if (storeInst->getValueToStore() == idx_store)
            {
                A = nullptr;
                isSimple = true;
            }
            if (!isSimple)
                continue;

            // 2. 检查所有load是否合法
            bool canReplace = true;
            std::vector<std::tuple<BasicBlock *, Instruction *, size_t>> loadsToReplace;
            BasicBlock *storeBB = bb;
            for (auto &bb2Ptr : func->getBasicBlocks())
            {
                BasicBlock *bb2 = bb2Ptr.get();
                auto &insts2 = bb2->getInstructions();
                for (size_t j = 0; j < insts2.size(); ++j)
                {
                    auto *load = dynamic_cast<LoadInst *>(insts2[j].get());
                    if (!load)
                        continue;
                    auto *gep2 = dynamic_cast<GetElementPtrInst *>(load->getPointer());
                    if (!gep2 || gep2->getPointerOperand() != arr)
                        continue;
                    if (gep2->getIndices().size() != 1)
                    {
                        canReplace = false;
                        break;
                    }
                    if (bb2 == storeBB)
                    {
                        // 只允许store之后的load
                        if (j <= i)
                            continue;
                        // 判断1:检查store和load之间有无其它store
                        for (size_t k = i + 1; k < j; ++k)
                        {
                            auto *otherStore = dynamic_cast<StoreInst *>(insts2[k].get());
                            if (otherStore)
                            {
                                auto *otherGep = dynamic_cast<GetElementPtrInst *>(otherStore->getPointer());
                                if (otherGep && isSameAddr(otherGep->getPointerOperand(), arr))
                                {
                                    canReplace = false;
                                    break;
                                }
                            }
                        }
                        if (!canReplace)
                        {
                            loadsToReplace.clear();
                            continue;
                        }
                    }
                    if (!canReplace)
                        break;
                    // 这里合并了判断2和判断3
                    // 判断2:检查路径上有无其它store
                    // 判断3:load到出口是否还有store，如果有则不能替换
                    // 判断:store到出口还要store，则不能替换
                    for (auto &exitBB : func->getExitBlocks())
                    {
                        if (ControlFlowAnalysis::hasStoreOnPath(storeBB, exitBB, arr))
                        {
                            // 如果有store到出口块，不能进行替换
                            if (verbose)
                            {
                                debugInfo << "Array Elimination: Cannot replace array access in " << bb->getName()
                                          << " due to store on path to exit block.\n";
                            }
                            canReplace = false;
                            break;
                        }
                    }
                    if (!canReplace)
                    {
                        // 判断到有非法load，则要把loadsToReplace清空
                        loadsToReplace.clear();
                        break;
                    }

                    loadsToReplace.emplace_back(bb2, load, j);
                }
                if (!canReplace)
                    break;
            }
            if (!canReplace)
                continue;

            // 3. 如果没有找到有效的load，此时说明不能进行替换store
            if (loadsToReplace.empty())
            {
                if (verbose)
                {
                    debugInfo << "Array Elimination: No valid loads found for array " << arr->getName() << " in " << bb->getName() << "\n";
                }
                continue;
            }
            // 3. 替换所有load为表达式
            for (auto &[bb2, load, pos] : loadsToReplace)
            {
                auto loadinst = dynamic_cast<LoadInst *>(load);
                if (!loadinst)
                    continue;
                auto *gep_load = dynamic_cast<GetElementPtrInst *>(loadinst->getPointer());
                Value *idx_load = gep_load->getIndices()[0];
                Value *newIdx_load = idx_load;
                Value *newExpr_load = nullptr;
                vector<Instruction *> needToAdd;
                // a[i]=i模式
                if (A == nullptr)
                {
                    if (needTypeCast)
                    {
                        newIdx_load = new CastInst(Opcode::SIToFP, idx_load, FloatType::getInstance(), "scalar_repl_cast_" + to_string(ArrayEliminationCount));
                        needToAdd.push_back(dynamic_cast<Instruction *>(newIdx_load));
                    }
                    newExpr_load = newIdx_load;
                }
                // a[i]=A+i模式
                else
                {
                    if (needTypeCast)
                    {
                        // 如果需要类型转换，使用CastInst
                        newIdx_load = new CastInst(Opcode::SIToFP, idx_load, FloatType::getInstance(), "scalar_repl_cast" + to_string(ArrayEliminationCount));
                        needToAdd.push_back(dynamic_cast<Instruction *>(newIdx_load));
                    }
                    newExpr_load = new BinaryOperator(binOpcode, A, newIdx_load, "scalar_repl" + to_string(ArrayEliminationCount));
                }
                auto newExprInst = dynamic_cast<Instruction *>(newExpr_load);
                if (!newExprInst)
                    continue;
                // 只有当A+j模式时才需要额外插入一条指令，否则直接使用循环变量即可
                if (A != nullptr)
                {
                    needToAdd.push_back(newExprInst);
                }
                load->replaceAllUsesWith(newExpr_load);
                auto &insts2 = bb2->getInstructions();
                load->removeThisFromOperands();
                needToDelete.push_back(insts2[pos].release());
                // 在原位置删除load指令
                insts2.erase(insts2.begin() + pos);
                // 在原位置插入新的表达式
                for (size_t k = 0; k < needToAdd.size(); ++k)
                {
                    insts2.insert(insts2.begin() + pos + k, std::unique_ptr<Instruction>(needToAdd[k]));
                    if (verbose)
                    {
                        debugInfo << "Array Elimination: Inserted scalar expression " << needToAdd[k]->getName()
                                  << " in " << bb2->getName() << "\n";
                    }
                }
                changed = true;
                if (verbose)
                {
                    debugInfo << "Array Elimination: Replaced array load " << loadinst->getName()
                              << " with scalar expression in " << bb2->getName() << "\n";
                }
            }
            // 4. 删除原store
            storeInst->removeThisFromOperands();
            needToDelete.push_back(insts[i].release());
            insts.erase(insts.begin() + i);
            --i;
            ArrayEliminationCount++;
            changed = true;
            if (verbose)
            {
                debugInfo << "Array Elimination: Replaced array store " << storeInst->getName()
                          << " with scalar expression in " << bb->getName() << "\n";
            }
        }
    }
    return changed;
}
bool CFGSimplificationPass::runOnFunction(Function *func)
{
    bool changed = false;
    auto &bbs = func->getBasicBlocks();
    vector<BasicBlock *> bbsToProcess;
    Value *arrayAddr = nullptr;
    for (auto &bbPtr : bbs)
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        // 1. 检查是否为 if (i > k) 结构
        for (size_t i = 0; i < insts.size(); ++i)
        {
            auto *icmp = dynamic_cast<ICmpInst *>(insts[i].get());
            if (!icmp || icmp->getPredicate() != ICmpInst::ICMP_SGT)
                continue;
            auto *iVar = icmp->getLHS();
            auto *kConst = dynamic_cast<ConstantInt *>(icmp->getRHS());
            if (!kConst)
                continue;
            int k = kConst->Value;

            // 2. 检查 then 分支是否为 s[k] = k
            // 这里假设 then 分支是下一个基本块，且有 store 指令 s[k] = k
            BasicBlock *thenBB = nullptr;
            BasicBlock *exitBB = nullptr;
            if (i + 1 < insts.size())
            {
                if (auto *br = dynamic_cast<BranchInst *>(insts[i + 1].get()))
                {
                    thenBB = br->TrueBlock;
                    exitBB = br->FalseBlock; // 该模式下无else分支
                }
            }
            if (!thenBB || !exitBB)
                continue;
            auto &thenInsts = thenBB->getInstructions();
            bool match = false;
            for (auto &tinstPtr : thenInsts)
            {
                if (auto *store = dynamic_cast<StoreInst *>(tinstPtr.get()))
                {
                    // 检查store的地址和数值是否都是k
                    auto *gep = dynamic_cast<GetElementPtrInst *>(store->getPointer());
                    auto *storeVal = dynamic_cast<ConstantInt *>(store->getValueToStore());
                    if (gep && storeVal && storeVal->Value == k)
                    {
                        // 检查GEP索引是否为k
                        if (gep->getIndices().size() == 1)
                        {
                            auto *idx = dynamic_cast<ConstantInt *>(gep->getIndices()[0]);
                            if (idx && idx->Value == k)
                            {
                                arrayAddr = gep->getPointerOperand();
                                match = true;
                                needToDelete.push_back(insts[i].release());     // 删除原有的 if (i > k) 结构
                                needToDelete.push_back(insts[i + 1].release()); // 删除分支指令
                                insts.erase(insts.begin() + i);                 // 删除if指令
                                insts.erase(insts.begin() + i);                 // 删除分支指令
                                break;
                            }
                        }
                    }
                }
            }
            if (!match)
                continue;
            // 找到匹配的 if (i > k) 结构和 then 分支 s[k] = k
            // bbsToProcess.push_back(bb);
            // bb即是整个循环的前驱基本块
            bbsToProcess.push_back(thenBB);
            bbsToProcess.push_back(exitBB);
            // 3. 递归/循环收集连续的k
            std::vector<int> kList = {k};
            BasicBlock *curBB = thenBB;
            while (true)
            {
                // 查找下一个 if (i > k+1) 结构
                auto &curInsts = curBB->getInstructions();
                bool foundNext = false;
                for (size_t j = 0; j < curInsts.size(); ++j)
                {
                    auto *icmp2 = dynamic_cast<ICmpInst *>(curInsts[j].get());
                    if (icmp2 && icmp2->getPredicate() == ICmpInst::ICMP_SGT)
                    {
                        auto *k2Const = dynamic_cast<ConstantInt *>(icmp2->getRHS());
                        if (k2Const && k2Const->Value == kList.back() + 1)
                        {
                            kList.push_back(k2Const->Value);
                            // 跳到下一个then分支
                            if (j + 1 < curInsts.size())
                            {
                                if (auto *br2 = dynamic_cast<BranchInst *>(curInsts[j + 1].get()))
                                {
                                    curBB = br2->TrueBlock;
                                    bbsToProcess.push_back(curBB);
                                    bbsToProcess.push_back(br2->FalseBlock);
                                    foundNext = true;
                                    break;
                                }
                            }
                        }
                    }
                }
                if (!foundNext)
                    break;
            }
            //
            // 4. 替换为循环
            if (kList.size() > 3)
            { // 只有连续if链足够长才替换
                int minK = kList.front();
                int maxK = kList.back();

                auto *minKthen = new BasicBlock("min_k_then", func);
                auto *loopCond = new BasicBlock("loop_cond", func);
                auto *loopBody = new BasicBlock("loop_body", func);
                auto *loopExit = new BasicBlock("loop_exit", func);
                // 比较指令min(i,maxK+1)
                auto *icmpMin = new ICmpInst(ICmpInst::ICMP_SLT, iVar, new ConstantInt(IntegerType::getInstance(), maxK + 1), "min_icmp");
                auto *condbr = new BranchInst(icmpMin, minKthen, loopCond);
                // 构建合流指令
                auto *kPhi = new PhiInst(IntegerType::getInstance(), "tk_loop");
                auto *min_i_maxK = new PhiInst(IntegerType::getInstance(), "min_i_maxK");
                // 设置循环条件
                auto *icmpLoop = new ICmpInst(ICmpInst::ICMP_SLT, kPhi, min_i_maxK, "loop_cond_icmp");
                // 设置跳转指令
                auto *brLoop = new BranchInst(icmpLoop, loopBody, loopExit);
                // 设置循环体指令
                auto *gepLoop = new GetElementPtrInst(arrayAddr, {kPhi}, "loop_gep");
                auto *storeLoop = new StoreInst(kPhi, gepLoop);
                auto *incK = new BinaryOperator(Opcode::Add, kPhi, new ConstantInt(IntegerType::getInstance(), 1), "inc_k");
                // 无条件跳转到循环条件
                auto *brToCond = new BranchInst(loopCond);
                // 设置初始值
                kPhi->addIncoming(new ConstantInt(IntegerType::getInstance(), minK), bb);
                kPhi->addIncoming(incK, loopBody);
                min_i_maxK->addIncoming(new ConstantInt(IntegerType::getInstance(), maxK + 1), bb);
                min_i_maxK->addIncoming(iVar, minKthen);
                // 将原来最外层if退出块指令复制到loopExit
                for (auto &instPtr : exitBB->getInstructions())
                {
                    std::unique_ptr<Instruction> instPtrCopy = move(instPtr);

                    loopExit->addInstruction(move(instPtrCopy)); // 使用move而不是insert，避免重复添加
                }
                exitBB->clearInstructions(); // 清空原有退出块指令
                // 将原来phi输入从exitBB的指令改为loopExit
                for (auto *user : exitBB->getUsers())
                {
                    if (auto *phi = dynamic_cast<PhiInst *>(user))
                    {
                        phi->replaceIncomingBasicBlock(exitBB, loopExit);
                    }
                }
                bb->addInstruction(std::unique_ptr<Instruction>(icmpMin));
                bb->addInstruction(std::unique_ptr<Instruction>(condbr));
                // 添加then跳转到循环条件-->不需要，因为cond块刚好在then块后面
                // minKthen->addInstruction(std::unique_ptr<Instruction>(thenBrToCond));
                // 将新构造的指令添加到基本块
                loopCond->addInstruction(std::unique_ptr<Instruction>(kPhi));
                loopCond->addInstruction(std::unique_ptr<Instruction>(min_i_maxK));
                loopCond->addInstruction(std::unique_ptr<Instruction>(icmpLoop));
                loopCond->addInstruction(std::unique_ptr<Instruction>(brLoop));
                // 添加循环体指令
                loopBody->addInstruction(std::unique_ptr<Instruction>(gepLoop));
                loopBody->addInstruction(std::unique_ptr<Instruction>(storeLoop));
                loopBody->addInstruction(std::unique_ptr<Instruction>(incK));
                loopBody->addInstruction(std::unique_ptr<Instruction>(brToCond));
                // 更新CFG
                // 建立原来ifthen前驱与循环条件的连接
                bb->addSuccessor(loopCond);
                loopCond->addPredecessor(bb);
                // bb跳转到ifthen，即i比maxk+1小
                bb->addSuccessor(minKthen);
                minKthen->addPredecessor(bb);
                // minKthen跳转到loopCond
                loopCond->addPredecessor(minKthen);
                minKthen->addSuccessor(loopCond);
                // 将原来最外层的exit的后继块复制到loopExit
                for (auto *succ : exitBB->getSuccessors())
                {
                    exitBB->removeSuccessor(succ);   // 删除原有后继
                    succ->removePredecessor(exitBB); // 删除原有后继
                    succ->addPredecessor(loopExit);  // 添加到新循环退出块
                    loopExit->addSuccessor(succ);    // 添加后继到循环退出块
                }
                // 更新内部CFG连接
                loopCond->addSuccessor(loopBody);
                loopCond->addSuccessor(loopExit);
                loopExit->addPredecessor(loopCond);
                loopBody->addPredecessor(loopCond);
                loopBody->addSuccessor(loopCond);
                // minKthen放在最前面可以减少一条分支指令
                func->addBasicBlock(unique_ptr<BasicBlock>(minKthen));
                func->addBasicBlock(unique_ptr<BasicBlock>(loopCond));
                func->addBasicBlock(unique_ptr<BasicBlock>(loopBody));
                func->addBasicBlock(unique_ptr<BasicBlock>(loopExit));

                // 删除原来的if链相关基本块
                for (auto *bbToDel : bbsToProcess)
                {
                    bbToDel->removeSelfBasicBlock(); // 删除基本块的CFG连接，便于删除基本块
                }

                changed = true;
                if (verbose)
                    debugInfo << "CFG Simplification: Replace if-chain [" << minK << "," << maxK << "] with loop in " << bb->getName() << "\n";
            }
            bbsToProcess.clear(); // 清空待处理基本块
            break;                // 找到一个匹配就退出当前基本块的检查
        }
        if (changed)
            break; // 如果已经进行了替换，退出函数
    }
    //  删除无用的基本块
    for (auto it = bbs.begin(); it != bbs.end();)
    {
        BasicBlock *bb = it->get();
        if (bb != func->getEntryBlock() && bb->getPredecessors().empty())
        {
            // 这里不能直接删除，把它放到needToDelete中,否则内存空间释放了
            needToDelete.push_back(it->release());
            // 从基本块列表中删除
            it = bbs.erase(it);
        }
        else
        {
            ++it;
        }
    }
    func->setLoops(ControlFlowAnalysis::findLoops(func)); // 重新计算循环
    return changed;
}
bool RemoveUselessWhilePass::runOnFunction(Function *func)
{
    bool changed = false;
    auto &loops = func->getLoops();
    // 允许多次遍历，直到没有可删的无用循环
    bool localChanged;
    do
    {
        localChanged = false;
        for (const auto &loop : loops)
        {
            if (loop.blocks.size() > 2)
                continue;
            bool onlyInc = true;
            BasicBlock *whilecond = nullptr;
            BasicBlock *whilebody = nullptr;
            for (auto *bb : loop.blocks)
            {
                if (bb == loop.header)
                {
                    whilecond = bb;
                    continue;
                }
                whilebody = bb;
                if (bb->getInstructions().size() > 2)
                {
                    onlyInc = false;
                    break;
                }
                for (auto &instPtr : bb->getInstructions())
                {
                    Instruction *inst = instPtr.get();
                    if (auto *bin = dynamic_cast<BinaryOperator *>(inst))
                    {
                        if (!(bin->getOpcode() == Opcode::Add || bin->getOpcode() == Opcode::Sub))
                        {
                            onlyInc = false;
                            break;
                        }
                        if (!loop.IsInductionVar(bin->getLHS()->getName()) && !loop.IsInductionVar(bin->getRHS()->getName()))
                        {
                            onlyInc = false;
                            break;
                        }
                    }
                    if (inst->hasExternalUse(loop))
                    {
                        onlyInc = false;
                        break;
                    }
                }
                if (!onlyInc)
                    break;
            }
            if (!onlyInc)
                continue;

            BasicBlock *prehead = nullptr;
            int count = 0;
            for (auto *pred : loop.header->getPredecessors())
            {
                if (pred == whilebody)
                    continue;
                count++;
                if (count > 1)
                {
                    onlyInc = false;
                    break;
                }
            }
            BasicBlock *exitBlock = nullptr;
            count = 0;
            for (auto *succ : loop.header->getSuccessors())
            {
                if (succ == whilebody)
                    continue;
                exitBlock = succ;
                count++;
                if (count > 1)
                {
                    onlyInc = false;
                    break;
                }
            }
            for (auto *pred : loop.header->getPredecessors())
            {
                if (pred == whilebody)
                    continue;
                for (auto &instPtr : pred->getInstructions())
                {
                    Instruction *inst = instPtr.get();
                    if (auto *br = dynamic_cast<BranchInst *>(inst))
                    {
                        if (br->getTrueBlock() == loop.header)
                            br->setTrueBlock(exitBlock);
                        if (br->getFalseBlock() == loop.header)
                            br->setFalseBlock(exitBlock);
                    }
                    prehead = pred;
                }
                pred->addSuccessor(exitBlock);
                exitBlock->addPredecessor(pred);
            }
            for (auto *bb : loop.blocks)
            {
                bb->removeSelfBasicBlock();
            }
            auto &insts = exitBlock->getInstructions();
            for (auto it = insts.begin(); it != insts.end();)
            {
                if (auto *phi = dynamic_cast<PhiInst *>(it->get()))
                {
                    if (find(phi->getIncomingBlocks().begin(),
                             phi->getIncomingBlocks().end(), loop.header) != phi->getIncomingBlocks().end())
                    {
                        phi->removeThisFromOperands();
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                        continue;
                    }
                }
                ++it;
            }
            localChanged = true;
            changed = true;
            if (verbose)
            {
                debugInfo << "RemoveUselessWhilePass: Removed useless while loop at header " << loop.header->getName() << "\n";
            }
            break; // 只处理一个，后面会重新获取loops
        }
        if (localChanged)
            func->setLoops(ControlFlowAnalysis::findLoops(func));
    } while (localChanged);
    return changed;
}
// 目前只支持整型规约
bool LoopSumReductionPass::runOnFunction(Function *func)
{
    bool changed = false;
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
                    canReduce = false; // 只处理两个phi指令
                    break;             // 只处理两个phi指令
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
    func->setLoops(ControlFlowAnalysis::findLoops(func)); // 重新计算循环
    return changed;
}
bool RemoveOnlyWriteArrayPass::runOnFunction(Function *func)
{
    bool changed = false;
    std::vector<AllocaInst *> arrayAllocas;

    // 1. 收集所有数组 alloca
    for (auto &bb : func->getBasicBlocks())
    {
        for (auto &inst : bb->getInstructions())
        {
            if (auto *alloca = dynamic_cast<AllocaInst *>(inst.get()))
            {
                arrayAllocas.push_back(alloca);
                if (verbose)
                {
                    debugInfo << "RemoveWriteOnlyArrayPass: Found array alloca " << alloca->getName() << " in function " << func->getName() << "\n";
                }
            }
        }
    }

    for (auto *alloca : arrayAllocas)
    {
        bool hasLoadOrCall = false;
        std::unordered_set<Instruction *> relatedInsts;
        std::vector<User *> worklist;
        worklist.push_back(alloca);

        // 1. 全局查找所有 load 和 call 指令
        for (auto &bb : func->getBasicBlocks())
        {
            for (auto &inst : bb->getInstructions())
            {
                // 检查load
                if (auto *load = dynamic_cast<LoadInst *>(inst.get()))
                {
                    Value *origPtr = load->getOriginalPointer();
                    if (origPtr == alloca)
                    {
                        hasLoadOrCall = true;
                        break;
                    }
                }
                // 检查call
                if (auto *call = dynamic_cast<CallInst *>(inst.get()))
                {
                    if (call->HasUsedArray(alloca))
                    {
                        hasLoadOrCall = true;
                        break;
                    }
                }
            }
            if (hasLoadOrCall)
                break;
        }

        // 2. 没有load/call才删除
        if (!hasLoadOrCall)
        {
            // ...原有BFS收集相关指令和删除逻辑...
            relatedInsts.insert(alloca);
            std::vector<User *> worklist;
            worklist.push_back(alloca);

            while (!worklist.empty())
            {
                User *user = worklist.back();
                worklist.pop_back();
                for (auto *u : user->getUsers())
                {
                    if (auto *store = dynamic_cast<StoreInst *>(u))
                    {
                        relatedInsts.insert(store);
                    }
                    else if (auto *gep = dynamic_cast<GetElementPtrInst *>(u))
                    {
                        relatedInsts.insert(gep);
                        worklist.push_back(gep);
                    }
                    else if (auto *bitcast = dynamic_cast<CastInst *>(u))
                    {
                        if (bitcast->getOpcode() != Opcode::BitCast)
                            continue;
                        relatedInsts.insert(bitcast);
                        worklist.push_back(bitcast);
                    }
                }
            }
            // 删除相关指令
            for (auto &bb : func->getBasicBlocks())
            {
                auto &insts = bb->getInstructions();
                for (auto it = insts.begin(); it != insts.end();)
                {
                    auto *inst = it->get();
                    if (relatedInsts.count(inst))
                    {
                        if (verbose)
                        {
                            debugInfo << "RemoveWriteOnlyArrayPass: Removing write-only array instruction " << inst->getName() << " in function " << func->getName() << "\n";
                        }
                        inst->removeThisFromOperands();
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
            changed = true;
        }
    }
    return changed;
}

// ========== 优化管道工厂 ==========
std::unique_ptr<PassManager> optimization::createOptimizationPipeline(OptimizationLevel level, bool verbose)
{
    auto pm = std::make_unique<PassManager>(verbose);

    if (level == OptimizationLevel::O0)
    {
        pm->addPass(std::make_unique<CFGSimplificationPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<ArrayEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveOnlyWriteArrayPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveUselessWhilePass>(verbose));
        pm->addPass(std::make_unique<LoopSumReductionPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPToBitCastPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        pm->addPass(std::make_unique<StrengthReductionPass>(verbose));
    }
    else if (level == OptimizationLevel::O1)
    {
        pm->addPass(std::make_unique<CFGSimplificationPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<ArrayEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveOnlyWriteArrayPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveUselessWhilePass>(verbose));
        pm->addPass(std::make_unique<LoopSumReductionPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPToBitCastPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        pm->addPass(std::make_unique<StrengthReductionPass>(verbose));
    }
    else if (level == OptimizationLevel::O2)
    {
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
    }
    // 测试优化
    else if (level == OptimizationLevel::O15)
    {
        pm->addPass(std::make_unique<CFGSimplificationPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<ArrayEliminationPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveUselessWhilePass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
    }
    // 测试先遣版优化级别(最激进优化级别)
    else if (level == OptimizationLevel::O16)
    {
        // 先简化CFG，然后函数内联后可以暴露更多优化机会:删除数组，优化后再删除无用循环
        pm->addPass(std::make_unique<CFGSimplificationPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<ArrayEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveOnlyWriteArrayPass>(verbose));
        // 消除数组消除pass后留下的gep指令，便于无用while消除
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // 删除无用的while循环后必须进行死代码消除
        pm->addPass(std::make_unique<RemoveUselessWhilePass>(verbose));
        pm->addPass(std::make_unique<LoopSumReductionPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPToBitCastPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        // phi指令限制了循环不变量外提，所以必须先消除phi指令
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        pm->addPass(std::make_unique<StrengthReductionPass>(verbose));
    }
    return pm;
}