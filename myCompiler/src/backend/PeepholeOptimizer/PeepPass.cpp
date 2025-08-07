#include "PeepPass.h"

using namespace RISCV;

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
                if (*operands[0]->getReg() == *operands[1]->getReg())
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
            // 检查源寄存器和目标寄存器是否相同
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
        return isJumpToNextInstruction(instr, bb);
    }

    return false;
}

bool RemoveRedundantJalPass::isJumpToNextInstruction(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb)
{
    // 获取基本块中的指令列表
    auto instructions = bb->getInstructions();

    // 找到当前指令在基本块中的位置
    auto it = std::find(instructions.begin(), instructions.end(), instr);
    if (it == instructions.end() || it == instructions.end() - 1)
    {
        return false; // 找不到指令或者是最后一条指令
    }

    // 获取下一条指令
    auto nextInstr = *(it + 1);

    // 获取跳转目标
    auto operands = instr->getOperands();
    if (operands.empty())
    {
        return false;
    }

    // 简化处理：检查跳转目标是否为标签类型
    // 这里需要根据实际的数据结构进行调整
    if (operands.back()->getType() == RISCVOperand::Type::LABEL)
    {
        // 简化判断：假设如果是标签操作数，可能是冗余跳转
        // 实际实现需要更复杂的标签匹配逻辑
        return false; // 暂时保守处理，不删除
    }

    return false;
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
