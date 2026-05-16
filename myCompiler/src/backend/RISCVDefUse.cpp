#include "RISCVDataStructure.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <functional>
#include <algorithm>

using namespace RISCV;

int RISCVFunction::getInstructionIndex(shared_ptr<RISCVInstruction> instr) const
{
    for (int i = 0; i < (int)instrList.size(); ++i)
    {
        if (instrList[i] == instr)
            return i;
    }
    return -1;
}

void RISCVFunction::buildDefUseChains()
{
    instrUsers.clear();
    instrUsedDefs.clear();
    instrList.clear();
    instrBBList.clear();

    // 收集函数内所有指令并建立全局索引
    for (const auto &bb : basicBlocks)
    {
        for (const auto &instr : bb->getInstructions())
        {
            instrList.push_back(instr);
            instrBBList.push_back(bb);
        }
    }

    int N = static_cast<int>(instrList.size());
    // 为每个寄存器记录最新定义位置，初始化为空
    unordered_map<shared_ptr<RISCVRegister>, int, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> lastDef;

    for (int i = 0; i < N; ++i)
    {
        auto instr = instrList[i];
        if (!instr)
            continue;

        // 处理 use：对于 instr 使用的每个寄存器，若存在上一个定义位置，将该定义位置记录为此 use 的被使用 def
        auto uses = instr->getUseRegisters();
        for (auto &ureg : uses)
        {
            auto it = lastDef.find(ureg);
            if (it != lastDef.end())
            {
                int defIdx = it->second;
                instrUsedDefs[i].push_back(defIdx);
                instrUsers[defIdx].push_back(i);
            }
            else
            {
                // 没有前序定义，跳过（可能是参数或物理寄存器）
            }
        }

        // 处理 def：对于 instr 定义的寄存器，更新 lastDef
        auto defs = instr->getDefRegisters();
        for (auto &dreg : defs)
        {
            lastDef[dreg] = i;
        }
    }

    // 构建完 def-use 后计算支配信息（使用 Lengauer-Tarjan 算法）
    computeDominators();
}

shared_ptr<RISCVInstruction> RISCVFunction::getInstructionByIndex(int idx) const
{
    if (idx < 0 || idx >= static_cast<int>(instrList.size()))
        return nullptr;
    return instrList[idx];
}

shared_ptr<RISCVBasicBlock> RISCVFunction::getInstructionBB(int instrIdx) const
{
    if (instrIdx < 0 || instrIdx >= static_cast<int>(instrBBList.size()))
        return nullptr;
    return instrBBList[instrIdx];
}

// 使用 Lengauer-Tarjan 算法计算支配树并构建支配集合
void RISCVFunction::computeDominators()
{
    dominatorSets.clear();
    if (basicBlocks.empty())
        return;

    // DFS 编号（只对入口可达节点）
    unordered_map<shared_ptr<RISCVBasicBlock>, int> dfsNum;
    vector<shared_ptr<RISCVBasicBlock>> vertex(1); // 1-based
    unordered_map<shared_ptr<RISCVBasicBlock>, shared_ptr<RISCVBasicBlock>> parentNode;

    int N = 0;
    std::function<void(shared_ptr<RISCVBasicBlock>)> dfs = [&](shared_ptr<RISCVBasicBlock> u) {
        dfsNum[u] = ++N;
        vertex.push_back(u);
        for (auto &v : u->getSuccessors())
        {
            if (!v)
                continue;
            if (!dfsNum.count(v))
            {
                parentNode[v] = u;
                dfs(v);
            }
        }
    };

    auto entry = basicBlocks.front();
    dfs(entry);

    if (N == 0)
        return;

    // 映射 predecessor 列表到 DFS 编号
    vector<vector<int>> preds(N + 1);
    for (int i = 1; i <= N; ++i)
    {
        auto bb = vertex[i];
        for (auto &p : bb->getPredecessors())
        {
            if (dfsNum.count(p))
                preds[i].push_back(dfsNum[p]);
        }
    }

    vector<int> semi(N + 1), idom(N + 1), ancestor(N + 1, 0), parent(N + 1, 0), label(N + 1, 0);
    vector<vector<int>> bucket(N + 1);

    for (int i = 1; i <= N; ++i)
    {
        semi[i] = i;
        label[i] = i;
    }

    for (int i = 1; i <= N; ++i)
    {
        if (parentNode.count(vertex[i]))
            parent[i] = dfsNum[parentNode[vertex[i]]];
        else
            parent[i] = 0;
    }

    std::function<void(int,int)> link = [&](int v, int w) {
        ancestor[w] = v;
    };

    std::function<int(int)> eval = [&](int v) -> int {
        if (ancestor[v] == 0)
            return v;
        // path compression
        vector<int> stack;
        int x = v;
        while (ancestor[x] != 0)
        {
            stack.push_back(x);
            x = ancestor[x];
        }
        // compress
        for (int i = (int)stack.size() - 1; i >= 0; --i)
        {
            int node = stack[i];
            if (semi[label[ancestor[node]]] < semi[label[node]])
                label[node] = label[ancestor[node]];
            ancestor[node] = x;
        }
        return label[v];
    };

    // main loop
    for (int w = N; w >= 2; --w)
    {
        for (int v : preds[w])
        {
            int u = eval(v);
            semi[w] = std::min(semi[w], semi[u]);
        }
        bucket[semi[w]].push_back(w);
        link(parent[w], w);
        for (int v : bucket[parent[w]])
        {
            int u = eval(v);
            idom[v] = (semi[u] < semi[v]) ? u : parent[w];
        }
        bucket[parent[w]].clear();
    }

    // finalize
    for (int w = 2; w <= N; ++w)
    {
        if (idom[w] != semi[w])
            idom[w] = idom[idom[w]];
    }
    idom[1] = 0;

    // 构建 dominatorSets：对每个可达节点，沿 idom 链收集所有支配节点
    for (int i = 1; i <= N; ++i)
    {
        unordered_set<shared_ptr<RISCVBasicBlock>> s;
        int cur = i;
        while (cur != 0)
        {
            s.insert(vertex[cur]);
            cur = idom[cur];
        }
        dominatorSets[vertex[i]] = std::move(s);
    }
}

bool RISCVFunction::dominates(shared_ptr<RISCVBasicBlock> dom, shared_ptr<RISCVBasicBlock> node) const
{
    if (!dom || !node)
        return false;
    auto it = dominatorSets.find(node);
    if (it == dominatorSets.end())
        return false;
    return it->second.count(dom) > 0;
}
