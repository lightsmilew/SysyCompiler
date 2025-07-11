#pragma once
#include "../../frontend/ASTNode.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <cstring>

using namespace ast;

// 前向声明
class BasicBlock;
class Function;
class Module;

// 类似LLVM的ir数据结构
class Type
{
public:
    enum TypeID
    {
        VoidTyID,
        IntegerTyID,
        FloatTyID,
        StringTyID,
        PointerTyID,
        ArrayTyID,
        FunctionTyID
    };

protected:
    TypeID ID;

public:
    Type(TypeID id) : ID(id) {}
    virtual ~Type() = default;

    TypeID getTypeID() const { return ID; }
    bool isVoidTy() const { return ID == VoidTyID; }
    bool isIntegerTy() const { return ID == IntegerTyID; }
    bool isFloatTy() const { return ID == FloatTyID; }
    bool isPointerTy() const { return ID == PointerTyID; }
    bool isArrayTy() const { return ID == ArrayTyID; }
    bool isFunctionTy() const { return ID == FunctionTyID; }
    bool isStringTy() const { return ID == StringTyID; }
    bool isTypeEqual(Type *a, Type *b);

    virtual string toString() const = 0;
};

class IntegerType : public Type
{
public:
    IntegerType() : Type(IntegerTyID) {}
    static IntegerType *getInstance()
    {
        static IntegerType instance;
        return &instance;
    }
    string toString() const override
    {
        return "i32";
    }
};

class FloatType : public Type
{
public:
    FloatType() : Type(FloatTyID) {}
    static FloatType *getInstance()
    {
        static FloatType instance;
        return &instance;
    }
    string toString() const override { return "float"; }
};

class StringType : public Type
{
public:
    StringType() : Type(StringTyID) {} // 自定义 StringTyID
    static StringType *getInstance()
    {
        static StringType instance;
        return &instance;
    }
    string toString() const override { return "i8*"; } // LLVM风格字符串
};

class VoidType : public Type
{
public:
    VoidType() : Type(VoidTyID) {}
    static VoidType *getInstance()
    {
        static VoidType instance;
        return &instance;
    }
    string toString() const override { return "void"; }
};

class PointerType : public Type
{
public:
    Type *ElementType;

    PointerType(Type *elemTy) : Type(PointerTyID), ElementType(elemTy) {}
    static PointerType *getInstance(Type *elemTy)
    {
        // 为每种元素类型创建不同的指针类型实例
        return new PointerType(elemTy);
    }
    string toString() const override { return ElementType->toString() + "*"; }
};

class ArrayType : public Type
{
public:
    Type *ElementType;
    unsigned NumElements;

    ArrayType(Type *elemTy, unsigned numElements)
        : Type(ArrayTyID), ElementType(elemTy), NumElements(numElements) {}
    unsigned getNumElements() const { return NumElements; }
    Type *getElementType() const { return ElementType; }
    static ArrayType *getInstance(Type *elemTy, unsigned numElements)
    {
        // 为每种元素类型和元素数量创建不同的数组类型实例
        return new ArrayType(elemTy, numElements);
    }
    string toString() const override
    {
        return "[" + to_string(NumElements) + " x " + ElementType->toString() + "]";
    }
};

class FunctionType : public Type
{
public:
    Type *ReturnType;
    vector<Type *> ParamTypes;

    FunctionType(Type *retTy, const vector<Type *> &paramTys)
        : Type(FunctionTyID), ReturnType(retTy), ParamTypes(paramTys) {}
    string toString() const override;
};

class User; // 前向声明

class Value
{
private:
    Type *Ty;
    string Name;
    vector<User *> Users; // 使用这个Value的所有User

public:
    Value(Type *ty, const string &name = "") : Ty(ty), Name(name) {}
    virtual ~Value() = default;

    Type *getType() const { return Ty; }
    void setType(Type *ty) { Ty = ty; }
    const string &getName() const { return Name; }
    void setName(const string &name) { Name = name; }

    // User管理方法
    void addUser(User *user) { Users.push_back(user); }
    void removeUser(User *user)
    {
        Users.erase(std::remove(Users.begin(), Users.end(), user), Users.end());
    }
    const vector<User *> &getUsers() const { return Users; }

    virtual string toString() const = 0;

