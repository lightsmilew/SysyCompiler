#pragma once
#include "../frontend/ASTNode.h"
using namespace ast;

// 类似LLVM的ir数据结构
class Type
{
public:
    enum TypeID
    {
        VoidTyID,
        IntegerTyID,
        FloatTyID,
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

    virtual string toString() const = 0;
};

class IntegerType : public Type
{
public:
    IntegerType() : Type(IntegerTyID) {}
    string toString() const override
    {
        return "i_32";
    }
};

class FloatType : public Type
{
public:
    FloatType() : Type(FloatTyID) {}
    string toString() const override { return "f_32"; }
};

class VoidType : public Type
{
public:
    VoidType() : Type(VoidTyID) {}
    string toString() const override { return "void"; }
};

class PointerType : public Type
{
public:
    Type *ElementType;

    PointerType(Type *elemTy) : Type(PointerTyID), ElementType(elemTy) {}
    Type *getElementType() const { return ElementType; }
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

    FunctionType(Type *retTy, const vector<Type *> &params)
        : Type(FunctionTyID), ReturnType(retTy), ParamTypes(params) {}
    Type *getReturnType() const { return ReturnType; }
    const vector<Type *> &getParamTypes() const { return ParamTypes; }
    string toString() const override;
};

class Value
{
private:
    Type *Ty;
    string Name;

public:
    Value(Type *ty, const string &name = "") : Ty(ty), Name(name) {}
    virtual ~Value() = default;

    Type *getType() const { return Ty; }
    const string &getName() const { return Name; }
    void setName(const string &name) { Name = name; }

    virtual string toString() const = 0;
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

class Instruction : public Value
{
public:
    Opcode Op;
    BasicBlock *Parent;

    Instruction(Type *ty, Opcode op, const string &name = "")
        : Value(ty, name), Op(op), Parent(nullptr) {}
    virtual string toString() const = 0;
};

class BinaryOperator : public Instruction
{
public:
    Value *LHS;
    Value *RHS;

    BinaryOperator(Opcode op, Value *lhs, Value *rhs, const string &name = "")
        : Instruction(lhs->getType(), op, name), LHS(lhs), RHS(rhs) {}
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
        : Instruction(new IntegerType(), Opcode::ICmp, name), Pred(pred), LHS(lhs), RHS(rhs) {}
    string toString() const override;
};

class AllocaInst : public Instruction
{
public:
    Type *AllocatedType;

    AllocaInst(Type *ty, const string &name = "")
        : Instruction(new PointerType(ty), Opcode::Alloca, name), AllocatedType(ty) {}
    string toString() const override;
};

class LoadInst : public Instruction
{
public:
    Value *Pointer;

    LoadInst(Value *ptr, const string &name = "")
        : Instruction(static_cast<PointerType *>(ptr->getType())->ElementType, Opcode::Load, name), Pointer(ptr) {}
    string toString() const override;
};

class StoreInst : public Instruction
{
public:
    Value *ValueToStore;
    Value *Pointer;

    StoreInst(Value *val, Value *ptr)
        : Instruction(new VoidType(), Opcode::Store), ValueToStore(val), Pointer(ptr) {}

    string toString() const override;
};

class CallInst : public Instruction
{
public:
    Function *CalledFunction;
    vector<Value *> Arguments;

    CallInst(Function *func, const vector<Value *> &args, const string &name = "")
        : Instruction(static_cast<FunctionType *>(func->getType())->ReturnType, Opcode::Call, name), CalledFunction(func), Arguments(args) {}
    string toString() const override;
};

class ReturnInst : public Instruction
{
public:
    Value *ReturnValue;

    // Return without value (void return)
    ReturnInst() : Instruction(new VoidType(), Opcode::Ret), ReturnValue(nullptr) {}
    // Return with value
    ReturnInst(Value *retVal)
        : Instruction(retVal->getType(), Opcode::Ret), ReturnValue(retVal) {}
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
        : Instruction(new VoidType(), Opcode::Br), TrueBlock(target), FalseBlock(nullptr), Condition(nullptr) {}

    // Conditional branch
    BranchInst(Value *cond, BasicBlock *trueBlock, BasicBlock *falseBlock)
        : Instruction(new VoidType(), Opcode::Br), TrueBlock(trueBlock), FalseBlock(falseBlock), Condition(cond) {}

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
        : Value(new VoidType(), name), Parent(parent) {}
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
