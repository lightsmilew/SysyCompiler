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
    std::cout << "Performing simplification phase..." << std::endl;

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

        if (degree >= K || moveRelated)
        {
            std::cout << "Warning: Node " << reg->toString()
                      << " no longer satisfies simplification conditions (degree="
                      << degree << ", K=" << K << ", move-related=" << moveRelated
                      << ")" << std::endl;
            // 重新分类节点
            classifyNode(reg);
            continue;
        }

        std::cout << "Simplifying node: " << reg->toString()
                  << " (degree=" << degree << ", K=" << K << ")" << std::endl;

        // 2. 从冲突图中移除选中的节点并压入栈
        // 获取邻居列表的副本，因为removeNode会修改原始数据
        auto neighbors = interferenceGraph.getNeighbors(reg);
        vector<shared_ptr<RISCVRegister>> neighborList(neighbors.begin(),
                                                       neighbors.end());

        // 移除节点（这会自动更新所有邻居的度数）
        interferenceGraph.removeNode(reg);

        // 将节点压入选择栈
        selectStack.push(reg);
        setNodeState(reg, NodeState::COLORED); // 标记为待着色状态

        std::cout << "Node " << reg->toString()
                  << " removed from graph and pushed to stack" << std::endl;

        // 3. 更新被移除节点的所有邻居的度数（已由removeNode自动完成）
        // 4. 重新分类受影响的节点到相应工作列表
        for (auto neighbor : neighborList)
        {
            // 跳过预着色寄存器和已经被移除的节点
            if (isPrecolored(neighbor) ||
                getNodeState(neighbor) == NodeState::COLORED)
            {
                continue;
            }

            // 获取邻居的新度数
            int newDegree = interferenceGraph.getDegree(neighbor);
            int neighborK = getK(neighbor->getType());
            bool neighborMoveRelated = moveList.isMoveRelated(neighbor);

            std::cout << "Updating neighbor " << neighbor->toString()
                      << " (new degree=" << newDegree << ", K=" << neighborK
                      << ", move-related=" << neighborMoveRelated << ")" << std::endl;

            // 从当前工作列表中移除邻居节点
            worklistManager.removeFromWorklist(neighbor);

            // 重新分类邻居节点
            classifyNode(neighbor);
        }

        simplifiedNodes++;

        // 打印当前工作列表状态
        std::cout << "After simplifying " << reg->toString() << ":" << std::endl;
        worklistManager.printWorklistSizes();
    }

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
}

// ============================================================================
// 合并阶段算法实现
// ============================================================================

void GraphColorRegisterAllocator::performCoalescing()
{
    std::cout << "Performing coalescing phase..." << std::endl;

    int coalescedPairs = 0;
    int constrainedPairs = 0;

    // 持续尝试合并直到没有更多可合并的节点对
    while (true)
    {
        // 1. 选择 move-related 的节点对作为合并候选
        auto candidatePair = selectCoalescingCandidate();

        if (!candidatePair.first || !candidatePair.second)
        {
            // 没有更多可合并的候选
            break;
        }

        auto reg1 = candidatePair.first;
        auto reg2 = candidatePair.second;

        std::cout << "Considering coalescing: " << reg1->toString() << " and "
                  << reg2->toString() << std::endl;

        // 2. 实现 Briggs 保守启发式安全性检查
        if (canSafelyCoalesce(reg1, reg2))
        {
            std::cout << "Coalescing is safe, executing merge..." << std::endl;

            // 3. 执行节点合并操作，更新冲突图结构
            executeCoalescing(reg1, reg2);
            coalescedPairs++;

            std::cout << "Successfully coalesced " << reg1->toString() << " and "
                      << reg2->toString() << std::endl;
        }
        else
        {
            std::cout << "Coalescing is not safe, marking moves as constrained"
                      << std::endl;

            // 标记相关的move为受限
            moveList.constrainMoves(reg1, reg2);
            constrainedPairs++;
        }

        // 打印当前工作列表状态
        worklistManager.printWorklistSizes();
    }

    std::cout << "Coalescing phase completed." << std::endl;
    std::cout << "Successfully coalesced pairs: " << coalescedPairs << std::endl;
    std::cout << "Constrained pairs: " << constrainedPairs << std::endl;
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
        std::cout << "Cannot coalesce: nodes already interfere" << std::endl;
        return false;
    }

    // 不能合并不同类型的寄存器
    if (reg1->getType() != reg2->getType())
    {
        std::cout << "Cannot coalesce: different register types" << std::endl;
        return false;
    }

    // 如果其中一个是预着色寄存器，需要特殊处理
    bool reg1Precolored = isPrecolored(reg1);
    bool reg2Precolored = isPrecolored(reg2);

    if (reg1Precolored && reg2Precolored)
    {
        // 两个都是预着色寄存器，只有当它们是同一个寄存器时才能合并
        bool canMerge = (reg1->getPhysicalReg() == reg2->getPhysicalReg());
        if (!canMerge)
        {
            std::cout
                << "Cannot coalesce: both precolored but different physical registers"
                << std::endl;
        }
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
                std::cout << "Cannot coalesce: virtual register has a neighbor with "
                             "the same physical register"
                          << std::endl;
                return false;
            }
        }

        // 对于虚拟寄存器的邻居，我们不需要额外检查
        // 它们与预着色寄存器的冲突是正常的，不会影响可着色性
        std::cout << "Safe to coalesce virtual register with precolored register"
                  << std::endl;
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

    std::cout << "Briggs heuristic: K=" << K
              << ", significant neighbors=" << significantNeighbors
              << ", safe=" << (isSafe ? "yes" : "no") << std::endl;

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

    std::cout << "Merging " << mergeReg->toString() << " into "
              << keepReg->toString() << std::endl;

    // 获取被合并节点的邻居列表（在修改图之前）
    auto mergeNeighbors = interferenceGraph.getNeighbors(mergeReg);
    vector<shared_ptr<RISCVRegister>> affectedNodes(mergeNeighbors.begin(),
                                                    mergeNeighbors.end());

    // 4. 处理合并后的 move 状态更新和节点重新分类

    // 更新move指令状态
    moveList.coalesceMoves(reg1, reg2);

    // 在冲突图中执行合并
    interferenceGraph.coalesceNodes(keepReg, mergeReg);

    // 标记被合并的节点状态
    setNodeState(mergeReg, NodeState::COALESCED);

    // 从工作列表中移除被合并的节点
    worklistManager.removeFromWorklist(mergeReg);

    // 重新分类保留的节点
    reclassifyNode(keepReg);

    // 重新分类所有受影响的邻居节点
    reclassifyAffectedNodes(affectedNodes);

    std::cout << "Coalescing completed. Kept node: " << keepReg->toString()
              << " (new degree=" << interferenceGraph.getDegree(keepReg) << ")"
              << std::endl;
}

