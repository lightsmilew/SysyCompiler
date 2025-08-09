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
    // 只处理乘法指令
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

    auto prevInstr = *(it - 1);
    if (prevInstr->getOpcode() != RISCVOpcode::LI)
    {
        return PeepOptiState::KEEP;
    }

    auto liTarget = prevInstr->getOperands()[0];
    auto mulOp1 = instr->getOperands()[1];
    auto mulOp2 = instr->getOperands()[2];
    bool liTargetIsOp1 = (*liTarget->getReg() == *mulOp1->getReg());
    bool liTargetIsOp2 = (*liTarget->getReg() == *mulOp2->getReg());
    if (!liTargetIsOp1 && !liTargetIsOp2)
    {
        return PeepOptiState::KEEP;
    }

    int64_t constant = prevInstr->getOperands()[1]->getImmediate();

    if (constant == 0)
    {
        instrs.erase(it - 1); // 删除原来的立即数加载指令

        auto liInst = RISCVInstruction::createPseudoLI(instr->getOperands()[0]->getReg(), 0);
        instr->replaceInstruction(liInst);
        return PeepOptiState::MODIFY;
    }
    else if (constant == 1)
    {
        instrs.erase(it - 1);

        auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, instr->getOperands()[0]->getReg(), liTargetIsOp1 ? mulOp2->getReg() : mulOp1->getReg());
        instr->replaceInstruction(mvInst);
        return PeepOptiState::MODIFY;
    }
    else if (constant == -1)
    {
        instrs.erase(it - 1);
        auto negInst = RISCVInstruction::createRType(RISCVOpcode::SUB, instr->getOperands()[0]->getReg(), make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO), liTargetIsOp1 ? mulOp2->getReg() : mulOp1->getReg());
        instr->replaceInstruction(negInst);
        return PeepOptiState::MODIFY;
    }
    else if (isPowerOfTwo(constant))
    {
        int shiftAmount = static_cast<int>(log2(constant));
        instrs.erase(it - 1);
        auto slliInst = RISCVInstruction::createIType(instr->getOpcode() == RISCVOpcode::MUL ? RISCVOpcode::SLLI : RISCVOpcode::SLLIW,
                                                      instr->getOperands()[0]->getReg(), liTargetIsOp1 ? mulOp2->getReg() : mulOp1->getReg(), shiftAmount);
        instr->replaceInstruction(slliInst);
        return PeepOptiState::MODIFY;
    }
    else if (constant < 0 && isPowerOfTwo(-constant))
    {
        int shiftAmount = static_cast<int>(log2(-constant));

        size_t currentIndex = std::distance(instrs.begin(), it);
        size_t prevIndex = currentIndex - 1;

        if (!instr->getOperands()[0] || !instr->getOperands()[0]->getReg())
        {
            return PeepOptiState::KEEP;
        }

        auto slliInst = RISCVInstruction::createIType(instr->getOpcode() == RISCVOpcode::MUL ? RISCVOpcode::SLLI : RISCVOpcode::SLLIW,
                                                      instr->getOperands()[0]->getReg(), liTargetIsOp1 ? mulOp2->getReg() : mulOp1->getReg(), shiftAmount);
        auto negInst = RISCVInstruction::createRType(RISCVOpcode::SUB, instr->getOperands()[0]->getReg(),
                                                     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO), instr->getOperands()[0]->getReg());

        instrs.erase(instrs.begin() + prevIndex);

        instrs[prevIndex] = slliInst;

        instrs.insert(instrs.begin() + prevIndex + 1, negInst);

        return PeepOptiState::MODIFY;
    }

    return PeepOptiState::KEEP;
}

bool StrengthReductionPass::isPowerOfTwo(int64_t n)
{
    return n > 0 && (n & (n - 1)) == 0;
}

// PeepOptiState ImmediatePropagationPass::optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb)
// {
//     auto opcode = instr->getOpcode();
//     auto &operands = instr->getOperands();

//     // 检查是否是可以优化的指令类型
//     if (!canUseImmediateForm(opcode) || operands.size() < 3)
//         return PeepOptiState::KEEP;