    // 输出引用形式的名称（如%var_name）
    virtual string toRef() const
    {
        if (getName().empty())
        {
            return toString(); // 如果没有名称，返回值本身
        }
        return "%" + getName();
    }

    // 替换所有使用这个Value的地方为newValue
    void replaceAllUsesWith(Value *newValue);
};

class User : public Value
{
public:
    vector<Value *> Operands;

    User(Type *ty, const vector<Value *> &operands, const string &name = "")
        : Value(ty, name), Operands(operands)
    {
        // 将自己添加到每个操作数的Users列表中
        for (Value *operand : operands)
        {
            if (operand)
            {
                operand->addUser(this);
            }
        }
    }

    virtual ~User()
    {
        // 析构时从所有操作数的Users列表中移除自己
        for (Value *operand : Operands)
        {
            if (operand)
            {
                operand->removeUser(this);
            }
        }
    }

    // 添加操作数
    void addOperand(Value *operand)
    {
        if (operand)
        {
            Operands.push_back(operand);
            operand->addUser(this);
        }
    }
    void setOperands(const vector<Value *> &operands)
    {
        // 替换当前操作数并更新Users列表
        for (Value *op : Operands)
        {
            if (op)
            {
                Operands.erase(std::remove(Operands.begin(), Operands.end(), op), Operands.end());
                op->removeUser(this);
            }
        }
        Operands = operands;
        for (Value *op : Operands)
        {
            addOperand(op); // 更新Users列表
        }
    }
    // 替换操作数
    void replaceOperand(Value *oldValue, Value *newValue)
    {
        for (size_t i = 0; i < Operands.size(); i++)
        {
            if (Operands[i] == oldValue)
            {
                if (oldValue)
                {
                    oldValue->removeUser(this);
                }
                Operands[i] = newValue;
                if (newValue)
                {
                    newValue->addUser(this);
                }
            }
        }
    }

    // 获取操作数数量
    unsigned getNumOperands() const { return Operands.size(); }
    // 获取所有操作数
    const vector<Value *> &getOperands() const { return Operands; }
    // 获取指定索引的操作数
    Value *getOperandByIndex(unsigned index) const
    {
        if (index < Operands.size())
        {
            return Operands[index];
        }
        throw std::out_of_range("Invalid operand index");
    }
    // 设置指定索引的操作数
    void setOperand(unsigned index, Value *value)
    {
        if (index < Operands.size())
        {
            if (Operands[index])
            {
                Operands[index]->removeUser(this);
            }
            Operands[index] = value;
            if (value)
            {
                value->addUser(this);
            }
        }
    }
    string toString() const override;
};

class Constant : public Value
{
public:
    Constant(Type *ty, const string &name = "") : Value(ty, name) {}

    // 常量输出值本身，不是引用
    string toRef() const override
    {
        return toString();
    }
};

class ConstantInt : public Constant
{
public:
    int Value;

    ConstantInt(IntegerType *ty, int val) : Constant(ty), Value(val) {}
    string toString() const override { return to_string(Value); }
};

class ConstantFloat : public Constant
{
public:
    float Value;

    ConstantFloat(FloatType *ty, float val) : Constant(ty), Value(val) {}
    string toString() const override { return to_string(Value); }
};

class ConstantString : public Constant
{
public:
    std::string Value;
    ConstantString(StringType *ty, const std::string &val)
        : Constant(ty), Value(val) {}
    string toString() const override
    {
        // 输出为 LLVM IR 字符串常量格式
        std::string s = "c\"";
        for (char c : Value)
        {
            if (c == '\\' || c == '\"')
                s += '\\'; // 转义
            s += c;
        }
        s += "\"";
        return s;
    }
};

class ConstantArray : public Constant
{
public:
    std::vector<Constant *> Elements;

    ConstantArray(ArrayType *ty, const std::vector<Constant *> &elements)
        : Constant(ty), Elements(elements) {}

