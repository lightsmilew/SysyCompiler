// #include "LinearScanRegisterAllocator.h"
// #include <iostream>
// #include <algorithm>
// #include <queue>
// #define INT_MAX 2147483647
// #define INT_MIN -2147483648
// using namespace RISCV;

// // 可用的物理寄存器定义
// const vector<shared_ptr<RISCVRegister>> LinearScanRegisterAllocator::availableGeneralRegs = {
//     // 临时寄存器 (caller-saved) - 优先使用
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T0, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T1, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T2, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T3, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T4, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T5, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T6, RegisterType::GENERAL),

//     // 参数寄存器 (caller-saved)
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A0, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A1, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A2, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A3, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A4, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A5, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A6, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A7, RegisterType::GENERAL),

//     // 保存寄存器 (callee-saved)
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S0, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S1, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S2, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S3, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S4, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S5, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S6, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S7, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S8, RegisterType::GENERAL),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S9, RegisterType::GENERAL)};

// const vector<shared_ptr<RISCVRegister>> LinearScanRegisterAllocator::availableFloatRegs = {
//     // 临时浮点寄存器 (caller-saved)
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT0, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT1, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT2, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT3, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT4, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT5, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT6, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT7, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT8, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT9, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT10, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT11, RegisterType::FLOAT),

//     // 浮点参数寄存器 (caller-saved)
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA0, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA1, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA2, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA3, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA4, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA5, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA6, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA7, RegisterType::FLOAT),

//     // 保存浮点寄存器 (callee-saved)
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS0, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS1, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS2, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS3, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS4, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS5, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS6, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS7, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS8, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS9, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS10, RegisterType::FLOAT),
//     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS11, RegisterType::FLOAT)};

// // 主要接口：为函数分配寄存器
// void LinearScanRegisterAllocator::allocateRegisters(shared_ptr<RISCVFunction> func)
// {
//     currentFunc = func;

//     // 清空所有数据结构
//     intervals.clear();
//     active.clear();
//     availableRegs.clear();
//     spilledIntervals.clear();
//     allocation.clear();
//     spilledRegs.clear();

//     std::cout << "=== Linear Scan Register Allocation ===" << std::endl;
//     std::cout << "Function: " << func->getName() << std::endl;

//     // 执行线性扫描算法的主要步骤
//     buildIntervalsFromLivenessInfo();
//     performLinearScan();
//     handleSpilledRegisters();

//     printStatistics();
// }
// //============================================================================
// // 算法主要阶段实现
// // ============================================================================

// // 从已有的活跃性信息构建活跃区间
// void LinearScanRegisterAllocator::buildIntervalsFromLivenessInfo()
// {
//     const auto &livenessInfo = currentFunc->getLivenessInfo();

//     // 直接从LivenessInfo中获取活跃区间
//     for (const auto &pair : livenessInfo.liveRanges)
//     {
//         auto reg = pair.first;
//         const auto &ranges = pair.second;

//         // 只处理虚拟寄存器
//         if (!reg->isVirtual())
//         {
//             continue;
//         }

//         // 为每个寄存器创建一个合并的活跃区间
//         if (!ranges.empty())
//         {
//             int start = ranges[0].start;
//             int end = ranges[0].end;

//             // 找到最早开始和最晚结束的位置
//             for (const auto &range : ranges)
//             {
//                 start = std::min(start, range.start);
//                 end = std::max(end, range.end);
//             }

//             intervals.emplace_back(reg, start, end);
//         }
//     }

//     // 按开始位置排序活跃区间
//     sortIntervalsByStart();

//     std::cout << "Built " << intervals.size() << " live intervals from existing liveness info" << std::endl;
// }

// // 执行线性扫描
// void LinearScanRegisterAllocator::performLinearScan()
// {
//     // 为每个活跃区间按开始位置顺序进行扫描
//     for (auto &interval : intervals)
//     {
//         // 移除已经结束的活跃区间
//         expireOldIntervals(interval.start);

//         // 获取当前寄存器类型的可用寄存器
//         auto currentAvailableRegs = getAvailableRegisters(interval.reg->getType());
//         int maxRegs = getRegisterCount(interval.reg->getType());

//         // 检查是否有可用寄存器
//         if (active.size() >= maxRegs)
//         {
//             // 需要溢出
//             spillAtInterval(&interval);
//         }
//         else
//         {
//             // 分配寄存器
//             auto allocatedReg = currentAvailableRegs[active.size()];
//             allocation[interval.reg] = allocatedReg;

//             // 将当前区间加入active列表，按结束位置排序
//             active.push_back(&interval);
//             std::sort(active.begin(), active.end(), LiveIntervalEndComparator());
//         }
//     }
// }

// // 处理溢出的寄存器
// void LinearScanRegisterAllocator::handleSpilledRegisters()
// {
//     for (auto spilledInterval : spilledIntervals)
//     {
//         spilledRegs.insert(spilledInterval->reg);
//         insertSpillCode(spilledInterval->reg);
//     }

