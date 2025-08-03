#pragma once
#include "../../frontend/ASTNode.h"
#include <algorithm>
#include <sstream>
#include <iomanip>
#include <cstdint>
#include <cstring>
#include <set>
#include <limits>
using namespace ast;

// ===== Forward Declarations =====
class BasicBlock;
class Function;
class Module;
class User;
class ArrayType;
class Value;

// ===== Address Comparison =====
std::string normalizeName(const std::string &name); // 归一化处理变量名
bool isSameAddr(Value *a,Value *b);
// ===== Type System Implementation =====
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

// ===== IntegerType Implementation =====
class IntegerType : public Type
{
public:
    IntegerType() : Type(IntegerTyID) {}

    static IntegerType *getInstance()
    {
        static IntegerType instance;
        return &instance;
    }
    string toString() const override { return "i32"; }
};

// ===== FloatType Implementation =====
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

// ===== StringType Implementation =====
class StringType : public Type
{
public:
    StringType() : Type(StringTyID) {}

    static StringType *getInstance()
    {
        static StringType instance;
        return &instance;
    }
    string toString() const override { return "i8*"; } // LLVM风格字符串
};

// ===== VoidType Implementation =====
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

// ====== PointerType Implementation =====
class PointerType : public Type
{
public:
    Type *ElementType;

    PointerType(Type *elemTy) : Type(PointerTyID), ElementType(elemTy) {}

    static PointerType *getInstance(Type *elemTy)
    {
        return new PointerType(elemTy);
    }
    string toString() const override { return ElementType->toString() + "*"; }

    Type *getGroundElementType() const;
    vector<size_t> getArrayIndices() const;
};
// ===== ArrayType Implementation =====
class ArrayType : public Type
{
public:
    Type *ElementType;
    unsigned NumElements;

    ArrayType(Type *elemTy, unsigned numElements)
        : Type(ArrayTyID), ElementType(elemTy), NumElements(numElements) {}

    static ArrayType *getInstance(Type *elemTy, unsigned numElements)
    {
        return new ArrayType(elemTy, numElements);
    }
    // 维度管理
    unsigned getNumElements() const { return NumElements; } // 获取数组长度
    size_t getArrayLength() const;                          // 获取数组的总长度
    vector<size_t> getArrayIndices() const;                 // 获取数组每一维的长度
    Type *getElementType() const { return ElementType; }    // 获取元素类型

    Type *getGroundElementType() const
    {
        // 获取数组的基本元素类型
        if (auto arrayType = dynamic_cast<ArrayType *>(ElementType))
        {
            return arrayType->getGroundElementType();
        }
        return ElementType; // 如果不是数组类型，返回自身
    }
    string toString() const override;
};

// ====== FunctionType Implementation =====
class FunctionType : public Type
{
public:
    Type *ReturnType;
    vector<Type *> ParamTypes;

    FunctionType(Type *retTy, const vector<Type *> &paramTys)
        : Type(FunctionTyID), ReturnType(retTy), ParamTypes(paramTys) {}

    string toString() const override;
};

// ===== PointerType方法实现（需要在ArrayType定义之后） =====
inline Type *PointerType::getGroundElementType() const
{
    if (auto arrayType = dynamic_cast<ArrayType *>(ElementType))
    {
        // 如果是数组类型，递归获取基本元素类型
        return arrayType->getGroundElementType();
    }

    return ElementType;
}

inline vector<size_t> PointerType::getArrayIndices() const
{
    // 获取数组的每一维长度
    if (auto arrayType = dynamic_cast<ArrayType *>(ElementType))
    {
        return arrayType->getArrayIndices();
    }
    return vector<size_t>{0}; // 标量返回0维度
}

// ===== Value System Implementation =====
class Value
{
private:
    Type *Ty;
    string Name;
    vector<User *> Users; // 使用这个Value的所有User

public:
    Value(Type *ty, const string &name = "") : Ty(ty), Name(name) {}
    virtual ~Value() = default;

    Type *getType() const { return Ty; }                     // 获取类型
    void setType(Type *ty) { Ty = ty; }                      // 设置类型
    const string &getName() const { return Name; }           // 获取名称
    void setName(const string &name) { Name = name; }        // 设置名称
    const vector<User *> &getUsers() const { return Users; } // 获取所有使用这个Value的User
    bool isGlobal() const;                                   // 判断是否是全局变量

