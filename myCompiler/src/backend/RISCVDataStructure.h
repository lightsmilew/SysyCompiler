#pragma once
#include "../midend/IRDataStructure.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <unordered_set>

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

        // 逻辑指令
        AND,
        ANDI,
        OR,
        ORI,
        XOR,
        XORI,
        SLL,
        SLLI,
        SRL,
        SRLI,
        SRA,
        SRAI,

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
        LB,
        LH,
        LW,
        LBU,
        LHU,
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
        FLW,
        FSW,

        // 立即数加载
        LUI,
        AUIPC,

        // 伪指令
        LI,
        LA,
        MV,
        FMV_S,

        // 系统指令
        ECALL,
        EBREAK
    };

    // 寄存器类型枚举
    enum class RegisterType
    {
        GENERAL, // 通用寄存器
        FLOAT,   // 浮点寄存器
        VIRTUAL  // 虚拟寄存器（寄存器分配前使用）
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
        RISCVRegister(PhysicalReg reg);   // 物理寄存器
        RISCVRegister(RegisterType type); // 虚拟寄存器

        // 访问器
        RegisterType getType() const { return type; }
        bool isVirtual() const { return virtualId != -1; }
        bool isPhysical() const { return virtualId == -1; }
        int getVirtualId() const { return virtualId; }
        PhysicalReg getPhysicalReg() const { return physicalReg; }

        string toString() const;
        bool operator==(const RISCVRegister &other) const;
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

        // 访问器
        RISCVOpcode getOpcode() const { return opcode; }
        InstructionType getInstrType() const { return instrType; }
        const vector<shared_ptr<RISCVOperand>> &getOperands() const { return operands; }

        // 设置注释
        void setComment(const string &c) { comment = c; }

        // 检查寄存器使用
        bool usesRegister(shared_ptr<RISCVRegister> reg) const;
        bool definesRegister(shared_ptr<RISCVRegister> reg) const;

        string toString() const;
    };

    // 栈帧管理
    struct StackFrame
    {
        int localVarSize;   // 局部变量大小
        int tempVarSize;    // 临时变量大小
        int savedRegSize;   // 保存寄存器大小
        int maxCallArgSize; // 最大函数调用参数大小

        StackFrame() : localVarSize(0), tempVarSize(0), savedRegSize(0), maxCallArgSize(0) {}

        int getTotalSize() const;
        void updateMaxCallArgSize(int size);
    };

    // RISC-V基本块
    class RISCVBasicBlock
    {
    private:
        string label;
        shared_ptr<RISCVFunction> parentFunc;
        vector<shared_ptr<RISCVInstruction>> instructions;

        // 活跃变量分析结果
        unordered_set<shared_ptr<RISCVRegister>> liveIn;
        unordered_set<shared_ptr<RISCVRegister>> liveOut;

    public:
        RISCVBasicBlock(const string &label, shared_ptr<RISCVFunction> func);

        // 指令管理
        void addInstruction(shared_ptr<RISCVInstruction> instr);
        void insertInstruction(int index, shared_ptr<RISCVInstruction> instr);
        void removeInstruction(int index);

        // 访问器
        const string &getLabel() const { return label; }
        const vector<shared_ptr<RISCVInstruction>> &getInstructions() const { return instructions; }
        shared_ptr<RISCVFunction> getParentFunc() const { return parentFunc; }

        // 活跃变量分析
        const unordered_set<shared_ptr<RISCVRegister>> &getLiveIn() const { return liveIn; }
        const unordered_set<shared_ptr<RISCVRegister>> &getLiveOut() const { return liveOut; }
        void setLiveIn(const unordered_set<shared_ptr<RISCVRegister>> &live) { liveIn = live; }
        void setLiveOut(const unordered_set<shared_ptr<RISCVRegister>> &live) { liveOut = live; }

        string toString() const;
    };

    // 全局变量块
    class RISCVGlobalBlock
    {
    private:
        string label;
        vector<string> data;
        int size;
        bool isReadOnly;

    public:
        RISCVGlobalBlock(const string &label, bool readOnly = false);

        void addData(const string &dataStr);
        void addData(const vector<string> &dataList);

        const string &getLabel() const { return label; }
        const vector<string> &getData() const { return data; }
        int getSize() const { return size; }
        bool getIsReadOnly() const { return isReadOnly; }

        string toString() const;
    };

    // RISC-V函数
    class RISCVFunction
    {
    private:
        string name;
        shared_ptr<RISCVModule> parentModule;
        vector<shared_ptr<RISCVBasicBlock>> basicBlocks;
        StackFrame stackFrame;

        // 函数调用约定相关
        vector<shared_ptr<RISCVRegister>> argRegs;
        shared_ptr<RISCVRegister> returnReg;

    public:
        RISCVFunction(const string &name, shared_ptr<RISCVModule> module);

        // 基本块管理
        void addBasicBlock(shared_ptr<RISCVBasicBlock> bb);
        shared_ptr<RISCVBasicBlock> getBasicBlock(const string &label) const;

        // 访问器
        const string &getName() const { return name; }
        const vector<shared_ptr<RISCVBasicBlock>> &getBasicBlocks() const { return basicBlocks; }
        StackFrame &getStackFrame() { return stackFrame; }

        // 寄存器管理
        const vector<shared_ptr<RISCVRegister>> &getArgRegs() const { return argRegs; }
        void setArgRegs(const vector<shared_ptr<RISCVRegister>> &regs) { argRegs = regs; }
        shared_ptr<RISCVRegister> getReturnReg() const { return returnReg; }
        void setReturnReg(shared_ptr<RISCVRegister> reg) { returnReg = reg; }

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
        shared_ptr<RISCVGlobalBlock> createGlobalBlock(const string &label, bool readOnly = false);
        void addGlobalBlock(shared_ptr<RISCVGlobalBlock> block);

        // 访问器
        const string &getName() const { return name; }
        const vector<shared_ptr<RISCVFunction>> &getFunctions() const { return functions; }
        const vector<shared_ptr<RISCVGlobalBlock>> &getGlobalBlocks() const { return globalBlocks; }

        string toString() const;
    };
}