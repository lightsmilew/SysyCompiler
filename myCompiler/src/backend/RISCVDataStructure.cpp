#include "RISCVDataStructure.h"
#include <sstream>
#include <stdexcept>

namespace RISCV
{
    // 静态变量初始化
    int RISCVRegister::nextVirtualId = 0;

    // RISCVRegister 实现
    RISCVRegister::RISCVRegister(PhysicalReg reg)
        : type(reg < PhysicalReg::FT0 ? RegisterType::GENERAL : RegisterType::FLOAT),
          physicalReg(reg), virtualId(-1) {}

    RISCVRegister::RISCVRegister(RegisterType regType)
        : type(regType), physicalReg(PhysicalReg::ZERO), virtualId(nextVirtualId++) {}

    string RISCVRegister::toString() const
    {
        if (isVirtual())
        {
            return (type == RegisterType::GENERAL ? "vr" : "vf") + std::to_string(virtualId);
        }

        // 物理寄存器名称映射
        switch (physicalReg)
        {
        case PhysicalReg::ZERO:
            return "zero";
        case PhysicalReg::RA:
            return "ra";
        case PhysicalReg::SP:
            return "sp";
        case PhysicalReg::GP:
            return "gp";
        case PhysicalReg::TP:
            return "tp";
        case PhysicalReg::T0:
            return "t0";
        case PhysicalReg::T1:
            return "t1";
        case PhysicalReg::T2:
            return "t2";
        case PhysicalReg::S0:
            return "s0";
        case PhysicalReg::S1:
            return "s1";
        case PhysicalReg::A0:
            return "a0";
        case PhysicalReg::A1:
            return "a1";
        case PhysicalReg::A2:
            return "a2";
        case PhysicalReg::A3:
            return "a3";
        case PhysicalReg::A4:
            return "a4";
        case PhysicalReg::A5:
            return "a5";
        case PhysicalReg::A6:
            return "a6";
        case PhysicalReg::A7:
            return "a7";
        case PhysicalReg::S2:
            return "s2";
        case PhysicalReg::S3:
            return "s3";
        case PhysicalReg::S4:
            return "s4";
        case PhysicalReg::S5:
            return "s5";
        case PhysicalReg::S6:
            return "s6";
        case PhysicalReg::S7:
            return "s7";
        case PhysicalReg::S8:
            return "s8";
        case PhysicalReg::S9:
            return "s9";
        case PhysicalReg::S10:
            return "s10";
        case PhysicalReg::S11:
            return "s11";
        case PhysicalReg::T3:
            return "t3";
        case PhysicalReg::T4:
            return "t4";
        case PhysicalReg::T5:
            return "t5";
        case PhysicalReg::T6:
            return "t6";
        // 浮点寄存器
        case PhysicalReg::FT0:
            return "ft0";
        case PhysicalReg::FT1:
            return "ft1";
        case PhysicalReg::FT2:
            return "ft2";
        case PhysicalReg::FT3:
            return "ft3";
        case PhysicalReg::FT4:
            return "ft4";
        case PhysicalReg::FT5:
            return "ft5";
        case PhysicalReg::FT6:
            return "ft6";
        case PhysicalReg::FT7:
            return "ft7";
        case PhysicalReg::FS0:
            return "fs0";
        case PhysicalReg::FS1:
            return "fs1";
        case PhysicalReg::FA0:
            return "fa0";
        case PhysicalReg::FA1:
            return "fa1";
        case PhysicalReg::FA2:
            return "fa2";
        case PhysicalReg::FA3:
            return "fa3";
        case PhysicalReg::FA4:
            return "fa4";
        case PhysicalReg::FA5:
            return "fa5";
        case PhysicalReg::FA6:
            return "fa6";
        case PhysicalReg::FA7:
            return "fa7";
        case PhysicalReg::FS2:
            return "fs2";
        case PhysicalReg::FS3:
            return "fs3";
        case PhysicalReg::FS4:
            return "fs4";
        case PhysicalReg::FS5:
            return "fs5";
        case PhysicalReg::FS6:
            return "fs6";
        case PhysicalReg::FS7:
            return "fs7";
        case PhysicalReg::FS8:
            return "fs8";
        case PhysicalReg::FS9:
            return "fs9";
        case PhysicalReg::FS10:
            return "fs10";
        case PhysicalReg::FS11:
            return "fs11";
        case PhysicalReg::FT8:
            return "ft8";
        case PhysicalReg::FT9:
            return "ft9";
        case PhysicalReg::FT10:
            return "ft10";
        case PhysicalReg::FT11:
            return "ft11";
        default:
            return "unknown";
        }
    }

