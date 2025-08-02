#include "GraphColorRegisterAllocator.h"
#include <algorithm>
#include <iostream>
#include <cmath>

using namespace RISCV;
// ============================================================================
// MoveList 实现
// ============================================================================

void MoveList::addMove(shared_ptr<RISCVRegister> src,
                       shared_ptr<RISCVRegister> dst,
                       shared_ptr<RISCVInstruction> instr)
{
    int moveIndex = moves.size();
    moves.emplace_back(src, dst, instr);
    moves.back().state = MoveState::WORKLIST_MOVES;

    // 建立寄存器到move的映射
    regToMoves[src].push_back(moveIndex);
    regToMoves[dst].push_back(moveIndex);
}

bool MoveList::isMoveRelated(shared_ptr<RISCVRegister> reg) const
{
    auto it = regToMoves.find(reg);
    if (it == regToMoves.end())
        return false;

    // 检查是否有活跃的move
    for (int moveIndex : it->second)
    {
        if (moves[moveIndex].state == MoveState::WORKLIST_MOVES ||
            moves[moveIndex].state == MoveState::ACTIVE_MOVES)
        {
            return true;
        }
    }
    return false;
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

void MoveList::coalesceMoves(shared_ptr<RISCVRegister> keepReg,
                             shared_ptr<RISCVRegister> mergeReg)
{
    auto it1 = regToMoves.find(keepReg);
    auto it2 = regToMoves.find(mergeReg);
    if (it1 == regToMoves.end() || it2 == regToMoves.end())
        return;

    // 替换所有 move 的 src/dst
    for (auto &move : moves)
    {
        if (*move.src == *mergeReg)
            move.src = keepReg;
        if (*move.dst == *mergeReg)
            move.dst = keepReg;
    }

    // 合并 move 索引并去重
    auto &vec = regToMoves[keepReg];
    vec.insert(vec.end(), it2->second.begin(), it2->second.end());
    std::sort(vec.begin(), vec.end());
    vec.erase(std::unique(vec.begin(), vec.end()), vec.end());
    regToMoves.erase(mergeReg);

    // 更新所有 move 的状态
    for (auto &move : moves)
    {
        if (*move.src == *keepReg && *move.dst == *keepReg)
            move.state = MoveState::COALESCED;
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
#ifdef DEBUG_REG_ALLOC
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
#endif
}

// 检查是否还有可以处理的move指令（状态为WORKLIST_MOVES）
bool MoveList::hasWorklistMoves() const
{
    for (const auto &move : moves)
    {
        if (move.state == MoveState::WORKLIST_MOVES)
        {
            return true;
        }
    }
    return false;
}

// 检查MoveList是否完全为空（用于你的循环条件）
bool MoveList::isEmpty() const
{
    return hasWorklistMoves() == false;
}

// 获取下一个可处理的move（用于合并阶段）
std::pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>> MoveList::getNextWorklistMove()
{
    for (auto &move : moves)
    {
        if (move.state == MoveState::WORKLIST_MOVES)
        {
            return std::make_pair(move.src, move.dst);
        }
    }
    return std::make_pair(nullptr, nullptr);
}

// 将指定的move标记为已处理（从WORKLIST_MOVES移除）
void MoveList::markMoveAsProcessed(shared_ptr<RISCVRegister> src, shared_ptr<RISCVRegister> dst, MoveState newState)
{
    for (auto &move : moves)
    {
        if (move.src == src && move.dst == dst && move.state == MoveState::WORKLIST_MOVES)
        {
            move.state = newState;
            break;
        }
    }
}

// 获取工作列表中move的数量（用于调试）
size_t MoveList::getWorklistMoveCount() const
{
    size_t count = 0;
    for (const auto &move : moves)
    {
        if (move.state == MoveState::WORKLIST_MOVES)
        {
            count++;
        }
    }
    return count;
}

// 调试输出工作列表中的move
void MoveList::printWorklistMoves() const
{
    std::cout << "Worklist moves (" << getWorklistMoveCount() << "):" << std::endl;
    for (const auto &move : moves)
    {
        if (move.state == MoveState::WORKLIST_MOVES)
        {
            std::cout << "  " << move.src->toString() << " -> " << move.dst->toString() << std::endl;
        }
    }
}
MoveState MoveList::getMoveState(shared_ptr<RISCVInstruction> instr)
{
    for (const auto &move : moves)
    {
        if (move.instruction == instr)
        {
            return move.state;
        }
    }
    return MoveState::WORKLIST_MOVES; // 默认返回工作列表状态
}