//     std::cout << "Spilled " << spilledRegs.size() << " registers" << std::endl;
// }

// // ============================================================================
// // 辅助函数实现
// // ============================================================================

// // 按开始位置排序活跃区间
// void LinearScanRegisterAllocator::sortIntervalsByStart()
// {
//     std::sort(intervals.begin(), intervals.end());
// }

// // 移除已经结束的活跃区间
// void LinearScanRegisterAllocator::expireOldIntervals(int currentPos)
// {
//     auto it = active.begin();
//     while (it != active.end())
//     {
//         if ((*it)->end <= currentPos)
//         {
//             // 区间已结束，从active列表中移除
//             it = active.erase(it);
//         }
//         else
//         {
//             ++it;
//         }
//     }
// }

// // 选择溢出候选寄存器
// shared_ptr<RISCVRegister> LinearScanRegisterAllocator::selectSpillCandidate()
// {
//     if (active.empty())
//     {
//         return nullptr;
//     }

//     // 启发式策略1：选择结束位置最晚的区间进行溢出
//     // 理由：结束晚的区间占用寄存器时间长，可能导致更多冲突
//     // active列表按结束位置从晚到早排序，第一个是最晚结束的
//     auto spillCandidate = active.front();

//     // 可以在这里实现更复杂的启发式策略，例如：
//     // - 基于使用频率的选择
//     // - 基于活跃区间长度的选择
//     // - 基于寄存器类型优先级的选择

//     return spillCandidate->reg;
// }

// // 在指定位置进行溢出
// void LinearScanRegisterAllocator::spillAtInterval(LiveInterval *interval)
// {
//     if (active.empty())
//     {
//         // 没有活跃区间，直接溢出当前区间
//         spilledIntervals.push_back(interval);
//         return;
//     }

//     // 使用selectSpillCandidate选择溢出候选
//     auto spillCandidateReg = selectSpillCandidate();
//     if (!spillCandidateReg)
//     {
//         spilledIntervals.push_back(interval);
//         return;
//     }

//     // 找到对应的活跃区间
//     LiveInterval *spillCandidate = nullptr;
//     for (auto activeInterval : active)
//     {
//         if (activeInterval->reg == spillCandidateReg)
//         {
//             spillCandidate = activeInterval;
//             break;
//         }
//     }

//     if (!spillCandidate)
//     {
//         spilledIntervals.push_back(interval);
//         return;
//     }

//     if (spillCandidate->end > interval->end)
//     {
//         // 溢出候选区间结束得更晚，溢出它并分配当前区间
//         allocation[interval->reg] = allocation[spillCandidate->reg];
//         spilledIntervals.push_back(spillCandidate);
//         allocation.erase(spillCandidate->reg);

//         // 从active中移除被溢出的区间，添加当前区间
//         auto it = std::find(active.begin(), active.end(), spillCandidate);
//         if (it != active.end())
//         {
//             active.erase(it);
//         }
//         active.push_back(interval);
//         std::sort(active.begin(), active.end(), LiveIntervalEndComparator());
//     }
//     else
//     {
//         // 溢出当前区间
//         spilledIntervals.push_back(interval);
//     }
// }

// // 插入溢出代码
// void LinearScanRegisterAllocator::insertSpillCode(shared_ptr<RISCVRegister> spilledReg)
// {
//     // 为溢出寄存器在栈上分配空间
//     string spillName = "spill_" + spilledReg->toString();
//     currentFunc->getStackFrame().allocateValueSpace(spillName, 4);

//     // 遍历所有指令，为溢出寄存器插入load/store代码
//     for (auto &bb : currentFunc->getBasicBlocks())
//     {
//         vector<shared_ptr<RISCVInstruction>> newInstructions;

//         for (auto instr : bb->getInstructions())
//         {
//             vector<shared_ptr<RISCVInstruction>> beforeInstr;
//             vector<shared_ptr<RISCVInstruction>> afterInstr;

//             // 检查指令是否使用了溢出寄存器
//             bool usesSpilledReg = false;
//             bool definesSpilledReg = false;

//             for (auto useReg : instr->getUseRegisters())
//             {
//                 if (useReg == spilledReg)
//                 {
//                     usesSpilledReg = true;
//                     break;
//                 }
//             }

//             for (auto defReg : instr->getDefRegisters())
//             {
//                 if (defReg == spilledReg)
//                 {
//                     definesSpilledReg = true;
//                     break;
//                 }
//             }

//             // 如果使用了溢出寄存器，在指令前插入load
//             if (usesSpilledReg)
//             {
//                 auto tempReg = make_shared<RISCVRegister>(spilledReg->getType());
//                 int offset = currentFunc->getStackFrame().getValueOffset(spillName);