    // User管理方法
    void addUser(User *user);                 // 添加使用这个Value的User
    void removeUser(User *user);              // 删除某个User
    void replaceAllUsesWith(Value *newValue); // 替换所有使用这个Value的地方为newValue
    virtual string toRef() const              // 输出引用形式的名称（如%var_name）
    {
        if (getName().empty())
        {
            return toString(); // 如果没有名称，返回值本身
        }
        return "%" + getName();
    }
    virtual string toString() const = 0;
};
struct ValueInfo
{
    Value *value;                                                // 变量的SSA值
    size_t SerialNumber;                                         // 变量的信号量(用于判断是否是新声明的变量)
    ValueInfo() : value(nullptr), SerialNumber(0) {}             // 默认构造函数
    ValueInfo(Value *v, size_t s) : value(v), SerialNumber(s) {} // 新定义的变量序号
    void setValue(Value *v) { value = v; }                       // 设置SSA值
    Value *getValue() const { return value; }                    // 获取SSA值
    size_t getSerialNumber() const { return SerialNumber; }      // 获取信号量
    void plusSerialNumber() { SerialNumber++; }                  // 信号量自增
};
// ===== User System Implementation =====
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
        // removeThisFromOperands();
    }
    void removeThisFromOperands();                         // 从所有操作数的Users列表中移除自己
    void addOperand(Value *operand);                       // 添加操作数
    void setOperands(const vector<Value *> &operands);     // 设置操作数
    void replaceOperand(Value *oldValue, Value *newValue); // 替换操作数
    unsigned getNumOperands() const;                       // 获取操作数数量
    const vector<Value *> &getOperands() const;            // 获取所有操作数
    Value *getOperandByIndex(unsigned index) const;        // 获取指定索引的操作数
    void setOperandByIndex(unsigned index, Value *value);  // 设置指定索引的操作数
    void removeOperandByIndex(unsigned index);
    string toString() const override;
};

// ===== Constant Implementation =====
class Constant : public Value
{
public:
    Constant(Type *ty, const string &name = "") : Value(ty, name) {}

    string toRef() const override { return toString(); } // 常量输出值本身，不是引用
};

// ===== ConstantInt Implementation =====
class ConstantInt : public Constant
{
public:
    int Value;

    ConstantInt(IntegerType *ty, int val) : Constant(ty), Value(val) {}

    string toString() const override { return to_string(Value); }
};

// ===== ConstantFloat Implementation =====
class ConstantFloat : public Constant
{
public:
    float Value;

    ConstantFloat(FloatType *ty, float val) : Constant(ty), Value(val) {}

    string toString() const override
    {
        // 16进制表示
        std::ostringstream oss;
        oss << std::hexfloat << std::setprecision(std::numeric_limits<float>::max_digits10) << Value;
        return oss.str();
    }
};

// ===== ConstantString Implementation =====
class ConstantString : public Constant
{
public:
    string Value;

    ConstantString(StringType *ty, const string &val)
        : Constant(ty), Value(val) {}

    string toString() const override;
};

// ===== ConstantArray Implementation =====
class ConstantArray : public Constant
{
public:
    std::vector<Constant *> Elements;

    ConstantArray(ArrayType *ty, const std::vector<Constant *> &elements)
        : Constant(ty), Elements(elements) {}

    string toString() const override;
};

// ===== GlobalVariable Implementation =====
class GlobalVariable : public Value
{
public:
    Type *OriginalType;
    Constant *Initializer;
    bool IsConstant;
    // 如果是数组则存入退化后的指针
    GlobalVariable(Type *ty, const string &name = "", Constant *init = nullptr, bool isConst = false)
        : Value(ty, name), Initializer(init), IsConstant(isConst), OriginalType(ty)
    {
        // 如果是数组类型，退化成指针类型
        if (ty->isArrayTy())
        {
            auto arrayType = static_cast<ArrayType *>(ty);
            setType(PointerType::getInstance(arrayType->getElementType()));
        }
        // 其他类型也用指针，统一用内存模型
        else if (ty->isIntegerTy() || ty->isFloatTy())
        {
            setType(PointerType::getInstance(ty));
        }
    };
    bool isArray() const;               // 判断是否为数组类型
    vector<size_t> getDims() const;     // 获取全局变量维度（标量返回0）
    size_t getTotallength() const;      // 获取数组总长度(标量返回0)
    Type *getGroundElementType() const; // 获取全局变量的基本元素类型
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
    //左移右移(算数左移右移)
    Sll,
    Sra,
    FAdd,
    FSub,
    FMul,
    FDiv,
    And,
    Or,
    // 比较运算符
    ICmp,
    FCmp,