// ============================================================================
// 冻结阶段算法实现
// ============================================================================

void GraphColorRegisterAllocator::performFreezing()
{
    std::cout << "Performing freezing phase..." << std::endl;

    // 从冻结工作列表中获取一个节点
    auto reg = worklistManager.getNext(WorklistManager::WorklistType::FREEZE);
    if (!reg)
    {
        std::cout << "No nodes in freeze worklist" << std::endl;
        return;
    }

    std::cout << "Freezing node: " << reg->toString() << std::endl;

    // 冻结与该节点相关的所有move指令
    moveList.freezeMoves(reg);

    // 将节点重新分类为可简化节点
    setNodeState(reg, NodeState::SIMPLIFY_READY);
    worklistManager.addToWorklist(reg, WorklistManager::WorklistType::SIMPLIFY);

    std::cout << "Node " << reg->toString()
              << " frozen and moved to simplify worklist" << std::endl;
    worklistManager.printWorklistSizes();
}

// ============================================================================
// 溢出选择算法实现
// ============================================================================

void GraphColorRegisterAllocator::selectSpillCandidates()
{
    std::cout << "Performing spill candidate selection..." << std::endl;

    // 从溢出工作列表中获取一个节点
    auto reg = worklistManager.getNext(WorklistManager::WorklistType::SPILL);
    if (!reg)
    {
        std::cout << "No nodes in spill worklist" << std::endl;
        return;
    }

    // 计算溢出代价
    double spillCost = calculateSpillCost(reg);
    std::cout << "Selected spill candidate: " << reg->toString()
              << " with spill cost: " << spillCost << std::endl;

    // 将节点标记为可简化节点（暂时不实际溢出，只是将其简化）
    setNodeState(reg, NodeState::SIMPLIFY_READY);
    worklistManager.addToWorklist(reg, WorklistManager::WorklistType::SIMPLIFY);

    // 记录这个节点是潜在的溢出候选
    spilledRegs.insert(reg);

    std::cout << "Node " << reg->toString()
              << " marked as potential spill and moved to simplify worklist"
              << std::endl;
    worklistManager.printWorklistSizes();
}

// 计算溢出代价
double GraphColorRegisterAllocator::calculateSpillCost(shared_ptr<RISCVRegister> reg)
{
    // 获取寄存器的使用和定义次数
    int useCount = 0;
    int defCount = 0;
    double avgLoopDepth = 1.0; // 默认循环深度为1

    // 遍历所有基本块和指令，统计寄存器的使用和定义次数
    for (auto &bb : currentFunc->getBasicBlocks())
    {
        // 获取基本块的循环深度（如果有循环分析信息）
        // 这里简化处理，实际应该从循环分析中获取
        double bbLoopDepth = 1.0;

        for (auto &instr : bb->getInstructions())
        {
            // 检查使用
            for (auto useReg : instr->getUseRegisters())
            {
                if (useReg == reg)
                {
                    useCount++;
                    avgLoopDepth = std::max(avgLoopDepth, bbLoopDepth);
                }
            }

            // 检查定义
            for (auto defReg : instr->getDefRegisters())
            {
                if (defReg == reg)
                {
                    defCount++;
                    avgLoopDepth = std::max(avgLoopDepth, bbLoopDepth);
                }
            }
        }
    }

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

    // 避免除以零
    liveLength = std::max(1, liveLength);

    // 计算溢出代价：使用频率越高、循环深度越深，溢出代价越高
    // 活跃长度越长，溢出代价相对较低（因为占用寄存器时间长）
    double cost =
        (useCount + defCount) * std::pow(2, avgLoopDepth) / liveLength;

    std::cout << "Spill cost calculation for " << reg->toString() << ":"
              << std::endl;
    std::cout << "- Use count: " << useCount << std::endl;
    std::cout << "- Def count: " << defCount << std::endl;
    std::cout << "- Avg loop depth: " << avgLoopDepth << std::endl;
    std::cout << "- Live length: " << liveLength << std::endl;
    std::cout << "- Calculated cost: " << cost << std::endl;

    return cost;
}