    string toString() const override
    {
        std::string s = "[";
        for (size_t i = 0; i < Elements.size(); ++i)
        {
            if (i > 0)
                s += ", ";
            s += Elements[i] ? Elements[i]->getType()->toString() + " " + Elements[i]->toString() : "undef";
        }
        s += "]";
        return s;
    }
};
class GlobalVariable : public Value
{
public:
    Type *basicType;
    Constant *Initializer;
    bool IsConstant;
    // 如果是数组则存入退化后的指针
    GlobalVariable(Type *ty, const string &name = "", Constant *init = nullptr, bool isConst = false)
        : Value(ty, name), Initializer(init), IsConstant(isConst), basicType(ty)
    {
        // 如果是数组类型，退化成指针类型
        if (ty->isArrayTy())
        {
            auto arrayType = static_cast<ArrayType *>(ty);
            setType(PointerType::getInstance(arrayType->getElementType()));
        }
    }
    string toString() const override;
};

enum class Opcode
{
    // 终结指令
    Ret,
    Br,

    // 二元运算符
    Add,
    Sub,
    Mul,
    SDiv,
    SRem,
    FAdd,
    FSub,
    FMul,
    FDiv,

    // 比较运算符
    ICmp,
    FCmp,

    // 内存操作符(数组)
    Alloca,
    Load,
    Store,
    GetElementPtr,

    // 类型转换符
    SIToFP, // signed int (i32) to float
    FPToSI, // float to signed int (i32)

    // 其他操作
    Call,
    Phi,
    Copy
};

class Instruction : public User
{
public:
    Opcode Op;
    // BasicBlock *Parent;

    // 无操作数的构造函数
    Instruction(Type *ty, Opcode op, const string &name = "")
        : User(ty, {}, name), Op(op) {}
    // 带操作数的构造函数
    Instruction(Type *ty, Opcode op, const vector<Value *> &operands, const string &name = "")
        : User(ty, operands, name), Op(op) {}
    Instruction *clone() const;
    // 获取操作符
    Opcode getOpcode() const { return Op; }
    // 复制指令并重命名操作数
    Instruction *cloneWithRename(const std::unordered_map<Value *, Value *> &valueMap) const;
    // 获取操作符名称
    string getOpcodeName() const
    {
        switch (Op)
        {
        case Opcode::Ret:
            return "ret";
        case Opcode::Br:
            return "br";
        case Opcode::Add:
            return "add";
        case Opcode::Sub:
            return "sub";
        case Opcode::Mul:
            return "mul";
        case Opcode::SDiv:
            return "sdiv";
        case Opcode::SRem:
            return "srem";
        case Opcode::FAdd:
            return "fadd";
        case Opcode::FSub:
            return "fsub";
        case Opcode::FMul:
            return "fmul";
        case Opcode::FDiv:
            return "fdiv";
        case Opcode::ICmp:
            return "icmp";
        case Opcode::FCmp:
            return "fcmp";
        case Opcode::Alloca:
            return "alloca";
        case Opcode::Load:
            return "load";
        case Opcode::Store:
            return "store";
        case Opcode::GetElementPtr:
            return "getelementptr";
        case Opcode::SIToFP:
            return "sitofp";
        case Opcode::FPToSI:
            return "fptosi";
        case Opcode::Call:
            return "call";
        case Opcode::Phi:
            return "phi";
        case Opcode::Copy:
            return "copy";
        default:
            throw std::runtime_error("Unknown opcode");
        }
    }
    // 是否为二元操作
    bool isBinaryOp() const
    {
        return Op == Opcode::Add || Op == Opcode::Sub || Op == Opcode::Mul ||
               Op == Opcode::SDiv || Op == Opcode::SRem || Op == Opcode::FAdd ||
               Op == Opcode::FSub || Op == Opcode::FMul || Op == Opcode::FDiv;
    }
    // 是否为比较操作
    bool isComparisonOp() const
    {
        return Op == Opcode::ICmp || Op == Opcode::FCmp;
    }
    // 是否为终结指令
    bool isTerminator() const
    {
        return Op == Opcode::Ret || Op == Opcode::Br;
    }
    // 是否为复制指令
    bool isCopy() const
    {
        return Op == Opcode::Copy;
    }
    // 是否有负面作用
    bool mayHaveSideEffects() const
    {
        return Op == Opcode::Store || Op == Opcode::Call || Op == Opcode::Br ||
               Op == Opcode::Ret || Op == Opcode::Alloca;
    }
    virtual string toString() const = 0;
};

class BinaryOperator : public Instruction
{
public:
    BinaryOperator(Opcode op, Value *lhs, Value *rhs, const string &name = "")
        : Instruction(lhs->getType(), op, vector<Value *>{lhs, rhs}, name) {}
    Value *getLHS() const { return getOperandByIndex(0); }
    Value *getRHS() const { return getOperandByIndex(1); }
    string toString() const override;
};

