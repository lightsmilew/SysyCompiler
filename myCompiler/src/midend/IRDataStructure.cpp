#include "ASTNode.h"

using namespace ast;

enum class InstructionType
{
    BINARY,
    UNARY,
    LOAD,
    STORE,
    JUMP,
    CONDITIONAL_JUMP,
    CALL,
    RETURN,
    ALLOCA,
    ASSIGN
};

struct Instruction
{
    InstructionType type;

    Instruction(InstructionType t) : type(t) {}
    virtual ~Instruction() = default;
};

struct Operand
{
    enum class OperandType
    {
        INT_IMMEDIATE,   // 整数立即数
        FLOAT_IMMEDIATE, // 浮点立即数
        REGISTER,        // 寄存器
    };

    OperandType opType;
    union
    {
        int intValue;     // 整数立即数
        float floatValue; // 浮点立即数
        int reg_id;       // 寄存器ID
    };

    // 构造函数
    Operand() : opType(OperandType::INT_IMMEDIATE), intValue(0) {}
    Operand(int val) : opType(OperandType::INT_IMMEDIATE), intValue(val) {}
    Operand(float val) : opType(OperandType::FLOAT_IMMEDIATE), floatValue(val) {}

    static Operand Register(int id)
    {
        Operand op;
        op.opType = OperandType::REGISTER;
        op.reg_id = id;
        return op;
    }
};

// 寄存器描述符
struct Register
{
    int id;            // 寄存器ID
    DataType dataType; // 寄存器存储的数据类型
    bool isAllocated;  // 是否已分配

    Register(int regId, DataType type)
        : id(regId), dataType(type), isAllocated(false) {}
};

// 寄存器管理器
class RegisterManager
{
private:
    vector<Register> registers;
    int nextRegId;

public:
    RegisterManager() : nextRegId(0) {}

    // 分配指定类型的寄存器
    int allocateRegister(DataType type)
    {
        for (auto &reg : registers)
        {
            if (!reg.isAllocated && reg.dataType.baseType == type.baseType)
            {
                reg.isAllocated = true;
                return reg.id;
            }
        }

        // 创建新寄存器
        int newId = nextRegId++;
        registers.emplace_back(newId, type);
        registers.back().isAllocated = true;
        return newId;
    }

    // 释放寄存器
    void freeRegister(int regId)
    {
        for (auto &reg : registers)
        {
            if (reg.id == regId)
            {
                reg.isAllocated = false;
                break;
            }
        }
    }

    // 获取寄存器类型
    DataType getRegisterType(int regId)
    {
        for (const auto &reg : registers)
        {
            if (reg.id == regId)
            {
                return reg.dataType;
            }
        }
        return DataType(PrimaryDataType::VOID);
    }
};

struct Binary : public Instruction
{
    BinaryOp opcode; // 操作码
    Operand src1;    // 操作数一
    Operand src2;    // 操作数二
    Operand dst;     // 目标寄存器
};

struct Unary : public Instruction
{
    UnaryOp opcode; // 操作码
    Operand src;    // 源操作数
    Operand dst;    // 目标寄存器
};

struct Jump : public Instruction
{
    BasicBlock *target; // 跳转目标地址或标签
};

struct ConditionalJump : public Instruction
{
    BinaryOp condition;      // 条件操作符
    Operand src1;            // 条件源操作数一
    Operand src2;            // 条件源操作数二
    BasicBlock *trueTarget;  // 条件为真时跳转的目标地址或标签
    BasicBlock *falseTarget; // 条件为假时跳转的目标地址或标签
};

struct Call : public Instruction
{
    string functionName;  // 被调用的函数名
    vector<Operand> args; // 函数参数列表
    Operand ret;          // 返回值寄存器
};

struct Assign : public Instruction
{
    Operand src; // 源操作数
    Operand dst; // 目标寄存器或内存地址
};

struct Load : public Instruction
{
    Operand src;    // 源内存地址
    Operand offset; // 偏移量
    Operand dst;    // 目标寄存器
};

struct Store : public Instruction
{
    Operand src;    // 源寄存器
    Operand offset; // 偏移量
    Operand dst;    // 目标内存地址
};

struct Alloca : public Instruction
{
    DataType type; // 分配的类型
    Operand dst;   // 目标寄存器或内存地址
};

struct Return : public Instruction
{
    Operand retValue; // 返回值寄存器或立即数
};

// 数据流分析和控制流分析所需基本数据结构
struct BasicBlock
{
    vector<Instruction *> instructions;
    string label;
};

struct Function
{
    string name;
    vector<BasicBlock> blocks;
};

struct Program
{
    // 程序的全局变量
    vector<BasicBlock *> globalVariables;
    // 程序的函数列表
    vector<Function *> functions;
};