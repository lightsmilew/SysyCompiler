#include "IRDataStructure.h"
#include <sstream>
#include <algorithm>

std::string FunctionType::toString() const
{
    std::stringstream ss;
    ss << ReturnType->toString() << " (";
    for (size_t i = 0; i < ParamTypes.size(); ++i)
    {
        if (i > 0)
            ss << ", ";
        ss << ParamTypes[i]->toString();
    }
    ss << ")";
    return ss.str();
}

// ===== Value System Implementation =====

// 实现 replaceAllUsesWith 方法
void Value::replaceAllUsesWith(Value *newValue)
{
    if (this == newValue)
        return; // 避免自替换

    // 复制Users列表，因为在替换过程中会修改这个列表
    vector<User *> usersCopy = Users;

    for (User *user : usersCopy)
    {
        user->replaceOperand(this, newValue);
    }

    // 此时Users应该已经被清空了（通过replaceOperand调用removeUser）
    Users.clear();
}

// GlobalVariable implementation
std::string GlobalVariable::toString() const
{
    std::stringstream ss;
    ss << "@" << getName() << " = ";
    if (IsConstant)
        ss << "constant ";
    else
        ss << "global ";

    // 直接使用存储的类型（不是指针包装）
    ss << getType()->toString();

    if (Initializer)
    {
        ss << " " << Initializer->toString();
    }
    else
    {
        ss << " zeroinitializer";
    }

    return ss.str();
}

// User implementation
std::string User::toString() const
{
    // User是抽象基类，通常不直接使用toString，而是由子类重写
    std::stringstream ss;
    ss << getName() << " = user with " << getNumOperands() << " operands";
    return ss.str();
}

// ===== Instruction System Implementation =====
std::string BinaryOperator::toString() const
{
    std::stringstream ss;
    std::string opStr;

    switch (Op)
    {
    case Opcode::Add:
        opStr = "add";
        break;
    case Opcode::Sub:
        opStr = "sub";
        break;
    case Opcode::Mul:
        opStr = "mul";
        break;
    case Opcode::SDiv:
        opStr = "sdiv";
        break;
    case Opcode::SRem:
        opStr = "srem";
        break;
    case Opcode::FAdd:
        opStr = "fadd";
        break;
    case Opcode::FSub:
        opStr = "fsub";
        break;
    case Opcode::FMul:
        opStr = "fmul";
        break;
    case Opcode::FDiv:
        opStr = "fdiv";
        break;
    default:
        opStr = "unknown";
        break;
    }

    ss << "%" << getName() << " = " << opStr << " " << getType()->toString()
       << " " << LHS->toRef() << ", " << RHS->toRef();

    return ss.str();
}

std::string UnaryOperator::toString() const
{
    std::stringstream ss;
    std::string opStr;

    switch (Op)
    {
    case Opcode::Sub:
        opStr = "sub";
        ss << "%" << getName() << " = " << opStr << " " << getType()->toString()
           << " 0, " << Operand->toRef();
        break;
    default:
        opStr = "unknown_unary";
        ss << "%" << getName() << " = " << opStr << " " << getType()->toString()
           << " " << Operand->toRef();
        break;
    }

    return ss.str();
}

std::string ICmpInst::toString() const
{
    std::stringstream ss;
    std::string predStr;

    switch (Pred)
    {
    case ICMP_EQ:
        predStr = "eq";
        break;
    case ICMP_NE:
        predStr = "ne";
        break;
    case ICMP_SLT:
        predStr = "slt";
        break;
    case ICMP_SLE:
        predStr = "sle";
        break;
    case ICMP_SGT:
        predStr = "sgt";
        break;
    case ICMP_SGE:
        predStr = "sge";
        break;
    }

    ss << "%" << getName() << " = icmp " << predStr << " " << LHS->getType()->toString()
       << " " << LHS->toRef() << ", " << RHS->toRef();

    return ss.str();
}

std::string FCmpInst::toString() const
{
    std::stringstream ss;
    std::string predStr;

    switch (Pred)
    {
    case FCMP_OEQ:
        predStr = "oeq";
        break;
    case FCMP_ONE:
        predStr = "one";
        break;
    case FCMP_OLT:
        predStr = "olt";
        break;
    case FCMP_OLE:
        predStr = "ole";
        break;
    case FCMP_OGT:
        predStr = "ogt";
        break;
    case FCMP_OGE:
        predStr = "oge";
        break;
    }

    ss << "%" << getName() << " = fcmp " << predStr << " " << LHS->getType()->toString()
       << " " << LHS->toRef() << ", " << RHS->toRef();

    return ss.str();
}

std::string AllocaInst::toString() const
{
    std::stringstream ss;
    ss << "%" << getName() << " = alloca " << AllocatedType->toString();
    return ss.str();
}

