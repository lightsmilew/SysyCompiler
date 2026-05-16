#include "PeepPass.h"
#include <algorithm>
#include <limits>
#include <tuple>
#include <optional>
#include "../DefUse.h"

using namespace RISCV;
using namespace std;

namespace
{
    bool containsRegister(const vector<shared_ptr<RISCVRegister>> &regs, const shared_ptr<RISCVRegister> &target)
    {
        for (const auto &reg : regs)
        {
            if (reg && target && *reg == *target)
            {
                return true;
            }
        }
        return false;
    }
}

// ========== RemoveRedundantMovePass ==========

PeepOptiState RemoveRedundantMovePass::optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb)
{
    if (isRedundantMove(instr))
    {
        // 删除多余的move指令
        return PeepOptiState::DELETE;
    }

    return PeepOptiState::KEEP;
}

bool RemoveRedundantMovePass::isRedundantMove(shared_ptr<RISCVInstruction> instr)
{
    // 检查是否为 move 指令（通常是 mv rd, rs 或 addi rd, rs, 0）
    auto opcode = instr->getOpcode();

    // 检查 addi rd, rs, 0 形式的move
    if (opcode == RISCVOpcode::ADDI)
    {
        auto operands = instr->getOperands();
        if (operands.size() == 3)
        {
            if (operands[2]->getType() == RISCVOperand::Type::IMMEDIATE &&
                operands[2]->getImmediate() == 0)
            {
                // 检查源寄存器和目标寄存器是否相同
                if (operands[0]->getType() == RISCVOperand::Type::REGISTER &&
                    operands[1]->getType() == RISCVOperand::Type::REGISTER &&
                    *operands[0]->getReg() == *operands[1]->getReg())
                {
                    return true;
                }
            }
        }
    }

    // 检查 mv rd, rs 形式的move（如果有这种指令）
    if (opcode == RISCVOpcode::MV)
    {
        auto operands = instr->getOperands();
        if (operands.size() == 2)
        {
            if (operands[0]->getType() == RISCVOperand::Type::REGISTER &&
                operands[1]->getType() == RISCVOperand::Type::REGISTER &&
                *operands[0]->getReg() == *operands[1]->getReg())
            {
                return true;
            }
        }
    }

    return false;
    }

