#include "GraphColorRegisterAllocator.h"
#include <algorithm>
#include <iostream>
#include <cmath>

using namespace RISCV;

// ============================================================================
// 简化阶段算法实现
// ============================================================================

void GraphColorRegisterAllocator::performSimplification()
{
#ifdef DEBUG_REG_ALLOC
    std::cout << "Performing simplification phase..." << std::endl;
#endif

    int simplifiedNodes = 0;

    // 持续简化直到简化工作列表为空
    while (!worklistManager.isEmpty(WorklistManager::WorklistType::SIMPLIFY))
    {
        // 1. 识别度数小于 K 且非 move-related 的节点
        auto reg = worklistManager.getNext(WorklistManager::WorklistType::SIMPLIFY);
        if (!reg)
        {
            break; // 工作列表为空
        }

        // 验证节点确实满足简化条件
        int degree = interferenceGraph.getDegree(reg);
        int K = getK(reg->getType());
        bool moveRelated = moveList.isMoveRelated(reg);

        // 2. 从冲突图中移除选中的节点并压入栈
        // 获取邻居列表的副本，因为removeNode会修改原始数据
        auto neighbors = interferenceGraph.getNeighbors(reg);
        vector<shared_ptr<RISCVRegister>> neighborList(neighbors.begin(),
                                                       neighbors.end());

        // 移除节点（这会自动更新所有邻居的度数）
        interferenceGraph.removeNode(reg);
        worklistManager.removeFromWorklist(reg);

#ifdef DEBUG_REG_ALLOC
        std::cout << "Removing node: " << reg->toString()
                  << " (degree=" << degree
                  << ", move-related=" << moveRelated << ")"
                  << " Neighbors: " << neighborList.size() << std::endl;
        for (const auto &neighbor : neighborList)
        {
            std::cout << neighbor->toString() << " ";
        }
        std::cout << std::endl;
#endif

        // 将节点压入选择栈
        selectStack.push(reg);
        setNodeState(reg, NodeState::COLORED); // 标记为待着色状态

        for (auto neighbor : neighborList)
        {
            classifyNode(neighbor);
        }

        simplifiedNodes++;

        // 打印当前工作列表状态
#ifdef DEBUG_REG_ALLOC
        std::cout << "After simplifying " << reg->toString() << ":" << std::endl;
        worklistManager.printWorklistSizes();
#endif
    }

#ifdef DEBUG_REG_ALLOC
    std::cout << "Simplification phase completed." << std::endl;
    std::cout << "Total nodes simplified: " << simplifiedNodes << std::endl;
    std::cout << "Nodes in selection stack: " << selectStack.size() << std::endl;

    // 验证简化结果
    if (!worklistManager.isEmpty(WorklistManager::WorklistType::SIMPLIFY))
    {
        std::cout
            << "Warning: Simplification ended but simplify worklist is not empty!"
            << std::endl;
    }
#endif
}

// ============================================================================
// 合并阶段算法实现
// ============================================================================

void GraphColorRegisterAllocator::performCoalescing()
{
#ifdef DEBUG_REG_ALLOC
    std::cout << "Performing coalescing phase..." << std::endl;
    moveList.printWorklistMoves();
#endif

    // 获取下一个可处理的move
    auto candidatePair = moveList.getNextWorklistMove();

    if (!candidatePair.first || !candidatePair.second)
    {
        return;
    }

    auto reg1 = candidatePair.first;
    auto reg2 = candidatePair.second;

    // 检查是否可以安全合并
    if (canSafelyCoalesce(reg1, reg2))
    {
        // 执行合并
        executeCoalescing(reg1, reg2);

#ifdef DEBUG_REG_ALLOC
        std::cout << "Successfully coalesced " << reg1->toString() << " and "
                  << reg2->toString() << std::endl;
#endif
    }
    else
    {
        // 标记move为受限
        moveList.markMoveAsProcessed(reg1, reg2, MoveState::CONSTRAINED);
    }
}