    bool RISCVRegister::operator==(const RISCVRegister &other) const
    {
        if (isVirtual() && other.isVirtual())
            return virtualId == other.virtualId && type == other.type;
        if (isPhysical() && other.isPhysical())
            return physicalReg == other.physicalReg;
        return false;
    }

    // RISCVOperand 实现
    RISCVOperand::RISCVOperand(shared_ptr<RISCVRegister> reg)
        : type(Type::REGISTER), reg(reg), immediate(0), offset(0) {}

    RISCVOperand::RISCVOperand(int64_t imm)
        : type(Type::IMMEDIATE), reg(nullptr), immediate(imm), offset(0) {}

    RISCVOperand::RISCVOperand(shared_ptr<RISCVRegister> base, int off)
        : type(Type::MEMORY), reg(base), immediate(0), offset(off) {}

    RISCVOperand::RISCVOperand(const string &lbl)
        : type(Type::LABEL), reg(nullptr), immediate(0), offset(0), label(lbl) {}

    string RISCVOperand::toString() const
    {
        switch (type)
        {
        case Type::REGISTER:
            return reg->toString();
        case Type::IMMEDIATE:
            return std::to_string(immediate);
        case Type::MEMORY:
            return std::to_string(offset) + "(" + reg->toString() + ")";
        case Type::LABEL:
            return label;
        default:
            return "unknown";
        }
    }

    bool RISCVInstruction::usesRegister(shared_ptr<RISCVRegister> reg) const
    {
        // 检查源操作数
        for (size_t i = 1; i < operands.size(); ++i)
        {
            if (operands[i]->getType() == RISCVOperand::Type::REGISTER &&
                *operands[i]->getReg() == *reg)
                return true;
            if (operands[i]->getType() == RISCVOperand::Type::MEMORY &&
                *operands[i]->getReg() == *reg)
                return true;
        }
        return false;
    }

    bool RISCVInstruction::definesRegister(shared_ptr<RISCVRegister> reg) const
    {
        // 检查目标操作数（通常是第一个操作数）
        if (!operands.empty() &&
            operands[0]->getType() == RISCVOperand::Type::REGISTER &&
            *operands[0]->getReg() == *reg)
            return true;
        return false;
    }