PeepOptiState FoldAdjacentMoveAndAddressPass::optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb)
{
    if (!instr || !bb)
        return PeepOptiState::KEEP;

    auto &instrs = bb->getInstructions();
    auto it = find(instrs.begin(), instrs.end(), instr);
    if (it == instrs.end())
        return PeepOptiState::KEEP;

    auto currentOpcode = instr->getOpcode();
    auto parentFunc = bb->getParentFunc();

    // 1) li + mv -> li (替换 mv 为 li，删除原 li)
    if (instr->getOpcode() == RISCVOpcode::LI)
    {
        auto nextIt = it;
        ++nextIt;
        if (nextIt != instrs.end() && (*nextIt)->getOpcode() == RISCVOpcode::MV)
        {
            auto liOps = instr->getOperands();
            auto mvOps = (*nextIt)->getOperands();
            if (liOps.size() >= 2 && mvOps.size() >= 2 &&
                liOps[0]->getType() == RISCVOperand::Type::REGISTER &&
                mvOps[1]->getType() == RISCVOperand::Type::REGISTER &&
                *liOps[0]->getReg() == *mvOps[1]->getReg())
            {
                auto dest = mvOps[0]->getReg();
                int64_t imm = liOps[1]->getImmediate();
                auto newLi = RISCVInstruction::createPseudoLI(dest, imm);
                (*nextIt)->replaceInstruction(newLi);
                return PeepOptiState::DELETE;
            }
        }
    }

    // 2) addi/addiw + load/store（跨基本块全量融合：一次收集所有使用点并同时替换）
    if (!parentFunc || !isAddressCalcOpcode(currentOpcode))
        return PeepOptiState::KEEP;

    auto addrOps = instr->getOperands();
    if (addrOps.size() < 3 || !addrOps[0] || !addrOps[1] || !addrOps[2])
        return PeepOptiState::KEEP;

    auto addrDestReg = addrOps[0]->getReg();
    auto baseReg = addrOps[1]->getReg();
    auto addrImmOp = addrOps[2];
    if (!addrDestReg || !baseReg || addrImmOp->getType() != RISCVOperand::Type::IMMEDIATE)
        return PeepOptiState::KEEP;

    int defIdx = parentFunc->getInstructionIndex(instr);
    if (defIdx < 0)
        return PeepOptiState::KEEP;

    auto defBB = parentFunc->getInstructionBB(defIdx);
    if (!defBB)
        return PeepOptiState::KEEP;

    auto usersMap = parentFunc->getInstrUsers();
    auto itUsers = usersMap.find(defIdx);
    if (itUsers == usersMap.end() || itUsers->second.empty())
        return PeepOptiState::KEEP;

    int64_t baseImm = addrImmOp->getImmediate();
    vector<pair<int, shared_ptr<RISCVInstruction>>> replacements;
    replacements.reserve(itUsers->second.size());

    for (int userIdx : itUsers->second)
    {
        auto userInstr = parentFunc->getInstructionByIndex(userIdx);
        auto userBB = parentFunc->getInstructionBB(userIdx);
        if (!userInstr || !userBB || !parentFunc->dominates(defBB, userBB))
        {
            return PeepOptiState::KEEP;
        }

        auto uopcode = userInstr->getOpcode();
        auto userOps = userInstr->getOperands();

        if (isLoadOpcode(uopcode) && userOps.size() >= 3)
        {
            auto loadDestReg = userOps[0]->getReg();
            auto loadBaseReg = userOps[1]->getReg();
            auto loadImmOp = userOps[2];
            if (!loadDestReg || !loadBaseReg || !loadImmOp || loadImmOp->getType() != RISCVOperand::Type::IMMEDIATE ||
                !(*loadBaseReg == *addrDestReg))
            {
                return PeepOptiState::KEEP;
            }

            int64_t merged = baseImm + loadImmOp->getImmediate();
            if (!isLegalMemoryImmediate(merged))
            {
                return PeepOptiState::KEEP;
            }

            auto fused = RISCVInstruction::createIType(uopcode, loadDestReg, baseReg, merged);
            replacements.emplace_back(userIdx, fused);
        }
        else if (isStoreOpcode(uopcode) && userOps.size() >= 3)
        {
            auto storeBaseReg = userOps[0]->getReg();
            auto storeValueReg = userOps[1]->getReg();
            auto storeImmOp = userOps[2];
            if (!storeBaseReg || !storeValueReg || !storeImmOp || storeImmOp->getType() != RISCVOperand::Type::IMMEDIATE ||
                !(*storeBaseReg == *addrDestReg))
            {
                return PeepOptiState::KEEP;
            }

            int64_t merged = baseImm + storeImmOp->getImmediate();
            if (!isLegalMemoryImmediate(merged))
            {
                return PeepOptiState::KEEP;
            }

            auto fused = RISCVInstruction::createSType(uopcode, baseReg, storeValueReg, merged);
            replacements.emplace_back(userIdx, fused);
        }
        else
        {
            return PeepOptiState::KEEP;
        }
    }

    if (replacements.empty())
        return PeepOptiState::KEEP;

    for (const auto &rp : replacements)
    {
        auto userInstr = parentFunc->getInstructionByIndex(rp.first);
        if (userInstr)
        {
            userInstr->replaceInstruction(rp.second);
        }
    }

    return PeepOptiState::DELETE;
}


bool FoldAdjacentMoveAndAddressPass::isLegalMemoryImmediate(int64_t value) const
{
    return value >= -2048 && value <= 2047;
}

// ========== RemoveRedundantJalPass ==========

PeepOptiState RemoveRedundantJalPass::optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb)
{
    if (isRedundantJal(instr, bb))
    {
        // 删除多余的jal指令
        return PeepOptiState::DELETE;
    }

    return PeepOptiState::KEEP;
}

