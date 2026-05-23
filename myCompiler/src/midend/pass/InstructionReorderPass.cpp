#include "InstructionReorderPass.h"
#include <algorithm>
#include <queue>
#include <unordered_map>
#include <vector>
using namespace std;
using namespace optimization;

namespace
{
    bool isPhiOrTerminator(Instruction *inst)
    {
        return inst->getOpcode() == Opcode::Phi || inst->isTerminator();
    }

    // 锚点：call / 有副作用 / alloca 等；load 仅通过内存依赖边约束顺序
    bool isAnchor(Instruction *inst)
    {
        if (isPhiOrTerminator(inst))
        {
            return true;
        }
        Opcode op = inst->getOpcode();
        if (op == Opcode::Call)
        {
            return true;
        }
        return inst->mayHaveSideEffects() || op == Opcode::StorePair;
    }

    bool isSchedulable(Instruction *inst)
    {
        return !isAnchor(inst);
    }

    int schedulePriority(Instruction *inst, const vector<Instruction *> &body,
                         const unordered_map<Instruction *, int> &indexOf)
    {
        if (inst->getOpcode() == Opcode::GetElementPtr)
        {
            return 0;
        }
        for (auto *other : body)
        {
            if (!other || other == inst || other->getOpcode() != Opcode::Load)
            {
                continue;
            }
            for (Value *op : other->getOperands())
            {
                if (op == inst)
                {
                    return 1;
                }
            }
        }
        return 100 + indexOf.at(inst);
    }

