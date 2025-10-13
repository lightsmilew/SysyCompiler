#include "IRDataStructure.h"
#include <sstream>
#include <algorithm>
#include <regex>
// 辅助函数：去除 _inl\d+ 后缀
std::string normalizeName(const std::string &name)
{
    static std::regex inl_regex("(_inl\\d+)");
    return std::regex_replace(name, inl_regex, "");
}
// 根据归一化后的名称判断两个指针是否有可能指向同一个地址
bool isSameAddr(Value *a, Value *b)
{
    return normalizeName(a->getName()) == normalizeName(b->getName());
}
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
    case Opcode::And:
        return "and";
    case Opcode::Or:
        return "or";
    case Opcode::Xor:
        return "xor";
    case Opcode::Xnor:
        return "xnor";
    case Opcode::Muld:
        return "muld";
    case Opcode::Mulhd:
        return "mulhd";
    case Opcode::Slld:
        return "slld";
    case Opcode::Srad:
        return "srad";
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
    case Opcode::Stored:
        return "stored";
    case Opcode::GetElementPtr:
        return "getelementptr";
    case Opcode::SIToFP:
        return "sitofp";
    case Opcode::FPToSI:
        return "fptosi";
    case Opcode::BitCast:
        return "bitcast";
    case Opcode::Sext:
        return "sext";
    case Opcode::Trunc:
        return "trunc";
    case Opcode::Call:
        return "call";
    case Opcode::Phi:
        return "phi";
    case Opcode::Copy:
        return "copy";
    case Opcode::Select:
        return "select";
    default:
        throw std::runtime_error("Unknown opcode");
    }
}
bool Instruction::isBinaryOp() const
{
    return Op == Opcode::Add || Op == Opcode::Sub || Op == Opcode::Mul ||
           Op == Opcode::SDiv || Op == Opcode::SRem || Op == Opcode::FAdd ||
           Op == Opcode::FSub || Op == Opcode::FMul || Op == Opcode::FDiv || Op == Opcode::And || Op == Opcode::Or ||
           Op == Opcode::Sll || Op == Opcode::Sra || Op == Opcode::Muld || Op == Opcode::Mulhd || Op == Opcode::Slld ||
           Op == Opcode::Srad || Op == Opcode::Xor || Op == Opcode::Xnor;
}
bool Instruction::isCommutativeOp() const
{
    return Op == Opcode::Add || Op == Opcode::Mul || Op == Opcode::And || Op == Opcode::Or || Op == Opcode::Xor ||
           Op == Opcode::Xnor || Op == Opcode::Muld || Op == Opcode::Mulhd || Op == Opcode::FAdd || Op == Opcode::FMul;
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
    if (Op == Opcode::Store ||
        Op == Opcode::Stored ||
        Op == Opcode::Br ||
        Op == Opcode::Ret ||
        Op == Opcode::Alloca)
    {
        return true;
    }
    else if (Op == Opcode::Call)
    {
        return dynamic_cast<const CallInst *>(this)->ifHasSideEffects();
    }
    return false; // 其他指令通常没有副作用
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
    case Opcode::Sll:
    case Opcode::Sra:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Xor:
    case Opcode::Xnor:
    case Opcode::Muld:
    case Opcode::Mulhd:
    case Opcode::Slld:
    case Opcode::Srad:
    case Opcode::ICmp:
    case Opcode::FCmp:
    case Opcode::Alloca:
    case Opcode::Load:
    case Opcode::Call:
    case Opcode::Phi:
    case Opcode::GetElementPtr:
    case Opcode::SIToFP:
    case Opcode::FPToSI:
    case Opcode::BitCast:
    case Opcode::Sext:
    case Opcode::Trunc:
    case Opcode::Copy:
    case Opcode::Select:
        return true;
    default:
        return false; // Ret, Br, Store等没有结果
    }
}
bool Instruction::hasExternalUse(const Loop &loop) const
{
    std::set<const Instruction *> visited;
    return hasExternalUse(loop, &visited);
}
bool Instruction::hasExternalUse(const Loop &loop, std::set<const Instruction *> *visited) const
{
    if (!visited)
    {
        std::set<const Instruction *> localVisited;
        return hasExternalUse(loop, &localVisited);
    }
    if (visited->count(this))
        return false; // 已访问，防止死循环
    visited->insert(this);

    for (auto *user : this->getUsers())
    {
        if (auto *phiInst = dynamic_cast<PhiInst *>(user))
        {
            // 递归判断 phi 的使用者
            if (phiInst->hasExternalUse(loop, visited))
                return true;
        }
        else if (auto *inst = dynamic_cast<Instruction *>(user))
        {
            // 非 phi 指令，判断是否在循环外
            if (!loop.containsInst(inst))
            {
                return true;
            }
            // 如果在循环内，不递归
        }
    }
    return false;
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
    case Opcode::Sll:
    case Opcode::Sra:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Xor:
    case Opcode::Xnor:
    case Opcode::Muld:
    case Opcode::Mulhd:
    case Opcode::Slld:
    case Opcode::Srad:
        // 对于二元操作符，直接创建新的BinaryOperator实例
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
    case Opcode::Stored:
        return new StoreInst(getOpcode(), getOperandByIndex(0), getOperandByIndex(1));
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
    case Opcode::BitCast:
    case Opcode::Sext:
    case Opcode::Trunc:
    {
        auto *cast = static_cast<const CastInst *>(this);
        return new CastInst(Op, getOperandByIndex(0), cast->DestType, getName());
    }
    case Opcode::Copy:
        return new CopyInst(getOperandByIndex(0), getName());
    case Opcode::Select:
    {
        auto *select = static_cast<const SelectInst *>(this);
        return new SelectInst(select->getCondition(), select->getTrueValue(), select->getFalseValue(), getName());
    }
    default:
        throw std::runtime_error("Clone not implemented for this opcode");
    }
}