//     // 检查操作数是否是寄存器
//     for (int i = 1; i < operands.size(); ++i)
//     {
//         if (operands[i]->getType() != RISCVOperand::Type::REGISTER)
//             continue;

//         auto regOperand = dynamic_pointer_cast<RISCVRegisterOperand>(operands[i]);
//         if (!regOperand)
//             continue;

//         auto reg = regOperand->getRegister();
//         if (!reg)
//             continue;

//         // 查找这个寄存器的常量值
//         auto constantValue = findConstantValue(reg, instr, bb);
//         if (!constantValue.has_value())
//             continue;

//         // 检查立即数是否在有效范围内
//         RISCVOpcode immediateOpcode = getImmediateOpcode(opcode);
//         if (immediateOpcode == RISCVOpcode::INVALID ||
//             !isValidImmediate(*constantValue, immediateOpcode))
//             continue;

//         // 创建使用立即数的新指令
//         auto newInstr = createImmediateInstruction(instr, i, *constantValue);
//         if (!newInstr)
//             continue;

//         // 替换原指令
//         auto &instructions = bb->getInstructions();
//         for (auto it = instructions.begin(); it != instructions.end(); ++it)
//         {
//             if (*it == instr)
//             {
//                 *it = newInstr;
//                 return PeepOptiState::MODIFIED;
//             }
//         }
//     }

//     return PeepOptiState::NO_CHANGE;
// }

// bool ImmediatePropagationPass::canUseImmediateForm(RISCVOpcode opcode)
// {
//     switch (opcode)
//     {
//     case RISCVOpcode::ADD:
//     case RISCVOpcode::SUB: // 注意：SUB 可以转为 ADDI (负数)
//     case RISCVOpcode::AND:
//     case RISCVOpcode::OR:
//     case RISCVOpcode::XOR:
//     case RISCVOpcode::SLL:
//     case RISCVOpcode::SRL:
//     case RISCVOpcode::SRA:
//     case RISCVOpcode::SLT:
//     case RISCVOpcode::SLTU:
//         return true;
//     default:
//         return false;
//     }
// }

// optional<int64_t> ImmediatePropagationPass::findConstantValue(
//     shared_ptr<RISCVRegister> reg,
//     shared_ptr<RISCVInstruction> currentInstr,
//     shared_ptr<RISCVBasicBlock> bb)
// {
//     auto &instructions = bb->getInstructions();

//     // 向前查找定义该寄存器的指令
//     bool foundCurrent = false;
//     for (auto it = instructions.rbegin(); it != instructions.rend(); ++it)
//     {
//         auto instrPtr = *it;

//         if (instrPtr == currentInstr)
//         {
//             foundCurrent = true;
//             continue;
//         }

//         if (!foundCurrent)
//             continue;

//         // 检查这个指令是否定义了我们要找的寄存器
//         auto defRegs = instrPtr->getDefRegisters();
//         bool definesTargetReg = false;

//         for (auto defReg : defRegs)
//         {
//             if (defReg && reg && *defReg == *reg)
//             {
//                 definesTargetReg = true;
//                 break;
//             }
//         }

//         if (!definesTargetReg)
//             continue;

//         // 检查定义指令的类型
//         auto defOpcode = instrPtr->getOpcode();
//         auto &defOperands = instrPtr->getOperands();

//         if (defOpcode == RISCVOpcode::LI && defOperands.size() >= 2)
//         {
//             // li reg, imm 指令
//             if (defOperands[1]->getType() == RISCVOperand::Type::IMMEDIATE)
//             {
//                 auto immOperand = dynamic_pointer_cast<RISCVImmediateOperand>(defOperands[1]);
//                 if (immOperand)
//                     return immOperand->getValue();
//             }
//         }
//         else if (defOpcode == RISCVOpcode::ADDI && defOperands.size() >= 3)
//         {
//             // addi reg, zero, imm 指令 (等价于li)
//             if (defOperands[1]->getType() == RISCVOperand::Type::REGISTER &&
//                 defOperands[2]->getType() == RISCVOperand::Type::IMMEDIATE)
//             {
//                 auto srcRegOperand = dynamic_pointer_cast<RISCVRegisterOperand>(defOperands[1]);
//                 auto immOperand = dynamic_pointer_cast<RISCVImmediateOperand>(defOperands[2]);