class UnaryOperator : public Instruction
{
public:
    UnaryOperator(Opcode op, Value *operand, const string &name = "")
        : Instruction(operand->getType(), op, vector<Value *>{operand}, name) {}
    Value *getOperand() const { return getOperandByIndex(0); }
    string toString() const override;
};

class ICmpInst : public Instruction
{
public:
    enum Predicate
    {
        ICMP_EQ,
        ICMP_NE,
        ICMP_SLT,
        ICMP_SLE,
        ICMP_SGT,
        ICMP_SGE
    };

public:
    Predicate Pred;

    ICmpInst(Predicate pred, Value *lhs, Value *rhs, const string &name = "")
        : Instruction(IntegerType::getInstance(), Opcode::ICmp, vector<Value *>{lhs, rhs}, name),
          Pred(pred) {}
    // 获取比较操作符
    Predicate getPredicate() const { return Pred; }
    Value *getLHS() const { return getOperandByIndex(0); }
    Value *getRHS() const { return getOperandByIndex(1); }
    string toString() const override;
};

class FCmpInst : public Instruction
{
public:
    enum Predicate
    {
        FCMP_OEQ, // ordered equal
        FCMP_ONE, // ordered not equal
        FCMP_OLT, // ordered less than
        FCMP_OLE, // ordered less or equal
        FCMP_OGT, // ordered greater than
        FCMP_OGE  // ordered greater or equal
    };

public:
    Predicate Pred;

    FCmpInst(Predicate pred, Value *lhs, Value *rhs, const string &name = "")
        : Instruction(FloatType::getInstance(), Opcode::FCmp, vector<Value *>{lhs, rhs}, name),
          Pred(pred) {}
    Predicate getPredicate() const { return Pred; }
    Value *getLHS() const { return getOperandByIndex(0); }
    Value *getRHS() const { return getOperandByIndex(1); }
    string toString() const override;
};

class AllocaInst : public Instruction
{
public:
    // 传入数组类型，返回退化后的指针
    Type *AllocatedType;

    AllocaInst(Type *ty, const string &name = "")
        : Instruction(PointerType::getInstance(dynamic_cast<ArrayType *>(ty)->ElementType), Opcode::Alloca, name), AllocatedType(ty) {}
    string toString() const override;
};

class LoadInst : public Instruction
{
public:
    LoadInst(Value *ptr, const string &name = "")
        : Instruction(getElementType(ptr), Opcode::Load, vector<Value *>{ptr}, name) {}
    Value *getPointer() const { return getOperandByIndex(0); }
    string toString() const override;

private:
    // 获取指针指向的元素类型
    static Type *getElementType(Value *ptr)
    {
        if (!ptr)
            return IntegerType::getInstance(); // 默认类型

        // 如果是指针类型，返回指向的元素类型
        if (auto ptrTy = dynamic_cast<PointerType *>(ptr->getType()))
        {
            return ptrTy->ElementType;
        }
        // 传入不是指针报错
        throw std::runtime_error("LoadInst: operand must be a pointer type");
    }
};

class StoreInst : public Instruction
{
public:
    StoreInst(Value *val, Value *ptr)
        : Instruction(VoidType::getInstance(), Opcode::Store, vector<Value *>{val, ptr}) {}
    Value *getValueToStore() const { return getOperandByIndex(0); }
    Value *getPointer() const { return getOperandByIndex(1); }
    string toString() const override;
};

class CallInst : public Instruction
{
public:
    CallInst(Function *func, const vector<Value *> &args, const string &name = "");
    // 这里无法给出函数实现，因为此时Function类还未定义
    Function *getCalledFunction() const;
    // 获取函数参数
    vector<Value *> getArguments() const
    {
        vector<Value *> args(getOperands().begin() + 1, getOperands().end());
        return args;
    }
    // 是否有返回值
    bool hasReturnValue() const
    {
        return getFunctionReturnType(getOperandByIndex(0)) != VoidType::getInstance();
    }

    vector<Value *> getIntArguments() const
    {
        vector<Value *> args;
        for (size_t i = 1; i < getOperands().size(); ++i)
        {
            if (getOperands()[i]->getType()->isIntegerTy())
            {
                args.push_back(getOperands()[i]);
            }
        }
        return args;
    }