    // 内存操作符
    Alloca,
    Load,
    Store,
    GetElementPtr,

    // 类型转换符
    SIToFP, // signed int (i32) to float
    FPToSI, // float to signed int (i32)
    BitCast,// 指针类型转换
    // 其他操作
    Call,
    Phi,
    Copy
};

// ====== Instruction System Implementation =====
class Instruction : public User
{
public:
    Opcode Op;

    Instruction(Type *ty, Opcode op, const string &name = "") // 无操作数的构造函数
        : User(ty, {}, name), Op(op)
    {
    }

    Instruction(Type *ty, Opcode op, const vector<Value *> &operands, // 带操作数的构造函数
                const string &name = "")
        : User(ty, operands, name), Op(op)
    {
    }

    Instruction *clone() const;             // 克隆指令
    Opcode getOpcode() const { return Op; } // 获取操作符
    Instruction *cloneWithRename(const std::unordered_map<Value *,
                                                          Value *> &valueMap,
                                 string suffix) const; // 复制指令并重命名操作数
    string getOpcodeName() const;                      // 获取操作符名称
    bool isBinaryOp() const;                           // 是否为二元操作
    bool isComparisonOp() const;                       // 是否为比较操作
    bool isTerminator() const;                         // 是否为终结指令
    bool isCopy() const;                               // 是否为复制指令
    bool mayHaveSideEffects() const;                   // 是否有负面作用
    bool hasResult() const;                            // 是否有结果
    virtual string toString() const = 0;
};

// ===== BinaryOperator Implementation =====
class BinaryOperator : public Instruction
{
public:
    BinaryOperator(Opcode op, Value *lhs, Value *rhs,
                   const string &name = "")
        : Instruction(lhs->getType(), op,
                      vector<Value *>{lhs, rhs}, name) {}

    Value *getDest() const { return const_cast<BinaryOperator *>(this); } // 获取目的操作数(本身)
    Value *getLHS() const { return getOperandByIndex(0); }                // 获取左操作数
    Value *getRHS() const { return getOperandByIndex(1); }                // 获取右操作数
    string toString() const override;
};

// ===== UnaryOperator Implementation =====
class UnaryOperator : public Instruction
{
public:
    UnaryOperator(Opcode op, Value *operand, const string &name = "")
        : Instruction(operand->getType(), op,
                      vector<Value *>{operand}, name) {}

    Value *getDest() const { return const_cast<UnaryOperator *>(this); } // 获取目的操作数(本身)
    Value *getOperand() const { return getOperandByIndex(0); }           // 获取操作数
    string toString() const override;
};

// ===== ICmpInst Implementation =====
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

    Predicate getPredicate() const { return Pred; }                 // 获取比较谓词
    Value *getDest() const { return const_cast<ICmpInst *>(this); } // 获取目的操作数(本身)
    Value *getLHS() const { return getOperandByIndex(0); }          // 获取左操作数
    Value *getRHS() const { return getOperandByIndex(1); }          // 获取右操作数
    string toString() const override;
};

// ===== FCmpInst Implementation =====
class FCmpInst : public Instruction
{
public:
    enum Predicate
    {
        FCMP_OEQ, // =
        FCMP_ONE, // !=
        FCMP_OLT, // <
        FCMP_OLE, // <=
        FCMP_OGT, // >
        FCMP_OGE  // >=
    };

public:
    Predicate Pred;

    FCmpInst(Predicate pred, Value *lhs, Value *rhs, const string &name = "")
        : Instruction(IntegerType::getInstance(), Opcode::FCmp, vector<Value *>{lhs, rhs}, name),
          Pred(pred) {}

    Predicate getPredicate() const { return Pred; }                 // 获取比较谓词
    Value *getDest() const { return const_cast<FCmpInst *>(this); } // 获取目的操作数(本身)
    Value *getLHS() const { return getOperandByIndex(0); }          // 获取左操作数
    Value *getRHS() const { return getOperandByIndex(1); }          // 获取右操作数
    string toString() const override;
};

// ===== AllocaInst Implementation =====
class AllocaInst : public Instruction
{
public:
    Type *AllocatedType; // 传入数组类型，返回退化后的指针
    bool IsInitialized = false; // 标记数组是否需要后端初始化
    AllocaInst(Type *ty, const string &name = "")
        : Instruction(PointerType::getInstance(dynamic_cast<ArrayType *>(ty)->ElementType),
                      Opcode::Alloca, name),
          AllocatedType(ty) {}