//                 auto loadInstr = RISCVInstruction::createIType(
//                     spilledReg->getType() == RegisterType::FLOAT ? RISCVOpcode::FLW : RISCVOpcode::LW,
//                     tempReg,
//                     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP, RegisterType::GENERAL),
//                     offset);
//                 beforeInstr.push_back(loadInstr);

//                 // 注意：这里需要修改指令中的寄存器引用，实际实现中需要重构指令
//             }

//             // 如果定义了溢出寄存器，在指令后插入store
//             if (definesSpilledReg)
//             {
//                 auto tempReg = make_shared<RISCVRegister>(spilledReg->getType());
//                 int offset = currentFunc->getStackFrame().getValueOffset(spillName);

//                 auto storeInstr = RISCVInstruction::createSType(
//                     spilledReg->getType() == RegisterType::FLOAT ? RISCVOpcode::FSW : RISCVOpcode::SW,
//                     tempReg,
//                     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP, RegisterType::GENERAL),
//                     offset);
//                 afterInstr.push_back(storeInstr);
//             }

//             // 构建新的指令序列
//             for (auto beforeI : beforeInstr)
//             {
//                 newInstructions.push_back(beforeI);
//             }
//             newInstructions.push_back(instr);
//             for (auto afterI : afterInstr)
//             {
//                 newInstructions.push_back(afterI);
//             }
//         }

//         // 注意：这里需要RISCVBasicBlock支持替换整个指令列表
//         // 实际实现中需要相应的接口
//     }
// }

// // ============================================================================
// // 寄存器管理函数
// // ============================================================================

// // 获取指定类型的可用寄存器
// vector<shared_ptr<RISCVRegister>> LinearScanRegisterAllocator::getAvailableRegisters(RegisterType type)
// {
//     if (type == RegisterType::GENERAL)
//     {
//         return availableGeneralRegs;
//     }
//     else if (type == RegisterType::FLOAT)
//     {
//         return availableFloatRegs;
//     }
//     return {};
// }

// // 获取指定类型的寄存器数量
// int LinearScanRegisterAllocator::getRegisterCount(RegisterType type)
// {
//     if (type == RegisterType::GENERAL)
//     {
//         return R_GENERAL;
//     }
//     else if (type == RegisterType::FLOAT)
//     {
//         return R_FLOAT;
//     }
//     return 0;
// }

// // 检查寄存器是否为预着色寄存器
// bool LinearScanRegisterAllocator::isPrecolored(shared_ptr<RISCVRegister> reg)
// {
//     return reg->isPhysical();
// }

// // ============================================================================
// // 调试和统计函数
// // ============================================================================

// // 打印统计信息
// void LinearScanRegisterAllocator::printStatistics()
// {
//     std::cout << "=== Linear Scan Allocation Statistics ===" << std::endl;
//     std::cout << "Function: " << currentFunc->getName() << std::endl;
//     std::cout << "Total live intervals: " << intervals.size() << std::endl;
//     std::cout << "Successfully allocated: " << allocation.size() << std::endl;
//     std::cout << "Spilled registers: " << spilledRegs.size() << std::endl;

//     // 按寄存器类型统计
//     int generalAllocated = 0, floatAllocated = 0;
//     int generalSpilled = 0, floatSpilled = 0;

//     for (auto &pair : allocation)
//     {
//         if (pair.first->getType() == RegisterType::GENERAL)
//         {
//             generalAllocated++;
//         }
//         else
//         {
//             floatAllocated++;
//         }
//     }

//     for (auto spilledReg : spilledRegs)
//     {
//         if (spilledReg->getType() == RegisterType::GENERAL)
//         {
//             generalSpilled++;
//         }
//         else
//         {
//             floatSpilled++;
//         }
//     }

//     std::cout << "General registers - Allocated: " << generalAllocated
//               << ", Spilled: " << generalSpilled << std::endl;
//     std::cout << "Float registers - Allocated: " << floatAllocated
//               << ", Spilled: " << floatSpilled << std::endl;
//     std::cout << "=========================================" << std::endl;
// }

// // 验证分配结果
// void LinearScanRegisterAllocator::validateAllocation()
// {
//     // 检查是否有冲突的分配
//     for (size_t i = 0; i < intervals.size(); i++)
//     {
//         for (size_t j = i + 1; j < intervals.size(); j++)
//         {
//             auto &interval1 = intervals[i];
//             auto &interval2 = intervals[j];

//             // 如果两个区间重叠且都被分配了寄存器
//             if (interval1.overlaps(interval2) &&
//                 allocation.count(interval1.reg) &&
//                 allocation.count(interval2.reg))
//             {

//                 auto reg1 = allocation[interval1.reg];
//                 auto reg2 = allocation[interval2.reg];

//                 if (reg1 == reg2)
//                 {
//                     std::cerr << "Error: Conflicting allocation for registers "
//                               << interval1.reg->toString() << " and "
//                               << interval2.reg->toString() << std::endl;
//                 }
//             }
//         }
//     }

//     std::cout << "Allocation validation completed" << std::endl;
// }