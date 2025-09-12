#include "ControlFlowAnalysis.h"
#include <stack>
using namespace optimization;
unordered_map<BasicBlock *, BasicBlock *> ControlFlowAnalysis::analyze(Function *func)
{
    // 1. 使用Lengauer-Tarjan算法计算支配树
    return computeIDom_LengauerTarjan(func);
}
bool ControlFlowAnalysis::dominates(unordered_map<BasicBlock *, BasicBlock *> idom, BasicBlock *dom, BasicBlock *node)
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
void ControlFlowAnalysis::dfs(BasicBlock *bb, std::unordered_map<BasicBlock *, int> &dfn, vector<BasicBlock *> &order, int &idx,
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
vector<Loop> ControlFlowAnalysis::findLoops(Function *func)
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

            // 构造exit块
            std::unordered_set<BasicBlock *> exitBlocks;
            for (auto *bb : header->getSuccessors())
            {
                if (loopBlocks.count(bb) == 0)
                {
                    exitBlocks.insert(bb);
                }
            }
            loop.exits.assign(exitBlocks.begin(), exitBlocks.end());

            loops.push_back(loop);
        }
    }
    return loops;
}
// 用于消除数组使用
// 判断从 startBB 到 endBB 的所有路径上是否有其它 store 到 arr
bool ControlFlowAnalysis::hasStoreOnPath(BasicBlock *startBB, BasicBlock *endBB, Value *arr)
{
    std::unordered_set<BasicBlock *> visited;
    std::stack<BasicBlock *> stk;
    // 从 startBB 开始遍历所有后继
    visited.insert(startBB);
    for (auto *succ : startBB->getSuccessors())
    {
        stk.push(succ);
    }
    // 查找startBB是否还有其他store
    // 从 startBB 开始遍历所有后继
    while (!stk.empty())
    {
        BasicBlock *cur = stk.top();
        stk.pop();
        if (cur == endBB)
            continue;
        if (visited.count(cur))
            continue;
        visited.insert(cur);

        // 检查当前基本块是否有 store 到 arr
        for (auto &instPtr : cur->getInstructions())
        {
            auto *store = dynamic_cast<StoreInst *>(instPtr.get());
            if (store)
            {
                auto *originalPtr = store->getOriginalPointer();
                if (isSameAddr(originalPtr, arr))
                {
                    return true; // 找到 store 到 arr，返回 true
                }
            }
            auto *call = dynamic_cast<CallInst *>(instPtr.get());
            if (call && call->HasModifiedArray(arr))
            {
                return true; // 找到调用函数修改了数组，返回 true
            }
        }
        // 遍历所有后继
        for (auto *succ : cur->getSuccessors())
        {
            stk.push(succ);
        }
    }
    return false;
}
// 用于CSE
bool ControlFlowAnalysis::hasStoreOnPath(BasicBlock *startBB, BasicBlock *endBB, Value *arr, Value *inst1, Value *inst2)
{
    auto loops = ControlFlowAnalysis::findLoops(startBB->Parent);
    std::unordered_set<BasicBlock *> visited;
    std::stack<BasicBlock *> stk;
    // 从 startBB 开始遍历所有后继
    visited.insert(startBB);
    for (auto *succ : startBB->getSuccessors())
    {
        stk.push(succ);
    }
    // 查找startBB的load之后以及endBB的load之前是否还有其他store
    for (auto &inst : startBB->getInstructions())
    {
        auto *store = dynamic_cast<StoreInst *>(inst.get());
        if (store)
        {
            auto *originalPtr = store->getOriginalPointer();
            if (isSameAddr(originalPtr, arr))
            {
                // 检查inst1是否在store之前
                int pos1 = startBB->getInstructionOrder(dynamic_cast<Instruction *>(inst1));
                int pos2 = startBB->getInstructionOrder(dynamic_cast<Instruction *>(store));
                if (pos2 > pos1)
                {
                    return true; // 在路径上
                }
                if (pos2 < pos1)
                {
                    // 如果pos2<pos1且startBB在循环中也返回true
                    for (auto &loop : loops)
                    {
                        if (loop.containsInBody(dynamic_cast<Instruction *>(inst1)))
                        {
                            return true;
                        }
                    }
                }
            }
        }
        auto *call = dynamic_cast<CallInst *>(inst.get());
        if (call && call->HasModifiedArray(arr))
        {
            // 检查inst1是否在store之前
            int pos1 = startBB->getInstructionOrder(dynamic_cast<Instruction *>(inst1));
            int pos2 = startBB->getInstructionOrder(dynamic_cast<Instruction *>(call));
            if (pos2 > pos1)
            {
                return true; // 在路径上
            }
            if (pos2 < pos1)
            {
                // 如果pos2<pos1且startBB在循环中也返回true
                for (auto &loop : loops)
                {
                    if (loop.containsInBody(dynamic_cast<Instruction *>(inst1)))
                    {
                        return true;
                    }
                }
            }
        }
    }
    for (auto &inst : endBB->getInstructions())
    {
        auto *store = dynamic_cast<StoreInst *>(inst.get());
        if (store)
        {
            auto *originalPtr = store->getOriginalPointer();
            if (isSameAddr(originalPtr, arr))
            {
                // 检查inst2是否在store之后
                int pos1 = endBB->getInstructionOrder(dynamic_cast<Instruction *>(inst2));
                int pos2 = endBB->getInstructionOrder(dynamic_cast<Instruction *>(store));
                if (pos2 < pos1)
                {
                    return true; // 在路径上
                }
                // 如果pos2>pos1且endBB在循环中也返回true
                if (pos2 > pos1)
                {
                    for (auto &loop : loops)
                    {
                        if (loop.containsInBody(dynamic_cast<Instruction *>(inst2)))
                        {
                            return true;
                        }
                    }
                }
            }
        }
        auto *call = dynamic_cast<CallInst *>(inst.get());
        if (call && call->HasModifiedArray(arr))
        {
            // 检查inst2是否在call之前
            int pos1 = startBB->getInstructionOrder(dynamic_cast<Instruction *>(inst2));
            int pos2 = startBB->getInstructionOrder(dynamic_cast<Instruction *>(call));
            if (pos2 < pos1)
            {
                return true; // 在路径上
            }
            if (pos2 > pos1)
            {
                // 如果pos2<pos1且startBB在循环中也返回true
                for (auto &loop : loops)
                {
                    if (loop.containsInBody(dynamic_cast<Instruction *>(inst2)))
                    {
                        return true;
                    }
                }
            }
        }
    }
    // 如果endBB是循环块，则要检测循环体内是否有对arr的store
    for (auto &loop : loops)
    {
        if (loop.containsBlock(endBB) || loop.containsBlock(startBB))
        {
            for (auto *bb : loop.blocks)
            {
                if (bb == endBB || bb == startBB)
                    continue; // 跳过自生
                // 检查循环体内是否有对arr的store
                for (auto &inst3 : bb->getInstructions())
                {
                    auto *store = dynamic_cast<StoreInst *>(inst3.get());
                    if (store)
                    {
                        auto *originalPtr = store->getOriginalPointer();
                        if (isSameAddr(originalPtr, arr))
                        {
                            return true; // 找到 store 到 arr，返回 true
                        }
                    }
                    auto *call = dynamic_cast<CallInst *>(inst3.get());
                    if (call && call->HasModifiedArray(arr))
                    {
                        return true; // 找到调用函数修改了数组，返回 true
                    }
                }
            }
        }
    }
    // 从 startBB 开始遍历所有后继
    while (!stk.empty())
    {
        BasicBlock *cur = stk.top();
        stk.pop();
        if (cur == endBB)
            continue;
        if (visited.count(cur))
            continue;
        visited.insert(cur);

        // 检查当前基本块是否有 store 到 arr
        for (auto &instPtr : cur->getInstructions())
        {
            auto *store = dynamic_cast<StoreInst *>(instPtr.get());
            if (store)
            {
                auto *originalPtr = store->getOriginalPointer();
                if (isSameAddr(originalPtr, arr))
                {
                    return true; // 找到 store 到 arr，返回 true
                }
            }
            auto *call = dynamic_cast<CallInst *>(instPtr.get());
            if (call && call->HasModifiedArray(arr))
            {
                return true; // 找到调用函数修改了数组，返回 true
            }
        }
        // 遍历所有后继
        for (auto *succ : cur->getSuccessors())
        {
            stk.push(succ);
        }
    }
    return false;
}
bool ControlFlowAnalysis::hasPhiInputOnPath(BasicBlock *startBB, BasicBlock *endBB, Value *phi, Value *inst1, Value *inst2)
{
    std::unordered_set<BasicBlock *> visited;
    std::stack<BasicBlock *> stk;
    auto *phiInst = dynamic_cast<PhiInst *>(phi);
    auto loops = ControlFlowAnalysis::findLoops(startBB->Parent);
    // 从 startBB 开始遍历所有后继
    visited.insert(startBB);
    for (auto *succ : startBB->getSuccessors())
    {
        stk.push(succ);
    }
    // 查找是否在startBB有输入或者在endBB有输入
    for (int i = 0; i < phiInst->getNumIncomingValues(); i++)
    {
        if (phiInst->getIncomingBlock(i) == startBB)
        {
            // 如果有输入则比较定义是否在使用前面,即判断是否在inst1后面，如果在后面则算在路径上
            int pos1 = startBB->getInstructionOrder(dynamic_cast<Instruction *>(inst1));
            int pos2 = startBB->getInstructionOrder(dynamic_cast<Instruction *>(phiInst->getIncomingValue(i)));
            if (pos2 > pos1)
            {
                return true; // 在路径上
            }
            if (pos2 < pos1)
            {
                // 如果pos2<pos1且startBB在循环中也返回true
                for (auto &loop : loops)
                {
                    if (loop.containsInBody(dynamic_cast<Instruction *>(inst1)))
                    {
                        return true;
                    }
                }
            }
        }
        else if (phiInst->getIncomingBlock(i) == endBB)
        {
            // 如果有输入则比较定义是否在使用前面,即判断是否在inst2前面，如果在前面则算在路径上
            int pos1 = endBB->getInstructionOrder(dynamic_cast<Instruction *>(inst2));
            int pos2 = endBB->getInstructionOrder(dynamic_cast<Instruction *>(phiInst->getIncomingValue(i)));
            if (pos2 < pos1)
            {
                return true; // 在路径上
            }
            // 如果pos2>pos1且在循环中也返回true
            if (pos2 > pos1)
            {
                for (auto loop : loops)
                {
                    if (loop.containsInBody(dynamic_cast<Instruction *>(phiInst->getIncomingValue(i))))
                    {
                        return true; // 在循环中
                    }
                }
            }
        }
    }
    // 如果endBB是循环头，则要检测循环体内是否有对arr的store
    for (auto &loop : loops)
    {
        if (endBB == loop.header)
        {
            for (auto *bb : loop.blocks)
            {
                if (bb == loop.header)
                    continue; // 跳过循环头
                for (auto *phiblock : phiInst->getIncomingBlocks())
                {
                    if (bb == phiblock)
                        return true; // 如果循环体内有phi输入，则返回true
                }
            }
        }
    }
    // 从 startBB 开始遍历所有后继
    while (!stk.empty())
    {
        BasicBlock *cur = stk.top();
        stk.pop();
        if (cur == endBB)
            continue;
        if (visited.count(cur))
            continue;
        visited.insert(cur);

        // 检查当前基本块是否有 phi 输入
        for (auto *bb : phiInst->getIncomingBlocks())
        {
            if (bb == cur)
            {
                return true;
            }
        }
        // 遍历所有后继
        for (auto *succ : cur->getSuccessors())
        {
            stk.push(succ);
        }
    }
    return false;
}