    bool getIsInitialized() const;                                    // 获取是否初始化
    void setIsInitialized(bool isInit);                               // 设置是否初始化
    int getAllocatedSize() const;                                     // 获取数组的元素总长度
    vector<size_t> getArrayEveryDimensionLength() const;              // 从左到右获取数组每一维的长度
    Value *getDest() const { return const_cast<AllocaInst *>(this); } // 获取目的操作数(本身)
    string toString() const override;
    Type *getGroundElementType() const
    {
        // 获取数组的基本元素类型
        if (auto arrayType = dynamic_cast<ArrayType *>(AllocatedType))
        {
            return arrayType->getGroundElementType();
        }
        return AllocatedType; // 如果不是数组类型，返回自身
    }
};

// ===== LoadInst Implementation =====
class LoadInst : public Instruction
{
public:
    LoadInst(Value *ptr, const string &name = "")
        : Instruction(getElementType(ptr), Opcode::Load,
                      vector<Value *>{ptr}, name) {}

    Value *getDest() const { return const_cast<LoadInst *>(this); } // 获取目的操作数(本身)
    Value *getPointer() const { return getOperandByIndex(0); }      // 获取指针操作数
    Value *getOriginalPointer() const;                              // 获取原始存储指针(用于gep展开时递归获取最上层指针)
    string toString() const override;

private:
    static Type *getElementType(Value *ptr); // 获取指针指向的元素类型
};

// ===== StoreInst Implementation =====
class StoreInst : public Instruction
{
public:
    StoreInst(Value *val, Value *ptr)
        : Instruction(VoidType::getInstance(), Opcode::Store,
                      vector<Value *>{val, ptr}) {}

    Value *getValueToStore() const { return getOperandByIndex(0); } // 获取要存储的值
    Value *getPointer() const { return getOperandByIndex(1); }      // 获取存储的指针
    Value *getOriginalPointer() const;                              // 获取原始存储指针(用于gep展开时递归获取最上层指针)
    string toString() const override;
};

// ===== CallInst Implementation =====
class CallInst : public Instruction
{
public:
    CallInst(Function *func, const vector<Value *> &args, const string &name = "");

    Function *getCalledFunction() const;                // 获取被调用的函数

    vector<Value *> getArguments() const;               // 获取函数参数
    vector<Value *> getIntArguments() const;            // 获取int类型参数
    vector<Value *> getFloatArguments() const;          // 获取float类型参数
    vector<Value *> getPtrArguments() const;            // 获取指针类型参数
    bool hasReturnValue() const { return !getType()->isVoidTy(); }
    bool IsModifyingGlobalVar(Value* ptr) const;        // 是否有修改副作用(修改全局变量或指针指向的值)
    bool ifHasSideEffects() const;                      // 是否有副作用，目前只粗略判断-->判断是否有store指令，如果有store指令则有副作用
    Value *getDest() const;                             // 如果是void类型 返回空指针
    string toString() const override;

private:
    static vector<Value *> constructOperands(Function *func, const vector<Value *> &args);
    static Type *getFunctionReturnType(Value *func);
};

// ===== ReturnInst Implementation =====
class ReturnInst : public Instruction
{
public:
    ReturnInst() : Instruction(VoidType::getInstance(), Opcode::Ret) {} // 无返回值
    ReturnInst(Value *retVal)                                           // 有返回值
        : Instruction(VoidType::getInstance(), Opcode::Ret,
                      vector<Value *>{retVal})
    {
    }

    Value *getReturnValue() const // 获取返回值
    {
        if (getNumOperands() > 0)
        {
            return getOperandByIndex(0);
        }
        return nullptr;
    }
    string toString() const override;
};

// ===== BranchInst Implementation =====
class BranchInst : public Instruction
{
public:
    BasicBlock *TrueBlock;
    BasicBlock *FalseBlock;

    BranchInst(BasicBlock *target) // 无条件跳转
        : Instruction(VoidType::getInstance(), Opcode::Br),
          TrueBlock(target), FalseBlock(nullptr)
    {
    }
    BranchInst(Value *cond, BasicBlock *trueBlock, BasicBlock *falseBlock) // 有条件跳转
        : Instruction(VoidType::getInstance(), Opcode::Br,
                      vector<Value *>{cond}),
          TrueBlock(trueBlock), FalseBlock(falseBlock)
    {
    }

    bool isConditional() const { return getNumOperands() > 0; } // 是否为条件分支
    Value *getCondition() const                                 // 获取条件
    {
        if (isConditional())
        {
            return getOperandByIndex(0);
        }
        return nullptr;
    }
    string toString() const override;
};