std::string LoadInst::toString() const
{
    std::stringstream ss;
    ss << "%" << getName() << " = load " << getType()->toString()
       << ", " << Pointer->getType()->toString() << " " << Pointer->toRef();
    return ss.str();
}

std::string StoreInst::toString() const
{
    std::stringstream ss;
    ss << "store " << ValueToStore->getType()->toString() << " " << ValueToStore->toRef()
       << ", " << Pointer->getType()->toString() << " " << Pointer->toRef();
    return ss.str();
}

std::string CallInst::toString() const
{
    std::stringstream ss;

    if (!getType()->isVoidTy())
    {
        ss << "%" << getName() << " = ";
    }

    ss << "call " << getType()->toString() << " @" << CalledFunction->getName() << "(";

    for (size_t i = 0; i < Arguments.size(); ++i)
    {
        if (i > 0)
            ss << ", ";
        ss << Arguments[i]->getType()->toString() << " " << Arguments[i]->toRef();
    }

    ss << ")";
    return ss.str();
}

// CallInst implementation
CallInst::CallInst(Function *func, const vector<Value *> &args, const string &name)
    : Instruction(getFunctionReturnType(func), Opcode::Call, constructOperands(func, args), name),
      CalledFunction(func), Arguments(args) {}

vector<Value *> CallInst::constructOperands(Function *func, const vector<Value *> &args)
{
    vector<Value *> operands;
    operands.push_back(func);
    operands.insert(operands.end(), args.begin(), args.end());
    return operands;
}

Type *CallInst::getFunctionReturnType(Value *func)
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

std::string ReturnInst::toString() const
{
    std::stringstream ss;
    if (ReturnValue)
    {
        ss << "ret " << ReturnValue->getType()->toString() << " " << ReturnValue->toRef();
    }
    else
    {
        ss << "ret void";
    }
    return ss.str();
}

std::string BranchInst::toString() const
{
    std::stringstream ss;
    if (Condition)
    {
        ss << "br " << Condition->getType()->toString() << " " << Condition->toRef()
           << ", label %" << TrueBlock->getName() << ", label %" << FalseBlock->getName();
    }
    else
    {
        ss << "br label %" << TrueBlock->getName();
    }
    return ss.str();
}

std::string PHINode::toString() const
{
    std::stringstream ss;
    ss << "%" << getName() << " = phi " << getType()->toString();

    for (size_t i = 0; i < IncomingValues.size(); ++i)
    {
        if (i > 0)
            ss << ",";
        ss << " [ " << IncomingValues[i].first->toString()
           << ", %" << IncomingValues[i].second->getName() << " ]";
    }

    return ss.str();
}

// ===== GetElementPtrInst Implementation =====
vector<Value *> GetElementPtrInst::constructOperands(Value *ptr, const vector<Value *> &indices)
{
    vector<Value *> operands;
    operands.push_back(ptr);
    operands.insert(operands.end(), indices.begin(), indices.end());
    return operands;
}

Type *GetElementPtrInst::calculateResultType(Value *ptr, const vector<Value *> &indices)
{
    // GEP总是返回指针类型
    if (indices.empty())
    {
        return ptr->getType(); // 返回原指针类型
    }

    Type *currentType = ptr->getType();
    if (auto ptrType = dynamic_cast<PointerType *>(currentType))
    {
        currentType = ptrType->ElementType;
    }

    // 跳过第一个索引（通常是0），处理后续索引
    for (size_t i = 1; i < indices.size(); ++i)
    {
        if (auto arrayType = dynamic_cast<ArrayType *>(currentType))
        {
            //获取到数组元素基本类型 即退化一维
            currentType = arrayType->ElementType;
        }
    }
    // 最终返回指向当前类型的指针
    return PointerType::getInstance(currentType);
}
bool Type ::isTypeEqual(Type* a, Type* b) {
    if (a == b) return true;
    if (a->getTypeID() != b->getTypeID()) return false;
    // 针对 ArrayType、PointerType 递归比较元素类型和长度
    if (a->isArrayTy() && b->isArrayTy()) {
        auto aa = static_cast<ArrayType*>(a);
        auto bb = static_cast<ArrayType*>(b);
        return aa->getNumElements() == bb->getNumElements() && isTypeEqual(aa->ElementType, bb->ElementType);
    }
    if (a->isPointerTy() && b->isPointerTy()) {
        return isTypeEqual(static_cast<PointerType*>(a)->ElementType, static_cast<PointerType*>(b)->ElementType);
    }
    // 基本类型直接比较

    return true;
}
std::string GetElementPtrInst::toString() const
{
    std::stringstream ss;
    ss << "%" << getName() << " = getelementptr ";

    // 获取基本类型
    Type *baseType = nullptr;
    if (auto ptrType = dynamic_cast<PointerType *>(PointerOperand->getType()))
    {
        baseType = ptrType->ElementType;
    }

    if (baseType)
    {
        ss << baseType->toString() << ", ";
    }

    ss << PointerOperand->getType()->toString() << " " << PointerOperand->toRef();

    for (Value *index : Indices)
    {
        ss << ", " << index->getType()->toString() << " " << index->toRef();
    }

    return ss.str();
}