    string RISCVInstruction::toString() const
    {
        std::stringstream ss;
        bool memoryAccess = false;

        // 操作码转字符串
        auto opcodeToString = [](RISCVOpcode op) -> string
        {
            switch (op)
            {
            case RISCVOpcode::ADD:
                return "add";
            case RISCVOpcode::ADDI:
                return "addi";
            case RISCVOpcode::SUB:
                return "sub";
            case RISCVOpcode::MUL:
                return "mul";
            case RISCVOpcode::DIV:
                return "div";
            case RISCVOpcode::REM:
                return "rem";
            case RISCVOpcode::AND:
                return "and";
            case RISCVOpcode::ANDI:
                return "andi";
            case RISCVOpcode::OR:
                return "or";
            case RISCVOpcode::ORI:
                return "ori";
            case RISCVOpcode::XOR:
                return "xor";
            case RISCVOpcode::XORI:
                return "xori";
            case RISCVOpcode::SLL:
                return "sll";
            case RISCVOpcode::SLLI:
                return "slli";
            case RISCVOpcode::SRL:
                return "srl";
            case RISCVOpcode::SRLI:
                return "srli";
            case RISCVOpcode::SRA:
                return "sra";
            case RISCVOpcode::SRAI:
                return "srai";
            case RISCVOpcode::SLT:
                return "slt";
            case RISCVOpcode::SLTI:
                return "slti";
            case RISCVOpcode::SLTU:
                return "sltu";
            case RISCVOpcode::SLTIU:
                return "sltiu";
            case RISCVOpcode::BEQ:
                return "beq";
            case RISCVOpcode::BNE:
                return "bne";
            case RISCVOpcode::BLT:
                return "blt";
            case RISCVOpcode::BGE:
                return "bge";
            case RISCVOpcode::BLTU:
                return "bltu";
            case RISCVOpcode::BGEU:
                return "bgeu";
            case RISCVOpcode::JAL:
                return "jal";
            case RISCVOpcode::JALR:
                return "jalr";
            case RISCVOpcode::LB:
                return "lb";
            case RISCVOpcode::LH:
                return "lh";
            case RISCVOpcode::LW:
                return "lw";
            case RISCVOpcode::LBU:
                return "lbu";
            case RISCVOpcode::LHU:
                return "lhu";
            case RISCVOpcode::SB:
                return "sb";
            case RISCVOpcode::SH:
                return "sh";
            case RISCVOpcode::SW:
                return "sw";
            case RISCVOpcode::FADD_S:
                return "fadd.s";
            case RISCVOpcode::FSUB_S:
                return "fsub.s";
            case RISCVOpcode::FMUL_S:
                return "fmul.s";
            case RISCVOpcode::FDIV_S:
                return "fdiv.s";
            case RISCVOpcode::FEQ_S:
                return "feq.s";
            case RISCVOpcode::FLT_S:
                return "flt.s";
            case RISCVOpcode::FLE_S:
                return "fle.s";
            case RISCVOpcode::FCVT_W_S:
                return "fcvt.w.s";
            case RISCVOpcode::FCVT_S_W:
                return "fcvt.s.w";
            case RISCVOpcode::FLW:
                return "flw";
            case RISCVOpcode::FSW:
                return "fsw";
            case RISCVOpcode::LUI:
                return "lui";
            case RISCVOpcode::AUIPC:
                return "auipc";
            case RISCVOpcode::LI:
                return "li";
            case RISCVOpcode::LA:
                return "la";
            case RISCVOpcode::MV:
                return "mv";
            case RISCVOpcode::FMV_S:
                return "fmv.s";
            case RISCVOpcode::FMV_W_X:
                return "fmv.w.x";
            case RISCVOpcode::ECALL:
                return "ecall";
            case RISCVOpcode::EBREAK:
                return "ebreak";
            default:
                return "unknown";
            }
        };

        ss << "    " << opcodeToString(opcode);

        // 特殊处理系统指令（不需要操作数）
        if (opcode == RISCVOpcode::ECALL || opcode == RISCVOpcode::EBREAK)
        {
            // 系统指令不需要操作数
        }
        // 特殊处理内存访问指令的操作数格式
        else if (instrType == InstructionType::S_TYPE && operands.size() >= 3)
        {
            // S-Type: sw rs2, offset(rs1)
            // operands[0] = rs1 (基址), operands[1] = rs2 (源值), operands[2] = offset
            ss << " " << operands[1]->toString() << ", " << operands[2]->toString() << "(" << operands[0]->toString() << ")";
        }
        else if ((instrType == InstructionType::I_TYPE) &&
                 (opcode == RISCVOpcode::LW || opcode == RISCVOpcode::LH || opcode == RISCVOpcode::LB ||
                  opcode == RISCVOpcode::LHU || opcode == RISCVOpcode::LBU || opcode == RISCVOpcode::FLW) &&
                 operands.size() >= 3)
        {
            // I-Type内存加载: lw rd, offset(rs1)
            // operands[0] = rd (目标), operands[1] = rs1 (基址), operands[2] = offset
            ss << " " << operands[0]->toString() << ", " << operands[2]->toString() << "(" << operands[1]->toString() << ")";
        }
        else
        {
            // 其他指令按正常顺序输出操作数
            for (size_t i = 0; i < operands.size(); ++i)
            {
                if (i == 0)
                    ss << " ";
                else
                    ss << ", ";
                ss << operands[i]->toString();
            }
        }

        if (!comment.empty())
            ss << "  # " << comment;

        return ss.str();
    }

