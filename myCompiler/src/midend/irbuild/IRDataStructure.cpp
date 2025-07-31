#include "IRDataStructure.h"
#include <sstream>
#include <algorithm>
size_t ArrayType::getArrayLength() const
{
    if (auto arrayType = dynamic_cast<ArrayType *>(ElementType))
    {
        return NumElements * arrayType->getArrayLength();
    }
    // 基础类型的数组长度为元素数量
    return NumElements;
}
vector<size_t> ArrayType::getArrayIndices() const
{
    vector<size_t> dimensions;

    dimensions.push_back(NumElements);
    Type *elemType = ElementType;
    while (auto nestedArray = dynamic_cast<ArrayType *>(elemType))
    {
        dimensions.push_back(nestedArray->getNumElements());
        elemType = nestedArray->getElementType();
    }

    return dimensions;
}
string ArrayType::toString() const
{
    return "[" + to_string(NumElements) + " x " + ElementType->toString() + "]";
}
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
void Value::replaceAllUsesWith(Value *newValue)
{
    // 避免自替换
    if (this == newValue)
        return;

    // 复制Users列表，因为在替换过程中会修改这个列表
    vector<User *> usersCopy = Users;

    for (User *user : usersCopy)
    {
        user->replaceOperand(this, newValue);
    }

    // 此时Users应该已经被清空了（通过replaceOperand调用removeUser）
    Users.clear();
}
void Value::addUser(User *user)
{
    Users.push_back(user);
}
void Value::removeUser(User *user)
{
    Users.erase(std::remove(Users.begin(), Users.end(), user), Users.end());
}

// ==== User System Implementation =====
void User::removeThisFromOperands()
{
    // 从所有操作数的Users列表中移除自己
    for (Value *operand : Operands)
    {
        if (operand)
        {
            operand->removeUser(this);
        }
    }
}
void User::addOperand(Value *operand)
{
    if (operand)
    {
        Operands.push_back(operand);
        operand->addUser(this);
    }
}
void User::setOperands(const vector<Value *> &operands)
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
        if (op)
        {
            op->addUser(this);
        }
    }
}
void User::replaceOperand(Value *oldValue, Value *newValue)
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
unsigned User::getNumOperands() const
{
    return Operands.size();
}
const vector<Value *> &User::getOperands() const
{
    return Operands;
}
Value *User::getOperandByIndex(unsigned index) const
{
    if (index < Operands.size())
    {
        return Operands[index];
    }
    throw std::out_of_range("Invalid operand index");
}
void User::setOperandByIndex(unsigned index, Value *value)
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
void User::removeOperandByIndex(unsigned index)
{
    if (index < Operands.size())
    {
        Value *oldValue = Operands[index];
        if (oldValue)
        {
            oldValue->removeUser(this);
        }
        Operands.erase(Operands.begin() + index);
    }
}

string ConstantString::toString() const
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

string ConstantArray::toString() const
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