bool RemoveRedundantJalPass::isRedundantJal(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb)
{
    auto opcode = instr->getOpcode();

    // 检查是否为跳转指令
    if (opcode == RISCVOpcode::JAL)
    {
        auto targetLabel = instr->getOperands()[1]->getLabel();
        auto Blocks = bb->getParentFunc()->getBasicBlocks();

        // 如果两个基本块是相邻的，则可以认为是冗余的
        for (size_t i = 0; i < Blocks.size(); ++i)
        {
            if (Blocks[i]->getLabel() == targetLabel)
            {
                // 检查当前基本块和目标基本块是否相邻
                if (i > 0 && Blocks[i - 1] == bb)
                {
                    return true;
                }
            }
        }
    }

    return false;
}

bool FoldAdjacentMoveAndAddressPass::isAddressCalcOpcode(RISCVOpcode opcode) const
{
    return opcode == RISCVOpcode::ADDI || opcode == RISCVOpcode::ADDIW;
}

bool FoldAdjacentMoveAndAddressPass::isLoadOpcode(RISCVOpcode opcode) const
{
    return opcode == RISCVOpcode::LB || opcode == RISCVOpcode::LH || opcode == RISCVOpcode::LW ||
           opcode == RISCVOpcode::LD || opcode == RISCVOpcode::LBU || opcode == RISCVOpcode::LHU ||
           opcode == RISCVOpcode::FLW || opcode == RISCVOpcode::FLD;
}

bool FoldAdjacentMoveAndAddressPass::isStoreOpcode(RISCVOpcode opcode) const
{
    return opcode == RISCVOpcode::SB || opcode == RISCVOpcode::SH || opcode == RISCVOpcode::SW ||
           opcode == RISCVOpcode::SD || opcode == RISCVOpcode::FSW || opcode == RISCVOpcode::FSD;
}

// ========== FoldCompareBranchPass ==========

namespace
{
    bool isSupportedCompareOpcode(RISCVOpcode opcode)
    {
        return opcode == RISCVOpcode::SLT || opcode == RISCVOpcode::SLTU ||
               opcode == RISCVOpcode::SLTI || opcode == RISCVOpcode::SLTIU;
    }

    bool isBranchOpcode(RISCVOpcode opcode)
    {
        return opcode == RISCVOpcode::BEQ || opcode == RISCVOpcode::BNE;
    }

    bool isCompareResultUseOrDef(const shared_ptr<RISCVInstruction> &instr,
                                 const shared_ptr<RISCVRegister> &reg)
    {
        auto usedRegs = instr->getUseRegisters();
        for (const auto &usedReg : usedRegs)
        {
            if (usedReg && reg && *usedReg == *reg)
            {
                return true;
            }
        }

        auto definedRegs = instr->getDefRegisters();
        for (const auto &definedReg : definedRegs)
        {
            if (definedReg && reg && *definedReg == *reg)
            {
                return true;
            }
        }

        return false;
    }

    RISCVOpcode mapDirectBranchOpcode(RISCVOpcode compareOpcode, bool branchOnCompareTrue)
    {
        switch (compareOpcode)
        {
        case RISCVOpcode::SLT:
        case RISCVOpcode::SLTI:
            return branchOnCompareTrue ? RISCVOpcode::BLT : RISCVOpcode::BGE;
        case RISCVOpcode::SLTU:
            return branchOnCompareTrue ? RISCVOpcode::BLTU : RISCVOpcode::BGEU;
        case RISCVOpcode::SLTIU:
            return branchOnCompareTrue ? RISCVOpcode::BEQ : RISCVOpcode::BNE;
        default:
            return compareOpcode;
        }
    }

    bool isAllowedCompareImmediate(RISCVOpcode compareOpcode, int64_t immediate)
    {
        if (compareOpcode == RISCVOpcode::SLTI)
        {
            return immediate == 0;
        }

        if (compareOpcode == RISCVOpcode::SLTIU)
        {
            return immediate == 1;
        }

        return false;
    }
}