//                 if (srcRegOperand && immOperand)
//                 {
//                     auto srcReg = srcRegOperand->getRegister();
//                     if (srcReg && srcReg->isPhysical() &&
//                         srcReg->getPhysicalReg() == RISCVRegister::PhysicalReg::ZERO)
//                     {
//                         return immOperand->getValue();
//                     }
//                 }
//             }
//         }

//         // 找到定义但不是常量，返回空
//         return nullopt;
//     }

//     return nullopt;
// }

// bool ImmediatePropagationPass::isValidImmediate(int64_t value, RISCVOpcode opcode)
// {
//     switch (opcode)
//     {
//     case RISCVOpcode::ADDI:
//     case RISCVOpcode::ANDI:
//     case RISCVOpcode::ORI:
//     case RISCVOpcode::XORI:
//         // 12位有符号立即数: [-2048, 2047]
//         return value >= -2048 && value <= 2047;

//     case RISCVOpcode::SLLI:
//     case RISCVOpcode::SRLI:
//     case RISCVOpcode::SRAI:
//         // 6位无符号立即数 (64位RISC-V): [0, 63]
//         return value >= 0 && value <= 63;

//     case RISCVOpcode::SLTI:
//     case RISCVOpcode::SLTIU:
//         // 12位立即数: [-2048, 2047]
//         return value >= -2048 && value <= 2047;

//     default:
//         return false;
//     }
// }

// shared_ptr<RISCVInstruction> ImmediatePropagationPass::createImmediateInstruction(
//     shared_ptr<RISCVInstruction> instr,
//     int operandIndex,
//     int64_t immediateValue)
// {
//     auto opcode = instr->getOpcode();
//     auto &operands = instr->getOperands();

//     // 特殊处理SUB指令：sub dst, src, reg -> addi dst, src, -imm
//     if (opcode == RISCVOpcode::SUB && operandIndex == 2)
//     {
//         immediateValue = -immediateValue;
//         opcode = RISCVOpcode::ADD; // 将被转换为ADDI
//     }

//     RISCVOpcode newOpcode = getImmediateOpcode(opcode);
//     if (newOpcode == RISCVOpcode::INVALID)
//         return nullptr;

//     // 创建新的操作数列表
//     vector<shared_ptr<RISCVOperand>> newOperands;

//     for (int i = 0; i < operands.size(); ++i)
//     {
//         if (i == operandIndex)
//         {
//             // 替换为立即数操作数
//             newOperands.push_back(make_shared<RISCVImmediateOperand>(immediateValue));
//         }
//         else
//         {
//             newOperands.push_back(operands[i]);
//         }
//     }

//     return make_shared<RISCVInstruction>(newOpcode, newOperands);
// }

// RISCVOpcode ImmediatePropagationPass::getImmediateOpcode(RISCVOpcode opcode)
// {
//     switch (opcode)
//     {
//     case RISCVOpcode::ADD:
//         return RISCVOpcode::ADDI;
//     case RISCVOpcode::SUB:
//         return RISCVOpcode::ADDI; // sub -> addi with negated immediate
//     case RISCVOpcode::AND:
//         return RISCVOpcode::ANDI;
//     case RISCVOpcode::OR:
//         return RISCVOpcode::ORI;
//     case RISCVOpcode::XOR:
//         return RISCVOpcode::XORI;
//     case RISCVOpcode::SLL:
//         return RISCVOpcode::SLLI;
//     case RISCVOpcode::SRL:
//         return RISCVOpcode::SRLI;
//     case RISCVOpcode::SRA:
//         return RISCVOpcode::SRAI;
//     case RISCVOpcode::SLT:
//         return RISCVOpcode::SLTI;
//     case RISCVOpcode::SLTU:
//         return RISCVOpcode::SLTIU;
//     default:
//         return RISCVOpcode::INVALID;
//     }
// }
