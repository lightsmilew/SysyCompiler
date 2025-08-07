#include "InstructionScheduler.h"
#include <algorithm>
#include <cassert>
#include <climits>
#include <iostream>
#include <unordered_map>

namespace RISCV
{

    InstructionScheduler::InstructionScheduler()
        : enableDualIssue(true), enableFloatOptimization(true), enablePreciseMemoryDependency(false),
          totalInstructionsScheduled(0), totalCyclesSaved(0)
    {
        // 初始化功能单元
        // 根据架构信息：1个load/store单元, 1个mul/div单元, 1个branch单元, 2个整数单元, 1个浮点单元
        units.emplace_back(FunctionalUnitType::LOAD_STORE);
        units.emplace_back(FunctionalUnitType::MUL_DIV);
        units.emplace_back(FunctionalUnitType::BRANCH);
        units.emplace_back(FunctionalUnitType::INTEGER);
        units.emplace_back(FunctionalUnitType::INTEGER);
        units.emplace_back(FunctionalUnitType::FLOAT);

        initializeLatencyMap();
    }

    void InstructionScheduler::initializeLatencyMap()
    {
        // 根据提供的延迟信息初始化

        // 普通算术指令 - 1周期
        latencyMap[RISCVOpcode::ADD] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::ADDI] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::SUB] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::AND] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::ANDI] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::OR] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::ORI] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::XOR] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::XORI] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::SLL] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::SLLI] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::SRL] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::SRLI] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::SRA] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::SRAI] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::SLT] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::SLTI] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::SLTU] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::SLTIU] = {1, 0, FunctionalUnitType::INTEGER};

        // 乘法指令 - 3周期
        latencyMap[RISCVOpcode::MUL] = {3, 0, FunctionalUnitType::MUL_DIV};
        latencyMap[RISCVOpcode::MULW] = {3, 0, FunctionalUnitType::MUL_DIV};

        // 除法指令 - 复杂延迟，这里简化为平均值
        latencyMap[RISCVOpcode::DIV] = {34, 0, FunctionalUnitType::MUL_DIV};
        latencyMap[RISCVOpcode::DIVW] = {34, 0, FunctionalUnitType::MUL_DIV};
        latencyMap[RISCVOpcode::REM] = {34, 0, FunctionalUnitType::MUL_DIV};
        latencyMap[RISCVOpcode::REMW] = {34, 0, FunctionalUnitType::MUL_DIV};

        // Load指令 - 3周期
        latencyMap[RISCVOpcode::LD] = {3, 0, FunctionalUnitType::LOAD_STORE};
        latencyMap[RISCVOpcode::LW] = {3, 0, FunctionalUnitType::LOAD_STORE};
        latencyMap[RISCVOpcode::LH] = {3, 0, FunctionalUnitType::LOAD_STORE};
        latencyMap[RISCVOpcode::LB] = {3, 0, FunctionalUnitType::LOAD_STORE};
        latencyMap[RISCVOpcode::LHU] = {3, 0, FunctionalUnitType::LOAD_STORE};
        latencyMap[RISCVOpcode::LBU] = {3, 0, FunctionalUnitType::LOAD_STORE};

        // Store指令 - 1周期
        latencyMap[RISCVOpcode::SD] = {1, 0, FunctionalUnitType::LOAD_STORE};
        latencyMap[RISCVOpcode::SW] = {1, 0, FunctionalUnitType::LOAD_STORE};
        latencyMap[RISCVOpcode::SH] = {1, 0, FunctionalUnitType::LOAD_STORE};
        latencyMap[RISCVOpcode::SB] = {1, 0, FunctionalUnitType::LOAD_STORE};

        // 分支指令 - 1周期
        latencyMap[RISCVOpcode::BEQ] = {1, 0, FunctionalUnitType::BRANCH};
        latencyMap[RISCVOpcode::BNE] = {1, 0, FunctionalUnitType::BRANCH};
        latencyMap[RISCVOpcode::BLT] = {1, 0, FunctionalUnitType::BRANCH};
        latencyMap[RISCVOpcode::BGE] = {1, 0, FunctionalUnitType::BRANCH};
        latencyMap[RISCVOpcode::BLTU] = {1, 0, FunctionalUnitType::BRANCH};
        latencyMap[RISCVOpcode::BGEU] = {1, 0, FunctionalUnitType::BRANCH};
        latencyMap[RISCVOpcode::JAL] = {1, 0, FunctionalUnitType::BRANCH};
        latencyMap[RISCVOpcode::JALR] = {1, 0, FunctionalUnitType::BRANCH};

        // 浮点算术指令 - 5周期
        latencyMap[RISCVOpcode::FADD_S] = {5, 0, FunctionalUnitType::FLOAT};
        latencyMap[RISCVOpcode::FSUB_S] = {5, 0, FunctionalUnitType::FLOAT};
        latencyMap[RISCVOpcode::FMUL_S] = {5, 0, FunctionalUnitType::FLOAT};

        // 浮点除法 - 特殊处理，repeat rate很重要
        latencyMap[RISCVOpcode::FDIV_S] = {22, 17, FunctionalUnitType::FLOAT}; // 平均值

        // 浮点比较 - 4周期
        latencyMap[RISCVOpcode::FEQ_S] = {4, 0, FunctionalUnitType::FLOAT};
        latencyMap[RISCVOpcode::FLT_S] = {4, 0, FunctionalUnitType::FLOAT};
        latencyMap[RISCVOpcode::FLE_S] = {4, 0, FunctionalUnitType::FLOAT};

        // 浮点内存访问
        latencyMap[RISCVOpcode::FLW] = {2, 0, FunctionalUnitType::LOAD_STORE};
        latencyMap[RISCVOpcode::FLD] = {2, 0, FunctionalUnitType::LOAD_STORE};
        latencyMap[RISCVOpcode::FSW] = {4, 0, FunctionalUnitType::LOAD_STORE};
        latencyMap[RISCVOpcode::FSD] = {4, 0, FunctionalUnitType::LOAD_STORE};

        // 浮点移动和转换
        latencyMap[RISCVOpcode::FMV_S] = {2, 0, FunctionalUnitType::FLOAT};
        latencyMap[RISCVOpcode::FMV_W_X] = {2, 0, FunctionalUnitType::FLOAT};
        latencyMap[RISCVOpcode::FMV_X_W] = {4, 0, FunctionalUnitType::FLOAT};
        latencyMap[RISCVOpcode::FCVT_W_S] = {4, 0, FunctionalUnitType::FLOAT};
        latencyMap[RISCVOpcode::FCVT_S_W] = {2, 0, FunctionalUnitType::FLOAT};

        // 立即数指令
        latencyMap[RISCVOpcode::LUI] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::AUIPC] = {1, 0, FunctionalUnitType::INTEGER};

        // 伪指令
        latencyMap[RISCVOpcode::LI] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::LA] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::MV] = {1, 0, FunctionalUnitType::INTEGER};
        latencyMap[RISCVOpcode::CALL] = {1, 0, FunctionalUnitType::BRANCH};
        latencyMap[RISCVOpcode::RET] = {1, 0, FunctionalUnitType::BRANCH};
    }

    InstructionLatency InstructionScheduler::getInstructionLatency(RISCVOpcode opcode) const
    {
        auto it = latencyMap.find(opcode);
        if (it != latencyMap.end())
        {
            return it->second;
        }
        // 默认延迟
        return {1, 0, FunctionalUnitType::INTEGER};
    }

    std::vector<DependencyNode> InstructionScheduler::buildDependencyGraph(
        const std::vector<std::shared_ptr<RISCVInstruction>> &instructions)
    {

        std::vector<DependencyNode> nodes;
        nodes.reserve(instructions.size());

        // 创建节点
        for (auto &instr : instructions)
        {
            nodes.emplace_back(instr);
            nodes.back().latencyInfo = getInstructionLatency(instr->getOpcode());
        }

        // 添加数据依赖
        addDataDependencies(nodes);

        return nodes;
    }

    void InstructionScheduler::addDataDependencies(std::vector<DependencyNode> &nodes)
    {
        // 使用自定义的Hash和Equal函数来正确比较寄存器
        std::unordered_map<std::shared_ptr<RISCVRegister>, int,
                           RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual>
            lastWrite;
        std::unordered_map<std::shared_ptr<RISCVRegister>, std::vector<int>,
                           RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual>
            reads;

        // 内存依赖分析：跟踪内存访问
        std::vector<int> memoryStores; // 所有store指令的索引
        std::vector<int> memoryLoads;  // 所有load指令的索引

        // 辅助函数：安全添加依赖边，避免重复
        auto addDependencyEdge = [](DependencyNode *from, DependencyNode *to)
        {
            // 检查是否已存在依赖关系
            if (std::find(from->successors.begin(), from->successors.end(), to) == from->successors.end())
            {
                from->successors.push_back(to);
                to->predecessors.push_back(from);
            }
        };

        for (int i = 0; i < nodes.size(); ++i)
        {
            auto &node = nodes[i];
            auto instruction = node.instruction;
            auto opcode = instruction->getOpcode();

            // 检查是否是内存访问指令
            bool isMemoryLoad = isMemoryLoadInstruction(opcode);
            bool isMemoryStore = isMemoryStoreInstruction(opcode);

            // 第一步：处理使用的寄存器 (RAW依赖)
            auto useRegs = instruction->getUseRegisters();
            for (auto reg : useRegs)
            {
                // RAW依赖：当前指令读取，之前有写入
                if (lastWrite.find(reg) != lastWrite.end())
                {
                    int writeIdx = lastWrite[reg];
                    if (writeIdx != i) // 避免自依赖
                        addDependencyEdge(&nodes[writeIdx], &node);
                }
                // 记录当前指令读取该寄存器
                reads[reg].push_back(i);
            }

            // 第二步：处理定义的寄存器 (WAR和WAW依赖)
            auto defRegs = instruction->getDefRegisters();
            for (auto reg : defRegs)
            {
                // WAR依赖：当前指令写入，之前有读取
                if (reads.find(reg) != reads.end())
                {
                    for (int readIdx : reads[reg])
                    {
                        if (readIdx != i) // 避免自依赖
                            addDependencyEdge(&nodes[readIdx], &node);
                    }
                    reads[reg].clear(); // 清除读取记录，因为寄存器被重新定义
                }

                // WAW依赖：当前指令写入，之前也有写入
                if (lastWrite.find(reg) != lastWrite.end())
                {
                    int writeIdx = lastWrite[reg];
                    if (writeIdx != i) // 避免自依赖
                        addDependencyEdge(&nodes[writeIdx], &node);
                }

                // 更新最后写入指令
                lastWrite[reg] = i;
            }

            // 第三步：处理内存依赖 (更精确的策略)
            if (isMemoryLoad)
            {
                // Load指令依赖之前的Store指令 (RAW依赖)
                if (!enablePreciseMemoryDependency)
                {
                    // 保守策略：依赖所有之前的Store指令
                    for (int storeIdx : memoryStores)
                    {
                        addDependencyEdge(&nodes[storeIdx], &node);
                    }
                }
                else
                {
                    // 精确策略：只依赖可能别名的Store指令
                    for (int storeIdx : memoryStores)
                    {
                        if (mayAlias(nodes[storeIdx].instruction, instruction))
                        {
                            addDependencyEdge(&nodes[storeIdx], &node);
                        }
                    }
                }
                memoryLoads.push_back(i);
            }

            if (isMemoryStore)
            {
                if (!enablePreciseMemoryDependency)
                {
                    // 保守策略
                    // WAR: Store依赖之前的Load (防止读取被覆盖)
                    for (int loadIdx : memoryLoads)
                    {
                        addDependencyEdge(&nodes[loadIdx], &node);
                    }
                    // WAW: Store依赖之前的Store (保持写入顺序)
                    for (int storeIdx : memoryStores)
                    {
                        addDependencyEdge(&nodes[storeIdx], &node);
                    }
                }
                else
                {
                    // 精确策略
                    for (int loadIdx : memoryLoads)
                    {
                        if (mayAlias(nodes[loadIdx].instruction, instruction))
                        {
                            addDependencyEdge(&nodes[loadIdx], &node);
                        }
                    }
                    for (int storeIdx : memoryStores)
                    {
                        if (mayAlias(nodes[storeIdx].instruction, instruction))
                        {
                            addDependencyEdge(&nodes[storeIdx], &node);
                        }
                    }
                }
                memoryStores.push_back(i);
            }
        }

#ifdef RISCV_SCHEDULER_DEBUG
        // 输出最终的依赖图统计
        std::cout << "\n=== Final Dependency Graph Statistics ===" << std::endl;
        int totalEdges = 0;
        for (int i = 0; i < nodes.size(); ++i)
        {
            std::cout << "Instruction " << i << " (" << nodes[i].instruction->toString() << "):" << std::endl;
            std::cout << "  Predecessors: ";
            for (auto pred : nodes[i].predecessors)
            {
                // 找到前驱的索引
                for (int j = 0; j < nodes.size(); ++j)
                {
                    if (&nodes[j] == pred)
                    {
                        std::cout << j << " ";
                        break;
                    }
                }
            }
            std::cout << std::endl;

            std::cout << "  Successors: ";
            for (auto succ : nodes[i].successors)
            {
                // 找到后继的索引
                for (int j = 0; j < nodes.size(); ++j)
                {
                    if (&nodes[j] == succ)
                    {
                        std::cout << j << " ";
                        totalEdges++;
                        break;
                    }
                }
            }
            std::cout << std::endl;
        }
        std::cout << "Total dependency edges: " << totalEdges << std::endl;
        std::cout << "===========================================" << std::endl;
#endif
    }

    void InstructionScheduler::calculateReadyTimes(std::vector<DependencyNode> &nodes)
    {
        // 使用Kahn算法进行拓扑排序计算最早就绪时间
        std::queue<DependencyNode *> queue;
        std::unordered_map<DependencyNode *, int> inDegree;

        // 初始化所有节点的入度和就绪时间
        for (auto &node : nodes)
        {
            node.readyTime = 0;
            inDegree[&node] = node.predecessors.size();

            // 入度为0的节点加入队列
            if (inDegree[&node] == 0)
            {
                queue.push(&node);
            }
        }

        int processedCount = 0;
        while (!queue.empty())
        {
            auto current = queue.front();
            queue.pop();
            processedCount++;

            // 处理当前节点的所有后继节点
            for (auto successor : current->successors)
            {
                // 计算后继节点的就绪时间
                int newReadyTime = current->readyTime + current->latencyInfo.latency;
                successor->readyTime = std::max(successor->readyTime, newReadyTime);

                // 减少后继节点的入度
                inDegree[successor]--;

                // 如果后继节点的所有前驱都已处理，将其加入队列
                if (inDegree[successor] == 0)
                {
                    queue.push(successor);
                }
            }
        }

        // 检查是否存在环路
        if (processedCount != nodes.size())
        {
            std::cerr << "Error: Dependency graph contains cycles! Processed "
                      << processedCount << " out of " << nodes.size() << " nodes." << std::endl;
        }
    }

    void InstructionScheduler::precomputeCriticalPaths(std::vector<DependencyNode> &nodes)
    {
        // 使用拓扑排序逆序计算关键路径
        std::vector<DependencyNode *> topoOrder;
        std::unordered_map<DependencyNode *, int> outDegree;
        std::queue<DependencyNode *> queue;

        // 初始化出度（后继节点数量）
        for (auto &node : nodes)
        {
            outDegree[&node] = node.successors.size();

            // 叶子节点（没有后继）先加入队列
            if (outDegree[&node] == 0)
            {
                queue.push(&node);
            }
        }

        // 拓扑排序（逆序：从叶子节点开始）
        while (!queue.empty())
        {
            auto current = queue.front();
            queue.pop();
            topoOrder.push_back(current);

            // 处理当前节点的所有前驱节点
            for (auto pred : current->predecessors)
            {
                outDegree[pred]--;
                if (outDegree[pred] == 0)
                {
                    queue.push(pred);
                }
            }
        }

        // 检查是否存在环（如果存在环，拓扑排序不会包含所有节点）
        if (topoOrder.size() != nodes.size())
        {
            std::cerr << "Warning: Cyclic dependency detected in critical path calculation!" << std::endl;
            // 对于有环的情况，使用保守策略
            for (auto &node : nodes)
            {
                node.criticalPath = node.latencyInfo.latency;
            }
            return;
        }

        // 按拓扑逆序计算关键路径（从叶子节点向根节点）
        for (auto node : topoOrder)
        {
            int maxSuccessorPath = 0;

            // 找到所有后继节点中的最大关键路径
            for (auto successor : node->successors)
            {
                maxSuccessorPath = std::max(maxSuccessorPath, successor->criticalPath);
            }

            // 当前节点的关键路径 = 自身延迟 + 最大后继路径
            node->criticalPath = node->latencyInfo.latency + maxSuccessorPath;
        }

#ifdef RISCV_SCHEDULER_DEBUG
        // 输出关键路径信息
        std::cout << "\n=== Critical Path Information ===" << std::endl;
        int maxPath = 0;
        DependencyNode *criticalNode = nullptr;

        for (auto &node : nodes)
        {
            std::cout << "Node " << &node - &nodes[0] << " ("
                      << node.instruction->toString() << "): "
                      << node.criticalPath << std::endl;

            if (node.criticalPath > maxPath)
            {
                maxPath = node.criticalPath;
                criticalNode = &node;
            }
        }

        std::cout << "Maximum critical path length: " << maxPath << std::endl;
        if (criticalNode)
        {
            std::cout << "Critical instruction: " << criticalNode->instruction->toString() << std::endl;
        }
        std::cout << "=================================" << std::endl;
#endif
    }

    int InstructionScheduler::calculateSimpleScore(DependencyNode *node) const
    {
        // O(1) 时间复杂度的简单评分
        int score = 0;

        // 1. 关键路径长度（最重要的因素）
        score += node->criticalPath * 100;

        // 2. 指令延迟
        score += node->latencyInfo.latency * 10;

        // 3. 后继节点越多，优先级越高（影响更多后续指令）
        score += node->successors.size() * 5;

        // 4. 特殊指令类型加权
        auto opcode = node->instruction->getOpcode();

        // 高延迟指令优先调度
        if (opcode == RISCVOpcode::MUL || opcode == RISCVOpcode::MULW)
        {
            score += 30;
        }
        if (opcode == RISCVOpcode::DIV || opcode == RISCVOpcode::DIVW ||
            opcode == RISCVOpcode::REM || opcode == RISCVOpcode::REMW)
        {
            score += 50; // 除法指令优先级最高
        }
        if (opcode == RISCVOpcode::FDIV_S)
        {
            score += 40; // 浮点除法也很高优先级
        }
        if (isMemoryLoadInstruction(opcode))
        {
            score += 20; // Load指令优先（为后续指令提供数据）
        }
        if (node->latencyInfo.unit == FunctionalUnitType::FLOAT)
        {
            score += 15; // 浮点指令一般延迟较高
        }

        return score;
    }

    bool InstructionScheduler::canScheduleInstruction(const DependencyNode &node, int currentTime)
    {
        // 检查数据依赖
        if (node.readyTime > currentTime)
        {
            return false;
        }

        // 检查功能单元可用性
        for (auto &unit : units)
        {
            if (unit.type == node.latencyInfo.unit)
            {
                if (unit.busyUntil <= currentTime)
                {
                    // 特殊处理浮点除法的repeat rate
                    if (node.instruction->getOpcode() == RISCVOpcode::FDIV_S)
                    {
                        if (unit.lastFloatDivTime >= 0 &&
                            currentTime - unit.lastFloatDivTime < node.latencyInfo.repeatRate)
                        {
                            return false;
                        }
                    }
                    return true;
                }
            }
        }

        return false;
    }

    void InstructionScheduler::scheduleInstruction(DependencyNode &node, int currentTime)
    {
        node.scheduledTime = currentTime;
        node.scheduled = true;

        // 占用功能单元
        for (auto &unit : units)
        {
            if (unit.type == node.latencyInfo.unit && unit.busyUntil <= currentTime)
            {
                unit.busyUntil = currentTime + 1; // 占用一个周期

                // 特殊处理浮点除法
                if (node.instruction->getOpcode() == RISCVOpcode::FDIV_S)
                {
                    unit.lastFloatDivTime = currentTime;
                }
                break;
            }
        }
    }

    DependencyNode *InstructionScheduler::selectNextInstruction(
        const std::vector<DependencyNode *> &readyList, int currentTime)
    {
        if (readyList.empty())
            return nullptr;

        // 使用预计算的关键路径进行快速选择
        DependencyNode *best = nullptr;
        int bestScore = -1;

        for (auto node : readyList)
        {
            if (!canScheduleInstruction(*node, currentTime))
                continue;

            // 🚀 使用简单评分函数，避免递归计算（O(1)时间复杂度）
            int score = calculateSimpleScore(node);

            if (score > bestScore)
            {
                bestScore = score;
                best = node;
            }
        }

        return best;
    }

    int InstructionScheduler::calculateCriticalPath(DependencyNode *node) const
    {
        // 如果已经调度过，返回0
        if (node->scheduled)
        {
            return 0;
        }

        // 当前节点的延迟
        int currentLatency = node->latencyInfo.latency;

        // 如果没有后继节点，返回当前节点的延迟
        if (node->successors.empty())
        {
            return currentLatency;
        }

        // 递归计算所有后继节点的关键路径，选择最大值
        int maxSuccessorPath = 0;
        for (auto successor : node->successors)
        {
            if (!successor->scheduled)
            {
                int successorPath = calculateCriticalPath(successor);
                maxSuccessorPath = std::max(maxSuccessorPath, successorPath);
            }
        }

        // 关键路径 = 当前延迟 + 最长后继路径
        return currentLatency + maxSuccessorPath;
    }

    bool InstructionScheduler::canDualIssue(const DependencyNode &inst1, const DependencyNode &inst2)
    {
        if (!enableDualIssue)
        {
            return false;
        }

        auto op1 = inst1.instruction->getOpcode();
        auto op2 = inst2.instruction->getOpcode();
        auto unit1 = inst1.latencyInfo.unit;
        auto unit2 = inst2.latencyInfo.unit;

        // 不能使用相同的功能单元（除了INTEGER，有两个）
        if (unit1 == unit2 && unit1 != FunctionalUnitType::INTEGER)
        {
            return false;
        }

        // 检查双发射约束
        int memoryOps = 0, branchOps = 0, mulDivOps = 0, floatOps = 0, addSubOps = 0;

        // 统计第一条指令
        if (unit1 == FunctionalUnitType::LOAD_STORE)
            memoryOps++;
        if (unit1 == FunctionalUnitType::BRANCH)
            branchOps++;
        if (unit1 == FunctionalUnitType::MUL_DIV)
            mulDivOps++;
        if (unit1 == FunctionalUnitType::FLOAT)
            floatOps++;
        if (unit1 == FunctionalUnitType::INTEGER &&
            (op1 == RISCVOpcode::ADD || op1 == RISCVOpcode::ADDI ||
             op1 == RISCVOpcode::SUB))
            addSubOps++;

        // 统计第二条指令
        if (unit2 == FunctionalUnitType::LOAD_STORE)
            memoryOps++;
        if (unit2 == FunctionalUnitType::BRANCH)
            branchOps++;
        if (unit2 == FunctionalUnitType::MUL_DIV)
            mulDivOps++;
        if (unit2 == FunctionalUnitType::FLOAT)
            floatOps++;
        if (unit2 == FunctionalUnitType::INTEGER &&
            (op2 == RISCVOpcode::ADD || op2 == RISCVOpcode::ADDI ||
             op2 == RISCVOpcode::SUB))
            addSubOps++;

        // 检查约束
        return memoryOps <= 1 && branchOps <= 1 && mulDivOps <= 1 &&
               floatOps <= 1 && addSubOps <= 2;
    }

    std::vector<std::shared_ptr<RISCVInstruction>> InstructionScheduler::scheduleInstructions(
        const std::vector<std::shared_ptr<RISCVInstruction>> &instructions)
    {

        if (instructions.empty())
            return instructions;

        // 统计信息更新
        totalInstructionsScheduled += instructions.size();

#ifdef RISCV_SCHEDULER_DEBUG
        // 估算原始序列的执行周期
        int originalCycles = estimateCycles(instructions);
#endif

        // 构建依赖图
        auto nodes = buildDependencyGraph(instructions);
        calculateReadyTimes(nodes);

        // 🚀 预计算关键路径（O(n)复杂度，避免后续递归计算）
        precomputeCriticalPaths(nodes);

        // 调度算法：列表调度 + 双发射
        std::vector<std::shared_ptr<RISCVInstruction>> scheduledInstructions;
        std::vector<DependencyNode *> readyList;
        int currentTime = 0;
        int scheduledCount = 0;

        // 初始化就绪列表
        for (auto &node : nodes)
        {
            if (node.predecessors.empty())
            {
                readyList.push_back(&node);
            }
        }

        while (scheduledCount < nodes.size())
        {
            // 尝试双发射
            std::vector<DependencyNode *> toSchedule;

            // 选择第一条指令
            auto inst1 = selectNextInstruction(readyList, currentTime);
            if (inst1 != nullptr)
            {
                toSchedule.push_back(inst1);

                // 尝试选择第二条指令进行双发射
                for (auto node : readyList)
                {
                    if (node != inst1 && canScheduleInstruction(*node, currentTime) &&
                        canDualIssue(*inst1, *node))
                    {
                        toSchedule.push_back(node);
                        break;
                    }
                }
            }

            // 调度选中的指令
            for (auto node : toSchedule)
            {
                scheduleInstruction(*node, currentTime);
                scheduledInstructions.push_back(node->instruction);
                scheduledCount++;

                // 从就绪列表中移除
                readyList.erase(std::remove(readyList.begin(), readyList.end(), node),
                                readyList.end());

                // 更新后继节点
                for (auto successor : node->successors)
                {
                    if (!successor->scheduled)
                    {
                        bool allPredsDone = true;
                        for (auto pred : successor->predecessors)
                        {
                            if (!pred->scheduled)
                            {
                                allPredsDone = false;
                                break;
                            }
                        }
                        if (allPredsDone &&
                            std::find(readyList.begin(), readyList.end(), successor) == readyList.end())
                        {
                            readyList.push_back(successor);
                        }
                    }
                }
            }

            // 如果没有可调度的指令，推进时间
            currentTime++;
        }

#ifdef RISCV_SCHEDULER_DEBUG
        // 输出调度前后对比
        int scheduledCycles = estimateCycles(scheduledInstructions);
        totalCyclesSaved += std::max(0, originalCycles - scheduledCycles);
        printSchedulingComparison(instructions, scheduledInstructions, originalCycles, scheduledCycles);
#endif

        return scheduledInstructions;
    }

    void InstructionScheduler::scheduleBasicBlock(std::shared_ptr<RISCVBasicBlock> bb)
    {
        auto &instructions = bb->getInstructions();

        // 只对有足够指令的基本块进行调度（调度开销 vs 收益考虑）
        if (instructions.size() < 3)
        {
            return;
        }

        // 将基本块按照call指令分割成多个调度段
        auto schedulingSegments = splitByCallInstructions(instructions);

        // 重新构建指令序列
        std::vector<std::shared_ptr<RISCVInstruction>> newInstructions;

        for (auto &segment : schedulingSegments)
        {
            if (segment.empty())
                continue;

            // 检查是否是单独的call指令段
            if (segment.size() == 1 && segment[0]->getOpcode() == RISCVOpcode::CALL)
            {
                // call指令直接添加，不调度
                newInstructions.insert(newInstructions.end(), segment.begin(), segment.end());
                continue;
            }

            // 分离控制流指令和可调度指令
            auto [schedulableInsts, controlFlowInsts] = separateControlFlowInstructions(segment);

            // 对可调度的指令进行调度
            if (!schedulableInsts.empty())
            {
                if (schedulableInsts.size() <= 2)
                {
                    // 如果指令数量少于等于2，直接添加到新指令序列
                    newInstructions.insert(newInstructions.end(), schedulableInsts.begin(), schedulableInsts.end());
                }
                else
                {
                    auto scheduledSegment = scheduleInstructions(schedulableInsts);
                    newInstructions.insert(newInstructions.end(), scheduledSegment.begin(), scheduledSegment.end());
                }
            }

            // 将控制流指令按原序添加到末尾（保持基本块的控制流语义）
            newInstructions.insert(newInstructions.end(), controlFlowInsts.begin(), controlFlowInsts.end());
        }

        // 更新基本块的指令序列
        bb->setInstructions(newInstructions);
    }

    void InstructionScheduler::printSchedulingComparison(
        const std::vector<std::shared_ptr<RISCVInstruction>> &originalInstructions,
        const std::vector<std::shared_ptr<RISCVInstruction>> &scheduledInstructions,
        int originalCycles,
        int scheduledCycles) const
    {
        std::cout << "\n"
                  << std::string(80, '=') << std::endl;
        std::cout << "                    INSTRUCTION SCHEDULING COMPARISON" << std::endl;
        std::cout << std::string(80, '=') << std::endl;

        // 输出头部信息
        std::cout << std::setw(40) << std::left << "BEFORE SCHEDULING"
                  << " | " << std::setw(40) << std::left << "AFTER SCHEDULING" << std::endl;
        std::cout << std::string(40, '-') << " | " << std::string(40, '-') << std::endl;

        // 计算最大长度，确保对齐
        size_t maxInstructions = std::max(originalInstructions.size(), scheduledInstructions.size());

        // 逐行对比输出指令
        for (size_t i = 0; i < maxInstructions; ++i)
        {
            std::string leftInstr = "";
            std::string rightInstr = "";

            // 原始指令序列
            if (i < originalInstructions.size())
            {
                leftInstr = std::to_string(i) + ": " + originalInstructions[i]->toString();
                // 限制长度，防止格式错乱
                if (leftInstr.length() > 38)
                {
                    leftInstr = leftInstr.substr(0, 35) + "...";
                }
            }

            // 调度后指令序列
            if (i < scheduledInstructions.size())
            {
                rightInstr = std::to_string(i) + ": " + scheduledInstructions[i]->toString();
                if (rightInstr.length() > 38)
                {
                    rightInstr = rightInstr.substr(0, 35) + "...";
                }
            }

            // 检查指令是否相同，用颜色标记变化
            bool same = (i < originalInstructions.size() && i < scheduledInstructions.size() &&
                         originalInstructions[i]->toString() == scheduledInstructions[i]->toString());

            if (!same && i < originalInstructions.size() && i < scheduledInstructions.size())
            {
                // 用 * 标记重排的指令
                std::cout << std::setw(40) << std::left << (leftInstr + " *")
                          << " | " << std::setw(40) << std::left << (rightInstr + " *") << std::endl;
            }
            else
            {
                std::cout << std::setw(40) << std::left << leftInstr
                          << " | " << std::setw(40) << std::left << rightInstr << std::endl;
            }
        }

        std::cout << std::string(40, '-') << " | " << std::string(40, '-') << std::endl;

        // 输出性能统计信息
        std::cout << "Instructions: " << std::setw(24) << originalInstructions.size()
                  << " | Instructions: " << scheduledInstructions.size() << std::endl;

        std::cout << "Estimated Cycles: " << std::setw(19) << originalCycles
                  << " | Estimated Cycles: " << scheduledCycles << std::endl;

        if (originalCycles > 0)
        {
            double improvement = ((double)(originalCycles - scheduledCycles) / originalCycles) * 100;
            std::cout << "Improvement: " << std::setw(22) << "baseline"
                      << " | Improvement: " << std::fixed << std::setprecision(2)
                      << improvement << "%" << std::endl;
        }

        // 输出调度变化摘要
        int reorderedCount = 0;
        for (size_t i = 0; i < std::min(originalInstructions.size(), scheduledInstructions.size()); ++i)
        {
            if (originalInstructions[i]->toString() != scheduledInstructions[i]->toString())
            {
                reorderedCount++;
            }
        }

        std::cout << "\nScheduling Summary:" << std::endl;
        std::cout << "  - Instructions reordered: " << reorderedCount << std::endl;
        std::cout << "  - Cycles saved: " << std::max(0, originalCycles - scheduledCycles) << std::endl;

        if (enableDualIssue)
        {
            std::cout << "  - Dual issue enabled: Yes" << std::endl;
        }

        std::cout << std::string(80, '=') << std::endl
                  << std::endl;
    }

    void InstructionScheduler::scheduleFunction(std::shared_ptr<RISCVFunction> func)
    {
        // 遍历函数中的所有基本块并进行调度
        for (auto &bb : func->getBasicBlocks())
        {
            if (bb->getInstructions().empty())
                continue;

            scheduleBasicBlock(bb);
        }
    }

    void InstructionScheduler::scheduleModule(std::shared_ptr<RISCVModule> module)
    {
        // 遍历模块中的所有函数并进行调度
        for (auto &func : module->getFunctions())
        {
            scheduleFunction(func);
        }
    }

    void InstructionScheduler::printSchedulingStatistics() const
    {
        std::cout << "=== Instruction Scheduling Statistics ===" << std::endl;
        std::cout << "Total instructions scheduled: " << totalInstructionsScheduled << std::endl;
        std::cout << "Estimated cycles saved: " << totalCyclesSaved << std::endl;
        if (totalInstructionsScheduled > 0)
        {
            std::cout << "Average cycles saved per instruction: "
                      << static_cast<double>(totalCyclesSaved) / totalInstructionsScheduled
                      << std::endl;
        }
        std::cout << "Dual issue enabled: " << (enableDualIssue ? "Yes" : "No") << std::endl;
        std::cout << "Float optimization enabled: " << (enableFloatOptimization ? "Yes" : "No") << std::endl;
        std::cout << "Precise memory dependency enabled: " << (enablePreciseMemoryDependency ? "Yes" : "No") << std::endl;
        std::cout << "=========================================" << std::endl;
    }

    int InstructionScheduler::estimateCycles(const std::vector<std::shared_ptr<RISCVInstruction>> &instructions) const
    {
        if (instructions.empty())
            return 0;

        // 重置功能单元状态
        std::vector<FunctionalUnit> tempUnits;
        tempUnits.emplace_back(FunctionalUnitType::LOAD_STORE);
        tempUnits.emplace_back(FunctionalUnitType::MUL_DIV);
        tempUnits.emplace_back(FunctionalUnitType::BRANCH);
        tempUnits.emplace_back(FunctionalUnitType::INTEGER);
        tempUnits.emplace_back(FunctionalUnitType::INTEGER);
        tempUnits.emplace_back(FunctionalUnitType::FLOAT);

        int currentCycle = 0;

        for (size_t i = 0; i < instructions.size(); ++i)
        {
            auto &instr = instructions[i];
            auto latency = getInstructionLatency(instr->getOpcode());

            // 找到最早可用的功能单元
            int earliestAvailable = INT_MAX;
            FunctionalUnit *selectedUnit = nullptr;

            for (auto &unit : tempUnits)
            {
                if (unit.type == latency.unit)
                {
                    if (unit.busyUntil < earliestAvailable)
                    {
                        earliestAvailable = unit.busyUntil;
                        selectedUnit = &unit;
                    }
                }
            }

            if (selectedUnit)
            {
                // 考虑双发射的情况
                int startTime = std::max(currentCycle, earliestAvailable);

                // 如果启用双发射，尝试在同一周期发射下一条指令
                if (enableDualIssue && i + 1 < instructions.size())
                {
                    auto nextInstr = instructions[i + 1];
                    auto nextLatency = getInstructionLatency(nextInstr->getOpcode());

                    // 检查能否双发射
                    FunctionalUnit *nextUnit = nullptr;
                    for (auto &unit : tempUnits)
                    {
                        if (unit.type == nextLatency.unit && &unit != selectedUnit)
                        {
                            if (unit.busyUntil <= startTime)
                            {
                                // 简化的双发射兼容性检查
                                if (latency.unit != nextLatency.unit ||
                                    latency.unit == FunctionalUnitType::INTEGER)
                                {
                                    nextUnit = &unit;
                                    break;
                                }
                            }
                        }
                    }

                    if (nextUnit)
                    {
                        // 可以双发射
                        selectedUnit->busyUntil = startTime + 1;
                        nextUnit->busyUntil = startTime + 1;
                        currentCycle = startTime + 1;
                        i++; // 跳过下一条指令
                        continue;
                    }
                }

                // 单独发射
                selectedUnit->busyUntil = startTime + 1;
                currentCycle = startTime + 1;
            }
            else
            {
                // 没有可用单元，推进一个周期
                currentCycle++;
            }
        }

        // 返回最后一个单元完成的时间
        int maxEndTime = currentCycle;
        for (const auto &unit : tempUnits)
        {
            maxEndTime = std::max(maxEndTime, unit.busyUntil);
        }

        return maxEndTime;
    }

    bool InstructionScheduler::isMemoryLoadInstruction(RISCVOpcode opcode) const
    {
        return (opcode == RISCVOpcode::LD || opcode == RISCVOpcode::LW ||
                opcode == RISCVOpcode::LH || opcode == RISCVOpcode::LB ||
                opcode == RISCVOpcode::LHU || opcode == RISCVOpcode::LBU ||
                opcode == RISCVOpcode::FLW || opcode == RISCVOpcode::FLD);
    }

    bool InstructionScheduler::isMemoryStoreInstruction(RISCVOpcode opcode) const
    {
        return (opcode == RISCVOpcode::SD || opcode == RISCVOpcode::SW ||
                opcode == RISCVOpcode::SH || opcode == RISCVOpcode::SB ||
                opcode == RISCVOpcode::FSW || opcode == RISCVOpcode::FSD);
    }

    bool InstructionScheduler::mayAlias(const std::shared_ptr<RISCVInstruction> &inst1,
                                        const std::shared_ptr<RISCVInstruction> &inst2) const
    {
        // 简化的别名分析：这里采用保守策略
        // 在实际实现中，可以根据指令的操作数进行更精确的分析

        auto &operands1 = inst1->getOperands();
        auto &operands2 = inst2->getOperands();

        // 如果两个内存指令都使用相同的基址寄存器，可能存在别名
        // 这里需要根据具体的指令格式来判断
        // 简化实现：如果都是栈访问(sp寄存器)，进行更详细的分析

        // 目前采用保守策略：假设所有内存访问都可能别名
        return true;
    }

    std::vector<std::vector<std::shared_ptr<RISCVInstruction>>>
    InstructionScheduler::splitByCallInstructions(const std::vector<std::shared_ptr<RISCVInstruction>> &instructions)
    {
        std::vector<std::vector<std::shared_ptr<RISCVInstruction>>> segments;
        std::vector<std::shared_ptr<RISCVInstruction>> currentSegment;

        for (auto instr : instructions)
        {
            auto opcode = instr->getOpcode();

            // 如果遇到call指令，结束当前段，call指令单独成段，开始新段
            if (opcode == RISCVOpcode::CALL)
            {
                // 添加call之前的段
                if (!currentSegment.empty())
                {
                    segments.push_back(currentSegment);
                    currentSegment.clear();
                }

                // call指令单独成段
                segments.push_back({instr});

                // 开始新段用于call之后的指令
            }
            else
            {
                currentSegment.push_back(instr);
            }
        }

        // 添加最后一段
        if (!currentSegment.empty())
        {
            segments.push_back(currentSegment);
        }

        return segments;
    }

    bool InstructionScheduler::containsNonSchedulableInstructions(const std::vector<std::shared_ptr<RISCVInstruction>> &instructions)
    {
        for (auto instr : instructions)
        {
            if (isNonSchedulableInstruction(instr->getOpcode()))
            {
                return true;
            }
        }
        return false;
    }

    std::pair<std::vector<std::shared_ptr<RISCVInstruction>>, std::vector<std::shared_ptr<RISCVInstruction>>>
    InstructionScheduler::separateControlFlowInstructions(const std::vector<std::shared_ptr<RISCVInstruction>> &instructions)
    {
        std::vector<std::shared_ptr<RISCVInstruction>> schedulableInsts;
        std::vector<std::shared_ptr<RISCVInstruction>> controlFlowInsts;

        for (size_t i = 0; i < instructions.size(); ++i)
        {
            auto instr = instructions[i];
            auto opcode = instr->getOpcode();

            // 分支和跳转指令需要保持在末尾，不参与调度
            if (opcode == RISCVOpcode::BEQ || opcode == RISCVOpcode::BNE ||
                opcode == RISCVOpcode::BLT || opcode == RISCVOpcode::BGE ||
                opcode == RISCVOpcode::BLTU || opcode == RISCVOpcode::BGEU ||
                opcode == RISCVOpcode::JAL || opcode == RISCVOpcode::JALR ||
                opcode == RISCVOpcode::RET)
            {
                controlFlowInsts.push_back(instr);
            }
            // 检查ret前的参数mv指令
            else if (isReturnValueSetupInstruction(instr, i, instructions))
            {
                controlFlowInsts.push_back(instr);
            }
            else
            {
                schedulableInsts.push_back(instr);
            }
        }

        return {schedulableInsts, controlFlowInsts};
    }

    bool InstructionScheduler::isNonSchedulableInstruction(RISCVOpcode opcode) const
    {
        return (opcode == RISCVOpcode::CALL);
    }

    bool InstructionScheduler::isReturnValueSetupInstruction(
        std::shared_ptr<RISCVInstruction> instr,
        size_t currentIndex,
        const std::vector<std::shared_ptr<RISCVInstruction>> &instructions) const
    {
        // 检查这是否是返回值设置指令（mv a0, vr 或 mv fa0, fvr）
        if (instr->getOpcode() != RISCVOpcode::MV)
            return false;

        auto defRegs = instr->getDefRegisters();
        if (defRegs.empty())
            return false;

        auto targetReg = defRegs[0];
        if (!targetReg)
            return false;

        bool isReturnReg = false;
        if (targetReg->getType() == RegisterType::INT)
        {
            if (targetReg->isPhysical() &&
                targetReg->getPhysicalReg() == RISCVRegister::PhysicalReg::A0)
            {
                isReturnReg = true;
            }
        }
        else if (targetReg->getType() == RegisterType::FLOAT)
        {
            if (targetReg->isPhysical() &&
                targetReg->getPhysicalReg() == RISCVRegister::PhysicalReg::FA0)
            {
                isReturnReg = true;
            }
        }

        if (!isReturnReg)
            return false;

        if (currentIndex + 1 >= instructions.size())
            return false;

        if (instructions[currentIndex + 1]->getOpcode() == RISCVOpcode::RET)
            return true;

        return false;
    }

} // namespace RISCV