// =====GlobalVariable implementation=====
bool GlobalVariable::isArray() const
{
    // 判断是否为数组类型
    return OriginalType->isArrayTy();
}
vector<size_t> GlobalVariable::getDims() const
{
    if (OriginalType->isArrayTy())
    {
        auto arrayType = static_cast<ArrayType *>(OriginalType);
        return arrayType->getArrayIndices();
    }
    return vector<size_t>{0}; // 标量返回0维度
}
size_t GlobalVariable::getTotallength() const
{
    // 获取数组总长度
    if (OriginalType->isArrayTy())
    {
        auto arrayType = static_cast<ArrayType *>(OriginalType);
        return arrayType->getArrayLength();
    }
    return 0; // 不是数组类型
}
Type *GlobalVariable::getGroundElementType() const
{
    // 获取数组的基本元素类型
    if (OriginalType->isArrayTy())
    {
        auto arrayType = static_cast<ArrayType *>(OriginalType);
        while (arrayType->getElementType()->isArrayTy())
        {
            arrayType = static_cast<ArrayType *>(arrayType->getElementType());
        }
        return arrayType->getElementType();
    }
    return OriginalType; // 如果不是数组类型，返回原始类型
}
std::string GlobalVariable::toString() const
{
    std::stringstream ss;
    ss << "@" << getName() << " = ";
    if (IsConstant)
        ss << "constant ";
    else
        ss << "global ";

    // 直接使用存储的类型（不是指针包装）
    ss << OriginalType->toString();

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

bool Value::isGlobal() const
{
    return dynamic_cast<GlobalVariable *>(const_cast<Value *>(this)) != nullptr;
}
// =====User implementation=====
std::string User::toString() const
{
    // User是抽象基类，通常不直接使用toString，而是由子类重写
    std::stringstream ss;
    ss << getName() << " = user with " << getNumOperands() << " operands";
    return ss.str();
}

// ===== Instruction System Implementation =====
string Instruction::getOpcodeName() const
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
    case Opcode::Sll:
        return "sll";   
    case Opcode::Sra:
        return "sra";
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
bool Instruction::isBinaryOp() const
{
    return Op == Opcode::Add || Op == Opcode::Sub || Op == Opcode::Mul ||
           Op == Opcode::SDiv || Op == Opcode::SRem || Op == Opcode::FAdd ||
           Op == Opcode::FSub || Op == Opcode::FMul || Op == Opcode::FDiv;
}
bool Instruction::isComparisonOp() const
{
    return Op == Opcode::ICmp || Op == Opcode::FCmp;
}
bool Instruction::isTerminator() const
{
    return Op == Opcode::Ret || Op == Opcode::Br;
}
bool Instruction::isCopy() const
{
    return Op == Opcode::Copy;
}
bool Instruction::mayHaveSideEffects() const
{
    return Op == Opcode::Store || Op == Opcode::Call || Op == Opcode::Br ||
           Op == Opcode::Ret || Op == Opcode::Alloca;
}
// 判断该指令是否有结果（即是否定义SSA变量）
bool Instruction::hasResult() const
{
    switch (Op)
    {
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::SDiv:
    case Opcode::SRem:
    case Opcode::FAdd:
    case Opcode::FSub:
    case Opcode::FMul:
    case Opcode::FDiv:
    case Opcode::ICmp:
    case Opcode::FCmp:
    case Opcode::Alloca:
    case Opcode::Load:
    case Opcode::Call:
    case Opcode::Phi:
    case Opcode::GetElementPtr:
    case Opcode::SIToFP:
    case Opcode::FPToSI:
    case Opcode::Copy:
        return true;
    default:
        return false; // Ret, Br, Store等没有结果
    }
}
Instruction *Instruction::cloneWithRename(const std::unordered_map<Value *, Value *> &valueMap, string suffix) const
{
    // 复制操作数，若在映射表中则替换
    std::vector<Value *> newOperands;
    for (auto *op : this->getOperands())
    {
        auto it = valueMap.find(op);
        if (it != valueMap.end())
            newOperands.push_back(it->second);
        else
            newOperands.push_back(op);
    }

    // 创建新指令(通过clone工厂方法)
    // clone() 会复制类型和操作码
    Instruction *newInst = this->clone();
    newInst->setOperands(newOperands);

    // 变量重命名(添加后缀_inl*)
    static int inline_cnt = 0;
    newInst->setName(this->getName() + suffix);

    return newInst;
}
Instruction *Instruction::clone() const
{
    switch (Op)
    {
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::SDiv:
    case Opcode::SRem:
    case Opcode::FAdd:
    case Opcode::FSub:
    case Opcode::FMul:
    case Opcode::FDiv:
        return new BinaryOperator(Op, getOperandByIndex(0), getOperandByIndex(1), getName());
    case Opcode::ICmp:
        return new ICmpInst(static_cast<const ICmpInst *>(this)->getPredicate(),
                            getOperandByIndex(0), getOperandByIndex(1), getName());
    case Opcode::FCmp:
        return new FCmpInst(static_cast<const FCmpInst *>(this)->getPredicate(),
                            getOperandByIndex(0), getOperandByIndex(1), getName());
    case Opcode::Alloca:
        return new AllocaInst(static_cast<const AllocaInst *>(this)->AllocatedType, getName());
    case Opcode::Load:
        return new LoadInst(getOperandByIndex(0), getName());
    case Opcode::Store:
        return new StoreInst(getOperandByIndex(0), getOperandByIndex(1));
    case Opcode::Call:
    {
        auto *call = static_cast<const CallInst *>(this);
        std::vector<Value *> args = call->getArguments();
        return new CallInst(call->getCalledFunction(), args, getName());
    }
    case Opcode::Ret:
        if (getNumOperands() > 0)
            return new ReturnInst(getOperandByIndex(0));
        else
            return new ReturnInst();
    case Opcode::Br:
    {
        auto *br = static_cast<const BranchInst *>(this);
        if (br->isConditional())
            return new BranchInst(br->getCondition(), br->TrueBlock, br->FalseBlock);
        else
            return new BranchInst(br->TrueBlock);
    }
    case Opcode::Phi:
    {
        auto *phi = static_cast<const PhiInst *>(this);
        auto *newPhi = new PhiInst(getType(), getName());
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
            newPhi->addIncoming(phi->getIncomingValue(i), phi->getIncomingBlock(i));
        return newPhi;
    }
    case Opcode::GetElementPtr:
    {
        auto *gep = static_cast<const GetElementPtrInst *>(this);
        return new GetElementPtrInst(gep->getPointerOperand(), gep->getIndices(), getName());
    }
    case Opcode::SIToFP:
    case Opcode::FPToSI:
    {
        auto *cast = static_cast<const CastInst *>(this);
        return new CastInst(Op, getOperandByIndex(0), cast->DestType, getName());
    }
    case Opcode::Copy:
        return new CopyInst(getOperandByIndex(0), getName());
    default:
        throw std::runtime_error("Clone not implemented for this opcode");
    }
}

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
    case Opcode::Sll:
        opStr = "sll";
        break;
    case Opcode::Sra:
        opStr = "sra";
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
       << " " << getLHS()->toRef() << ", " << getRHS()->toRef();

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
           << " 0, " << getOperand()->toRef();
        break;
    default:
        opStr = "unknown_unary";
        ss << "%" << getName() << " = " << opStr << " " << getType()->toString()
           << " " << getOperand()->toRef();
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

    ss << "%" << getName() << " = icmp " << predStr << " " << getLHS()->getType()->toString()
       << " " << getLHS()->toRef() << ", " << getRHS()->toRef();

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

    ss << "%" << getName() << " = fcmp " << predStr << " " << getLHS()->getType()->toString()
       << " " << getLHS()->toRef() << ", " << getRHS()->toRef();

    return ss.str();
}