PeepOptiState FoldCompareBranchPass::optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb)
{
    if (!instr || !bb || !isSupportedCompareOpcode(instr->getOpcode()))
    {
        return PeepOptiState::KEEP;
    }

    auto &instrs = bb->getInstructions();
    auto it = find(instrs.begin(), instrs.end(), instr);
    if (it == instrs.end())
    {
        return PeepOptiState::KEEP;
    }

    auto compareOperands = instr->getOperands();
    if (compareOperands.size() != 3 || !compareOperands[0] || compareOperands[0]->getType() != RISCVOperand::Type::REGISTER)
    {
        return PeepOptiState::KEEP;
    }

    auto resultReg = compareOperands[0]->getReg();
    if (!resultReg)
    {
        return PeepOptiState::KEEP;
    }

    auto scanIt = it;
    ++scanIt;
    if (scanIt == instrs.end())
    {
        return PeepOptiState::KEEP;
    }

    bool hasXori = false;
    auto xoriIt = instrs.end();

    if ((*scanIt)->getOpcode() == RISCVOpcode::XORI)
    {
        auto xoriOperands = (*scanIt)->getOperands();
        if (xoriOperands.size() != 3 || !xoriOperands[0] || !xoriOperands[1] || !xoriOperands[2] ||
            xoriOperands[0]->getType() != RISCVOperand::Type::REGISTER ||
            xoriOperands[1]->getType() != RISCVOperand::Type::REGISTER ||
            xoriOperands[2]->getType() != RISCVOperand::Type::IMMEDIATE)
        {
            return PeepOptiState::KEEP;
        }

        if (!(*xoriOperands[0]->getReg() == *resultReg) || !(*xoriOperands[1]->getReg() == *resultReg) || xoriOperands[2]->getImmediate() != 1)
        {
            return PeepOptiState::KEEP;
        }

        hasXori = true;
        xoriIt = scanIt;
        ++scanIt;
        if (scanIt == instrs.end())
        {
            return PeepOptiState::KEEP;
        }
    }

    auto compareOpcode = instr->getOpcode();
    auto branchSearchStart = scanIt;
    auto branchIt = instrs.end();
    for (auto candidateIt = branchSearchStart; candidateIt != instrs.end(); ++candidateIt)
    {
        auto candidate = *candidateIt;
        if (candidate == instr)
        {
            continue;
        }

        if (hasXori && candidateIt == xoriIt)
        {
            continue;
        }

        if (!isBranchOpcode(candidate->getOpcode()))
        {
            if (isCompareResultUseOrDef(candidate, resultReg))
            {
                return PeepOptiState::KEEP;
            }
            continue;
        }

        auto branchOperands = candidate->getOperands();
        if (branchOperands.size() != 3 || !branchOperands[2] || branchOperands[2]->getType() != RISCVOperand::Type::LABEL)
        {
            continue;
        }

        bool resultOnLeft = branchOperands[0] && branchOperands[0]->getType() == RISCVOperand::Type::REGISTER &&
                            branchOperands[0]->getReg() && *branchOperands[0]->getReg() == *resultReg &&
                            branchOperands[1] && branchOperands[1]->getType() == RISCVOperand::Type::REGISTER &&
                            branchOperands[1]->getReg() && branchOperands[1]->getReg()->isPhysical() &&
                            branchOperands[1]->getReg()->getPhysicalReg() == RISCVRegister::PhysicalReg::ZERO;
        bool resultOnRight = branchOperands[1] && branchOperands[1]->getType() == RISCVOperand::Type::REGISTER &&
                             branchOperands[1]->getReg() && *branchOperands[1]->getReg() == *resultReg &&
                             branchOperands[0] && branchOperands[0]->getType() == RISCVOperand::Type::REGISTER &&
                             branchOperands[0]->getReg() && branchOperands[0]->getReg()->isPhysical() &&
                             branchOperands[0]->getReg()->getPhysicalReg() == RISCVRegister::PhysicalReg::ZERO;
        if (!resultOnLeft && !resultOnRight)
        {
            continue;
        }

        bool branchOnCompareTrue = (candidate->getOpcode() == RISCVOpcode::BNE) ^ hasXori;
        auto newBranchOpcode = mapDirectBranchOpcode(compareOpcode, branchOnCompareTrue);
        auto lhsReg = compareOperands[1]->getReg();
        if (!lhsReg)
        {
            return PeepOptiState::KEEP;
        }

        shared_ptr<RISCVRegister> rhsReg;
        if (compareOpcode == RISCVOpcode::SLT || compareOpcode == RISCVOpcode::SLTU)
        {
            if (compareOperands[2]->getType() != RISCVOperand::Type::REGISTER)
            {
                return PeepOptiState::KEEP;
            }
            rhsReg = compareOperands[2]->getReg();
        }
        else
        {
            if (compareOperands[2]->getType() != RISCVOperand::Type::IMMEDIATE || !isAllowedCompareImmediate(compareOpcode, compareOperands[2]->getImmediate()))
            {
                return PeepOptiState::KEEP;
            }
            rhsReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO);
        }

        if (!rhsReg)
        {
            return PeepOptiState::KEEP;
        }

        branchIt = candidateIt;
        auto directBranch = RISCVInstruction::createBType(newBranchOpcode, lhsReg, rhsReg, branchOperands[2]->getLabel());
        candidate->replaceInstruction(directBranch);
        break;
    }

    if (branchIt == instrs.end())
    {
        return PeepOptiState::KEEP;
    }

    if (hasXori && xoriIt != instrs.end())
    {
        instrs.erase(xoriIt);
    }

    return PeepOptiState::DELETE;
}

