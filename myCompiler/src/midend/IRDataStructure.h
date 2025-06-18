#pragma once
#include "../frontend/ASTNode.h"
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
        BooleanTyID,
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
    bool isBooleanTy() const { return ID == BooleanTyID; }

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
        return "i_32";
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
    string toString() const override { return "f_32"; }
};

class BooleanType : public Type
{
public:
    BooleanType() : Type(BooleanTyID) {}
    static BooleanType *getInstance()
    {
        static BooleanType instance;
        return &instance;
    }
    string toString() const override { return "i_1"; } // 使用 i1 表示布尔类型
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
        static PointerType instance(elemTy);
        return &instance;
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

    string toString() const override;
};

class Constant : public Value
{
public:
    Constant(Type *ty, const string &name = "") : Value(ty, name) {}
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

class GlobalVariable : public Value
{
public:
    Constant *Initializer;
    bool IsConstant;

    GlobalVariable(Type *ty, const string &name = "", Constant *init = nullptr, bool isConst = false)
        : Value(ty, name), Initializer(init), IsConstant(isConst) {}
    string toString() const override;
};

enum class Opcode
{
    // Terminator instructions
    Ret,
    Br,

    // Binary operations
    Add,
    Sub,
    Mul,
    SDiv,
    SRem,
    FAdd,
    FSub,
    FMul,
    FDiv,

    // Comparison operations
    ICmp,
    FCmp,

    // Memory operations
    Alloca,
    Load,
    Store,

    // Conversion operations
    I2F,
    F2I,

    // Other operations
    Call,
    Phi
};

class Instruction : public User
{
public:
    Opcode Op;
    BasicBlock *Parent;

    // usually added to a BasicBlock after creation
    Instruction(Type *ty, Opcode op, const string &name = "")
        : User(ty, {}, name), Op(op), Parent(nullptr) {}

    // 带操作数的构造函数
    Instruction(Type *ty, Opcode op, const vector<Value *> &operands, const string &name = "")
        : User(ty, operands, name), Op(op), Parent(nullptr) {}

    virtual string toString() const = 0;
};

class BinaryOperator : public Instruction
{
public:
    Value *LHS;
    Value *RHS;

    // binary operator constructor
    BinaryOperator(Opcode op, Value *lhs, Value *rhs, const string &name = "")
        : Instruction(lhs->getType(), op, vector<Value *>{lhs, rhs}, name), LHS(lhs), RHS(rhs) {}
    // unary operator constructor
    BinaryOperator(Opcode op, Value *operand, const string &name = "")
        : Instruction(operand->getType(), op, vector<Value *>{operand}, name), LHS(operand), RHS(nullptr) {}
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
    Value *LHS;
    Value *RHS;

    ICmpInst(Predicate pred, Value *lhs, Value *rhs, const string &name = "")
        : Instruction(BooleanType::getInstance(), Opcode::ICmp, vector<Value *>{lhs, rhs}, name),
          Pred(pred), LHS(lhs), RHS(rhs) {}
    string toString() const override;
};

class AllocaInst : public Instruction
{
public:
    Type *AllocatedType;

    AllocaInst(Type *ty, const string &name = "")
        : Instruction(PointerType::getInstance(ty), Opcode::Alloca, name), AllocatedType(ty) {}
    string toString() const override;
};

class LoadInst : public Instruction
{
public:
    Value *Pointer;

    LoadInst(Value *ptr, const string &name = "")
        : Instruction(getElementType(ptr), Opcode::Load, vector<Value *>{ptr}, name), Pointer(ptr) {}
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

        // 如果传入的不是指针，这是一个错误，但为了健壮性返回默认类型
        return IntegerType::getInstance();
    }
};

class StoreInst : public Instruction
{
public:
    Value *ValueToStore;
    Value *Pointer;

    StoreInst(Value *val, Value *ptr)
        : Instruction(VoidType::getInstance(), Opcode::Store, vector<Value *>{val, ptr}),
          ValueToStore(val), Pointer(ptr) {}
    string toString() const override;
};

class CallInst : public Instruction
{
public:
    Function *CalledFunction;
    vector<Value *> Arguments;

    CallInst(Function *func, const vector<Value *> &args, const string &name = "")
        : Instruction(getFunctionReturnType(func), Opcode::Call, constructOperands(func, args), name),
          CalledFunction(func), Arguments(args) {}
    string toString() const override;

private:
    static vector<Value *> constructOperands(Function *func, const vector<Value *> &args)
    {
        vector<Value *> operands;
        operands.push_back(func);
        operands.insert(operands.end(), args.begin(), args.end());
        return operands;
    }

    static Type *getFunctionReturnType(Value *func)
    {
        if (!func)
        {
            throw std::invalid_argument("CallInst: function cannot be null");
        }

        FunctionType *funcTy = nullptr;

        // 如果是函数类型
        if (auto ft = dynamic_cast<FunctionType *>(func->getType()))
        {
            funcTy = ft;
        }
        // 函数指针类型
        else if (auto ptrTy = dynamic_cast<PointerType *>(func->getType()))
        {
            funcTy = dynamic_cast<FunctionType *>(ptrTy->ElementType);
        }

        if (!funcTy)
        {
            throw std::invalid_argument("CallInst: operand is not a function");
        }

        return funcTy->ReturnType;
    }
};

class ReturnInst : public Instruction
{
public:
    Value *ReturnValue;

    // Return without value (void return)
    ReturnInst() : Instruction(VoidType::getInstance(), Opcode::Ret), ReturnValue(nullptr) {}
    // Return with value
    ReturnInst(Value *retVal)
        : Instruction(retVal->getType(), Opcode::Ret, vector<Value *>{retVal}), ReturnValue(retVal) {}
    string toString() const override;
};

class BranchInst : public Instruction
{
public:
    BasicBlock *TrueBlock;
    BasicBlock *FalseBlock;
    Value *Condition;

    // Unconditional branch
    BranchInst(BasicBlock *target)
        : Instruction(VoidType::getInstance(), Opcode::Br), TrueBlock(target), FalseBlock(nullptr), Condition(nullptr) {}

    // Conditional branch
    BranchInst(Value *cond, BasicBlock *trueBlock, BasicBlock *falseBlock)
        : Instruction(VoidType::getInstance(), Opcode::Br, vector<Value *>{cond}),
          TrueBlock(trueBlock), FalseBlock(falseBlock), Condition(cond) {}

    string toString() const override;
};

class PHINode : public Instruction
{
public:
    vector<pair<Value *, BasicBlock *>> IncomingValues;

    PHINode(Type *ty, const string &name = "")
        : Instruction(ty, Opcode::Phi, name) {}
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
    string toString() const override;
};

// ===== Function =====
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

    string toString() const override;
};

// ===== Module =====
class Module
{
public:
    string Name;
    vector<unique_ptr<Function>> Functions;
    vector<unique_ptr<GlobalVariable>> GlobalVariables;

    Module(const string &name, const vector<unique_ptr<Function>> &funcs = {},
           const vector<unique_ptr<GlobalVariable>> &globals = {})
        : Name(name), Functions(funcs), GlobalVariables(globals) {}

    string toString() const;
};
