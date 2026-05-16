#include "RISCVDataStructure.h"

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

    // 构建完 def-use 后计算支配信息
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

// 简单迭代算法计算支配集合（入口块支配自身）
void RISCVFunction::computeDominators()
{
    dominatorSets.clear();
    // 初始化：所有节点的支配集合为所有节点；入口的支配集合为它自己
    if (basicBlocks.empty())
        return;

    unordered_set<shared_ptr<RISCVBasicBlock>> allBBs(basicBlocks.begin(), basicBlocks.end());
    for (auto &bb : basicBlocks)
    {
        dominatorSets[bb] = allBBs;
    }

    auto entry = basicBlocks.front();
    dominatorSets[entry].clear();
    dominatorSets[entry].insert(entry);

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (auto &bb : basicBlocks)
        {
            if (bb == entry)
                continue;

            // newDom = intersection of dominators of predecessors
            unordered_set<shared_ptr<RISCVBasicBlock>> newDom;
            bool first = true;
            for (auto pred : bb->getPredecessors())
            {
                if (first)
                {
                    newDom = dominatorSets[pred];
                    first = false;
                }
                else
                {
                    unordered_set<shared_ptr<RISCVBasicBlock>> tmp;
                    for (auto &x : newDom)
                    {
                        if (dominatorSets[pred].count(x))
                            tmp.insert(x);
                    }
                    newDom.swap(tmp);
                }
            }

            // add self
            newDom.insert(bb);

            if (newDom != dominatorSets[bb])
            {
                dominatorSets[bb] = newDom;
                changed = true;
            }
        }
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
