#pragma once
#include "../midend/irbuild/IRDataStructure.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>

using std::find;
using std::shared_ptr;
using std::string;
using std::unordered_map;
using std::unordered_set;
using std::vector;

namespace RISCV
{
    // 前向声明
    class RISCVRegister;
    class RISCVInstruction;
    class RISCVBasicBlock;
    class RISCVFunction;
    class RISCVModule;

    // RISC-V指令类型枚举
    enum class InstructionType
    {
        R_TYPE, // 寄存器-寄存器操作
        I_TYPE, // 立即数操作
        S_TYPE, // 存储操作
        B_TYPE, // 分支操作
        U_TYPE, // 上位立即数操作
        J_TYPE, // 跳转操作
        PSEUDO  // 伪指令
    };

    // RISC-V操作码枚举
    enum class RISCVOpcode
    {
        // 基本算术指令
        ADD,
        ADDI,
        SUB,
        MUL,
        DIV,
        REM,
        ADDW,
        ADDIW,
        SUBW,
        MULW,
        DIVW,
        REMW,
        // 逻辑指令
        AND,
        ANDI,
        OR,
        ORI,
        XOR,
        XORI,
        SLL,
        SLLI,
        SLLW,
        SLLIW,
        SRL,
        SRLI,
        SRLW,
        SRA,
        SRAI,
        SRAW,

        // 比较指令
        SLT,
        SLTI,
        SLTU,
        SLTIU,

        // 分支指令
        BEQ,
        BNE,
        BLT,
        BGE,
        BLTU,
        BGEU,

        // 跳转指令
        JAL,
        JALR,

        // 内存访问指令
        LD,
        LB,
        LH,
        LW,
        LBU,
        LHU,
        SD,
        SB,
        SH,
        SW,

        // 浮点算术指令
        FADD_S,
        FSUB_S,
        FMUL_S,
        FDIV_S,

        // 浮点比较指令
        FEQ_S,
        FLT_S,
        FLE_S,

        // 浮点转换指令
        FCVT_W_S,
        FCVT_S_W,

        // 浮点内存访问
        FLD,
        FLW,
        FSD,
        FSW,

        // 立即数加载
        LUI,
        AUIPC,

        // 伪指令
        LI,
        LA,
        MV,
        FMV_S,
        FMV_W_X, // 整数寄存器到浮点寄存器的移动
        FMV_X_W, // 浮点寄存器到整数寄存器的移动

        // 系统指令
        ECALL,
        EBREAK,

        // 函数调用指令
        CALL, // 伪指令，用于函数调用
        RET,  // 伪指令，用于函数返回

        // 自创的伪指令
        INIT,
    };

    // 寄存器类型枚举
    enum class RegisterType
    {
        INT,
        FLOAT,
    };

    // RISC-V寄存器类
    class RISCVRegister
    {
    public:
        enum class PhysicalReg
        {
            // 通用寄存器
            ZERO = 0,
            RA,
            SP,
            GP,
            TP,
            T0,
            T1,
            T2,
            S0,
            S1,
            A0,
            A1,
            A2,
            A3,
            A4,
            A5,
            A6,
            A7,
            S2,
            S3,
            S4,
            S5,
            S6,
            S7,
            S8,
            S9,
            S10,
            S11,
            T3,
            T4,
            T5,
            T6,

            // 浮点寄存器
            FT0 = 32,
            FT1,
            FT2,
            FT3,
            FT4,
            FT5,
            FT6,
            FT7,
            FS0,
            FS1,
            FA0,
            FA1,
            FA2,
            FA3,
            FA4,
            FA5,
            FA6,
            FA7,
            FS2,
            FS3,
            FS4,
            FS5,
            FS6,
            FS7,
            FS8,
            FS9,
            FS10,
            FS11,
            FT8,
            FT9,
            FT10,
            FT11
        };

    private:
        RegisterType type;
        PhysicalReg physicalReg;
        int virtualId; // 虚拟寄存器ID（-1表示物理寄存器）
        static int nextVirtualId;