    // StackFrame 实现
    int StackFrame::getTotalSize() const
    {
        return valueStackSize + raStackSize + argStackSize;
    }

    int StackFrame::allocateSpace(Value *value, int size)
    {
        if (valueToOffset.find(value) != valueToOffset.end())
        {
            // 已经分配过，返回现有偏移
            return valueToOffset[value];
        }

        int offset = currentOffset;
        valueToOffset[value] = offset;
        currentOffset += size;

        // 更新相应的大小统计
        valueStackSize += size;

        return offset;
    }

    int StackFrame::getOffset(Value *value) const
    {
        auto it = valueToOffset.find(value);
        if (it != valueToOffset.end())
        {
            return it->second;
        }
        throw std::runtime_error("Value not found in stack frame");
    }

    bool StackFrame::hasAllocation(Value *value) const
    {
        return valueToOffset.find(value) != valueToOffset.end();
    }

    int StackFrame::getAlignedSize() const
    {
        int total = getTotalSize();
        // 16字节对齐
        return (total + 15) & ~15;
    }

    // RISCVBasicBlock 实现
    RISCVBasicBlock::RISCVBasicBlock(const string &label, shared_ptr<RISCVFunction> func)
        : label(label), parentFunc(func) {}

    void RISCVBasicBlock::addInstruction(shared_ptr<RISCVInstruction> instr)
    {
        instructions.push_back(instr);
    }

    void RISCVBasicBlock::insertInstruction(int index, shared_ptr<RISCVInstruction> instr)
    {
        if (index >= 0 && index <= static_cast<int>(instructions.size()))
            instructions.insert(instructions.begin() + index, instr);
    }

    void RISCVBasicBlock::removeInstruction(int index)
    {
        if (index >= 0 && index < static_cast<int>(instructions.size()))
            instructions.erase(instructions.begin() + index);
    }

    string RISCVBasicBlock::toString() const
    {
        std::stringstream ss;
        ss << label << ":\n";
        for (const auto &instr : instructions)
        {
            ss << instr->toString() << "\n";
        }
        return ss.str();
    }

    // RISCVGlobalBlock 实现
    RISCVGlobalBlock::RISCVGlobalBlock(const string &label)
        : label(label), size(0) {}

    void RISCVGlobalBlock::addData(const string &dataStr)
    {
        data.push_back(dataStr);
        isStringData.push_back(false); // 默认不是字符串数据
        size += 4;                     // 假设每个数据项4字节
    }

    void RISCVGlobalBlock::addData(const vector<string> &dataList)
    {
        for (const auto &item : dataList)
        {
            addData(item);
        }
    }

    void RISCVGlobalBlock::addStringData(const string &strData)
    {
        data.push_back(strData);
        isStringData.push_back(true); // 标记为字符串数据
        size += strData.length() + 1; // 字符串长度加上空字符
    }

    bool RISCVGlobalBlock::isZeroValue(const string &value) const
    {
        // 检查各种零值表示形式
        if (value == "0")
        {
            return true;
        }
        // 检查浮点零值的十六进制表示
        if (value == "0x00000000" || value == "0x0" || value == "0X00000000" || value == "0X0")
        {
            return true;
        }
        // 检查其他可能的零值表示
        if (value == "0.0" || value == "0f" || value == "0.0f")
        {
            return true;
        }
        return false;
    }