// ========== DeadCodeEliminationPass ==========

PeepOptiState DeadCodeEliminationPass::optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb)
{
    if (isDeadCode(instr, bb))
    {
        // 删除死代码
        return PeepOptiState::DELETE;
    }

    return PeepOptiState::KEEP;
}

bool DeadCodeEliminationPass::isDeadCode(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb)
{

    auto definedRegs = instr->getDefRegisters();
    if (definedRegs.empty())
    {
        return false;
    }

    for (auto reg : definedRegs)
    {
        if (isRegisterRedefined(reg, instr, bb))
        {
            return true;
        }
    }

    return false;
}

bool DeadCodeEliminationPass::hasSideEffects(shared_ptr<RISCVInstruction> instr)
{
    auto opcode = instr->getOpcode();

    if (opcode == RISCVOpcode::SW || opcode == RISCVOpcode::SH || opcode == RISCVOpcode::SB ||
        opcode == RISCVOpcode::SD)
    {
        return true;
    }

    if (opcode == RISCVOpcode::JAL || opcode == RISCVOpcode::JALR ||
        opcode == RISCVOpcode::BEQ || opcode == RISCVOpcode::BNE ||
        opcode == RISCVOpcode::BLT || opcode == RISCVOpcode::BGE ||
        opcode == RISCVOpcode::BLTU || opcode == RISCVOpcode::BGEU)
    {
        return true;
    }

    if (opcode == RISCVOpcode::CALL || opcode == RISCVOpcode::ECALL || opcode == RISCVOpcode::EBREAK)
    {
        return true;
    }

    return false;
}

bool DeadCodeEliminationPass::isRegisterRedefined(shared_ptr<RISCVRegister> reg,
                                                  shared_ptr<RISCVInstruction> startInstr,
                                                  shared_ptr<RISCVBasicBlock> bb)
{
    auto instructions = bb->getInstructions();

    auto it = std::find(instructions.begin(), instructions.end(), startInstr);
    if (it == instructions.end())
    {
        return false;
    }

    for (++it; it != instructions.end(); ++it)
    {
        auto instr = *it;

        if (instr->getOpcode() == RISCVOpcode::CALL)
            return false;

        auto usedRegs = instr->getUseRegisters();
        for (auto usedReg : usedRegs)
        {
            if (*usedReg == *reg)
            {
                return false;
            }
        }

        auto definedRegs = instr->getDefRegisters();
        for (auto definedReg : definedRegs)
        {
            if (*definedReg == *reg)
            {
                return true;
            }
        }
    }

    return false;
}