    public:
        // 构造函数
        RISCVRegister() {}
        RISCVRegister(PhysicalReg reg);   // 物理寄存器
        RISCVRegister(RegisterType type); // 虚拟寄存器
        // RISCVRegister(PhysicalReg reg, RegisterType regType); // 物理寄存器指定类型

        // 访问器
        RegisterType getType() const { return type; }
        RegisterType getRegType() const { return type; } // 兼容性方法
        bool isVirtual() const { return virtualId != -1; }
        bool isPhysical() const { return virtualId == -1; }
        int getVirtualId() const { return virtualId; }
        PhysicalReg getPhysicalReg() const { return physicalReg; }

        string toString() const;
        bool operator==(const RISCVRegister &other) const;

        struct RegisterHash
        {
            size_t operator()(const std::shared_ptr<RISCV::RISCVRegister> &reg) const
            {
                if (reg->isPhysical())
                    return std::hash<int>()(static_cast<int>(reg->getPhysicalReg())) ^ std::hash<int>()(static_cast<int>(reg->getType()));
                else
                    return std::hash<int>()(reg->getVirtualId()) ^ std::hash<int>()(static_cast<int>(reg->getType()));
            }
        };

        struct RegisterEqual
        {
            bool operator()(const std::shared_ptr<RISCV::RISCVRegister> &lhs,
                            const std::shared_ptr<RISCV::RISCVRegister> &rhs) const
            {
                if (!lhs || !rhs)
                    return false;
                return *lhs == *rhs; // 用你自定义的 operator==
            }
        };
    };

    // 操作数类
    class RISCVOperand
    {
    public:
        enum class Type
        {
            REGISTER,  // 寄存器操作数
            IMMEDIATE, // 立即数操作数
            MEMORY,    // 内存操作数
            LABEL      // 标签操作数
        };

    private:
        Type type;
        shared_ptr<RISCVRegister> reg; // 寄存器（用于REGISTER和MEMORY类型）
        int64_t immediate;             // 立即数值
        string label;                  // 标签名
        int offset;                    // 内存偏移量

    public:
        // 构造函数
        RISCVOperand(shared_ptr<RISCVRegister> reg);              // 寄存器操作数
        RISCVOperand(int64_t imm);                                // 立即数操作数
        RISCVOperand(shared_ptr<RISCVRegister> base, int offset); // 内存操作数
        RISCVOperand(const string &label);                        // 标签操作数

        // 访问器
        Type getType() const { return type; }
        shared_ptr<RISCVRegister> getReg() const { return reg; }
        int64_t getImmediate() const { return immediate; }
        const string &getLabel() const { return label; }
        int getOffset() const { return offset; }

        string toString() const;
    };

    // RISC-V指令类
    class RISCVInstruction
    {
    private:
        RISCVOpcode opcode;
        InstructionType instrType;
        vector<shared_ptr<RISCVOperand>> operands;
        string comment; // 调试注释

    public:
        // 基础构造函数
        RISCVInstruction(RISCVOpcode op, InstructionType type)
            : opcode(op), instrType(type) {}

        // 工厂方法用于创建不同类型的指令
        static shared_ptr<RISCVInstruction> createRType(RISCVOpcode op,
                                                        shared_ptr<RISCVRegister> rd,
                                                        shared_ptr<RISCVRegister> rs1,
                                                        shared_ptr<RISCVRegister> rs2);

        static shared_ptr<RISCVInstruction> createIType(RISCVOpcode op,
                                                        shared_ptr<RISCVRegister> rd,
                                                        shared_ptr<RISCVRegister> rs1,
                                                        int64_t imm);

        static shared_ptr<RISCVInstruction> createSType(RISCVOpcode op,
                                                        shared_ptr<RISCVRegister> rs1,
                                                        shared_ptr<RISCVRegister> rs2,
                                                        int64_t imm);

        static shared_ptr<RISCVInstruction> createBType(RISCVOpcode op,
                                                        shared_ptr<RISCVRegister> rs1,
                                                        shared_ptr<RISCVRegister> rs2,
                                                        const string &label);

        static shared_ptr<RISCVInstruction> createUType(RISCVOpcode op,
                                                        shared_ptr<RISCVRegister> rd,
                                                        int64_t imm);