// 选择合并候选节点对
std::pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>>
GraphColorRegisterAllocator::selectCoalescingCandidate()
{
    // 遍历所有move指令，寻找可以合并的节点对
    const auto &allMoves = moveList.getAllMoves();

    for (const auto &move : allMoves)
    {
        // 只考虑工作列表中的move（未被冻结或约束的）
        if (move.state != MoveState::WORKLIST_MOVES)
        {
            continue;
        }

        auto src = move.src;
        auto dst = move.dst;

        // 检查两个寄存器是否都还在图中且可以合并
        if (interferenceGraph.getNodes().find(src) ==
                interferenceGraph.getNodes().end() ||
            interferenceGraph.getNodes().find(dst) ==
                interferenceGraph.getNodes().end())
        {
            continue;
        }

        // 跳过已经合并的节点
        if (getNodeState(src) == NodeState::COALESCED ||
            getNodeState(dst) == NodeState::COALESCED)
        {
            continue;
        }

        // 检查是否可以合并（基本条件检查）
        if (moveList.canCoalesce(src, dst))
        {
            return std::make_pair(src, dst);
        }
    }

    // 没有找到合适的候选
    return std::make_pair(nullptr, nullptr);
}

// 检查是否可以安全合并两个节点
bool GraphColorRegisterAllocator::canSafelyCoalesce(
    shared_ptr<RISCVRegister> reg1, shared_ptr<RISCVRegister> reg2)
{
    // 基本检查：不能合并已经冲突的节点
    if (interferenceGraph.interferes(reg1, reg2))
    {
        return false;
    }

    // 不能合并不同类型的寄存器
    if (reg1->getType() != reg2->getType())
    {
        return false;
    }

    // 如果其中一个是预着色寄存器，需要特殊处理
    bool reg1Precolored = isPrecolored(reg1);
    bool reg2Precolored = isPrecolored(reg2);

    if (reg1Precolored && reg2Precolored)
    {
        // 两个都是预着色寄存器，只有当它们是同一个寄存器时才能合并
        bool canMerge = (reg1->getPhysicalReg() == reg2->getPhysicalReg());
        return canMerge;
    }

    if (reg1Precolored || reg2Precolored)
    {
        // 一个预着色，一个虚拟寄存器的情况
        auto virtualReg = reg1Precolored ? reg2 : reg1;
        auto precoloredReg = reg1Precolored ? reg1 : reg2;

        // 修改后的检查：不再检查虚拟寄存器的邻居是否与预着色寄存器冲突
        // 因为这种冲突是正常的，不会影响图的可着色性

        // 只需确保虚拟寄存器的邻居中，已经是预着色寄存器的节点
        // 不与我们要合并的预着色寄存器冲突（它们必须是不同的物理寄存器）
        const auto &neighbors = interferenceGraph.getNeighbors(virtualReg);
        for (auto neighbor : neighbors)
        {
            if (isPrecolored(neighbor) &&
                neighbor->getPhysicalReg() == precoloredReg->getPhysicalReg())
            {
                return false;
            }
        }

        // 对于虚拟寄存器的邻居，我们不需要额外检查
        // 它们与预着色寄存器的冲突是正常的，不会影响可着色性
        return true;
    }

    // 两个都是虚拟寄存器，使用Briggs保守启发式
    return briggsConservativeHeuristic(reg1, reg2);
}

// Briggs保守启发式安全性检查
bool GraphColorRegisterAllocator::briggsConservativeHeuristic(
    shared_ptr<RISCVRegister> reg1, shared_ptr<RISCVRegister> reg2)
{
    // Briggs启发式：合并后的节点N，其邻居中度数>=K的节点数量 < K
    // 这确保了合并不会使可着色的图变为不可着色的图

    int K = getK(reg1->getType());

    // 获取两个节点的邻居并集
    auto unionNeighbors = getUnionOfNeighbors(reg1, reg2);

    // 计算度数 >= K 的邻居数量
    int significantNeighbors = 0;
    for (auto neighbor : unionNeighbors)
    {
        if (interferenceGraph.getDegree(neighbor) >= K)
        {
            significantNeighbors++;
        }
    }

    bool isSafe = significantNeighbors < K;

#ifdef DEBUG_REG_ALLOC
    std::cout << "Briggs heuristic: K=" << K
              << ", significant neighbors=" << significantNeighbors
              << ", safe=" << (isSafe ? "yes" : "no") << std::endl;
#endif

    return isSafe;
}