int AllocaInst::getAllocatedSize() const
{
    if (auto arrayType = dynamic_cast<ArrayType *>(AllocatedType))
    {
        return arrayType->getArrayLength() * 4;
    }
    // 非数组类型的分配大小为1
    return 1;
}
// 从左到右获取数组每一维的长度
// 例如：int a[2][3][4] -> 返回 {2, 3, 4}
// 如果是指针类型则返回空数组
// 如果是基本类型则返回空数组
// 如果是数组类型则返回每一维的长度
vector<size_t> AllocaInst::getArrayEveryDimensionLength() const
{
    vector<size_t> dimensions;
    if (auto arrayType = dynamic_cast<ArrayType *>(AllocatedType))
    {
        dimensions.push_back(arrayType->getNumElements());
        Type *elemType = arrayType->getElementType();
        while (auto nestedArray = dynamic_cast<ArrayType *>(elemType))
        {
            dimensions.push_back(nestedArray->getNumElements());
            elemType = nestedArray->getElementType();
        }
    }
    return dimensions;
}
std::string AllocaInst::toString() const
{
    std::stringstream ss;
    ss << "%" << getName() << " = alloca " << AllocatedType->toString();
    return ss.str();
}

Type *LoadInst::getElementType(Value *ptr)
{
    // load操作数为空，报错
    if (!ptr)
        throw std::runtime_error("LoadInst: pointer operand is null");
    // 如果是指针类型，返回指向的元素类型
    if (auto ptrTy = dynamic_cast<PointerType *>(ptr->getType()))
    {
        return ptrTy->ElementType;
    }
    // 传入不是指针报错
    throw std::runtime_error("LoadInst: operand must be a pointer type");
}
std::string LoadInst::toString() const
{
    std::stringstream ss;
    ss << "%" << getName() << " = load " << getType()->toString()
       << ", " << getPointer()->getType()->toString() << " " << getPointer()->toRef();
    return ss.str();
}