    string RISCVGlobalBlock::toString() const
    {
        std::stringstream ss;

        ss << ".globl " << label << "\n";
        ss << label << ":\n";

        // 优化连续的零数据
        size_t i = 0;
        while (i < data.size())
        {
            if (isStringData[i])
            {
                // 处理字符串数据
                ss << "    .asciz \"" << data[i] << "\"\n";
                i++;
            }
            else if (isZeroValue(data[i]))
            {
                // 计算连续零的数量
                size_t zeroCount = 0;
                size_t j = i;
                while (j < data.size() && !isStringData[j] && isZeroValue(data[j]))
                {
                    zeroCount++;
                    j++;
                }

                // 如果连续零的数量大于等于2，使用.zero指令优化
                if (zeroCount >= 2)
                {
                    size_t zeroBytes = zeroCount * 4; // 假设每个word是4字节
                    ss << "    .zero " << zeroBytes << "\n";
                    i = j; // 跳过所有连续的零
                }
                else
                {
                    // 连续零较少，使用常规.word输出
                    for (size_t k = 0; k < zeroCount; k++)
                    {
                        ss << "    .word 0\n";
                    }
                    i = j;
                }
            }
            else
            {
                // 非零数据，正常输出
                ss << "    .word " << data[i] << "\n";
                i++;
            }
        }

        return ss.str();
    }

    // RISCVFunction 实现
    RISCVFunction::RISCVFunction(const string &name, shared_ptr<RISCVModule> module)
        : name(name), parentModule(module) {}

    void RISCVFunction::addBasicBlock(shared_ptr<RISCVBasicBlock> bb)
    {
        basicBlocks.push_back(bb);
    }

    shared_ptr<RISCVBasicBlock> RISCVFunction::getBasicBlock(const string &label) const
    {
        for (const auto &bb : basicBlocks)
        {
            if (bb->getLabel() == label)
                return bb;
        }
        return nullptr;
    }

    string RISCVFunction::toString() const
    {
        std::stringstream ss;
        ss << name << ":\n";

        for (const auto &bb : basicBlocks)
        {
            ss << bb->toString();
        }

        return ss.str();
    }

    // RISCVModule 实现
    void RISCVModule::addFunction(shared_ptr<RISCVFunction> func)
    {
        functions.push_back(func);
        functionMap[func->getName()] = func;
    }

    shared_ptr<RISCVFunction> RISCVModule::getFunction(const string &name) const
    {
        auto it = functionMap.find(name);
        return (it != functionMap.end()) ? it->second : nullptr;
    }

    shared_ptr<RISCVGlobalBlock> RISCVModule::createGlobalBlock(const string &label)
    {
        auto block = std::make_shared<RISCVGlobalBlock>(label);
        globalBlocks.push_back(block);
        return block;
    }

    void RISCVModule::addGlobalBlock(shared_ptr<RISCVGlobalBlock> block)
    {
        globalBlocks.push_back(block);
    }

    string RISCVModule::toString() const
    {
        std::stringstream ss;
        ss << "# Generated RISC-V assembly for module: " << name << "\n\n";

        // 输出全局变量
        for (const auto &global : globalBlocks)
        {
            ss << global->toString() << "\n";
        }

        // 输出函数
        for (const auto &func : functions)
        {

            ss << func->toString() << "\n";
        }

        return ss.str();
    }

    // RISCVInstruction 工厂方法实现
    shared_ptr<RISCVInstruction> RISCVInstruction::createRType(RISCVOpcode op,
                                                               shared_ptr<RISCVRegister> rd,
                                                               shared_ptr<RISCVRegister> rs1,
                                                               shared_ptr<RISCVRegister> rs2)
    {
        auto instr = make_shared<RISCVInstruction>(op, InstructionType::R_TYPE);
        instr->operands = {make_shared<RISCVOperand>(rd),
                           make_shared<RISCVOperand>(rs1),
                           make_shared<RISCVOperand>(rs2)};
        return instr;
    }

    shared_ptr<RISCVInstruction> RISCVInstruction::createIType(RISCVOpcode op,
                                                               shared_ptr<RISCVRegister> rd,
                                                               shared_ptr<RISCVRegister> rs1,
                                                               int64_t imm)
    {
        auto instr = make_shared<RISCVInstruction>(op, InstructionType::I_TYPE);
        instr->operands = {make_shared<RISCVOperand>(rd),
                           make_shared<RISCVOperand>(rs1),
                           make_shared<RISCVOperand>(imm)};
        return instr;
    }