        static shared_ptr<RISCVInstruction> createJType(RISCVOpcode op,
                                                        shared_ptr<RISCVRegister> rd,
                                                        const string &label);

        static shared_ptr<RISCVInstruction> createPseudo(RISCVOpcode op,
                                                         shared_ptr<RISCVRegister> rd,
                                                         shared_ptr<RISCVRegister> rs1);

        static shared_ptr<RISCVInstruction> createPseudoLI(shared_ptr<RISCVRegister> rd, int64_t imm);
        static shared_ptr<RISCVInstruction> createPseudoLA(shared_ptr<RISCVRegister> rd, const string &label);
        static shared_ptr<RISCVInstruction> createPseudoCALL(const string &label = "");
        static shared_ptr<RISCVInstruction> createPseudoRET();
        static shared_ptr<RISCVInstruction> createPseudoECALL();
        static shared_ptr<RISCVInstruction> createPseudoINIT(const string &name, int64_t offset, int64_t size);

        // 访问器
        RISCVOpcode getOpcode() const
        {
            return opcode;
        }
        InstructionType getInstrType() const { return instrType; }
        vector<shared_ptr<RISCVOperand>> &getOperands() { return operands; }

        // 设置注释
        void setComment(const string &c) { comment = c; }

        void setOffsetForLiInstruction(int64_t offset)
        {
            if (instrType == InstructionType::PSEUDO && opcode == RISCVOpcode::LI && operands.size() == 2)
            {
                // 假设第二个操作数是立即数
                if (operands[1]->getType() == RISCVOperand::Type::IMMEDIATE)
                {
                    operands[1] = make_shared<RISCVOperand>(offset);
                }
            }
        }

        void setOffsetForAddiInstruction(int64_t offset)
        {
            if (instrType == InstructionType::I_TYPE && opcode == RISCVOpcode::ADDI && operands.size() == 3)
            {
                // 假设第三个操作数是立即数
                if (operands[2]->getType() == RISCVOperand::Type::IMMEDIATE)
                {
                    operands[2] = make_shared<RISCVOperand>(offset);
                }
            }
        }

        // 活跃性分析：获取指令使用和定义的寄存器
        vector<shared_ptr<RISCVRegister>> getUseRegisters() const;
        vector<shared_ptr<RISCVRegister>> getDefRegisters() const;

        // 辅助函数：检查操作数是否为寄存器
        bool isRegisterOperand(shared_ptr<RISCVOperand> operand) const;

        // 寄存器替换函数：用于溢出处理
        void replaceUseRegister(shared_ptr<RISCVRegister> oldReg, shared_ptr<RISCVRegister> newReg);
        void replaceDefRegister(shared_ptr<RISCVRegister> oldReg, shared_ptr<RISCVRegister> newReg);

        // 更换指令
        void replaceInstruction(shared_ptr<RISCVInstruction> newInstr)
        {
            opcode = newInstr->getOpcode();
            instrType = newInstr->getInstrType();
            operands = newInstr->getOperands();
            comment = newInstr->comment; // 保留注释
        }

        string toString() const;
    };

    // 栈帧结构体
    // 高地址 (函数入口时的 sp)
    // +------------------+ <- sp (函数入口)
    // | raStackSize      | ← 保存返回地址 ra 寄存器
    // +------------------+
    // | valueStackSize   | ← 局部变量、临时值、alloca等
    // +------------------+
    // | argStackSize     | ← 为调用其他函数预留的参数空间
    // +------------------+ <- sp (函数执行中)
    // 低地址

    struct StackFrame
    {
        int valueStackSize;  // 所有Value（局部变量、临时值、参数等）的栈空间
        int raStackSize;     // ra寄存器需要的栈空间（ABI规范）
        int argStackSize;    // 传参预留的栈空间（ABI规范）
        int maxArgStackSize; // 最大参数栈空间（用于函数调用）

        unordered_map<string, int> valueToOffset;
        unordered_map<string, int> callerToOffset;
        unordered_map<string, unordered_map<int, int>> calleeToOffset; // 被调用函数参数偏移