    vector<Value *> getFloatArguments() const
    {
        vector<Value *> args;
        for (size_t i = 1; i < getOperands().size(); ++i)
        {
            if (getOperands()[i]->getType()->isFloatTy())
            {
                args.push_back(getOperands()[i]);
            }
        }
        return args;
    }

    vector<Value *> getPtrArguments() const
    {
        vector<Value *> args;
        for (size_t i = 1; i < getOperands().size(); ++i)
        {
            if (getOperands()[i]->getType()->isPointerTy())
            {
                args.push_back(getOperands()[i]);
            }
        }
        return args;
    }
    string toString() const override;

private:
    static vector<Value *> constructOperands(Function *func, const vector<Value *> &args);
    static Type *getFunctionReturnType(Value *func);
};

class ReturnInst : public Instruction
{
public:
    // Return without value (void return)
    ReturnInst() : Instruction(VoidType::getInstance(), Opcode::Ret) {}
    // Return with value
    ReturnInst(Value *retVal)
        : Instruction(VoidType::getInstance(), Opcode::Ret, vector<Value *>{retVal}) {}
    // 获取返回值
    Value *getReturnValue() const
    {
        if (getNumOperands() > 0)
        {
            return getOperandByIndex(0);
        }
        return nullptr; // 无返回值
    }
    string toString() const override;
};

class BranchInst : public Instruction
{
public:
    BasicBlock *TrueBlock;
    BasicBlock *FalseBlock;

    // 无条件跳转
    BranchInst(BasicBlock *target)
        : Instruction(VoidType::getInstance(), Opcode::Br), TrueBlock(target), FalseBlock(nullptr) {}

    // 有条件跳转
    BranchInst(Value *cond, BasicBlock *trueBlock, BasicBlock *falseBlock)
        : Instruction(VoidType::getInstance(), Opcode::Br, vector<Value *>{cond}),
          TrueBlock(trueBlock), FalseBlock(falseBlock) {}
    // 是否为条件分支
    bool isConditional() const { return getNumOperands() > 0; }
    // 获取条件
    Value *getCondition() const
    {
        if (isConditional())
        {
            return getOperandByIndex(0);
        }
        return nullptr;
    }
    string toString() const override;
};

class PhiInst : public Instruction
{
public:
    vector<BasicBlock *> IncomingValues;

    PhiInst(Type *ty, const string &name = "")
        : Instruction(ty, Opcode::Phi, name) {}
    // 添加前驱基本块和对应的值
    void addIncoming(Value *value, BasicBlock *block)
    {
        IncomingValues.emplace_back(block);
        addOperand(value); // 添加到操作数列表中
    }
    // 获取前驱基本块和对应的值长度
    unsigned getNumIncomingValues() const
    {
        return IncomingValues.size();
    }
    // 获取前驱value
    Value *getIncomingValue(unsigned index) const
    {
        if (index < getNumOperands())
        {
            return getOperandByIndex(index);
        }
        throw std::out_of_range("Invalid incoming value index");
    }
    // 获取前驱基本块
    BasicBlock *getIncomingBlock(unsigned index) const
    {
        if (index < IncomingValues.size())
        {
            return IncomingValues[index];
        }
        throw std::out_of_range("Invalid incoming block index");
    }
    string toString() const override;
};

class GetElementPtrInst : public Instruction
{
    // 只能传入指针
public:
    GetElementPtrInst(Value *ptr, const vector<Value *> &indices, const string &name = "")
        : Instruction(calculateResultType(ptr, indices), Opcode::GetElementPtr,
                      constructOperands(ptr, indices), name) {}
    // 获取指针操作数
    Value *getPointerOperand() const { return getOperandByIndex(0); }
    // 获取索引操作数
    vector<Value *> getIndices() const
    {
        vector<Value *> indices(getOperands().begin() + 1, getOperands().end());
        return indices;
    }
    string toString() const override;

private:
    static vector<Value *> constructOperands(Value *ptr, const vector<Value *> &indices);
    static Type *calculateResultType(Value *ptr, const vector<Value *> &indices);
};

class CastInst : public Instruction
{
public:
    Type *DestType;