    shared_ptr<RISCVInstruction> RISCVInstruction::createSType(RISCVOpcode op,
                                                               shared_ptr<RISCVRegister> rs1,
                                                               shared_ptr<RISCVRegister> rs2,
                                                               int64_t imm)
    {
        auto instr = make_shared<RISCVInstruction>(op, InstructionType::S_TYPE);
        instr->operands = {make_shared<RISCVOperand>(rs1),
                           make_shared<RISCVOperand>(rs2),
                           make_shared<RISCVOperand>(imm)};
        return instr;
    }

    shared_ptr<RISCVInstruction> RISCVInstruction::createBType(RISCVOpcode op,
                                                               shared_ptr<RISCVRegister> rs1,
                                                               shared_ptr<RISCVRegister> rs2,
                                                               const string &label)
    {
        auto instr = make_shared<RISCVInstruction>(op, InstructionType::B_TYPE);
        instr->operands = {make_shared<RISCVOperand>(rs1),
                           make_shared<RISCVOperand>(rs2),
                           make_shared<RISCVOperand>(label)};
        return instr;
    }

    shared_ptr<RISCVInstruction> RISCVInstruction::createUType(RISCVOpcode op,
                                                               shared_ptr<RISCVRegister> rd,
                                                               int64_t imm)
    {
        auto instr = make_shared<RISCVInstruction>(op, InstructionType::U_TYPE);
        instr->operands = {make_shared<RISCVOperand>(rd),
                           make_shared<RISCVOperand>(imm)};
        return instr;
    }

    shared_ptr<RISCVInstruction> RISCVInstruction::createJType(RISCVOpcode op,
                                                               shared_ptr<RISCVRegister> rd,
                                                               const string &label)
    {
        auto instr = make_shared<RISCVInstruction>(op, InstructionType::J_TYPE);
        instr->operands = {make_shared<RISCVOperand>(rd),
                           make_shared<RISCVOperand>(label)};
        return instr;
    }

    shared_ptr<RISCVInstruction> RISCVInstruction::createPseudo(RISCVOpcode op,
                                                                shared_ptr<RISCVRegister> rd,
                                                                shared_ptr<RISCVRegister> rs1)
    {
        auto instr = make_shared<RISCVInstruction>(op, InstructionType::PSEUDO);
        instr->operands = {make_shared<RISCVOperand>(rd),
                           make_shared<RISCVOperand>(rs1)};
        return instr;
    }

    shared_ptr<RISCVInstruction> RISCVInstruction::createPseudoLI(shared_ptr<RISCVRegister> rd, int64_t imm)
    {
        auto instr = make_shared<RISCVInstruction>(RISCVOpcode::LI, InstructionType::PSEUDO);
        instr->operands = {make_shared<RISCVOperand>(rd),
                           make_shared<RISCVOperand>(imm)};
        return instr;
    }

    shared_ptr<RISCVInstruction> RISCVInstruction::createPseudoLA(shared_ptr<RISCVRegister> rd, const string &label)
    {
        auto instr = make_shared<RISCVInstruction>(RISCVOpcode::LA, InstructionType::PSEUDO);
        instr->operands = {make_shared<RISCVOperand>(rd),
                           make_shared<RISCVOperand>(label)};
        return instr;
    }

    shared_ptr<RISCVInstruction> RISCVInstruction::createPseudoCALL(const string &label)
    {
        auto instr = make_shared<RISCVInstruction>(RISCVOpcode::CALL, InstructionType::PSEUDO);
        instr->operands = {make_shared<RISCVOperand>(label)};
        return instr;
    }

    shared_ptr<RISCVInstruction> RISCVInstruction::createPseudoRET()
    {
        auto instr = make_shared<RISCVInstruction>(RISCVOpcode::RET, InstructionType::PSEUDO);
        instr->operands = {}; // RET 不需要操作数
        return instr;
    }
}