        // 已分配的栈空间偏移（从0开始分配）
        int valueOffset;
        int calleeArgOffset;
        int callerArgOffset;

        StackFrame() : valueStackSize(0), raStackSize(0), argStackSize(0), maxArgStackSize(0), valueOffset(0), calleeArgOffset(0), callerArgOffset(0) {}

        int getTotalSize();
        int getAlignedSize();

        // 分配栈空间并返回偏移量
        int allocateValueSpace(const string &valueName, int size = 4);
        int allocateCalleeArgSpace(const string &calleeName, int ArgNumber, int size = 4); // 为被调用函数参数分配空间
        int allocateCallerArgSpace(const string &valueName, int size = 4);
        int allocateRaSpace(int size = 4); // 为返回地址分配空间

        // 获取栈偏移量
        int getValueOffset(const string &valueName) const;
        int getCallerArgOffset(const string &valueName);                           // 获取调用参数偏移
        int getCalleeArgOffset(const string &calleeName, int ArgNumber) const;     // 获取被调用函数参数
        int getRaOffset() { return raStackSize > 0 ? getAlignedSize() - 8 : -1; }; // 获取返回地址偏移

        // 检查是否有分配的栈空间
        bool hasAllocation_value(const string &valueName) const;
        bool hasAllocation_callerArg(const string &valueName) const;                 // 检查调用参数是否有分配
        bool hasAllocation_calleeArg(const string &callerName, int ArgNumber) const; // 检查被调用
    };

    // RISC-V基本块
    class RISCVBasicBlock
    {
    private:
        string label;
        shared_ptr<RISCVFunction> parentFunc;
        vector<shared_ptr<RISCVInstruction>> instructions;

        // 控制流图信息
        vector<shared_ptr<RISCVBasicBlock>> predecessors; // 前驱基本块
        vector<shared_ptr<RISCVBasicBlock>> successors;   // 后继基本块

        // 活跃变量分析结果
        unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> use; // 在定义前使用的寄存器
        unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> def; // 在基本块中定义的寄存器
        unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> liveIn;
        unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> liveOut;

    public:
        RISCVBasicBlock(const string &label, shared_ptr<RISCVFunction> func);

        // 指令管理
        void addInstruction(shared_ptr<RISCVInstruction> instr);
        void insertInstruction(int index, shared_ptr<RISCVInstruction> instr);
        void removeInstruction(int index);
        void setInstructions(const vector<shared_ptr<RISCVInstruction>> &instrs);

        // 访问器
        const string &getLabel() const { return label; }
        vector<shared_ptr<RISCVInstruction>> &getInstructions() { return instructions; }
        shared_ptr<RISCVFunction> getParentFunc() const { return parentFunc; }

        // 控制流图管理
        void addPredecessor(shared_ptr<RISCVBasicBlock> pred) { predecessors.push_back(pred); } // 添加前驱基本块
        void removePredecessor(shared_ptr<RISCVBasicBlock> pred)
        {
            auto it = find(predecessors.begin(), predecessors.end(), pred);
            if (it != predecessors.end())
            {
                predecessors.erase(it);
            }
        }
        void addSuccessor(shared_ptr<RISCVBasicBlock> succ) { successors.push_back(succ); } // 添加后继基本块
        void removeSuccessor(shared_ptr<RISCVBasicBlock> succ)
        {
            auto it = find(successors.begin(), successors.end(), succ);
            if (it != successors.end())
            {
                successors.erase(it);
            }
        } // 移除后继基本块
        const vector<shared_ptr<RISCVBasicBlock>> &getPredecessors() const { return predecessors; }
        const vector<shared_ptr<RISCVBasicBlock>> &getSuccessors() const { return successors; }