// 获取两个节点的邻居并集
vector<shared_ptr<RISCVRegister>>
GraphColorRegisterAllocator::getUnionOfNeighbors(
    shared_ptr<RISCVRegister> reg1, shared_ptr<RISCVRegister> reg2)
{
    unordered_set<shared_ptr<RISCVRegister>, RegisterHash, RegisterEqual>
        unionSet;

    // 添加reg1的邻居
    const auto &neighbors1 = interferenceGraph.getNeighbors(reg1);
    for (auto neighbor : neighbors1)
    {
        if (neighbor != reg2) // 排除reg2本身
        {
            unionSet.insert(neighbor);
        }
    }

    // 添加reg2的邻居
    const auto &neighbors2 = interferenceGraph.getNeighbors(reg2);
    for (auto neighbor : neighbors2)
    {
        if (neighbor != reg1) // 排除reg1本身
        {
            unionSet.insert(neighbor);
        }
    }

    // 转换为vector返回
    return vector<shared_ptr<RISCVRegister>>(unionSet.begin(), unionSet.end());
}

// 执行节点合并操作
void GraphColorRegisterAllocator::executeCoalescing(
    shared_ptr<RISCVRegister> reg1, shared_ptr<RISCVRegister> reg2)
{
    // 决定哪个节点保留，哪个节点被合并
    // 通常保留预着色寄存器，或者保留度数较高的节点
    shared_ptr<RISCVRegister> keepReg, mergeReg;

    if (isPrecolored(reg1))
    {
        keepReg = reg1;
        mergeReg = reg2;
    }
    else if (isPrecolored(reg2))
    {
        keepReg = reg2;
        mergeReg = reg1;
    }
    else
    {
        // 两个都是虚拟寄存器，保留度数较高的
        if (interferenceGraph.getDegree(reg1) >=
            interferenceGraph.getDegree(reg2))
        {
            keepReg = reg1;
            mergeReg = reg2;
        }
        else
        {
            keepReg = reg2;
            mergeReg = reg1;
        }
    }

#ifdef DEBUG_REG_ALLOC
    std::cout << "Merging " << mergeReg->toString() << " into "
              << keepReg->toString() << std::endl;
#endif

    // 获取被合并节点的邻居列表（在修改图之前）
    auto mergeNeighbors = interferenceGraph.getNeighbors(mergeReg);
    vector<shared_ptr<RISCVRegister>> affectedNodes(mergeNeighbors.begin(),
                                                    mergeNeighbors.end());

    // 4. 处理合并后的 move 状态更新和节点重新分类

    // 更新move指令状态
    moveList.coalesceMoves(reg1, reg2);

    coalescingManager.addPair(keepReg, mergeReg);

    // 在冲突图中执行合并
    interferenceGraph.coalesceNodes(keepReg, mergeReg);
    readInterferenceGraph.coalesceNodes(keepReg, mergeReg);

    // 标记被合并的节点状态
    setNodeState(mergeReg, NodeState::COALESCED);

    // 从工作列表中移除被合并的节点
    worklistManager.removeFromWorklist(mergeReg);

    // 重新分类保留的节点
    reclassifyNode(keepReg);

    // 重新分类所有受影响的邻居节点
    reclassifyAffectedNodes(affectedNodes);

#ifdef DEBUG_REG_ALLOC
    std::cout << "Coalescing completed. Kept node: " << keepReg->toString()
              << " (new degree=" << interferenceGraph.getDegree(keepReg) << ")"
              << std::endl;
#endif
}

// ============================================================================
// 冻结阶段算法实现
// ============================================================================