    CastInst(Opcode op, Value *operand, Type *destType, const string &name = "")
        : Instruction(destType, op, vector<Value *>{operand}, name), DestType(destType) {}
    // 获取操作数
    Value *getOperand() const { return getOperandByIndex(0); }
    string toString() const override;
};
class CopyInst : public Instruction
{
public:
    CopyInst(Value *source, const string &name = "")
        : Instruction(source->getType(), Opcode::Copy, vector<Value *>{source}, name) {}
    // 获取目标
    Value *getDest() const
    {
        return const_cast<CopyInst *>(this); // CopyInst的目标就是自身，类型转换为Value*
    }
    // 获取源
    Value *getSource() const
    {
        return getOperandByIndex(0);
    }
    string toString() const override;
};

// ===== Basic Block =====
class BasicBlock : public Value
{
public:
    vector<unique_ptr<Instruction>> Instructions;
    Function *Parent;
    vector<BasicBlock *> Predecessors;
    vector<BasicBlock *> Successors;

    BasicBlock(const string &name = "", Function *parent = nullptr)
        : Value(VoidType::getInstance(), name), Parent(parent) {}

    // 添加指令到基本块
    void addInstruction(unique_ptr<Instruction> inst)
    {
        Instructions.push_back(std::move(inst));
    }
    void insertBeforeTerminator(std::unique_ptr<Instruction> inst);
    // 插入指令
    void insert(unique_ptr<Instruction> inst, unsigned index)
    {
        if (index > Instructions.size())
        {
            throw std::out_of_range("Index out of range for inserting instruction");
        }
        Instructions.insert(Instructions.begin() + index, std::move(inst));
    }

    // 添加前驱基本块
    void addPredecessor(BasicBlock *pred)
    {
        if (std::find(Predecessors.begin(), Predecessors.end(), pred) == Predecessors.end())
        {
            Predecessors.push_back(pred);
        }
    }

    // 添加后继基本块
    void addSuccessor(BasicBlock *succ)
    {
        if (std::find(Successors.begin(), Successors.end(), succ) == Successors.end())
        {
            Successors.push_back(succ);
        }
    }

    // 移除前驱基本块
    void removePredecessor(BasicBlock *pred)
    {
        Predecessors.erase(std::remove(Predecessors.begin(), Predecessors.end(), pred),
                           Predecessors.end());
    }

    // 移除后继基本块
    void removeSuccessor(BasicBlock *succ)
    {
        Successors.erase(std::remove(Successors.begin(), Successors.end(), succ),
                         Successors.end());
    }

    // 获取终结指令
    Instruction *getTerminator()
    {
        return Instructions.empty() ? nullptr : Instructions.back().get();
    }
    // 获取所有指令
    vector<unique_ptr<Instruction>> &getInstructions()
    {
        return Instructions;
    }
    // 获取前驱基本块
    const vector<BasicBlock *> &getPredecessors() const
    {
        return Predecessors;
    }
    // 获取后继基本块
    const vector<BasicBlock *> &getSuccessors() const
    {
        return Successors;
    }
    // 检查是否有终结指令
    bool hasTerminator()
    {
        Instruction *term = getTerminator();
        return term && (term->Op == Opcode::Ret || term->Op == Opcode::Br);
    }
    // 判断是否包含特定指令(通过名称)
    bool contains(Instruction *inst) const
    {
        const std::string &name = inst->getName();
        return std::any_of(Instructions.begin(), Instructions.end(),
                           [&](const unique_ptr<Instruction> &i)
                           {
                               return i->getName() == name;
                           });
    }
    string toString() const override;
};

// ===== Function =====
// Argument 类表示函数参数

class Argument : public Value
{
public:
    Function *Parent;
    unsigned ArgNo;

    Argument(Type *ty, unsigned argNo, const string &name = "", Function *parent = nullptr)
        : Value(ty, name), Parent(parent), ArgNo(argNo) {}
    string toString() const override;
};

class Function : public Value
{
public:
    vector<unique_ptr<BasicBlock>> BasicBlocks;
    vector<unique_ptr<Argument>> Arguments;
    Module *Parent;

    Function(FunctionType *funcTy, const string &name = "", Module *parent = nullptr)
        : Value(funcTy, name), Parent(parent) {}

    // 添加基本块
    BasicBlock *addBasicBlock(const string &name = "")
    {
        auto bb = make_unique<BasicBlock>(name, this);
        BasicBlock *ptr = bb.get();
        BasicBlocks.push_back(std::move(bb));
        return ptr;
    }