PeepOptiState StrengthReductionPass::optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb)
{
    if (instr->getOpcode() != RISCVOpcode::MULW && instr->getOpcode() != RISCVOpcode::MUL)
    {
        return PeepOptiState::KEEP;
    }

    auto &instrs = bb->getInstructions();
    auto it = find(instrs.begin(), instrs.end(), instr);
    if (it == instrs.begin())
    {
        return PeepOptiState::KEEP;
    }

    // 获取乘法指令的操作数
    auto mulOp1 = instr->getOperands()[1];
    auto mulOp2 = instr->getOperands()[2];

    // 向前搜索li指令
    auto liInfo = findLIInstruction(mulOp1->getReg(), it, instrs);
    bool isOp1 = true;
    if (!liInfo.has_value())
    {
        liInfo = findLIInstruction(mulOp2->getReg(), it, instrs);
        isOp1 = false;
        if (!liInfo.has_value())
        {
            return PeepOptiState::KEEP;
        }
    }

    auto [liIt, constant] = liInfo.value();

    // 执行强度削减优化
    return performStrengthReduction(instr, bb, it, liIt, constant, isOp1, mulOp1, mulOp2);
}

bool StrengthReductionPass::isPowerOfTwo(int64_t n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

optional<tuple<vector<shared_ptr<RISCVInstruction>>::iterator, int64_t>>
StrengthReductionPass::findLIInstruction(shared_ptr<RISCVRegister> targetReg,
                                         vector<shared_ptr<RISCVInstruction>>::iterator currentIt,
                                         vector<shared_ptr<RISCVInstruction>> &instrs)
{
    // 向前搜索最多10条指令（可调整这个限制）
    const int SEARCH_LIMIT = 10;
    int searchCount = 0;

    auto it = currentIt;
    while (it != instrs.begin() && searchCount < SEARCH_LIMIT)
    {
        --it;
        searchCount++;

        auto instr = *it;

        // 检查是否是li指令
        if (instr->getOpcode() == RISCVOpcode::LI && instr->getOperands().size() >= 2)
        {
            auto liTarget = instr->getOperands()[0]->getReg();
            if (liTarget && *liTarget == *targetReg)
            {
                int64_t constant = instr->getOperands()[1]->getImmediate();
                return make_tuple(it, constant);
            }
        }

        // 检查这条指令是否重定义了目标寄存器
        auto defRegs = instr->getDefRegisters();
        for (auto defReg : defRegs)
        {
            if (defReg && *defReg == *targetReg)
            {
                return nullopt;
            }
        }
    }

    return nullopt;
}

PeepOptiState StrengthReductionPass::performStrengthReduction(
    shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb,
    vector<shared_ptr<RISCVInstruction>>::iterator currentIt,
    vector<shared_ptr<RISCVInstruction>>::iterator liIt,
    int64_t constant, bool isOp1,
    shared_ptr<RISCVOperand> mulOp1, shared_ptr<RISCVOperand> mulOp2)
{
    auto &instrs = bb->getInstructions();

    // 获取索引以避免迭代器失效
    size_t currentIndex = std::distance(instrs.begin(), currentIt);
    size_t liIndex = std::distance(instrs.begin(), liIt);

    // 确定变量寄存器（非常量的那个操作数）
    auto variableReg = isOp1 ? mulOp2->getReg() : mulOp1->getReg();
    auto destReg = instr->getOperands()[0]->getReg();

    if (constant == 0)
    {
        // x * 0 = 0
        instrs.erase(instrs.begin() + liIndex);
        // 如果li指令在mul指令前面，当前指令索引需要减1
        if (liIndex < currentIndex)
        {
            currentIndex--;
        }

        auto liInst = RISCVInstruction::createPseudoLI(destReg, 0);
        instrs[currentIndex] = liInst;
        return PeepOptiState::MODIFY;
    }
    else if (constant == 1)
    {
        // x * 1 = x
        instrs.erase(instrs.begin() + liIndex);
        if (liIndex < currentIndex)
        {
            currentIndex--;
        }

        auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, destReg, variableReg);
        instrs[currentIndex] = mvInst;
        return PeepOptiState::MODIFY;
    }
    else if (constant == -1)
    {
        // x * -1 = -x
        instrs.erase(instrs.begin() + liIndex);
        if (liIndex < currentIndex)
        {
            currentIndex--;
        }

        auto negInst = RISCVInstruction::createRType(RISCVOpcode::SUB, destReg,
                                                     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO), variableReg);
        instrs[currentIndex] = negInst;
        return PeepOptiState::MODIFY;
    }
    else if (isPowerOfTwo(constant))
    {
        // x * 2^n = x << n
        int shiftAmount = static_cast<int>(log2(constant));
        instrs.erase(instrs.begin() + liIndex);
        if (liIndex < currentIndex)
        {
            currentIndex--;
        }

        auto slliInst = RISCVInstruction::createIType(
            instr->getOpcode() == RISCVOpcode::MUL ? RISCVOpcode::SLLI : RISCVOpcode::SLLIW,
            destReg, variableReg, shiftAmount);
        instrs[currentIndex] = slliInst;
        return PeepOptiState::MODIFY;
    }
    else if (constant < 0 && isPowerOfTwo(-constant))
    {
        // x * (-2^n) = -(x << n)
        int shiftAmount = static_cast<int>(log2(-constant));
        instrs.erase(instrs.begin() + liIndex);
        if (liIndex < currentIndex)
        {
            currentIndex--;
        }

        auto slliInst = RISCVInstruction::createIType(
            instr->getOpcode() == RISCVOpcode::MUL ? RISCVOpcode::SLLI : RISCVOpcode::SLLIW,
            destReg, variableReg, shiftAmount);
        auto negInst = RISCVInstruction::createRType(RISCVOpcode::SUB, destReg,
                                                     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO), destReg);

        instrs[currentIndex] = slliInst;
        instrs.insert(instrs.begin() + currentIndex + 1, negInst);
        return PeepOptiState::MODIFY;
    }

    return PeepOptiState::KEEP;
}