// ===== PhiInst Implementation =====
class PhiInst : public Instruction
{
public:
    vector<BasicBlock *> IncomingValues;

    PhiInst(Type *ty, const string &name = "")
        : Instruction(ty, Opcode::Phi, name) {}

    unsigned getIndexByBasicBlock(BasicBlock *block) const;        // 获取前驱基本块的索引
    void addIncoming(Value *value, BasicBlock *block);             // 添加前驱基本块和对应的值
    void removeIncoming(unsigned index);                           // 删除指定前驱基本块和对应的值
    unsigned getNumIncomingValues() const;                         // 获取前驱基本块和对应的值长度
    Value *getIncomingValue(unsigned index) const;                 // 获取前驱value
    BasicBlock *getIncomingBlock(unsigned index) const;            // 获取前驱基本块
    void replaceIncomingBasicBlock(BasicBlock *oldBlock, BasicBlock *newBlock); // 替换前驱基本块
    void setIncomingBlock(unsigned index, BasicBlock *block);      // 设置前驱基本块
    Value *getDest() const { return const_cast<PhiInst *>(this); } // 获取目的操作数(本身)
    string toString() const override;
};

// ===== GetElementPtrInst Implementation =====
class GetElementPtrInst : public Instruction
{
public:
    int num_addedzero; // 用于记录补的0的个数
    GetElementPtrInst(Value *ptr, const vector<Value *> &indices, const string &name = "")
        : Instruction(calculateResultType(ptr, indices), Opcode::GetElementPtr,
                      constructOperands(ptr, indices), name) {}

    vector<Value *> getIndices() const;  // 获取索引操作数
    vector<int> *getArrayStride() const; // 获取数组的步长
    Value *getDest() const;              // 获取目的操作数(本身)
    Value *getPointerOperand() const;    // 获取指针操作数
    Value *getOriginalPointerOperand() const; // 获取原始指针操作数(用于gep展开时递归获取最上层指针)
    string toString() const override;

private:
    vector<Value *> constructOperands(Value *ptr, const vector<Value *> &indices);
    Type *calculateResultType(Value *ptr, const vector<Value *> &indices);
};

// ===== CastInst Implementation =====
class CastInst : public Instruction
{
public:
    Type *DestType;
    CastInst(Opcode op, Value *operand, Type *destType, const string &name = "")
        : Instruction(destType, op, vector<Value *>{operand}, name), DestType(destType) {}

    Value *getDest() const { return const_cast<CastInst *>(this); } // 获取目的操作数(本身)
    Value *getOperand() const { return getOperandByIndex(0); }      // 获取操作数
    string toString() const override;
};

// ===== CopyInst Implementation =====
class CopyInst : public Instruction
{
public:
    CopyInst(Value *source, const string &name = "")
        : Instruction(source->getType(), Opcode::Copy, vector<Value *>{source}, name) {}

    Value *getDest() const { return const_cast<CopyInst *>(this); } // 获取目的操作数(本身)
    Value *getSource() const { return getOperandByIndex(0); }       // 获取源操作数
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
    // 活跃变量
    std::set<std::string> LiveIn;  // 活跃变量集合
    std::set<std::string> LiveOut; // 活跃变量集合
    BasicBlock(const string &name = "", Function *parent = nullptr)
        : Value(VoidType::getInstance(), name), Parent(parent) {}

    void addInstruction(unique_ptr<Instruction> inst);         // 添加指令到基本块
    void insertBeforeTerminator(unique_ptr<Instruction> inst); // 在终结指令前插入指令
    void insert(unique_ptr<Instruction> inst, unsigned index); // 插入指令
    void addPredecessor(BasicBlock *pred);                     // 添加前驱基本块
    void removePredecessor(BasicBlock *pred);                  // 移除前驱基本块
    void addSuccessor(BasicBlock *succ);                       // 添加后继基本块
    void removeSuccessor(BasicBlock *succ);                    // 移除后继基本块
    const vector<BasicBlock *> &getPredecessors() const;       // 获取前驱基本块
    const vector<BasicBlock *> &getSuccessors() const;         // 获取后继基本块
    void setLiveIn(const std::set<std::string> &liveIn);       // 设置活跃变量集合
    const std::set<std::string> &getLiveIn() const;            // 获取活跃变量集合
    void setLiveOut(const std::set<std::string> &liveOut);     // 设置活跃变量集合
    const std::set<std::string> &getLiveOut() const;           // 获取活跃变量集合

