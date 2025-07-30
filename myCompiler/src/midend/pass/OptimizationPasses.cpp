#include "OptimizationPasses.h"
#include "Lengauer-Tarjan.h"
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
    }
    // 删除所有标记为删除的函数
    module->Functions.erase(
        std::remove_if(
            module->Functions.begin(),
            module->Functions.end(),
            [](const auto &func)
            { return func->isDeletedFunction(); }),
        module->Functions.end());
    return changed;
}
std::string PassManager::toString() const
{
    std::stringstream ss;
    for (const auto &pass : passes)
    {
        ss << pass->getName() << ": \n"
           << pass->toString() << "\n";
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
                needToDelete.push_back(it->release());
                it = insts.erase(it);
                changed = true;
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
    idom.clear();
    idom = computeIDom_LengauerTarjan(func);
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
                    //  load特判
                    //  如果查到的load在本load之前的块则跳过(load仅支持同基本块消除)
                    if (inst->getOpcode() == Opcode::Load && defBB != bb.get())
                    {
                        ++it;
                        continue;
                    }
                    if (defBB == bb.get() || dominates(defBB, bb.get()))
                    {
                        inst->replaceAllUsesWith(found->second.first);
                        if (verbose)
                        {
                            debugInfo << inst->toString() << " replaced with "
                                      << found->second.first->toString() << " in "
                                      << bb->getName() << endl;
                        }
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
    for (auto *v : inst->getOperands())
    {
        if (auto *ci = dynamic_cast<ConstantInt *>(v))
        {
            ops.push_back("int:" + std::to_string(ci->Value));
        }
        else if (auto *cf = dynamic_cast<ConstantFloat *>(v))
        {
            ops.push_back("float:" + std::to_string(cf->Value));
        }
        else
        {
            ops.push_back("var:" + v->getName());
        }
    }
    return {inst->getOpcodeName(), ops};
}
// 判断指令是否可以作为公共子表达式
bool CommonSubexpressionEliminationPass::canBeCommonSubexpression(Instruction *inst, BasicBlock *bb)
{
    if (inst->getOpcode() == Opcode::Load)
    {
        return CanLoadCSE(inst, bb);
    }
    // 如果有phi作为操作数，不做CSE，因为此时变量依赖合流，不同位置的值可能不一样
    for (auto *v : inst->getOperands())
    {
        if (dynamic_cast<PhiInst *>(v))
        {
            return false;
        }
    }
    // 只处理无副作用的二元运算和getelementptr,不包括Store Call Ret Br
    return (inst->isBinaryOp() || inst->getOpcode() == Opcode::GetElementPtr) && !inst->mayHaveSideEffects();
}
// 返回每个BB的直接支配者
std::unordered_map<BasicBlock *, BasicBlock *>
CommonSubexpressionEliminationPass::computeIDom_LengauerTarjan(Function *func)
{
    auto &bbs = func->getBasicBlocks();
    if (bbs.empty())
        return {};

    LTContext ctx;
    BasicBlock *entry = bbs[0].get();
    ctx.N = 0;
    dfsLT(entry, ctx);

    std::vector<BasicBlock *> vertex = ctx.vertex;
    int n = vertex.size();
    std::unordered_map<BasicBlock *, BasicBlock *> idom;

    // 1. 计算semi-dominator
    for (int i = n - 1; i >= 1; --i)
    {
        BasicBlock *w = vertex[i];
        for (auto *v : ctx.pred[w])
        {
            BasicBlock *u = eval(v, ctx);
            if (ctx.semi[u] < ctx.semi[w])
                ctx.semi[w] = ctx.semi[u];
        }
        ctx.idom[w] = vertex[ctx.semi[w] - 1]; // 初始设置为 semi[w] 的节点
        ctx.bucket[vertex[ctx.semi[w] - 1]].push_back(w);
        ctx.ancestor[w] = ctx.parent[w];
        if (ctx.parent[w])
        { // 防止入口块 parent 为 nullptr
            for (auto *v : ctx.bucket[ctx.parent[w]])
            {
                BasicBlock *u = eval(v, ctx);
                ctx.idom[v] = (ctx.semi[u] < ctx.semi[v]) ? u : ctx.parent[w];
            }
            ctx.bucket[ctx.parent[w]].clear();
        }
    }
    // 2. 显式计算idom
    for (int i = 1; i < n; ++i)
    {
        BasicBlock *w = vertex[i];
        if (ctx.idom[w] != vertex[ctx.semi[w] - 1])
            ctx.idom[w] = ctx.idom[ctx.idom[w]];
    }
    ctx.idom[entry] = nullptr;

    // 3. 输出
    std::unordered_map<BasicBlock *, BasicBlock *> result;
    for (auto &p : ctx.idom)
        if (p.first)
            result[p.first] = p.second;
    return result;
}
// 判断storeBB是否支配loadBB（所有到loadBB的路径都经过storeBB）
bool CommonSubexpressionEliminationPass::dominates(BasicBlock *dom, BasicBlock *node)
{
    if (dom == node)
        return true;
    while (node && idom.count(node))
    {
        node = idom[node];
        if (node == dom)
            return true;
    }
    return false;
}
// 修改load指令CSE处理，跨基本块暂时不做，难度太高
bool CommonSubexpressionEliminationPass::CanLoadCSE(Instruction *inst, BasicBlock *bb)
{
    // 只允许同一基本块内的load做CSE，且store和load之间没有其他store
    auto *loadInst = dynamic_cast<LoadInst *>(inst);
    if (!loadInst)
        return false;
    Value *addr = loadInst->getPointer();
    if (!addr)
        return false;

    int loadPos = bb->getInstructionOrder(inst);
    int lastStorePos = -1;
    int storeCount = 0;
    auto &insts = bb->getInstructions();
    for (size_t i = 0; i < insts.size(); ++i)
    {
        if (auto *store = dynamic_cast<StoreInst *>(insts[i].get()))
        {
            if (store->getPointer() == addr)
            {
                storeCount++;
                lastStorePos = i;
            }
        }
    }
    // 只允许唯一一次store，且store在load之前
    return storeCount == 1 && lastStorePos < loadPos;
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
        auto loops = findLoops(func);
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
                            canMoveToPreheader(inst) && isLoopInvariant(inst, loop))
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
bool LoopInvariantCodeMotionPass::canMoveToPreheader(Instruction *inst)
{
    // 修改这里 phi指令不外提
    // 且操作数是phi指令也不能外提
    // copy指令不能外提，因为是由合流产生
    return !inst->mayHaveSideEffects() && inst->getOpcode() != Opcode::Load && inst->getOpcode() != Opcode::Copy;
}

// 辅助：DFS遍历，记录访问顺序和父节点
void dfs(BasicBlock *bb, std::unordered_map<BasicBlock *, int> &dfn, vector<BasicBlock *> &order, int &idx,
         std::unordered_map<BasicBlock *, int> &inStack, std::vector<std::pair<BasicBlock *, BasicBlock *>> &backedges)
{
    dfn[bb] = idx++;
    inStack[bb] = 1;
    order.push_back(bb);
    for (auto *succ : bb->getSuccessors())
    {
        if (!dfn.count(succ))
        {
            dfs(succ, dfn, order, idx, inStack, backedges);
        }
        else if (inStack[succ]) // succ在递归栈上，说明是回边
        {
            backedges.push_back({bb, succ});
        }
    }
    inStack[bb] = 0;
}

// 查找所有自然循环（基于回边）
vector<LoopInvariantCodeMotionPass::Loop> LoopInvariantCodeMotionPass::findLoops(Function *func)
{
    vector<Loop> loops;
    auto &bbs = func->getBasicBlocks();
    if (bbs.empty())
        return loops;

    // 1. DFS遍历，记录访问顺序和回边
    std::unordered_map<BasicBlock *, int> dfn, inStack;
    vector<BasicBlock *> order;
    int idx = 0;
    std::vector<std::pair<BasicBlock *, BasicBlock *>> backedges;
    dfs(bbs[0].get(), dfn, order, idx, inStack, backedges);

    // 2. 按循环头分组所有回边
    std::unordered_map<BasicBlock *, std::vector<BasicBlock *>> headerToBackedges;
    for (auto &[from, to] : backedges)
    {
        headerToBackedges[to].push_back(from);
    }

    // 3. 对每个循环头，合并所有回边，收集完整循环体
    for (auto &[header, backedges] : headerToBackedges)
    {
        std::unordered_set<BasicBlock *> loopBlocks;
        std::stack<BasicBlock *> stk;
        loopBlocks.insert(header);
        for (auto *from : backedges)
        {
            if (loopBlocks.insert(from).second)
                stk.push(from);
        }
        while (!stk.empty())
        {
            BasicBlock *cur = stk.top();
            stk.pop();
            for (auto *pred : cur->getPredecessors())
            {
                if (!loopBlocks.count(pred))
                {
                    loopBlocks.insert(pred);
                    stk.push(pred);
                }
            }
        }
        // 关键：循环头必须有前驱在循环体外，才是真正的循环头
        bool hasOutsidePred = false;
        for (auto *pred : header->getPredecessors())
        {
            if (loopBlocks.find(pred) == loopBlocks.end())
            {
                hasOutsidePred = true;
                break;
            }
        }
        if (!hasOutsidePred)
            continue; // 跳过伪循环头

        // 去重
        bool duplicate = false;
        for (auto &l : loops)
        {
            std::set<BasicBlock *> s1(loopBlocks.begin(), loopBlocks.end());
            std::set<BasicBlock *> s2(l.blocks.begin(), l.blocks.end());
            if (l.header == header && s1 == s2)
            {
                duplicate = true;
                break;
            }
        }
        if (!duplicate)
        {
            Loop loop;
            loop.header = header;
            loop.blocks.assign(loopBlocks.begin(), loopBlocks.end());
            loops.push_back(loop);
        }
    }
    // 调试输出
    if (verbose)
    {
        debugInfo << "Found " << loops.size() << " loops in function " << func->getName() << ":\n";
        for (auto &loop : loops)
        {
            debugInfo << "Loop Header: " << loop.header->getName() << ", Blocks: ";
            for (auto *bb : loop.blocks)
                debugInfo << bb->getName() << " ";
            debugInfo << "\n";
        }
    }
    return loops;
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
    return changed;
}
// 判断是否适合内联
bool FunctionInliningPass::shouldInline(Function *callee)
{

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
    // 不内联递归/库函数/过大函数/控制流复杂
    if (callee->isLibraryFunction() || callee->isRecursive() || callee->getInstructionCount() > 20 || callee->getBasicBlocks().size() > 5)
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
    string suffix = getsuffix();

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
                if (gep->getNumOperands() < 5)
                {
                    // 如果GEP指令的操作数少于5个，直接跳过
                    ++it;
                    continue;
                }
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
// ========== 优化管道工厂 ==========
std::unique_ptr<PassManager> optimization::createOptimizationPipeline(OptimizationLevel level, bool verbose)
{
    auto pm = std::make_unique<PassManager>(verbose);

    if (level == OptimizationLevel::O0)
    {
        // pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // // 消除phi
        // pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
    }
    else if (level == OptimizationLevel::O1)
    {
        // pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        // pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        // pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        // pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        // pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        // pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
    }
    else if (level == OptimizationLevel::O2)
    {
        // 消除phi
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
    }
    // 以下为调试内容
    else if (level == OptimizationLevel::O10)
    {
        // 函数内联会产生phi指令
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
    }
    else if (level == OptimizationLevel::O11)
    {
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
    }
    else if (level == OptimizationLevel::O12)
    {
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
    }
    else if (level == OptimizationLevel::O13)
    {
        // 必须要先消除phi才能进行循环不变量外提
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
    }
    else if (level == OptimizationLevel::O14)
    {
        // pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        // pm->addPass(std::make_unique<LiveVariableAnalysisPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        //pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        //pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
    }
    else if (level == OptimizationLevel::O15)
    {
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
    }
    else if (level == OptimizationLevel::O16)
    {
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
    }
    return pm;
}