// ========== ImmediatePropagationPass ==========

PeepOptiState ImmediatePropagationPass::optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb)
{
    auto opcode = instr->getOpcode();

    if (!canUseImmediateForm(opcode))
    {
        return PeepOptiState::KEEP;
    }

    auto &instrs = bb->getInstructions();
    auto it = find(instrs.begin(), instrs.end(), instr);

    auto operands = instr->getOperands();
    if (operands.size() < 3)
    {
        return PeepOptiState::KEEP;
    }

    auto op1Reg = operands[1]->getReg();
    if (op1Reg)
    {
        auto liInfo = findLIInstruction(op1Reg, it, instrs);
        if (liInfo.has_value())
        {
            auto [liIt, constant] = liInfo.value();

            RISCVOpcode immediateOpcode = getImmediateOpcode(opcode);
            if (immediateOpcode != opcode && isValidImmediate(constant, immediateOpcode))
            {
                return performImmediatePropagation(instr, bb, it, liIt, constant, immediateOpcode, 1);
            }
        }
    }

    if (operands.size() >= 3 && canPropagateSecondOperand(opcode))
    {
        auto op2Reg = operands[2]->getReg();
        if (op2Reg)
        {
            auto liInfo = findLIInstruction(op2Reg, it, instrs);
            if (liInfo.has_value())
            {
                auto [liIt, constant] = liInfo.value();

                if (opcode == RISCVOpcode::SUB)
                {
                    constant = -constant;
                }

                RISCVOpcode immediateOpcode = getImmediateOpcode(opcode);
                if (immediateOpcode != opcode && isValidImmediate(constant, immediateOpcode))
                {
                    return performImmediatePropagation(instr, bb, it, liIt, constant, immediateOpcode, 2);
                }
            }
        }
    }

    return PeepOptiState::KEEP;
}

bool ImmediatePropagationPass::canUseImmediateForm(RISCVOpcode opcode)
{
    switch (opcode)
    {
    case RISCVOpcode::ADD:
    case RISCVOpcode::SUB:
    case RISCVOpcode::AND:
    case RISCVOpcode::OR:
    case RISCVOpcode::XOR:
    case RISCVOpcode::SLL:
    case RISCVOpcode::SRL:
    case RISCVOpcode::SRA:
    case RISCVOpcode::SLT:
    case RISCVOpcode::SLTU:
        return true;
    default:
        return false;
    }
}

bool ImmediatePropagationPass::canPropagateSecondOperand(RISCVOpcode opcode)
{
    switch (opcode)
    {
    case RISCVOpcode::ADD:
    case RISCVOpcode::AND:
    case RISCVOpcode::OR:
    case RISCVOpcode::XOR:
    case RISCVOpcode::SUB:
        return true;
    default:
        return false;
    }
}