bool MoveList::canCoalesce(shared_ptr<RISCVRegister> reg1,
                           shared_ptr<RISCVRegister> reg2) const
{
    auto it1 = regToMoves.find(reg1);
    auto it2 = regToMoves.find(reg2);

    if (it1 == regToMoves.end() || it2 == regToMoves.end())
        return false;

    // 查找连接这两个寄存器的move指令
    for (int moveIndex1 : it1->second)
    {
        for (int moveIndex2 : it2->second)
        {
            if (moveIndex1 == moveIndex2)
            {
                const auto &move = moves[moveIndex1];
                if (move.state == MoveState::WORKLIST_MOVES &&
                    ((move.src == reg1 && move.dst == reg2) ||
                     (move.src == reg2 && move.dst == reg1)))
                {
                    return true;
                }
            }
        }
    }
    return false;
}

void MoveList::freezeMoves(shared_ptr<RISCVRegister> reg)
{
    auto it = regToMoves.find(reg);
    if (it == regToMoves.end())
        return;

    for (int moveIndex : it->second)
    {
        if (moves[moveIndex].state == MoveState::WORKLIST_MOVES ||
            moves[moveIndex].state == MoveState::ACTIVE_MOVES)
        {
            moves[moveIndex].state = MoveState::FROZEN;
        }
    }
}

void MoveList::coalesceMoves(shared_ptr<RISCVRegister> reg1,
                             shared_ptr<RISCVRegister> reg2)
{
    auto it1 = regToMoves.find(reg1);
    auto it2 = regToMoves.find(reg2);

    if (it1 == regToMoves.end() || it2 == regToMoves.end())
        return;

    // 查找并标记相关的move为已合并
    for (int moveIndex1 : it1->second)
    {
        for (int moveIndex2 : it2->second)
        {
            if (moveIndex1 == moveIndex2)
            {
                const auto &move = moves[moveIndex1];
                if ((move.src == reg1 && move.dst == reg2) ||
                    (move.src == reg2 && move.dst == reg1))
                {
                    moves[moveIndex1].state = MoveState::COALESCED;
                }
            }
        }
    }
}

void MoveList::constrainMoves(shared_ptr<RISCVRegister> reg1,
                              shared_ptr<RISCVRegister> reg2)
{
    auto it1 = regToMoves.find(reg1);
    auto it2 = regToMoves.find(reg2);

    if (it1 == regToMoves.end() || it2 == regToMoves.end())
        return;

    // 查找并标记相关的move为受限
    for (int moveIndex1 : it1->second)
    {
        for (int moveIndex2 : it2->second)
        {
            if (moveIndex1 == moveIndex2)
            {
                const auto &move = moves[moveIndex1];
                if ((move.src == reg1 && move.dst == reg2) ||
                    (move.src == reg2 && move.dst == reg1))
                {
                    moves[moveIndex1].state = MoveState::CONSTRAINED;
                }
            }
        }
    }
}

vector<int> MoveList::getRelatedMoves(shared_ptr<RISCVRegister> reg) const
{
    auto it = regToMoves.find(reg);
    return it != regToMoves.end() ? it->second : vector<int>();
}

void MoveList::printMoves() const
{
    std::cout << "=== Move Instructions ===" << std::endl;
    for (size_t i = 0; i < moves.size(); i++)
    {
        const auto &move = moves[i];
        std::cout << i << ": " << move.src->toString() << " -> "
                  << move.dst->toString();
        std::cout << " (";
        switch (move.state)
        {
        case MoveState::COALESCED:
            std::cout << "COALESCED";
            break;
        case MoveState::CONSTRAINED:
            std::cout << "CONSTRAINED";
            break;
        case MoveState::FROZEN:
            std::cout << "FROZEN";
            break;
        case MoveState::WORKLIST_MOVES:
            std::cout << "WORKLIST";
            break;
        case MoveState::ACTIVE_MOVES:
            std::cout << "ACTIVE";
            break;
        }
        std::cout << ")" << std::endl;
    }
    std::cout << "=========================" << std::endl;
}