std::string StoreInst::toString() const
{
    std::stringstream ss;
    ss << "store " << getValueToStore()->getType()->toString() << " " << getValueToStore()->toRef()
       << ", " << getPointer()->getType()->toString() << " " << getPointer()->toRef();
    return ss.str();
}

// CallInst implementation
CallInst::CallInst(Function *func, const vector<Value *> &args, const string &name)
    : Instruction(getFunctionReturnType(func), Opcode::Call, constructOperands(func, args), name) {}

vector<Value *> CallInst::constructOperands(Function *func, const vector<Value *> &args)
{
    vector<Value *> operands;
    operands.push_back(func);
    operands.insert(operands.end(), args.begin(), args.end());
    return operands;
}
Function *CallInst::getCalledFunction() const
{
    return dynamic_cast<Function *>(getOperandByIndex(0));
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
vector<Value *> CallInst::getArguments() const
{
    vector<Value *> args(getOperands().begin() + 1, getOperands().end());
    return args;
}
vector<Value *> CallInst::getIntArguments() const
{
    vector<Value *> args;
    for (size_t i = 1; i < getNumOperands(); ++i)
    {
        if (getOperandByIndex(i)->getType()->isIntegerTy())
        {
            args.push_back(getOperandByIndex(i));
        }
    }
    return args;
}
vector<Value *> CallInst::getFloatArguments() const
{
    vector<Value *> args;
    for (size_t i = 1; i < getNumOperands(); ++i)
    {
        if (getOperandByIndex(i)->getType()->isFloatTy())
        {
            args.push_back(getOperandByIndex(i));
        }
    }
    return args;
}
vector<Value *> CallInst::getPtrArguments() const
{
    vector<Value *> args;
    for (size_t i = 1; i < getNumOperands(); ++i)
    {
        if (getOperandByIndex(i)->getType()->isPointerTy())
        {
            args.push_back(getOperandByIndex(i));
        }
    }
    return args;
}
Value *CallInst::getDest() const
{
    if (!hasReturnValue())
    {
        return nullptr; // 如果没有返回值，返回nullptr
    }
    return const_cast<CallInst *>(this); // 否则返回自身
}
std::string CallInst::toString() const
{
    std::stringstream ss;

    if (hasReturnValue())
    {
        ss << "%" << getName() << " = ";
    }

    ss << "call " << getType()->toString() << " @" << getCalledFunction()->getName() << "(";

    for (size_t i = 0; i < getArguments().size(); ++i)
    {
        if (i > 0)
            ss << ", ";
        ss << getArguments()[i]->getType()->toString() << " " << getArguments()[i]->toRef();
    }

    ss << ")";
    return ss.str();
}

std::string ReturnInst::toString() const
{
    std::stringstream ss;
    if (getReturnValue())
    {
        ss << "ret " << getReturnValue()->getType()->toString() << " " << getReturnValue()->toRef();
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
    if (isConditional())
    {
        ss << "br " << getCondition()->getType()->toString() << " " << getCondition()->toRef()
           << ", label %" << TrueBlock->getName() << ", label %" << FalseBlock->getName();
    }
    else
    {
        ss << "br label %" << TrueBlock->getName();
    }
    return ss.str();
}

unsigned PhiInst::getIndexByBasicBlock(BasicBlock *block) const
{
    auto it = std::find(IncomingValues.begin(), IncomingValues.end(), block);
    if (it != IncomingValues.end())
    {
        return std::distance(IncomingValues.begin(), it);
    }
    return -1; // 如果没有找到，返回无效索引
}
void PhiInst::addIncoming(Value *value, BasicBlock *block)
{
    IncomingValues.emplace_back(block);
    block->addUser(this); // 添加到BasicBlock的用户列表中
    // 添加到操作数列表中
    addOperand(value);
}
void PhiInst::removeIncoming(unsigned index)
{
    if (index < IncomingValues.size())
    {
        // 从操作数列表中删除对应的值
        removeOperandByIndex(index);
        // 删除IncomingValues中的对应块
        IncomingValues.erase(IncomingValues.begin() + index);
    }
    else
    {
        throw std::out_of_range("Invalid incoming index");
    }
}
unsigned PhiInst::getNumIncomingValues() const
{
    return IncomingValues.size();
}
Value *PhiInst::getIncomingValue(unsigned index) const
{
    if (index < getNumOperands())
    {
        return getOperandByIndex(index);
    }
    throw std::out_of_range("Invalid incoming value index");
}
BasicBlock *PhiInst::getIncomingBlock(unsigned index) const
{
    if (index < IncomingValues.size())
    {
        return IncomingValues[index];
    }
    throw std::out_of_range("Invalid incoming block index");
}
void PhiInst::setIncomingBlock(unsigned index, BasicBlock *block)
{
    if (index < IncomingValues.size())
    {
        IncomingValues[index] = block;
        block->addUser(this); // 添加到BasicBlock的用户列表中
    }
    else
    {
        throw std::out_of_range("Invalid incoming block index");
    }
}
std::string PhiInst::toString() const
{
    std::stringstream ss;
    ss << "%" << getName() << " = phi " << getType()->toString();

    for (size_t i = 0; i < IncomingValues.size(); ++i)
    {
        if (i > 0)
            ss << ",";
        ss << " [ " << getOperandByIndex(i)->toRef()
           << ", %" << IncomingValues[i]->getName() << " ]";
    }
    return ss.str();
}

// ===== CopyInst Implementation =====
std::string CopyInst::toString() const
{
    std::stringstream ss;
    ss << "%" << getName() << " = copy " << getSource()->getType()->toString()
       << " " << getSource()->toRef();
    return ss.str();
}

// ===== GetElementPtrInst Implementation =====
Value *GetElementPtrInst::getPointerOperand() const
{
    return getOperandByIndex(0);
}
vector<Value *> GetElementPtrInst::getIndices() const
{
    vector<Value *> indices(getOperands().begin() + 1, getOperands().end());
    return indices;
}
Value *GetElementPtrInst::getDest() const
{
    return const_cast<GetElementPtrInst *>(this);
}
vector<int> *GetElementPtrInst::getArrayStride() const
{
    auto stride = getPointerOperand()->getType();
    auto ptrType = dynamic_cast<PointerType *>(stride);
    if (ptrType)
    {
        auto arrayType = dynamic_cast<ArrayType *>(ptrType->ElementType);
        if (arrayType)
        {
            auto indices = arrayType->getArrayIndices();
            return new vector<int>(indices.begin(), indices.end());
        }
    }
    return nullptr; // 如果不是数组类型，返回空指针
}
vector<Value *> GetElementPtrInst::constructOperands(Value *ptr, const vector<Value *> &indices)
{
    vector<Value *> operands;
    operands.push_back(ptr);
    operands.insert(operands.end(), indices.begin(), indices.end());
    int dimensions = 0;
    if (auto ptrType = dynamic_cast<PointerType *>(ptr->getType()))
    {
        auto elementType = dynamic_cast<ArrayType *>(ptrType->ElementType);
        // 一层指针返回 1维
        if (!elementType)
            dimensions = 1;
        // 数组类型的指针，+1是因为第一个索引是指针本身
        else
            dimensions = elementType->getArrayIndices().size() + 1;
    }
    // 如果索引小于维度数，补齐为0
    for (int i = indices.size(); i < dimensions; ++i)
    {
        operands.push_back(new ConstantInt(IntegerType::getInstance(), 0)); // 补齐为0
    }
    num_addedzero = dimensions - indices.size(); // 记录补齐的0的数量
    return operands;
}
Type *GetElementPtrInst::calculateResultType(Value *ptr, const vector<Value *> &indices)
{
    // 如果是指针类型
    if (ptr->getType()->isPointerTy())
    {
        if (indices.empty())
        {
            // 返回原指针类型
            return ptr->getType();
        }
        auto ptrType = static_cast<PointerType *>(ptr->getType());
        Type *currentType = ptrType->ElementType;
        // 跳过第一个索引(已经解引用)，处理后续索引
        for (size_t i = 1; i < indices.size(); ++i)
        {
            if (auto arrayType = dynamic_cast<ArrayType *>(currentType))
            {
                // 获取到数组元素基本类型 即退化一维
                currentType = arrayType->ElementType;
            }
        }
        // 如果仍然是数组则退化返回指针类型
        if (auto arrayType = dynamic_cast<ArrayType *>(currentType))
        {
            return PointerType::getInstance(arrayType->ElementType);
        }
        // 否则返回基础类型的指针
        return PointerType::getInstance(currentType);
    }
    // 如果不是指针类型，报错
    throw std::runtime_error("GetElementPtrInst: operand is not a pointer");
}

bool Type::isTypeEqual(Type *a, Type *b)
{
    if (a == b)
        return true;
    if (a->getTypeID() != b->getTypeID())
        return false;
    // 针对 ArrayType、PointerType 递归比较元素类型和长度
    if (a->isArrayTy() && b->isArrayTy())
    {
        auto aa = static_cast<ArrayType *>(a);
        auto bb = static_cast<ArrayType *>(b);
        return aa->getNumElements() == bb->getNumElements() && isTypeEqual(aa->ElementType, bb->ElementType);
    }
    if (a->isPointerTy() && b->isPointerTy())
    {
        return isTypeEqual(static_cast<PointerType *>(a)->ElementType, static_cast<PointerType *>(b)->ElementType);
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
    if (auto ptrType = dynamic_cast<PointerType *>(getPointerOperand()->getType()))
    {
        baseType = ptrType->ElementType;
    }

    if (baseType)
    {
        ss << baseType->toString() << ", ";
    }

    ss << getPointerOperand()->getType()->toString() << " " << getPointerOperand()->toRef();

    for (Value *index : getIndices())
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
    case Opcode::BitCast:
        opStr = "bitcast";
        break;
    default:
        opStr = "cast";
        break;
    }

    ss << "%" << getName() << " = " << opStr << " "
       << getOperand()->getType()->toString() << " " << getOperand()->toRef()
       << " to " << DestType->toString();

    return ss.str();
}

// ===== BasicBlock Implementation =====
void BasicBlock::addInstruction(unique_ptr<Instruction> inst)
{
    Instructions.push_back(std::move(inst));
}
void BasicBlock::insertBeforeTerminator(std::unique_ptr<Instruction> inst)
{
    auto &insts = getInstructions();
    auto termIt = std::find_if(
        insts.begin(), insts.end(),
        [](const std::unique_ptr<Instruction> &i)
        { return i->isTerminator(); });
    this->insert(std::move(inst), termIt - insts.begin());
}
void BasicBlock::insert(unique_ptr<Instruction> inst, unsigned index)
{
    if (index > Instructions.size())
    {
        throw std::out_of_range("Index out of range for inserting instruction");
    }
    Instructions.insert(Instructions.begin() + index, std::move(inst));
}
void BasicBlock::addPredecessor(BasicBlock *pred)
{
    if (std::find(Predecessors.begin(), Predecessors.end(), pred) == Predecessors.end())
    {
        Predecessors.push_back(pred);
    }
}
void BasicBlock::addSuccessor(BasicBlock *succ)
{
    if (std::find(Successors.begin(), Successors.end(), succ) == Successors.end())
    {
        Successors.push_back(succ);
    }
}
void BasicBlock::removePredecessor(BasicBlock *pred)
{
    Predecessors.erase(std::remove(Predecessors.begin(), Predecessors.end(), pred),
                       Predecessors.end());
}
void BasicBlock::removeSuccessor(BasicBlock *succ)
{
    Successors.erase(std::remove(Successors.begin(), Successors.end(), succ),
                     Successors.end());
}
Instruction *BasicBlock::getTerminator()
{
    return Instructions.empty() ? nullptr : Instructions.back().get();
}
vector<unique_ptr<Instruction>> &BasicBlock::getInstructions()
{
    return Instructions;
}
const vector<BasicBlock *> &BasicBlock::getPredecessors() const
{
    return Predecessors;
}
const vector<BasicBlock *> &BasicBlock::getSuccessors() const
{
    return Successors;
}
void BasicBlock::setLiveIn(const std::set<std::string> &liveIn)
{
    LiveIn = liveIn;

} // 设置活跃变量集合
const std::set<std::string> &BasicBlock::getLiveIn() const
{
    return LiveIn;
} // 获取活跃变量集合
void BasicBlock::setLiveOut(const std::set<std::string> &liveOut)
{
    LiveOut = liveOut;
} // 设置活跃变量集合
const std::set<std::string> &BasicBlock::getLiveOut() const
{
    return LiveOut;
} // 获取活跃变量集合
bool BasicBlock::hasTerminator()
{
    Instruction *term = getTerminator();
    return term && (term->Op == Opcode::Ret || term->Op == Opcode::Br);
}
bool BasicBlock::containsByName(Instruction *inst) const
{
    const std::string &name = inst->getName();
    return std::any_of(Instructions.begin(), Instructions.end(),
                       [&](const unique_ptr<Instruction> &i)
                       {
                           return i->getName() == name;
                       });
}
int BasicBlock::getInstructionOrder(Instruction *inst) const
{
    for (int i = 0; i < Instructions.size(); ++i)
    {
        if (Instructions[i].get() == inst)
        {
            return i;
        }
    }
    return -1; // Not found
}
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
BasicBlock *Function::addBasicBlock(const string &name)
{
    auto bb = make_unique<BasicBlock>(name, this);
    BasicBlock *ptr = bb.get();
    BasicBlocks.push_back(std::move(bb));
    return ptr;
}
Argument *Function::addArgument(Type *type, const string &name)
{
    auto arg = make_unique<Argument>(type, Arguments.size(), name, this);
    Argument *ptr = arg.get();
    Arguments.push_back(std::move(arg));
    return ptr;
}
const vector<unique_ptr<Argument>> &Function::getArguments() const
{
    return Arguments;
}
const vector<Argument *> Function::getIntArguments() const
{
    vector<Argument *> intArgs;
    for (const auto &arg : Arguments)
    {
        if (arg->getType()->isIntegerTy())
        {
            intArgs.push_back(arg.get());
        }
    }
    return intArgs;
}
const vector<Argument *> Function::getFloatArguments() const
{
    vector<Argument *> floatArgs;
    for (const auto &arg : Arguments)
    {
        if (arg->getType()->isFloatTy())
        {
            floatArgs.push_back(arg.get());
        }
    }
    return floatArgs;
}
const vector<Argument *> Function::getPtrArguments() const
{
    vector<Argument *> ptrArgs;
    for (const auto &arg : Arguments)
    {
        if (arg->getType()->isPointerTy())
        {
            ptrArgs.push_back(arg.get());
        }
    }
    return ptrArgs;
}
const vector<Loop> &Function::getLoops()const
{
    return Loops;
}
void Function::setLoops(const vector<Loop> &loops)
{
    Loops = loops;
}
BasicBlock *Function::getEntryBlock()
{
    return BasicBlocks.empty() ? nullptr : BasicBlocks[0].get();
}
FunctionType *Function::getFunctionType()
{
    return static_cast<FunctionType *>(getType());
}
vector<unique_ptr<BasicBlock>> &Function::getBasicBlocks()
{
    return BasicBlocks;
}
unsigned Function::getInstructionCount() const
{
    unsigned count = 0;
    for (const auto &bb : BasicBlocks)
    {
        count += bb->getInstructions().size();
    }
    return count;
}
bool Function::isLibraryFunction() const
{
    return getName() == "getint" || getName() == "getch" ||
           getName() == "getfloat" || getName() == "getarray" ||
           getName() == "getfarray" || getName() == "putint" ||
           getName() == "putch" || getName() == "putfloat" ||
           getName() == "putarray" || getName() == "putfarray" ||
           getName() == "putf" ||
           getName() == "_sysy_starttime" || getName() == "_sysy_stoptime";
}
bool Function::isRecursive() const
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
void Function::setDeleted(bool deleted)
{
    isDeleted = deleted;
}
bool Function::isDeletedFunction() const
{
    return isDeleted;
}
bool Function::shouldBeOutput() const
{
    // 如果是库函数或被标记为删除，则不输出
    return !isLibraryFunction() && !isDeletedFunction();
}
vector<size_t>Function::getIndexOfNotUsedArguments() const
{
    vector<size_t> notUsedArgs;
    for (size_t i = 0; i < Arguments.size(); ++i)
    {
        bool used=Arguments[i]->getUsers().size() > 0; // 如果参数有用户，则认为被使用
        if (!used)
        {
            notUsedArgs.push_back(i);
        }
    }
    return notUsedArgs;
}
std::string Function::toString() const
{
    std::stringstream ss;

    // 函数签名
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

    // 基本块内容
    for (const auto &bb : BasicBlocks)
    {
        ss << bb->toString();
    }

    ss << "}\n";

    return ss.str();
}

// ===== Module Implementation =====
Function *Module::addFunction(FunctionType *funcType, const string &name)
{
    auto func = make_unique<Function>(funcType, name, this);
    Function *ptr = func.get();
    Functions.push_back(std::move(func));
    return ptr;
}
GlobalVariable *Module::addGlobalVariable(Type *type, const string &name,
                                          Constant *initializer, bool isConstant)
{
    auto global = make_unique<GlobalVariable>(type, name, initializer, isConstant);
    GlobalVariable *ptr = global.get();
    GlobalVariables.push_back(std::move(global));
    return ptr;
}
Function *Module::getFunction(const string &name)
{
    // 遇到stoptime和starttime时添加前缀_sysy_以匹配SysY标准库函数
    string tmp_name = (name == "starttime" || name == "stoptime") ? "_sysy_" + name : name;
    for (auto &func : Functions)
    {
        if (func->getName() == tmp_name)
        {
            return func.get();
        }
    }
    return nullptr;
}
GlobalVariable *Module::getGlobalVariable(const string &name)
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
// Debug
string Module::getBasicBlockInfo()
{
    std::stringstream ss;
    for (int i = 13; i < Functions.size(); i++)
    {
        ss << Functions[i]->getName() << ":" << std::endl;
        for (const auto &it : Functions[i]->BasicBlocks)
        {
            ss << "BasicBlockSuccs " << it->getName() << ":" << std::endl;
            ss << "                   Successors: ";
            for (auto suc : it->getSuccessors())
            {
                ss << suc->getName() << " ";
            }
            ss << std::endl;
            ss << "                   Predecessors: ";
            for (auto pre : it->getPredecessors())
            {
                ss << pre->getName() << " ";
            }
            ss << std::endl;
        }
    }
    return ss.str();
}
std::string Module::toString() const
{
    std::stringstream ss;

    ss << "; ModuleID = '" << Name << "'\n\n";

    // 全局变量
    for (const auto &gv : GlobalVariables)
    {
        ss << gv->toString() << "\n";
    }

    if (!GlobalVariables.empty())
    {
        ss << "\n";
    }
    for (const auto &func : Functions)
    {
        if (func->shouldBeOutput())
        {
            ss << func->toString() << "\n";
        }
    }
    return ss.str();
}