void GraphColorRegisterAllocator::performFreezing()
{
#ifdef DEBUG_REG_ALLOC
    std::cout << "Performing freezing phase..." << std::endl;
#endif

    // 从冻结工作列表中获取一个节点
    auto reg = worklistManager.getNext(WorklistManager::WorklistType::FREEZE);
    if (!reg)
    {
        return;
    }

#ifdef DEBUG_REG_ALLOC
    std::cout << "Freezing node: " << reg->toString() << std::endl;
#endif

    // 冻结与该节点相关的所有move指令
    moveList.freezeMoves(reg);

    // 将节点重新分类为可简化节点
    setNodeState(reg, NodeState::SIMPLIFY_READY);
    worklistManager.addToWorklist(reg, WorklistManager::WorklistType::SIMPLIFY);

#ifdef DEBUG_REG_ALLOC
    std::cout << "Node " << reg->toString()
              << " frozen and moved to simplify worklist" << std::endl;
#endif
    worklistManager.printWorklistSizes();
}

// ============================================================================
// 溢出选择算法实现
// ============================================================================

void GraphColorRegisterAllocator::selectSpillCandidates()
{
#ifdef DEBUG_REG_ALLOC
    std::cout << "Performing spill candidate selection..." << std::endl;
#endif

    shared_ptr<RISCVRegister> bestReg = nullptr;
    double minCost = std::numeric_limits<double>::max();
    for (auto reg : worklistManager.getAllNodes(WorklistManager::WorklistType::SPILL))
    {
        double cost = calculateSpillCost(reg);
        if (cost < minCost)
        {
            minCost = cost;
            bestReg = reg;
        }
    }

    if (!bestReg)
    {
        return;
    }

#ifdef DEBUG_REG_ALLOC
    std::cout << "Selected spill candidate: " << bestReg->toString()
              << " with spill cost: " << minCost << std::endl;
#endif

    // 将节点标记为可简化节点（暂时不实际溢出，只是将其简化）
    worklistManager.removeFromWorklist(bestReg);
    setNodeState(bestReg, NodeState::SIMPLIFY_READY);
    worklistManager.addToWorklist(bestReg, WorklistManager::WorklistType::SIMPLIFY);

    // 记录这个节点是潜在的溢出候选
    spilledRegs.insert(bestReg);

#ifdef DEBUG_REG_ALLOC
    std::cout << "Node " << bestReg->toString()
              << " marked as potential spill and moved to simplify worklist"
              << std::endl;
#endif
    worklistManager.printWorklistSizes();
}

// 计算溢出代价
double GraphColorRegisterAllocator::calculateSpillCost(shared_ptr<RISCVRegister> reg)
{
    // 获取寄存器的使用和定义次数

    int useCount = 0;
    int defCount = 0;
    double loopDepthSum = 0.0;
    int loopDepthCount = 0;

    // 遍历所有基本块和指令，统计寄存器的使用和定义次数及循环深度
    for (auto &bb : currentFunc->getBasicBlocks())
    {
        double bbLoopDepth = 1.0; // 实际应从循环分析获取
        for (auto &instr : bb->getInstructions())
        {
            for (auto useReg : instr->getUseRegisters())
            {
                if (useReg == reg)
                {
                    useCount++;
                    loopDepthSum += bbLoopDepth;
                    loopDepthCount++;
                }
            }
            for (auto defReg : instr->getDefRegisters())
            {
                if (defReg == reg)
                {
                    defCount++;
                    loopDepthSum += bbLoopDepth;
                    loopDepthCount++;
                }
            }
        }
    }

    double avgLoopDepth = loopDepthCount ? (loopDepthSum / loopDepthCount) : 1.0;

    // 获取寄存器的活跃长度
    const auto &livenessInfo = currentFunc->getLivenessInfo();
    int liveLength = 0;

    if (livenessInfo.liveRanges.find(reg) != livenessInfo.liveRanges.end())
    {
        const auto &ranges = livenessInfo.liveRanges.at(reg);
        for (const auto &range : ranges)
        {
            liveLength += (range.end - range.start);
        }
    }

    // 分母加1，避免除以零并平滑极端情况
    double cost = (useCount + defCount) * std::pow(2, avgLoopDepth) / (liveLength + 1);

    return cost;
}
