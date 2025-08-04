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
            loops.push_back(loop);
        }
    }
    return loops;
}
// 判断从 startBB 到 endBB 的所有路径上是否有其它 store 到 arr
bool ControlFlowAnalysis::hasStoreOnPath(BasicBlock *startBB, BasicBlock *endBB, Value *arr)
{
    std::unordered_set<BasicBlock *> visited;
    std::stack<BasicBlock *> stk;
    // 从 startBB 开始遍历所有后继
    visited.insert(startBB);
    for(auto *succ : startBB->getSuccessors())
    {
        stk.push(succ);
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
        }
        // 遍历所有后继
        for (auto *succ : cur->getSuccessors())
        {
            stk.push(succ);
        }
    }
    return false;
}