// ===== CastInst Implementation =====
std::string CastInst::toString() const
{
    std::stringstream ss;
    std::string opStr;

    switch (Op)
    {
    case Opcode::SIToFP:
        opStr = "sitofp";
        break;
    case Opcode::FPToSI:
        opStr = "fptosi";
        break;
    default:
        opStr = "cast";
        break;
    }

    ss << "%" << getName() << " = " << opStr << " "
       << Operand->getType()->toString() << " " << Operand->toRef()
       << " to " << DestType->toString();

    return ss.str();
}

// ===== BasicBlock Implementation =====
std::string BasicBlock::toString() const
{
    std::stringstream ss;

    // Block label
    if (!getName().empty())
    {
        ss << getName() << ":\n";
    }

    // Instructions
    for (const auto &inst : Instructions)
    {
        ss << "  " << inst->toString() << "\n";
    }

    return ss.str();
}

// ===== Argument Implementation =====
std::string Argument::toString() const
{
    return "%" + getName();
}

// ===== Function Implementation =====
std::string Function::toString() const
{
    std::stringstream ss;

    // Function signature - only return type
    FunctionType *funcTy = static_cast<FunctionType *>(getType());
    ss << "define " << funcTy->ReturnType->toString()
       << " @" << getName() << "(";

    for (size_t i = 0; i < Arguments.size(); ++i)
    {
        if (i > 0)
            ss << ", ";
        ss << Arguments[i]->getType()->toString() << " %" << Arguments[i]->getName();
    }

    ss << ") {\n";

    // Basic blocks
    for (const auto &bb : BasicBlocks)
    {
        ss << bb->toString();
    }

    ss << "}\n";

    return ss.str();
}

// ===== Module Implementation =====
std::string Module::toString() const
{
    std::stringstream ss;

    ss << "; ModuleID = '" << Name << "'\n\n";

    // Global variables
    for (const auto &gv : GlobalVariables)
    {
        ss << gv->toString() << "\n";
    }

    if (!GlobalVariables.empty())
    {
        ss << "\n";
    }

    // Functions
    for (const auto &func : Functions)
    {
        ss << func->toString() << "\n";
    }

    return ss.str();
}

// ===== Utility Functions =====

// 类型转换辅助函数
namespace IRUtils
{
    // 从AST DataType转换为IR Type
    Type *convertASTTypeToIRType(const DataType &astType)
    {
        switch (astType.baseType)
        {
        case PrimaryDataType::INT:
            if (astType.isArray())
            {
                Type *elementType = IntegerType::getInstance();
                for (int i = astType.arraySizes().size() - 1; i >= 0; --i)
                {
                    elementType = new ArrayType(elementType, astType.arraySizes()[i]);
                }
                return elementType;
            }
            return IntegerType::getInstance();

        case PrimaryDataType::FLOAT:
            if (astType.isArray())
            {
                Type *elementType = FloatType::getInstance();
                for (int i = astType.arraySizes().size() - 1; i >= 0; --i)
                {
                    elementType = new ArrayType(elementType, astType.arraySizes()[i]);
                }
                return elementType;
            }
            return FloatType::getInstance();

        case PrimaryDataType::VOID:
            return VoidType::getInstance();

        default:
            return VoidType::getInstance();
        }
    }

    // 检查两个类型是否兼容
    bool isTypeCompatible(Type *t1, Type *t2)
    {
        if (t1 == t2)
            return true;

        // 数值类型之间可以转换
        if ((t1->isIntegerTy() || t1->isFloatTy()) &&
            (t2->isIntegerTy() || t2->isFloatTy()))
        {
            return true;
        }

        return false;
    }

    // 获取二元运算的结果类型
    Type *getBinaryOpResultType(Type *lhs, Type *rhs, BinaryOp op)
    {
        // 浮点运算优先
        if (lhs->isFloatTy() || rhs->isFloatTy())
        {
            return FloatType::getInstance();
        }

        // 比较运算返回布尔类型
        if (op == BinaryOp::Lt || op == BinaryOp::Gt ||
            op == BinaryOp::Le || op == BinaryOp::Ge ||
            op == BinaryOp::Eq || op == BinaryOp::Ne)
        {
            return BooleanType::getInstance();
        }

        // 逻辑运算返回布尔类型
        if (op == BinaryOp::And || op == BinaryOp::Or)
        {
            return BooleanType::getInstance();
        }

        // 其他算术运算返回整数类型
        return IntegerType::getInstance();
    }
}
