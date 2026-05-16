#include "DefUse.h"

using namespace RISCV;

void DefUseAnalyzer::buildForBasicBlock(std::shared_ptr<RISCVBasicBlock> bb)
{
    if (!bb)
        return;

    auto it = cache.find(bb.get());
    if (it != cache.end())
        return; // 已经构建

    Info info;
    auto &instrs = bb->getInstructions();
    info.instrCount = static_cast<int>(instrs.size());

    for (int i = 0; i < (int)instrs.size(); ++i)
    {
        auto instr = instrs[i];
        if (!instr)
            continue;
        for (auto &useReg : instr->getUseRegisters())
        {
            info.usePos[useReg].push_back(i);
        }
        for (auto &defReg : instr->getDefRegisters())
        {
            info.defPos[defReg].push_back(i);
        }
    }

    cache[bb.get()] = std::move(info);
}

bool DefUseAnalyzer::isRegUsedBeforeRedef(std::shared_ptr<RISCVBasicBlock> bb, std::shared_ptr<RISCVRegister> reg, int startIdx) const
{
    if (!bb || !reg)
        return false;

    auto it = cache.find(bb.get());
    if (it == cache.end())
    {
        // 非常量方法：const_cast 以便调用 build
        const_cast<DefUseAnalyzer *>(this)->buildForBasicBlock(bb);
        it = cache.find(bb.get());
        if (it == cache.end())
            return false;
    }

    const Info &info = it->second;

    // 查找第一个 usePos >= startIdx
    auto uit = info.usePos.find(reg);
    if (uit == info.usePos.end())
        return false;
    for (int pos : uit->second)
    {
        if (pos < startIdx)
            continue;
        // 找到一个使用，检查是否在此之前存在定义
        auto dit = info.defPos.find(reg);
        bool defBefore = false;
        if (dit != info.defPos.end())
        {
            for (int dpos : dit->second)
            {
                if (dpos >= startIdx)
                {
                    // 定义出现在或在使用之后，说明在使用之前没有新的定义
                    continue;
                }
                // 如果存在定义位置在 startIdx 之后且小于 use pos，则需要更细检测
            }
        }
        // 更简洁的策略：如果存在 use pos >= startIdx 并且存在 def pos >= startIdx 且 def pos < use pos，则说明先定义后使用 -> not used before redefinition
        bool usedBeforeRedef = true;
        if (dit != info.defPos.end())
        {
            for (int dpos : dit->second)
            {
                if (dpos >= startIdx && dpos <= pos)
                {
                    usedBeforeRedef = false;
                    break;
                }
            }
        }
        if (usedBeforeRedef)
            return true;
    }

    return false;
}
