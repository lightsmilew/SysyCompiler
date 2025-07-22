#include "InstructionSelector.h"

void InstructionSelector::buildControlFlowGraph()
{
    // 遍历IR函数的所有基本块，构建RISC-V基本块的前驱后继关系
    for (size_t i = 0; i < irFunction->BasicBlocks.size(); ++i)
    {
        auto irBB = irFunction->BasicBlocks[i].get();
        auto riscvBB = currentFunc->getBasicBlock(irBB->getName());

        if (!riscvBB)
            continue;

        // 获取IR基本块的后继，并在RISC-V基本块中建立相应关系
        const auto &irSuccessors = irBB->getSuccessors();
        for (auto irSuccBB : irSuccessors)
        {
            auto riscvSuccBB = currentFunc->getBasicBlock(irSuccBB->getName());
            if (riscvSuccBB)
            {
                // 建立后继关系
                riscvBB->addSuccessor(riscvSuccBB);
                // 建立前驱关系
                riscvSuccBB->addPredecessor(riscvBB);
            }
        }
    }
}

void InstructionSelector::computeBasicBlockUseDef()
{
    for (auto &bb : currentFunc->getBasicBlocks())
    {
        unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> useSet;
        unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> defSet;

        for (const auto &instr : bb->getInstructions())
        {
            // 遍历指令，更新 use 和 def 集合
            for (const auto &reg : instr->getUseRegisters())
            {
                useSet.insert(reg);
            }

            for (const auto &reg : instr->getDefRegisters())
            {
                defSet.insert(reg);
            }
        }
        bb->setUse(useSet);
        bb->setDef(defSet);
    }
}

void InstructionSelector::computeLiveInOut()
{
    bool changed;
    do
    {
        changed = false;
        for (auto &bb : currentFunc->getBasicBlocks())
        {
            unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> newLiveIn;
            unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> newLiveOut;

            // 计算 LiveOut
            for (const auto &succ : bb->getSuccessors())
            {
                if (succ == bb)
                    continue; // 避免自循环
                const auto &succLiveIn = succ->getLiveIn();
                newLiveOut.insert(succLiveIn.begin(), succLiveIn.end());
            }

            // 计算 LiveIn
            const auto &use = bb->getUse();
            const auto &def = bb->getDef();
            newLiveIn = use;
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

void InstructionSelector::computeLiveRanges()
{
    auto &livenessInfo = currentFunc->getLivenessInfo();
    livenessInfo.clear();

    // 首先为每个基本块分配指令编号
    unordered_map<shared_ptr<RISCVBasicBlock>, int> blockStartInstr;
    unordered_map<shared_ptr<RISCVBasicBlock>, int> blockEndInstr;
    int totalInstrNum = 0;

    // 计算每个基本块的指令编号范围
    for (auto &bb : currentFunc->getBasicBlocks())
    {
        blockStartInstr[bb] = totalInstrNum;
        totalInstrNum += bb->getInstructions().size();
        blockEndInstr[bb] = totalInstrNum - 1;
    }
    livenessInfo.totalInstructions = totalInstrNum;

    // 辅助函数：延长或添加新的活跃区间
    auto extendOrAddRange = [&](shared_ptr<RISCVRegister> reg, int start, int end)
    {
        if (livenessInfo.liveRanges.find(reg) == livenessInfo.liveRanges.end())
        {
            livenessInfo.liveRanges[reg] = vector<LiveRange>();
        }

        auto &ranges = livenessInfo.liveRanges[reg];

        // 如果没有现有区间，或者新区间与最后一个区间不相邻/重叠，则添加新区间
        if (ranges.empty() || ranges.back().start > end + 1)
        {
            ranges.push_back(LiveRange(start, end));
        }
        else
        {
            // 扩展现有区间
            ranges.back().start = std::min(ranges.back().start, start);
            ranges.back().end = std::max(ranges.back().end, end);
        }
    };

    // 辅助函数：在指定位置截断活跃区间
    auto truncateRangeAt = [&](shared_ptr<RISCVRegister> reg, int instrPos)
    {
        if (livenessInfo.liveRanges.find(reg) == livenessInfo.liveRanges.end())
        {
            return;
        }

        auto &ranges = livenessInfo.liveRanges[reg];
        for (auto &range : ranges)
        {
            if (range.contains(instrPos))
            {
                // 如果区间包含定义点，在定义点处截断
                if (range.start < instrPos)
                {
                    range.end = instrPos - 1;
                }
                else
                {
                    // 如果定义点就是区间开始，移除这个区间
                    range.start = range.end + 1; // 标记为无效区间
                }
            }
        }

        // 移除无效区间
        ranges.erase(std::remove_if(ranges.begin(), ranges.end(),
                                    [](const LiveRange &r)
                                    { return r.start > r.end; }),
                     ranges.end());
    };

    // for 基本块 in 逆序排列的基本块列表
    auto &basicBlocks = currentFunc->getBasicBlocks();
    for (int bbIndex = basicBlocks.size() - 1; bbIndex >= 0; --bbIndex)
    {
        auto bb = basicBlocks[bbIndex];
        int blockStart = blockStartInstr[bb];
        int blockEnd = blockEndInstr[bb];

        // for 变量 in 基本块的LiveOut集合中变量
        for (auto reg : bb->getLiveOut())
        {
            // 延长原有Range或添加新Range(block开始，block结束)
            extendOrAddRange(reg, blockStart, blockEnd);
        }

        // for 指令 in 基本块的逆序排列的指令列表
        const auto &instructions = bb->getInstructions();
        for (int instrIndex = instructions.size() - 1; instrIndex >= 0; --instrIndex)
        {
            auto instr = instructions[instrIndex];
            int instrPos = blockStart + instrIndex;

            // if (指令存在定值)
            auto defRegs = instr->getDefRegisters();
            for (auto reg : defRegs)
            {
                // 将被定值的变量的range在此处截断
                truncateRangeAt(reg, instrPos);
                // 记录定义点
                livenessInfo.defPoints[reg].push_back(instrPos);
            }

            // for 操作数 in 指令
            auto useRegs = instr->getUseRegisters();
            for (auto reg : useRegs)
            {
                // 延长原有Range或添加新Range(block开始，指令位置)
                extendOrAddRange(reg, blockStart, instrPos);
                // 记录使用点
                livenessInfo.usePoints[reg].push_back(instrPos);
            }
        }
    }

    // 对所有寄存器的活跃区间进行排序和合并
    for (auto &[reg, ranges] : livenessInfo.liveRanges)
    {
        if (ranges.empty())
            continue;

        // 按起始位置排序
        std::sort(ranges.begin(), ranges.end());

        // 合并重叠或相邻的区间
        vector<LiveRange> mergedRanges;
        mergedRanges.push_back(ranges[0]);

        for (size_t i = 1; i < ranges.size(); ++i)
        {
            auto &current = ranges[i];
            auto &last = mergedRanges.back();

            if (current.start <= last.end + 1)
            {
                // 合并区间
                last.end = std::max(last.end, current.end);
            }
            else
            {
                // 添加新区间
                mergedRanges.push_back(current);
            }
        }

        ranges = mergedRanges;
    }
}