        // 活跃变量分析
        const unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> &getLiveIn() const { return liveIn; }
        const unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> &getLiveOut() const { return liveOut; }
        void setLiveIn(const unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> &live) { liveIn = live; }
        void setLiveOut(const unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> &live) { liveOut = live; }
        void addLiveIn(shared_ptr<RISCVRegister> reg)
        {
            liveIn.insert(reg);
        }
        void addLiveOut(shared_ptr<RISCVRegister> reg)
        {
            liveOut.insert(reg);
        }
        const unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> &getUse() const { return use; }
        const unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> &getDef() const { return def; }
        void setUse(const unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> &useSet) { use = useSet; }
        void setDef(const unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> &defSet) { def = defSet; }

        string toString() const;
    };

    // 全局变量块
    class RISCVGlobalBlock
    {
    private:
        string label;
        vector<string> data;
        vector<bool> isStringData; // 标记对应位置是否为字符串数据
        vector<bool> isArrayData;  // 标记对应位置是否为数组数据
        int size;

        // 辅助方法：检查字符串是否为零值
        bool isZeroValue(const string &value) const;

    public:
        RISCVGlobalBlock(const string &label);

        void addData(const string &dataStr);
        void addData(const vector<string> &dataList);
        void addStringData(const string &strData); // 添加字符串数据
        void addZeroData(int numElements);         // 添加大量零数据的优化方法

        const string &getLabel() const { return label; }
        const vector<string> &getData() const { return data; }
        int getSize() const { return size; }

        string toString() const;
    };

    // 活跃区间定义
    struct LiveRange
    {
        int start;
        int end;

        LiveRange(int s, int e) : start(s), end(e) {}

        // 检查两个区间是否重叠
        bool overlaps(const LiveRange &other) const
        {
            return !(end < other.start || other.end < start);
        }

        // 检查指令点是否在区间内
        bool contains(int instrNum) const
        {
            return instrNum >= start && instrNum <= end;
        }

        // 区间长度
        int length() const
        {
            return end - start + 1;
        }

        // 合并两个相邻或重叠的区间
        static LiveRange merge(const LiveRange &a, const LiveRange &b)
        {
            return LiveRange(std::min(a.start, b.start), std::max(a.end, b.end));
        }

        // 比较操作符
        bool operator<(const LiveRange &other) const
        {
            return start < other.start;
        }

        bool operator==(const LiveRange &other) const
        {
            return start == other.start && end == other.end;
        }
    };

    // 活跃性分析信息
    struct LivenessInfo
    {
        // 每个寄存器的多段活跃区间
        unordered_map<
            shared_ptr<RISCVRegister>,
            vector<LiveRange>,
            RISCVRegister::RegisterHash,
            RISCVRegister::RegisterEqual>
            liveRanges;

        // 每个寄存器的所有使用点
        unordered_map<shared_ptr<RISCVRegister>, vector<int>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> usePoints;

        // 每个寄存器的所有定义点
        unordered_map<shared_ptr<RISCVRegister>, vector<int>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> defPoints;

        // 总指令数
        int totalInstructions;

        LivenessInfo() : totalInstructions(0) {}

        // 获取寄存器的所有活跃区间
        const vector<LiveRange> &getLiveRanges(shared_ptr<RISCVRegister> reg) const
        {
            static vector<LiveRange> empty;
            auto it = liveRanges.find(reg);
            return it != liveRanges.end() ? it->second : empty;
        }