    bool reorderBasicBlock(BasicBlock *bb, bool verbose, stringstream &debugInfo)
    {
        auto &insts = bb->getInstructions();
        if (insts.size() <= 2)
        {
            return false;
        }

        vector<unique_ptr<Instruction>> phiInsts;
        vector<unique_ptr<Instruction>> termInsts;
        vector<unique_ptr<Instruction>> bodyInsts;

        for (auto &instPtr : insts)
        {
            Instruction *inst = instPtr.get();
            if (!inst)
            {
                continue;
            }
            if (inst->getOpcode() == Opcode::Phi)
            {
                phiInsts.push_back(std::move(instPtr));
            }
            else if (inst->isTerminator())
            {
                termInsts.push_back(std::move(instPtr));
            }
            else
            {
                bodyInsts.push_back(std::move(instPtr));
            }
        }

        if (bodyInsts.size() <= 1)
        {
            insts.clear();
            for (auto &p : phiInsts)
            {
                insts.push_back(std::move(p));
            }
            for (auto &p : bodyInsts)
            {
                insts.push_back(std::move(p));
            }
            for (auto &p : termInsts)
            {
                insts.push_back(std::move(p));
            }
            return false;
        }

        vector<Instruction *> body;
        body.reserve(bodyInsts.size());
        unordered_map<Instruction *, int> indexOf;
        for (size_t i = 0; i < bodyInsts.size(); ++i)
        {
            body.push_back(bodyInsts[i].get());
            indexOf[bodyInsts[i].get()] = static_cast<int>(i);
        }

        const int n = static_cast<int>(body.size());
        vector<vector<int>> succ(n);
        vector<int> inDegree(n, 0);
        vector<vector<char>> hasEdge(n, vector<char>(n, 0));

        auto addEdge = [&](int from, int to)
        {
            if (from == to || from < 0 || to < 0 || from >= n || to >= n)
            {
                return;
            }
            if (hasEdge[from][to])
            {
                return;
            }
            hasEdge[from][to] = 1;
            succ[from].push_back(to);
            inDegree[to]++;
        };

        unordered_map<Value *, Instruction *> defInBlock;
        for (Instruction *inst : body)
        {
            if (inst->hasResult())
            {
                defInBlock[inst] = inst;
            }
        }

        // 数据依赖
        for (int i = 0; i < n; ++i)
        {
            Instruction *inst = body[i];
            for (Value *op : inst->getOperands())
            {
                auto it = defInBlock.find(op);
                if (it != defInBlock.end())
                {
                    addEdge(indexOf[it->second], i);
                }
            }
        }

        // 锚点之间保持原有相对顺序；纯计算可在无数据/内存依赖时越过锚点
        vector<int> anchorIdx;
        for (int i = 0; i < n; ++i)
        {
            if (isAnchor(body[i]))
            {
                anchorIdx.push_back(i);
            }
        }
        for (size_t k = 1; k < anchorIdx.size(); ++k)
        {
            addEdge(anchorIdx[k - 1], anchorIdx[k]);
        }

        // 内存顺序：store 不能晚于其后的 load；store 之间、load-store 保持原序
        for (int i = 0; i < n; ++i)
        {
            Opcode opi = body[i]->getOpcode();
            if (opi != Opcode::Store && opi != Opcode::Stored && opi != Opcode::StorePair)
            {
                continue;
            }
            for (int j = i + 1; j < n; ++j)
            {
                Opcode opj = body[j]->getOpcode();
                if (opj == Opcode::Load || opj == Opcode::Store || opj == Opcode::Stored ||
                    opj == Opcode::StorePair)
                {
                    addEdge(i, j);
                }
            }
        }
        for (int i = 0; i < n; ++i)
        {
            if (body[i]->getOpcode() != Opcode::Load)
            {
                continue;
            }
            for (int j = i + 1; j < n; ++j)
            {
                Opcode opj = body[j]->getOpcode();
                if (opj == Opcode::Store || opj == Opcode::Stored || opj == Opcode::StorePair)
                {
                    addEdge(i, j);
                }
            }
        }

        auto cmpReady = [&](int a, int b)
        {
            int pa = schedulePriority(body[a], body, indexOf);
            int pb = schedulePriority(body[b], body, indexOf);
            if (pa != pb)
            {
                return pa < pb;
            }
            return a < b;
        };

        vector<int> order;
        order.reserve(n);
        priority_queue<int, vector<int>, decltype(cmpReady)> ready(cmpReady);

        for (int i = 0; i < n; ++i)
        {
            if (inDegree[i] == 0)
            {
                ready.push(i);
            }
        }

        while (!ready.empty())
        {
            int u = ready.top();
            ready.pop();
            order.push_back(u);
            for (int v : succ[u])
            {
                if (--inDegree[v] == 0)
                {
                    ready.push(v);
                }
            }
        }

        if (static_cast<int>(order.size()) != n)
        {
            // 存在环，保持原顺序
            order.clear();
            for (int i = 0; i < n; ++i)
            {
                order.push_back(i);
            }
            return false;
        }

        bool changed = false;
        for (int i = 0; i < n; ++i)
        {
            if (order[i] != i)
            {
                changed = true;
                break;
            }
        }
        if (!changed)
        {
            insts.clear();
            for (auto &p : phiInsts)
            {
                insts.push_back(std::move(p));
            }
            for (auto &p : bodyInsts)
            {
                insts.push_back(std::move(p));
            }
            for (auto &p : termInsts)
            {
                insts.push_back(std::move(p));
            }
            return false;
        }

        vector<unique_ptr<Instruction>> newBody;
        newBody.reserve(bodyInsts.size());
        for (int idx : order)
        {
            newBody.push_back(std::move(bodyInsts[idx]));
        }

        if (verbose)
        {
            debugInfo << "InstructionReorder: reordered " << newBody.size()
                      << " instructions in " << bb->getName() << "\n";
        }

        insts.clear();
        for (auto &p : phiInsts)
        {
            insts.push_back(std::move(p));
        }
        for (auto &p : newBody)
        {
            insts.push_back(std::move(p));
        }
        for (auto &p : termInsts)
        {
            insts.push_back(std::move(p));
        }
        return true;
    }
}

bool InstructionReorderPass::runOnFunction(Function *func)
{
    if (func->isLibraryFunction())
    {
        return false;
    }

    bool changed = false;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        if (!bbPtr)
        {
            continue;
        }
        changed |= reorderBasicBlock(bbPtr.get(), verbose, debugInfo);
    }
    return changed;
}