void BinaryOperator::exchangeOperands()
{
    if (isCommutativeOp())
    {
        std::swap(Operands[0], Operands[1]);
    }
    else
    {
        throw std::runtime_error("exchangeOperands can only be called on commutative operators");
    }
}
std::string BinaryOperator::toString() const
{
    std::stringstream ss;
    std::string opStr;

    opStr = getOpcodeName(); // 使用getOpcodeName获取操作码名称

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

void ICmpInst::exchangeOperands()
{
    // 交换左右操作数
    std::swap(Operands[0], Operands[1]);

    // 调整谓词
    switch (Pred)
    {
    case ICMP_EQ:
        // 相等不变
        break;
    case ICMP_NE:
        // 不等不变
        break;
    case ICMP_SLT:
        Pred = ICMP_SGT;
        break;
    case ICMP_SLE:
        Pred = ICMP_SGE;
        break;
    case ICMP_SGT:
        Pred = ICMP_SLT;
        break;
    case ICMP_SGE:
        Pred = ICMP_SLE;
        break;
    default:
        throw std::runtime_error("Unknown ICmp predicate");
    }
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

void FCmpInst::exchangeOperands()
{
    // 交换左右操作数
    std::swap(Operands[0], Operands[1]);

    // 调整谓词
    switch (Pred)
    {
    case FCMP_OEQ:
        // 相等不变
        break;
    case FCMP_ONE:
        // 不等不变
        break;
    case FCMP_OLT:
        Pred = FCMP_OGT;
        break;
    case FCMP_OLE:
        Pred = FCMP_OGE;
        break;
    case FCMP_OGT:
        Pred = FCMP_OLT;
        break;
    case FCMP_OGE:
        Pred = FCMP_OLE;
        break;
    default:
        throw std::runtime_error("Unknown FCmp predicate");
    }
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
bool AllocaInst::getIsInitialized() const
{
    return IsInitialized;
}
void AllocaInst::setIsInitialized(bool isInit)
{
    IsInitialized = isInit;
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
Type *AllocaInst::getGroundElementType() const
{
    // 获取数组的基本元素类型
    if (auto arrayType = dynamic_cast<ArrayType *>(AllocatedType))
    {
        return arrayType->getGroundElementType();
    }
    return AllocatedType; // 如果不是数组类型，返回自身
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
Value *LoadInst::getDest() const
{
    return const_cast<LoadInst *>(this);
}
Value *LoadInst::getPointer() const
{
    return getOperandByIndex(0);
}
Value *LoadInst::getOriginalPointer() const
{
    // 获取原始存储指针(用于gep展开时递归获取最上层指针)
    Value *pointer = getPointer();
    while (true)
    {
        if (auto gepInst = dynamic_cast<GetElementPtrInst *>(pointer))
        {
            pointer = gepInst->getOriginalPointerOperand();
        }
        else if (auto castInst = dynamic_cast<CastInst *>(pointer))
        {
            pointer = castInst->getOperand();
        }
        else
        {
            break;
        }
    }
    return pointer;
}
std::string LoadInst::toString() const
{
    std::stringstream ss;
    ss << "%" << getName() << " = load " << getType()->toString()
       << ", " << getPointer()->getType()->toString() << " " << getPointer()->toRef();
    return ss.str();
}

Value *StoreInst::getValueToStore() const
{
    return getOperandByIndex(0);
}
Value *StoreInst::getPointer() const
{
    return getOperandByIndex(1);
}
Value *StoreInst::getOriginalPointer() const
{
    // 获取原始存储指针(用于gep展开时递归获取最上层指针)
    Value *pointer = getPointer();
    while (true)
    {
        if (auto gepInst = dynamic_cast<GetElementPtrInst *>(pointer))
        {
            pointer = gepInst->getOriginalPointerOperand();
        }
        else if (auto castInst = dynamic_cast<CastInst *>(pointer))
        {
            pointer = castInst->getOperand();
        }
        else
        {
            break;
        }
    }
    return pointer;
}
std::string StoreInst::toString() const
{
    std::stringstream ss;
    string opcodeStr = getOpcodeName(); // 使用getOpcodeName获取操作码名称
    ss << opcodeStr << " " << getValueToStore()->getType()->toString() << " " << getValueToStore()->toRef()
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
bool CallInst::hasReturnValue() const
{
    return !getType()->isVoidTy();
}
bool CallInst::HasModifiedArray(Value *value) const
{
    Function *func = getCalledFunction();
    if (!func)
        throw std::runtime_error("CallInst: function is null");
    // 该变量作为函数实参并在函数体内对对应的形参进行了修改
    for (size_t i = 0; i < getArguments().size(); i++)
    {
        Value *origin = getArguments()[i];
        while (true)
        {
            if (auto gepInst = dynamic_cast<GetElementPtrInst *>(origin))
            {
                origin = gepInst->getOriginalPointerOperand();
            }
            else if (auto castInst = dynamic_cast<CastInst *>(origin))
            {
                origin = castInst->getOperand();
            }
            else
            {
                break;
            }
        }
        // 数组作为函数参数
        if (isSameAddr(origin, value))
        {
            string funcName = func->getName();
            if (!func->isLibraryFunction() && origin->getName() == func->getArgumentByIndex(i)->getName())
                continue; // 跳过自身，避免无限递归
            if (func->isLibraryFunction() && (funcName == "getarray" || funcName == "getfarray"))
                return true; // 特例：getarray和getfarray函数会修改对应位置的形参
            // 检查该函数是否修改了对应位置的形参
            if (!func->isLibraryFunction() && HasModifiedArray(func->getArgumentByIndex(i)))
            {
                return true;
            }
        }
    }
    for (auto &bb : func->getBasicBlocks())
    {
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (auto storeInst = dynamic_cast<StoreInst *>(inst))
            {
                auto storeOriginalPointer = storeInst->getOriginalPointer();
                if (isSameAddr(storeOriginalPointer, value))
                {
                    return true; // 找到修改该全局变量的store指令
                }
            }
            else if (auto callInst = dynamic_cast<CallInst *>(inst))
            {
                // 如果是递归函数就直接跳过(已检查过)
                if (callInst->getCalledFunction() == func)
                {
                    continue;
                }
                // 递归检查被调用的函数是否修改了全局变量
                if (callInst->HasModifiedArray(value))
                {
                    return true;
                }
            }
        }
    }
    return false; // 如果没有找到修改该全局变量的指令
}
bool CallInst::HasUsedArray(Value *ptr) const
{
    Function *func = getCalledFunction();
    if (!func)
        throw std::runtime_error("CallInst: function is null");

    // 1. 检查参数传递
    for (size_t i = 0; i < getArguments().size(); i++)
    {
        Value *origin = getArguments()[i];
        while (true)
        {
            if (auto gepInst = dynamic_cast<GetElementPtrInst *>(origin))
            {
                origin = gepInst->getOriginalPointerOperand();
            }
            else if (auto castInst = dynamic_cast<CastInst *>(origin))
            {
                origin = castInst->getOperand();
            }
            else
            {
                break;
            }
        }
        if (isSameAddr(origin, ptr))
        {
            std::string funcName = func->getName();
            if (!func->isLibraryFunction() && origin->getName() == func->getArgumentByIndex(i)->getName())
                continue; // 跳过自身，避免无限递归
            if (func->isLibraryFunction() && (funcName == "putint" || funcName == "putfloat" || funcName == "putch" || funcName == "putarray" || funcName == "putfarray" || funcName == "putf"))
                return true;
            // 检查该函数是否“使用”对应形参
            else if (!func->isLibraryFunction() && HasUsedArray(func->getArgumentByIndex(i)))
            {
                return true;
            }
        }
    }

    // 2. 检查函数体内是否有load或递归调用
    for (auto &bb : func->getBasicBlocks())
    {
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (auto loadInst = dynamic_cast<LoadInst *>(inst))
            {
                auto loadOriginalPointer = loadInst->getOriginalPointer();
                if (isSameAddr(loadOriginalPointer, ptr))
                {
                    return true; // 找到对该数组的load
                }
            }
            else if (auto callInst = dynamic_cast<CallInst *>(inst))
            {
                // 跳过递归
                if (callInst->getCalledFunction() == func)
                    continue;
                // 递归检查被调用函数是否“使用”该数组
                if (callInst->HasUsedArray(ptr))
                {
                    return true;
                }
            }
        }
    }
    return false;
}
bool CallInst::ifHasSideEffects() const
{
    // 判断是否有副作用
    if (getCalledFunction() == nullptr)
        return false; // 如果没有调用函数，则没有副作用
    // 如果是库函数则有副作用
    Function *func = getCalledFunction();
    if (func->isLibraryFunction())
        return true;
    // 检查参数中是否有指针类型的参数
    for (Value *arg : getArguments())
    {
        if (arg->getType()->isPointerTy())
            return true; // 如果有指针类型参数，则可能有副作用
    }
    // 检查函数体内是否有store指令
    for (auto &bb : func->getBasicBlocks())
    {
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (auto storeInst = dynamic_cast<StoreInst *>(inst))
            {
                auto storeOriginalPointer = storeInst->getOriginalPointer();
                // 如果store指令的原始指针是函数参数或全局变量，则有副作用
                if (std::find(getArguments().begin(), getArguments().end(), storeOriginalPointer) != getArguments().end() ||
                    (storeOriginalPointer->isGlobal()))
                {
                    return true; // 找到修改全局变量或函数参数的store指令
                }
            }
            else if (auto callInst = dynamic_cast<CallInst *>(inst))
            {
                // 如果是递归函数就直接跳过(已检查过)
                if (callInst->getCalledFunction() == func)
                {
                    continue;
                }
                // 递归检查被调用的函数是否有副作用
                if (callInst->ifHasSideEffects())
                {
                    return true; // 如果被调用的函数有副作用，则当前调用也有副作用
                }
            }
        }
    }
    // 如果没有找到任何副作用的迹象，则认为没有副作用
    return false; // 否则没有副作用
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

Value *ReturnInst::getReturnValue() const
{
    if (getNumOperands() > 0)
    {
        return getOperandByIndex(0);
    }
    return nullptr;
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

bool BranchInst::isConditional() const
{
    return getNumOperands() > 0;
}
Value *BranchInst::getCondition() const
{
    if (isConditional())
    {
        return getOperandByIndex(0);
    }
    return nullptr;
}
BasicBlock *BranchInst::getTrueBlock() const
{
    return TrueBlock;
} // 获取真分支基本块
BasicBlock *BranchInst::getFalseBlock() const
{
    return FalseBlock;
} // 获取假分支基本块
void BranchInst::setTrueBlock(BasicBlock *block)
{
    TrueBlock = block;
} // 设置真分支基本块
void BranchInst::setFalseBlock(BasicBlock *block)
{
    FalseBlock = block;
} // 设置假分支基本块
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
void PhiInst::setIncomingValue(unsigned index, Value *value)
{
    if (index < getNumOperands())
    {
        // 设置操作数
        setOperandByIndex(index, value);
    }
    else
    {
        throw std::out_of_range("Invalid incoming value index");
    }
}
void PhiInst::removeIncoming(unsigned index)
{
    if (index < IncomingValues.size())
    {
        // 从操作数列表中删除对应的值
        removeOperandByIndex(index);
        // 删除基本块的使用关系
        IncomingValues[index]->removeUser(this);
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
void PhiInst::replaceIncomingBasicBlock(BasicBlock *oldBlock, BasicBlock *newBlock)
{
    auto it = std::find(IncomingValues.begin(), IncomingValues.end(), oldBlock);
    if (it != IncomingValues.end())
    {
        unsigned index = std::distance(IncomingValues.begin(), it);
        IncomingValues[index] = newBlock;
        newBlock->addUser(this);    // 添加到BasicBlock的用户列表中
        oldBlock->removeUser(this); // 从旧块的用户列表中移除
    }
    else
    {
        throw std::runtime_error("PhiInst: old block not found in incoming blocks");
    }
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
vector<BasicBlock *> PhiInst::getIncomingBlocks() const
{
    return IncomingValues; // 返回所有IncomingValues
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
// ===== SelectInst Implementation =====
std::string SelectInst::toString() const
{
    std::stringstream ss;
    ss << "%" << getName() << " = select " << getCondition()->getType()->toString()
       << " " << getCondition()->toRef() << ", " << getTrueValue()->toRef()
       << ", " << getFalseValue()->toRef();
    return ss.str();
}
// ===== GetElementPtrInst Implementation =====
Value *GetElementPtrInst::getPointerOperand() const
{
    return getOperandByIndex(0);
}
Value *GetElementPtrInst::getOriginalPointerOperand() const
{
    Value *ptr = getPointerOperand();
    if (auto gep = dynamic_cast<GetElementPtrInst *>(ptr))
    {
        // 如果是嵌套的GetElementPtrInst，递归获取原始指针
        return gep->getOriginalPointerOperand();
    }
    return ptr; // 返回最外层的指针操作数
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

    opStr = getOpcodeName(); // 使用getOpcodeName获取操作码名称

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
void BasicBlock::clearInstructions()
{
    Instructions.clear();
} // 清空指令列表
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
bool BasicBlock::containsByName(const std::string &name) const
{
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
void BasicBlock::removeSelfBasicBlock()
{
    // 先将自身从前驱的后继中和后继的前驱中删除
    for (auto *pred : Predecessors)
    {
        pred->removeSuccessor(this);
        removePredecessor(pred);
    }
    for (auto *succ : Successors)
    {
        succ->removePredecessor(this);
        removeSuccessor(succ);
    }
}
void BasicBlock::setParent(Function *parent)
{
    Parent = parent;
} // 设置父函数
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
// ===== Loop Implementation =====
bool Loop::containsInst(Instruction *inst) const
{
    return std::any_of(blocks.begin(), blocks.end(),
                       [&](BasicBlock *bb)
                       { return bb->containsByName(inst->getName()); });
}
bool Loop::containsBlock(BasicBlock *bb) const
{
    return std::find(blocks.begin(), blocks.end(), bb) != blocks.end();
}
bool Loop::containsInBody(Instruction *inst) const
{
    // 判断指令是否在循环体内(不包括header)
    return std::any_of(blocks.begin(), blocks.end(),
                       [&](BasicBlock *bb)
                       {
                           if (bb == header)
                               return false;
                           return bb->containsByName(inst->getName());
                       });
}
Value *Loop::getLoopCondition() const
{
    BasicBlock *headerBlock = header;
    Instruction *terminator = headerBlock->getTerminator();
    if (auto brInst = dynamic_cast<BranchInst *>(terminator))
    {
        if (brInst->isConditional())
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
bool Loop::IsInductionVar(const std::string &name) const
{
    for (auto &inst : header->getInstructions())
    {
        if (inst->getName() == name)
        {
            if (auto phiInst = dynamic_cast<PhiInst *>(inst.get()))
            {
                return true; // 如果是Phi指令，说明是归纳变量
            }
        }
    }
    return false; // 如果没有找到Phi指令，说明不是归纳变量
}
BasicBlock *Loop::getPreheader() const
{
    // 寻找前驱基本块
    for (auto &bb : header->getPredecessors())
    {
        // 如果不在blocks中
        if (std::find(blocks.begin(), blocks.end(), bb) == blocks.end())
        {
            return bb;
        }
    }
    throw std::runtime_error("Loop does not have a preheader.");
}
void Loop::breakCFG()
{
    for (auto bb : blocks)
    {
        // 删除前驱和后继的连接
        bb->removeSelfBasicBlock();
    }
}
// ===== Function Implementation =====
BasicBlock *Function::addBasicBlock(const string &name)
{
    auto bb = make_unique<BasicBlock>(name, this);
    BasicBlock *ptr = bb.get();
    BasicBlocks.push_back(std::move(bb));
    return ptr;
}
void Function::addBasicBlock(unique_ptr<BasicBlock> bb)
{
    BasicBlock *ptr = bb.get();
    BasicBlocks.push_back(std::move(bb));
    ptr->setParent(this);
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
Value *Function::getArgumentByIndex(size_t index) const
{
    if (index < Arguments.size())
    {
        return Arguments[index].get();
    }
    throw std::out_of_range("Function: Argument index out of range");
}
const vector<Loop> &Function::getLoops() const
{
    return Loops;
}
void Function::setLoops(const vector<Loop> &loops)
{
    Loops = loops;
}
Module *Function::getParent() const
{
    return Parent;
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
vector<BasicBlock *> Function::getExitBlocks() const
{
    std::vector<BasicBlock *> exits;
    for (const auto &bbPtr : BasicBlocks)
    {
        BasicBlock *bb = bbPtr.get();
        if (!bb->getInstructions().empty())
        {
            Instruction *term = bb->getTerminator();
            if (term && term->getOpcode() == Opcode::Ret)
            {
                exits.push_back(bb);
            }
        }
    }
    return exits;
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
vector<size_t> Function::getIndexOfNotUsedArguments() const
{
    vector<size_t> notUsedArgs;
    for (size_t i = 0; i < Arguments.size(); ++i)
    {
        bool used = Arguments[i]->getUsers().size() > 0; // 如果参数有用户，则认为被使用
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
        // 暂时先把内联后的函数也输出，用于调试
        // if(!func->isLibraryFunction())
        // {
        //     ss << func->toString() << "\n";
        // }
    }
    return ss.str();
}