        // 检查两个寄存器是否冲突
        bool interferes(shared_ptr<RISCVRegister> reg1, shared_ptr<RISCVRegister> reg2) const
        {
            auto it1 = liveRanges.find(reg1);
            auto it2 = liveRanges.find(reg2);

            if (it1 == liveRanges.end() || it2 == liveRanges.end())
            {
                return false;
            }

            // 检查任意两个区间是否重叠
            for (const auto &range1 : it1->second)
            {
                for (const auto &range2 : it2->second)
                {
                    if (range1.overlaps(range2))
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        // 检查寄存器在指定指令点是否活跃
        bool isLiveAt(shared_ptr<RISCVRegister> reg, int instrNum) const
        {
            auto it = liveRanges.find(reg);
            if (it == liveRanges.end())
                return false;

            for (const auto &range : it->second)
            {
                if (range.contains(instrNum))
                {
                    return true;
                }
            }
            return false;
        }

        // 获取寄存器的总活跃长度
        int getTotalLiveLength(shared_ptr<RISCVRegister> reg) const
        {
            auto it = liveRanges.find(reg);
            if (it == liveRanges.end())
                return 0;

            int total = 0;
            for (const auto &range : it->second)
            {
                total += range.length();
            }
            return total;
        }

        // 清空所有信息
        void clear()
        {
            liveRanges.clear();
            usePoints.clear();
            defPoints.clear();
            totalInstructions = 0;
        }
        std::string toString() const
        {
            std::ostringstream oss;
            for (const auto &pair : liveRanges)
            {
                const auto &reg = pair.first;
                const auto &ranges = pair.second;

                oss << "Register: " << reg->toString() << "\n";
                oss << "Live Ranges:\n";
                for (const auto &range : ranges)
                {
                    oss << "  [" << range.start << ", " << range.end << "]\n";
                }
            }
            oss << "Total Instructions: " << totalInstructions << "\n";
            return oss.str();
        }

        void addLiveRange(shared_ptr<RISCVRegister> reg, int start, int end)
        {
            if (start > end)
                return;

            auto it = liveRanges.find(reg);
            if (it != liveRanges.end())
            {
                // 如果寄存器已经有活跃区间，尝试合并
                auto &ranges = it->second;
                for (auto &range : ranges)
                {
                    if (range.overlaps(LiveRange(start, end)))
                    {
                        // 合并区间
                        range = LiveRange::merge(range, LiveRange(start, end));
                        return;
                    }
                }
                // 如果没有重叠，直接添加新的区间
                ranges.push_back(LiveRange(start, end));
            }
            else
            {
                // 首次插入
                liveRanges[reg] = {LiveRange(start, end)};
            }
        }
    };

    class RISCVLoop
    {
    private:
        int depth;
        shared_ptr<RISCVBasicBlock> header;       // 循环头部基本块
        vector<shared_ptr<RISCVBasicBlock>> body; // 循环体基本块
        vector<shared_ptr<RISCVBasicBlock>> exit; // 循环出口基本块
        shared_ptr<RISCVLoop> parentLoop;         // 所在的父循环
        vector<shared_ptr<RISCVLoop>> childLoops; // 子循环列表

    public:
        RISCVLoop() {}
        void setHeader(shared_ptr<RISCVBasicBlock> h) { header = h; }
        void addBodyBlock(shared_ptr<RISCVBasicBlock> block) { body.push_back(block); }
        void addExitBlock(shared_ptr<RISCVBasicBlock> block) { exit.push_back(block); }
        void setParentLoop(shared_ptr<RISCVLoop> parent) { parentLoop = parent; }
        void addChildLoop(shared_ptr<RISCVLoop> child) { childLoops.push_back(child); }
        void setDepth(int d) { depth = d; }
        int getDepth() const { return depth; }
        shared_ptr<RISCVBasicBlock> getHeader() const { return header; }
        shared_ptr<RISCVLoop> getParentLoop() const { return parentLoop; }
        const vector<shared_ptr<RISCVBasicBlock>> &getBody() const { return body; }
        const vector<shared_ptr<RISCVLoop>> &getChildLoops() const { return childLoops; }
        const vector<shared_ptr<RISCVBasicBlock>> &getExitBlocks() const { return exit; }
        bool containsBlock(shared_ptr<RISCVBasicBlock> block) const
        {
            if (header == block)
                return true;
            for (const auto &b : body)
            {
                if (b == block)
                    return true;
            }
            for (const auto &e : exit)
            {
                if (e == block)
                    return true;
            }
            return false;
        }
    };

    class LoopInfo
    {
    private:
        vector<shared_ptr<RISCVLoop>> loops; // 存储所有循环
    public:
        void addLoop(shared_ptr<RISCVLoop> loop) { loops.push_back(loop); }
        const vector<shared_ptr<RISCVLoop>> &getLoops() const { return loops; }
        int getDepth(shared_ptr<RISCVBasicBlock> block) const
        {
            for (const auto &loop : loops)
            {
                if (loop->containsBlock(block))
                {
                    return loop->getDepth();
                }
            }
            return 0;
        }
    };

    // RISC-V函数
    class RISCVFunction
    {
    private:
        string name;
        shared_ptr<RISCVModule> parentModule;
        vector<shared_ptr<RISCVBasicBlock>> basicBlocks;
        StackFrame stackFrame;
        LivenessInfo livenessInfo;                                                             // 活跃性分析结果
        LoopInfo loopInfo;                                                                     // 循环信息
        vector<shared_ptr<RISCVRegister>> usedCalleeSavedRegs;                                 // 被调用函数使用的保存寄存器
        unordered_map<string, vector<shared_ptr<RISCVInstruction>>> moveInstructionsAfterCall; // 用于函数调用后移动指令的映射
        unordered_map<string, shared_ptr<RISCVInstruction>> instructionNeedReGetOffset;        // 用于溢出处理的指令

    public:
        RISCVFunction(const string &name, shared_ptr<RISCVModule> module);

        shared_ptr<RISCVModule> getParentModule() const { return parentModule; }

        // 基本块管理
        void addBasicBlock(shared_ptr<RISCVBasicBlock> bb);
        shared_ptr<RISCVBasicBlock> getBasicBlock(const string &label) const;

        // 访问器
        const string &getName() const { return name; }
        const vector<shared_ptr<RISCVBasicBlock>> &getBasicBlocks() { return basicBlocks; }
        StackFrame &getStackFrame() { return stackFrame; }

        // 函数调用后的移动指令
        void addMoveInstructionAfterCall(const string &calleeName, shared_ptr<RISCVInstruction> instr)
        {
            moveInstructionsAfterCall[calleeName].push_back(instr);
        }
        const vector<shared_ptr<RISCVInstruction>> &getMoveInstructionsAfterCall(const string &calleeName)
        {
            static vector<shared_ptr<RISCVInstruction>> empty;
            auto it = moveInstructionsAfterCall.find(calleeName);
            return it != moveInstructionsAfterCall.end() ? it->second : empty;
        }
        // 活跃性信息访问
        const LivenessInfo &getLivenessInfo() const { return livenessInfo; }
        LivenessInfo &getLivenessInfo() { return livenessInfo; }

        // 获得用于溢出处理的指令
        const unordered_map<string, shared_ptr<RISCVInstruction>> &getInstructionNeedReGetOffset() const
        {
            return instructionNeedReGetOffset;
        }
        void addInstructionNeedReGetOffset(string ArgName, shared_ptr<RISCVInstruction> instr)
        {
            instructionNeedReGetOffset[ArgName] = instr;
        }

        void addUsedCalleeSavedReg(shared_ptr<RISCVRegister> reg);

        const vector<shared_ptr<RISCVRegister>> &getUsedCalleeSavedRegs() const
        {
            return usedCalleeSavedRegs;
        }

        void clearUsedCalleeSavedRegs()
        {
            usedCalleeSavedRegs.clear();
        }

        void setLoopInfo(LoopInfo &info) { loopInfo = info; }
        const LoopInfo &getLoopInfo() const { return loopInfo; }

        string toString() const;
    };

    // RISC-V模块
    class RISCVModule
    {
    private:
        string name;
        vector<shared_ptr<RISCVFunction>> functions;
        vector<shared_ptr<RISCVGlobalBlock>> globalBlocks;
        unordered_map<string, shared_ptr<RISCVFunction>> functionMap;

    public:
        RISCVModule(const string &name) : name(name) {}

        // 函数管理
        void addFunction(shared_ptr<RISCVFunction> func);
        shared_ptr<RISCVFunction> getFunction(const string &name) const;

        // 全局变量管理
        shared_ptr<RISCVGlobalBlock> createGlobalBlock(const string &label);
        void addGlobalBlock(shared_ptr<RISCVGlobalBlock> block);

        // 访问器
        const string &getName() const { return name; }
        const vector<shared_ptr<RISCVFunction>> &getFunctions() const { return functions; }
        const vector<shared_ptr<RISCVGlobalBlock>> &getGlobalBlocks() const { return globalBlocks; }

        string toString() const;
    };
}