    Instruction *getTerminator();                       // 获取终结指令
    vector<unique_ptr<Instruction>> &getInstructions(); // 获取所有指令
    void clearInstructions();                           // 清空指令
    bool hasTerminator();                               // 检查是否有终结指令
    bool containsByName(const std::string &name) const; // 判断是否包含特定指令(通过名称)
    int getInstructionOrder(Instruction *inst) const;   // 获取指令在基本块中的顺序
    void removeSelfBasicBlock();                        // 删除基本块(从父函数中移除)
    void setParent(Function *parent);                   // 设置父函数
    string toString() const override;
};

// ===== Argument =====
class Argument : public Value
{
public:
    Function *Parent;
    unsigned ArgNo;

    Argument(Type *ty, unsigned argNo, const string &name = "", Function *parent = nullptr)
        : Value(ty, name), Parent(parent), ArgNo(argNo) {}

    string toString() const override;
};
struct Loop
{
    BasicBlock *header;
    // blocks是循环体内的所有基本块
    // exits是循环体的出口基本块（可能有多个）
    vector<BasicBlock *> blocks;
    vector<BasicBlock *> exits;
    bool contains(Instruction *inst) const
    {
        return std::any_of(blocks.begin(), blocks.end(),
                           [&](BasicBlock *bb)
                           { return bb->containsByName(inst->getName()); });
    }
    Value *getLoopCondition()const
    {
        BasicBlock *headerBlock = header;
        Instruction *terminator = headerBlock->getTerminator();
        if(auto brInst = dynamic_cast<BranchInst *>(terminator))
        {
            if(brInst->isConditional())
            {
                return brInst->getCondition();
            }
            else 
            {
                return new ConstantInt(IntegerType::getInstance(), 1); // 如果没有条件，返回常量1
            }
        }
        else 
        {
            throw std::runtime_error("Loop header does not have a valid terminator instruction.");
        }
    }
};
// ===== Function =====
class Function : public Value
{
public:
    vector<unique_ptr<BasicBlock>> BasicBlocks;
    vector<unique_ptr<Argument>> Arguments;
    vector<Loop> Loops;                                 // 循环信息
    Module *Parent;
    bool isDeleted = false;                             // 标记函数是否被删除
    Function(FunctionType *funcTy, const string &name = "", Module *parent = nullptr)
        : Value(funcTy, name), Parent(parent), isDeleted(false) {}

    BasicBlock *addBasicBlock(const string &name = ""); // 添加基本块
    void addBasicBlock(unique_ptr<BasicBlock> block);   // 添加基本块(使用unique_ptr)
    BasicBlock *getEntryBlock();                        // 获取入口基本块
    vector<unique_ptr<BasicBlock>> &getBasicBlocks();   // 获取所有基本块

    Argument *addArgument(Type *type, const string &name = ""); // 添加参数
    const vector<unique_ptr<Argument>> &getArguments() const;   // 获取函数参数
    const vector<Argument *> getIntArguments() const;           // 获取int类型参数
    const vector<Argument *> getFloatArguments() const;         // 获取float类型参数
    const vector<Argument *> getPtrArguments() const;           // 获取指针类型参数
    Value* getArgumentByIndex(size_t index) const;              // 根据索引获取参数
    const vector<Loop> &getLoops() const;                       // 获取循环信息
    void setLoops(const vector<Loop> &loops);                   // 设置循环信息
    FunctionType *getFunctionType();                            // 获取函数类型
    unsigned getInstructionCount() const;                       // 获取指令数量
    bool isLibraryFunction() const;                             // 是否为库函数
    bool isRecursive() const;                                   // 是否为递归函数
    void setDeleted(bool deleted);                              // 设置函数是否被删除
    bool isDeletedFunction() const;                             // 获取函数是否被删除
    bool shouldBeOutput() const;                                // 是否应该输出到IR文件
    vector<size_t> getIndexOfNotUsedArguments() const;          // 获取未使用参数的索引
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

    Function *addFunction(FunctionType *funcType, const string &name); // 添加函数
    GlobalVariable *addGlobalVariable(Type *type, const string &name,  // 添加全局变量
                                      Constant *initializer = nullptr,
                                      bool isConstant = false);
    Function *getFunction(const string &name);             // 根据名称查找函数
    GlobalVariable *getGlobalVariable(const string &name); // 根据名称查找全局变量

    string toString() const;

    // Debug输出
    string getBasicBlockInfo(); // 输出基本块后继信息
};
