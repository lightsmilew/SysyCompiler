#include "CalcLiveIntervals.h"
#include "RISCVBuilder.h"
#include <iostream>
using namespace std;

vector<shared_ptr<RISCVBasicBlock>> getPostOrder(shared_ptr<RISCVFunction> currentFunc)
{
    vector<shared_ptr<RISCVBasicBlock>> postOrder;
    unordered_set<shared_ptr<RISCVBasicBlock>> visited;

    function<void(shared_ptr<RISCVBasicBlock>)> dfs = [&](shared_ptr<RISCVBasicBlock> bb)
    {
        if (visited.find(bb) != visited.end())
            return;
        visited.insert(bb);

        for (auto succ : bb->getSuccessors())
        {
            dfs(succ);
        }
        postOrder.push_back(bb);
    };

    // 从入口基本块开始
    if (!currentFunc->getBasicBlocks().empty())
    {
        dfs(currentFunc->getBasicBlocks()[0]);
    }

    return postOrder;
}

void computeBasicBlockUseDef(shared_ptr<RISCVFunction> currentFunc)
{
    for (auto &bb : currentFunc->getBasicBlocks())
    {
        unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> useSet;
        unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> defSet;

        for (const auto &instr : bb->getInstructions())
        {
            // 先处理 use：只加入未被本块内 def 过的寄存器
            for (const auto &reg : instr->getUseRegisters())
            {
                if (defSet.find(reg) == defSet.end())
                    useSet.insert(reg);
            }

            // 再处理 def：只记录第一次定义的寄存器
            for (const auto &reg : instr->getDefRegisters())
            {
                if (defSet.find(reg) == defSet.end())
                    defSet.insert(reg);
            }
        }
        bb->setUse(useSet);
        bb->setDef(defSet);
    }
}

void computeLiveInOut(shared_ptr<RISCVFunction> currentFunc)
{
    vector<shared_ptr<RISCVBasicBlock>> postOrder = getPostOrder(currentFunc);

    bool changed;
    do
    {
        changed = false;
        for (auto bb : postOrder)
        {
            unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> newLiveIn;
            unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> newLiveOut;

            // 计算 LiveOut
            for (const auto &succ : bb->getSuccessors())
            {
                if (succ == bb)
                    continue;
                const auto &succLiveIn = succ->getLiveIn();
                for (const auto &reg : succLiveIn)
                {
                    newLiveOut.insert(reg);
                }
            }

            // 计算 LiveIn
            const auto &use = bb->getUse();
            const auto &def = bb->getDef();
            for (const auto &reg : use)
            {
                newLiveIn.insert(reg);
            }
            for (const auto &reg : newLiveOut)
            {
                if (def.find(reg) == def.end())
                    newLiveIn.insert(reg);
            }

            // 检查是否有变化
            if (newLiveIn != bb->getLiveIn() || newLiveOut != bb->getLiveOut())
            {
                bb->setLiveIn(newLiveIn);
                bb->setLiveOut(newLiveOut);
                changed = true;
            }
        }
    } while (changed);
}

void computeLiveRanges(shared_ptr<RISCVFunction> currentFunc)
{
    auto &livenessInfo = currentFunc->getLivenessInfo();
    livenessInfo.clear();

    // 1. 获取逆后序的基本块列表
    vector<shared_ptr<RISCVBasicBlock>> postOrder = getPostOrder(currentFunc);

    // 编号每条指令
    unordered_map<shared_ptr<RISCVInstruction>, int> instrIndex;
    int instrNum = 0;
    for (auto it = postOrder.rbegin(); it != postOrder.rend(); ++it)
    {
        auto bb = *it;
        if (bb->getInstructions().empty())
            continue;

        // 记录每条指令的索引
        for (const auto &instr : bb->getInstructions())
        {
            instrIndex[instr] = instrNum++;
        }
    }
    livenessInfo.totalInstructions = instrNum;

    // 区间合并工具
    auto extendOrAddRange = [&](shared_ptr<RISCVRegister> reg, int start, int end)
    {
        if (start > end)
            return;
        auto &ranges = livenessInfo.liveRanges[reg];
        vector<int> toMerge;
        int newStart = start, newEnd = end;
        for (int i = 0; i < ranges.size(); i++)
        {
            const auto &range = ranges[i];
            if (newStart <= range.end + 1 && newEnd >= range.start - 1)
            {
                toMerge.push_back(i);
                newStart = std::min(newStart, range.start);
                newEnd = std::max(newEnd, range.end);
            }
        }
        if (toMerge.empty())
        {
            ranges.push_back(LiveRange(newStart, newEnd));
        }
        else
        {
            LiveRange mergedRange(newStart, newEnd);
            for (int i = toMerge.size() - 1; i >= 0; i--)
                ranges.erase(ranges.begin() + toMerge[i]);
            ranges.push_back(mergedRange);
        }
    };

    auto truncateRangeAt = [&](shared_ptr<RISCVRegister> reg, int defPos)
    {
        auto &ranges = livenessInfo.liveRanges[reg];
        vector<LiveRange> newRanges;

        for (const auto &range : ranges)
        {
            if (range.end < defPos)
            {
                // 完全在定义点之前，保留
                newRanges.push_back(range);
            }
            else if (range.start > defPos)
            {
                // 完全在定义点之后，保留
                newRanges.push_back(range);
            }
            else
            {
                if (range.end > defPos)
                {
                    newRanges.push_back(LiveRange(defPos + 1, range.end));
                }
            }
        }

        ranges = newRanges;
    };

    // 主算法：逆序遍历基本块
    for (auto bb : postOrder)
    {
        if (bb->getInstructions().empty())
            continue;
        int bbStart = instrIndex[bb->getInstructions().front()];
        int bbEnd = instrIndex[bb->getInstructions().back()];

        // Step 1: 对LiveOut的变量，延长区间到整个块
        for (const auto &reg : bb->getLiveOut())
        {
            extendOrAddRange(reg, bbStart, bbEnd);
        }

        // Step 2: 逆序遍历指令
        for (auto it = bb->getInstructions().rbegin(); it != bb->getInstructions().rend(); ++it)
        {
            auto instr = *it;
            int pos = instrIndex[instr];

            // 先处理def，截断区间：移除该reg的所有区间（即后续use不会再延长到更前面）
            for (const auto &defReg : instr->getDefRegisters())
            {
                truncateRangeAt(defReg, pos);
                livenessInfo.defPoints[defReg].push_back(pos);
            }

            // 再处理use，延长区间到块头-当前指令
            for (const auto &useReg : instr->getUseRegisters())
            {
                extendOrAddRange(useReg, bbStart, pos);
                livenessInfo.usePoints[useReg].push_back(pos);
            }
        }
    }
}