    // 添加参数
    Argument *addArgument(Type *type, const string &name = "")
    {
        auto arg = make_unique<Argument>(type, Arguments.size(), name, this);
        Argument *ptr = arg.get();
        Arguments.push_back(std::move(arg));
        return ptr;
    }
    // 获取函数参数
    const vector<unique_ptr<Argument>> &getArguments() const
    {
        return Arguments;
    }
    // 获取入口基本块
    BasicBlock *getEntryBlock()
    {
        return BasicBlocks.empty() ? nullptr : BasicBlocks[0].get();
    }

    // 获取函数类型
    FunctionType *getFunctionType()
    {
        return static_cast<FunctionType *>(getType());
    }
    // 获取所有基本块
    const vector<unique_ptr<BasicBlock>> &getBasicBlocks() const
    {
        return BasicBlocks;
    }
    // 获取指令数量
    unsigned getInstructionCount() const
    {
        unsigned count = 0;
        for (const auto &bb : BasicBlocks)
        {
            count += bb->getInstructions().size();
        }
        return count;
    }
    bool isLibraryFunction() const
    {
        // 判断是否为标准库函数
        return getName() == "getint" || getName() == "getch" ||
               getName() == "getfloat" || getName() == "getarray" ||
               getName() == "getfarray" || getName() == "putint" ||
               getName() == "putch" || getName() == "putfloat" ||
               getName() == "putarray" || getName() == "putfarray" ||
               getName() == "putf" ||
               getName() == "_sysy_starttime" || getName() == "_sysy_stoptime";
    }
    // 是否为递归函数
    bool isRecursive() const
    {
        for (const auto &bb : BasicBlocks)
        {
            for (const auto &instPtr : bb->getInstructions())
            {
                if (auto *call = dynamic_cast<CallInst *>(instPtr.get()))
                {
                    if (call->getCalledFunction() == this)
                        return true;
                }
            }
        }
        return false;
    }
    string toString() const override;
};

// ===== Module =====
class Module
{
public:
    string Name;
    vector<unique_ptr<Function>> Functions;
    vector<unique_ptr<GlobalVariable>> GlobalVariables;

    Module(const string &name) : Name(name) {}

    // 添加函数
    Function *addFunction(FunctionType *funcType, const string &name)
    {
        auto func = make_unique<Function>(funcType, name, this);
        Function *ptr = func.get();
        Functions.push_back(std::move(func));
        return ptr;
    }

    // 添加全局变量
    GlobalVariable *addGlobalVariable(Type *type, const string &name,
                                      Constant *initializer = nullptr, bool isConstant = false)
    {
        auto global = make_unique<GlobalVariable>(type, name, initializer, isConstant);
        GlobalVariable *ptr = global.get();
        GlobalVariables.push_back(std::move(global));
        return ptr;
    }

    // 根据名称查找函数
    Function *getFunction(const string &name)
    {
        string tmp_name = (name == "starttime" || name == "stoptime") ? "_sysy_" + name : name; // 添加前缀_sysy_以匹配SysY标准库函数
        for (auto &func : Functions)
        {
            if (func->getName() == tmp_name)
            {
                return func.get();
            }
        }
        return nullptr;
    }

    // 根据名称查找全局变量
    GlobalVariable *getGlobalVariable(const string &name)
    {
        for (auto &global : GlobalVariables)
        {
            if (global->getName() == name)
            {
                return global.get();
            }
        }
        return nullptr;
    }
    // 输出基本块后继信息
    void printBasic()
    {
        for (int i = 13; i < Functions.size(); i++)
        {
            std::cout << Functions[i]->getName() << ":" << std::endl;
            int j = 0;
            for (const auto &it : Functions[i]->BasicBlocks)
            {
                std::cout << "BasicBlockSuccs " << j << ":" << std::endl;
                std::cout << "                   Successors: ";
                for (auto suc : it->getSuccessors())
                {
                    std::cout << suc->getName() << " ";
                }
                std::cout << std::endl;
                std::cout << "                   Predecessors: ";
                for (auto pre : it->getPredecessors())
                {
                    std::cout << pre->getName() << " ";
                }
                std::cout << std::endl;
                j++;
            }
        }
    }
    string toString() const;
};