bool ImmediatePropagationPass::isValidImmediate(int64_t value, RISCVOpcode opcode)
{
    switch (opcode)
    {
    case RISCVOpcode::ADDI:
    case RISCVOpcode::ANDI:
    case RISCVOpcode::ORI:
    case RISCVOpcode::XORI:
    case RISCVOpcode::SLTI:
    case RISCVOpcode::SLTIU:
        return value >= -2048 && value <= 2047;

    case RISCVOpcode::SLLI:
    case RISCVOpcode::SRLI:
    case RISCVOpcode::SRAI:
        return value >= 0 && value <= 63;

    default:
        return false;
    }
}

RISCVOpcode ImmediatePropagationPass::getImmediateOpcode(RISCVOpcode opcode)
{
    switch (opcode)
    {
    case RISCVOpcode::ADD:
    case RISCVOpcode::SUB:
        return RISCVOpcode::ADDI;
    case RISCVOpcode::AND:
        return RISCVOpcode::ANDI;
    case RISCVOpcode::OR:
        return RISCVOpcode::ORI;
    case RISCVOpcode::XOR:
        return RISCVOpcode::XORI;
    case RISCVOpcode::SLL:
        return RISCVOpcode::SLLI;
    case RISCVOpcode::SRL:
        return RISCVOpcode::SRLI;
    case RISCVOpcode::SRA:
        return RISCVOpcode::SRAI;
    case RISCVOpcode::SLT:
        return RISCVOpcode::SLTI;
    case RISCVOpcode::SLTU:
        return RISCVOpcode::SLTIU;
    default:
        return opcode;
    }
}

PeepOptiState ImmediatePropagationPass::performImmediatePropagation(
    shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb,
    vector<shared_ptr<RISCVInstruction>>::iterator currentIt,
    vector<shared_ptr<RISCVInstruction>>::iterator liIt,
    int64_t constant, RISCVOpcode immediateOpcode, int operandIndex)
{
    auto &instrs = bb->getInstructions();

    size_t currentIndex = std::distance(instrs.begin(), currentIt);
    size_t liIndex = std::distance(instrs.begin(), liIt);

    auto operands = instr->getOperands();
    auto destReg = operands[0]->getReg();

    shared_ptr<RISCVRegister> srcReg;
    if (operandIndex == 1)
    {
        srcReg = operands[2]->getReg();
    }
    else
    {
        srcReg = operands[1]->getReg();
    }

    auto immediateInstr = RISCVInstruction::createIType(immediateOpcode, destReg, srcReg, constant);

    instrs.erase(instrs.begin() + liIndex);
    if (liIndex < currentIndex)
    {
        currentIndex--;
    }

    instrs[currentIndex] = immediateInstr;

    return PeepOptiState::MODIFY;
}

optional<tuple<vector<shared_ptr<RISCVInstruction>>::iterator, int64_t>>
ImmediatePropagationPass::findLIInstruction(shared_ptr<RISCVRegister> targetReg,
                                            vector<shared_ptr<RISCVInstruction>>::iterator currentIt,
                                            vector<shared_ptr<RISCVInstruction>> &instrs)
{
    const int SEARCH_LIMIT = 10;
    int searchCount = 0;

    auto it = currentIt;
    while (it != instrs.begin() && searchCount < SEARCH_LIMIT)
    {
        --it;
        searchCount++;

        auto instr = *it;

        // 检查是否是li指令
        if (instr->getOpcode() == RISCVOpcode::LI && instr->getOperands().size() >= 2)
        {
            auto liTarget = instr->getOperands()[0]->getReg();
            if (liTarget && *liTarget == *targetReg)
            {
                int64_t constant = instr->getOperands()[1]->getImmediate();
                return make_tuple(it, constant);
            }
        }

        auto defRegs = instr->getDefRegisters();
        for (auto defReg : defRegs)
        {
            if (defReg && *defReg == *targetReg)
            {
                return nullopt;
            }
        }
    }

    return nullopt;
}
