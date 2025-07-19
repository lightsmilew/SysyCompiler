#include "InstructionSelector.h"
using namespace RISCV;

shared_ptr<RISCVRegister> InstructionSelector::getOrCreateVirtualReg(Value *value)
{
    // 立即数
    if (auto constantIntValue = dynamic_cast<ConstantInt *>(value))
    {
        return LiInt(constantIntValue->Value);
    }
    else if (auto constantFloatValue = dynamic_cast<ConstantFloat *>(value))
    {
        return LiFloat(constantFloatValue->Value);
    }
    // 全局变量
    else if (auto globlVar = dynamic_cast<GlobalVariable *>(value))
    {
        return LaGlobl(globlVar);
    }

    // 变量
    auto valueName = value->getName();
    if (registerMap.find(valueName) != registerMap.end())
    {
        return registerMap[valueName];
    }
    else
    {
        auto virtualReg = make_shared<RISCVRegister>(RegisterType::VIRTUAL);
        registerMap[valueName] = virtualReg;
        return virtualReg;
    }

    return nullptr;
}

shared_ptr<RISCVRegister> InstructionSelector::getTempReg()
{
    auto tempReg = make_shared<RISCVRegister>(RegisterType::VIRTUAL);
    tempRegisters.push_back(tempReg);
    return tempReg;
}

shared_ptr<RISCVRegister> InstructionSelector::LaGlobl(GlobalVariable *globlvar)
{
    auto globReg = getTempReg();
    auto laInst = RISCVInstruction::createPseudoLA(globReg, globlvar->getName());

    return globReg;
}

shared_ptr<RISCVRegister> InstructionSelector::LiInt(int value)
{

    auto destReg = getTempReg();
    auto LiInst = RISCVInstruction::createPseudoLI(destReg, value);
    currentBB->addInstruction(LiInst);

    return destReg;
}

shared_ptr<RISCVRegister> InstructionSelector::LiFloat(float floatValue)
{
    auto tmpReg = getTempReg();
    uint32_t hexValue;
    memcpy(&hexValue, &floatValue, sizeof(floatValue));
    auto LiInst = RISCVInstruction::createPseudoLI(tmpReg, hexValue);
    currentBB->addInstruction(LiInst);

    auto destReg = getTempReg();
    auto FmvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_W_X, destReg, tmpReg);
    currentBB->addInstruction(FmvInst);

    return destReg;
}

void InstructionSelector::selectInstructions(shared_ptr<RISCVFunction> func, Function *irFunc)
{
    currentFunc = func;
    irFunction = irFunc;

    // 创建虚拟寄存器映射表
    registerMap.clear();

    // 处理函数体
    for (size_t i = 0; i < irFunc->BasicBlocks.size(); ++i)
    {
        auto irBB = irFunc->BasicBlocks[i].get();
        auto riscvBB = func->getBasicBlock(irBB->getName());
        currentBB = riscvBB;

        // 遍历基本块中的所有指令
        for (auto &irInstr : irBB->Instructions)
        {
            visitInstruction(irInstr.get());
        }
    }
}

// 当基本块中使用alloca指令访问函数参数时，我应该将该块空间与寄存器联合起来
void InstructionSelector::visitInstruction(Instruction *inst)
{
    switch (inst->Op)
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
        if (auto binOp = dynamic_cast<BinaryOperator *>(inst))
        {
            visitBinaryOp(binOp);
        }
        break;
    case Opcode::Load:
        if (auto loadInst = dynamic_cast<LoadInst *>(inst))
        {
            visitLoadInst(loadInst);
        }
        break;
    case Opcode::Store:
        if (auto storeInst = dynamic_cast<StoreInst *>(inst))
        {
            visitStoreInst(storeInst);
        }
        break;
    case Opcode::Call:
        if (auto callInst = dynamic_cast<CallInst *>(inst))
        {
            visitCallInst(callInst);
        }
        break;
    case Opcode::Ret:
        if (auto retInst = dynamic_cast<ReturnInst *>(inst))
        {
            visitReturnInst(retInst);
        }
        break;
    case Opcode::Br:
        if (auto brInst = dynamic_cast<BranchInst *>(inst))
        {
            visitBranchInst(brInst);
        }
        break;
    case Opcode::Alloca:
        if (auto allocaInst = dynamic_cast<AllocaInst *>(inst))
        {
            visitAllocaInst(allocaInst);
        }
        break;
    case Opcode::GetElementPtr:
        if (auto gepInst = dynamic_cast<GetElementPtrInst *>(inst))
        {
            visitElementPtrInst(gepInst);
        }
        break;
    case Opcode::ICmp:
        if (auto icmpInst = dynamic_cast<ICmpInst *>(inst))
        {
            visitICmpInst(icmpInst);
        }
        break;
    case Opcode::FCmp:
        if (auto fcmpInst = dynamic_cast<FCmpInst *>(inst))
        {
            visitFCmpInst(fcmpInst);
        }
        break;
    case Opcode::SIToFP:
        if (auto castInst = dynamic_cast<CastInst *>(inst))
        {
            visitSIToFPInst(castInst);
        }
        break;
    case Opcode::FPToSI:
        if (auto castInst = dynamic_cast<CastInst *>(inst))
        {
            visitFPToSIInst(castInst);
        }
        break;
    case Opcode::Copy:
        if (auto copyInst = dynamic_cast<CopyInst *>(inst))
        {
            visitCopyInst(copyInst);
        }
        break;
    default:
        // 其他指令暂时忽略
        break;
    }
}

void InstructionSelector::visitBinaryOp(BinaryOperator *inst)
{
    auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
    auto rhsReg = getOrCreateVirtualReg(inst->getRHS());

    auto destReg = getOrCreateVirtualReg(inst->getDest());

    RISCVOpcode opcode;

    switch (inst->Op)
    {
    case Opcode::Add:
        opcode = RISCVOpcode::ADD;
        break;
    case Opcode::Sub:
        opcode = RISCVOpcode::SUB;
        break;
    case Opcode::Mul:
        opcode = RISCVOpcode::MUL;
        break;
    case Opcode::SDiv:
        opcode = RISCVOpcode::DIV;
        break;
    case Opcode::SRem:
        opcode = RISCVOpcode::REM;
        break;
    case Opcode::FAdd:
        opcode = RISCVOpcode::FADD_S;
        break;
    case Opcode::FSub:
        opcode = RISCVOpcode::FSUB_S;
        break;
    case Opcode::FMul:
        opcode = RISCVOpcode::FMUL_S;
        break;
    case Opcode::FDiv:
        opcode = RISCVOpcode::FDIV_S;
        break;
    default:
        return;
    }

    // 生成计算指令
    auto riscvInst = RISCVInstruction::createRType(opcode, destReg, lhsReg, rhsReg);
    currentBB->addInstruction(riscvInst);
}

void InstructionSelector::visitLoadInst(LoadInst *inst)
{
    auto ptrReg = getOrCreateVirtualReg(inst->getPointer());
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    // 根据数据类型选择合适的加载指令
    RISCVOpcode loadOpcode = RISCVOpcode::LW;
    if (inst->getType()->isFloatTy())
    {
        loadOpcode = RISCVOpcode::FLW; // 浮点数加载
    }
    else if (inst->getType()->isPointerTy())
    {
        loadOpcode = RISCVOpcode::LD; // 指针加载
    }

    auto loadInst = RISCVInstruction::createIType(loadOpcode, destReg, ptrReg, 0);
    currentBB->addInstruction(loadInst);
}

void InstructionSelector::visitStoreInst(StoreInst *inst)
{
    auto valueReg = getOrCreateVirtualReg(inst->getValueToStore());
    auto ptrReg = getOrCreateVirtualReg(inst->getPointer());

    // 根据要存储的数据类型选择合适的存储指令
    RISCVOpcode storeOpcode = RISCVOpcode::SW;
    if (inst->getValueToStore()->getType()->isFloatTy())
    {
        storeOpcode = RISCVOpcode::FSW; // 浮点数存储
    }
    else if (inst->getValueToStore()->getType()->isPointerTy())
    {
        storeOpcode = RISCVOpcode::SD; // 指针存储
    }

    auto storeInst = RISCVInstruction::createSType(storeOpcode, ptrReg, valueReg, 0);
    currentBB->addInstruction(storeInst);
}

void InstructionSelector::visitAllocaInst(AllocaInst *inst)
{
    StackFrame stack = currentFunc->getStackFrame();
    stack.allocateValueSpace(inst->getName(), inst->getAllocatedSize());
    auto imm = LiInt(stack.getValueOffset(inst->getName()));
    auto addrReg = getOrCreateVirtualReg(inst->getDest());

    auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);
    auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, addrReg, spReg, imm);
    currentBB->addInstruction(addInst);
}

void InstructionSelector::visitElementPtrInst(GetElementPtrInst *inst)
{
    auto baseAddr = getOrCreateVirtualReg(inst->getPointerOperand());
    auto destReg = getOrCreateVirtualReg(inst->getDest());
    auto offsetReg = LiInt(1);
    auto totalOffsetReg = LiInt(0);
    auto tmpReg = getTempReg();
    auto strideReg = getTempReg();

    auto indices = inst->getIndices();
    auto stridePtr = inst->getArrayStride();

    // 处理每个维度的索引
    for (int i = static_cast<int>(indices.size()) - 1; i >= 0; --i)
    {
        auto indexReg = getOrCreateVirtualReg(indices[i]);

        // totalOffset += offset * index * 4
        auto mulInst = RISCVInstruction::createRType(RISCVOpcode::MUL, tmpReg, indexReg, offsetReg);
        currentBB->addInstruction(mulInst);
        auto shiftInst = RISCVInstruction::createIType(RISCVOpcode::SLLI, tmpReg, tmpReg, 2); // 左移2位，相当于乘以4
        currentBB->addInstruction(shiftInst);
        auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, totalOffsetReg, totalOffsetReg, tmpReg);
        currentBB->addInstruction(addInst);

        // offset *= stride
        if (i == 0)
        {
            // 最后一个维度的偏移量不需要乘以stride
            break;
        }

        int stride = (*stridePtr)[i - 1];
        if (stride != 1)
        {
            auto liStrideInst = RISCVInstruction::createPseudoLI(strideReg, stride);
            currentBB->addInstruction(liStrideInst);
            auto mulStrideInst = RISCVInstruction::createRType(RISCVOpcode::MUL, offsetReg, offsetReg, strideReg);
            currentBB->addInstruction(mulStrideInst);
        }
    }

    auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, destReg, baseAddr, totalOffsetReg);
}

void InstructionSelector::visitCallInst(CallInst *inst)
{
    // 1. 处理参数传递 - 严格按照RISC-V ABI规范
    // 前8个整数/指针参数使用a0-a7，前8个浮点参数使用fa0-fa7
    // 超出的参数按顺序存放在调用者栈帧的参数区域
    int intArgIndex = 0;
    int floatArgIndex = 0;
    int argNum = 0;
    auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);
    auto stack = currentFunc->getStackFrame();

    // 逐个处理每个参数，避免同时占用过多临时寄存器
    for (auto arg : inst->getArguments())
    {
        shared_ptr<RISCVRegister> argReg = getOrCreateVirtualReg(arg);

        if (arg->getType()->isFloatTy())
        {
            if (floatArgIndex < 8)
            {
                // 浮点参数使用fa0-fa7
                auto paramReg = make_shared<RISCVRegister>(static_cast<RISCVRegister::PhysicalReg>(
                    static_cast<int>(RISCVRegister::PhysicalReg::FA0) + floatArgIndex));

                auto moveInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_S, paramReg, argReg);
                currentBB->addInstruction(moveInst);
                floatArgIndex++;
            }
            else
            {

                stack.allocateCalleeArgSpace(argNum);
                int offset = stack.getCalleeArgOffset(argNum);

                auto tempReg = getTempReg();
                auto liInst = RISCVInstruction::createPseudoLI(tempReg, offset);
                currentBB->addInstruction(liInst);
                auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, spReg, tempReg);
                currentBB->addInstruction(addInst);
                auto storeInst = RISCVInstruction::createSType(RISCVOpcode::FSW, tempReg, argReg, 0);

                floatArgIndex++;
            }
        }
        else
        {
            // 整数/指针参数
            if (intArgIndex < 8)
            {
                // 整数参数使用a0-a7
                auto paramReg = make_shared<RISCVRegister>(static_cast<RISCVRegister::PhysicalReg>(
                    static_cast<int>(RISCVRegister::PhysicalReg::A0) + intArgIndex));

                auto moveInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, paramReg, argReg);
                currentBB->addInstruction(moveInst);
                intArgIndex++;
            }
            else
            {
                bool isPtr = arg->getType()->isPointerTy();
                stack.allocateCalleeArgSpace(argNum, isPtr ? 8 : 4);
                int offset = stack.getCalleeArgOffset(argNum);

                auto tempReg = getTempReg();
                auto liInst = RISCVInstruction::createPseudoLI(tempReg, offset);
                currentBB->addInstruction(liInst);
                auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, spReg, tempReg);
                currentBB->addInstruction(addInst);
                auto storeInst = RISCVInstruction::createSType(isPtr ? RISCVOpcode::SD : RISCVOpcode::SW, tempReg, argReg, 0);
                intArgIndex++;
            }
        }
        argNum++;
    }

    // 2. 生成函数调用指令
    auto callInst = RISCVInstruction::createPseudoCALL(inst->getCalledFunction()->getName());
    currentBB->addInstruction(callInst);

    // 3. 处理返回值
    if (inst->hasReturnValue())
    {
        auto destReg = getOrCreateVirtualReg(inst->getDest());
        if (inst->getType()->isFloatTy())
        {
            auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_S, destReg, std::make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA0));
            currentBB->addInstruction(mvInst);
        }
        else
        {
            auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, destReg, std::make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A0));
            currentBB->addInstruction(mvInst);
        }
    }
}

void InstructionSelector::visitReturnInst(ReturnInst *inst)
{
    if (inst->getReturnValue())
    {
        // 有返回值，将值移动到返回寄存器
        auto valueReg = getOrCreateVirtualReg(inst->getReturnValue());
        auto returnReg = make_shared<RISCVRegister>(
            inst->getReturnValue()->getType()->isFloatTy() ? RISCVRegister::PhysicalReg::FA0 : RISCVRegister::PhysicalReg::A0);

        // 根据类型选择正确的移动指令
        RISCVOpcode moveOpcode = inst->getReturnValue()->getType()->isFloatTy() ? RISCVOpcode::FMV_S : RISCVOpcode::MV;
        auto moveInst = RISCVInstruction::createPseudo(moveOpcode, returnReg, valueReg);
        currentBB->addInstruction(moveInst);
    }

    // 对于其他函数，直接返回
    auto retInst = RISCVInstruction::createPseudoRET();
    currentBB->addInstruction(retInst);
}

void InstructionSelector::visitBranchInst(BranchInst *inst)
{
    if (inst->getCondition())
    {
        // 条件分支
        auto condReg = getOrCreateVirtualReg(inst->getCondition());

        if (condReg->getType() == RegisterType::FLOAT)
        {
            // 如果条件是浮点类型，需要先转换为整数
            auto intCondReg = getTempReg();
            auto ftoiInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_X_W, intCondReg, condReg);
            currentBB->addInstruction(ftoiInst);
            condReg = intCondReg; // 使用转换后的整数寄存器作为条件
        }

        // 生成条件分支指令 - 条件应该在通用寄存器中
        auto brInst = RISCVInstruction::createBType(RISCVOpcode::BNE, condReg,
                                                    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO),
                                                    inst->TrueBlock->getName());
        currentBB->addInstruction(brInst);

        // 生成无条件跳转到false分支
        if (inst->FalseBlock)
        {
            auto jumpInst = RISCVInstruction::createJType(RISCVOpcode::JAL,
                                                          make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO),
                                                          inst->FalseBlock->getName());
            currentBB->addInstruction(jumpInst);
        }
    }
    else
    {
        // 无条件分支
        auto jumpInst = RISCVInstruction::createJType(RISCVOpcode::JAL,
                                                      make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO),
                                                      inst->TrueBlock->getName());
        currentBB->addInstruction(jumpInst);
    }
}

void InstructionSelector::visitICmpInst(ICmpInst *inst)
{
    auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
    auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    switch (inst->Pred)
    {
    case ICmpInst::ICMP_EQ:
    {
        auto xorInst = RISCVInstruction::createRType(RISCVOpcode::XOR, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(xorInst);
        auto seqzInst = RISCVInstruction::createIType(RISCVOpcode::SLTIU, destReg, destReg, 1);
        currentBB->addInstruction(seqzInst);
    }
    break;
    case ICmpInst::ICMP_NE:
    {
        auto xorInst = RISCVInstruction::createRType(RISCVOpcode::XOR, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(xorInst);
        auto snezInst = RISCVInstruction::createRType(RISCVOpcode::SLTU, destReg,
                                                      make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO),
                                                      destReg);
        currentBB->addInstruction(snezInst);
    }
    break;
    case ICmpInst::ICMP_SLT:
    {
        auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::SLT, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(cmpInst);
    }
    break;
    case ICmpInst::ICMP_SLE:
    {
        auto sltInst = RISCVInstruction::createRType(RISCVOpcode::SLT, destReg, rhsReg, lhsReg);
        currentBB->addInstruction(sltInst);
        auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, destReg, 1);
        currentBB->addInstruction(xoriInst);
    }
    break;
    case ICmpInst::ICMP_SGT:
    {
        auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::SLT, destReg, rhsReg, lhsReg);
        currentBB->addInstruction(cmpInst);
    }
    break;
    case ICmpInst::ICMP_SGE:
    {
        auto sltInst = RISCVInstruction::createRType(RISCVOpcode::SLT, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(sltInst);
        auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, destReg, 1);
        currentBB->addInstruction(xoriInst);
    }
    break;
    default:
        return;
    }
}

void InstructionSelector::visitFCmpInst(FCmpInst *inst)
{
    // 修改：使用浮点寄存器类型
    auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
    auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    switch (inst->Pred)
    {
    case FCmpInst::FCMP_OEQ:
    {
        auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::FEQ_S, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(cmpInst);
    }
    break;
    case FCmpInst::FCMP_OLT:
    {
        auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::FLT_S, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(cmpInst);
    }
    break;
    case FCmpInst::FCMP_OLE:
    {
        auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::FLE_S, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(cmpInst);
    }
    break;
    case FCmpInst::FCMP_OGT:
    {
        auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::FLT_S, destReg, rhsReg, lhsReg);
        currentBB->addInstruction(cmpInst);
    }
    break;
    case FCmpInst::FCMP_OGE:
    {
        auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::FLE_S, destReg, rhsReg, lhsReg);
        currentBB->addInstruction(cmpInst);
    }
    break;
    case FCmpInst::FCMP_ONE:
    {
        auto feqInst = RISCVInstruction::createRType(RISCVOpcode::FEQ_S, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(feqInst);
        auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, destReg, 1);
        currentBB->addInstruction(xoriInst);
    }
    break;
    default:
        return;
    }
}

void InstructionSelector::visitSIToFPInst(CastInst *inst)
{
    // 处理有符号整数到浮点数的转换指令
    auto srcReg = getOrCreateVirtualReg(inst->getOperand());

    // 创建目标浮点寄存器 - 使用临时寄存器管理
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    // 生成 RISC-V 的 fcvt.s.w 指令（整数到单精度浮点）
    auto fcvtInst = RISCVInstruction::createPseudo(RISCVOpcode::FCVT_S_W, destReg, srcReg);
    currentBB->addInstruction(fcvtInst);
}

void InstructionSelector::visitFPToSIInst(CastInst *inst)
{
    // 处理浮点数到有符号整数的转换指令
    auto srcReg = getOrCreateVirtualReg(inst->getOperand());

    // 创建目标整数寄存器 - 使用临时寄存器管理
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    // 生成 RISC-V 的 fcvt.w.s 指令（单精度浮点到整数）
    // 使用RTZ（Round toward Zero）舍入模式，这是C语言标准的行为
    auto fcvtInst = RISCVInstruction::createPseudo(RISCVOpcode::FCVT_W_S, destReg, srcReg);
    currentBB->addInstruction(fcvtInst);
}

void InstructionSelector::visitCopyInst(CopyInst *inst)
{
    // Copy指令用于将源值复制到目标位置
    // 在我们的"所有变量溢出到栈上"策略中，这实际上是从源位置加载值，然后存储到目标位置

    // 获取源值的寄存器
    auto srcReg = getOrCreateVirtualReg(inst->getSource());

    // 创建临时寄存器进行复制操作
    RegisterType regType = inst->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::GENERAL;
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    // 生成移动指令
    RISCVOpcode moveOpcode;
    if (inst->getType()->isFloatTy())
    {
        // 浮点数使用 fmv.s 指令
        moveOpcode = RISCVOpcode::FMV_S;
    }
    else
    {
        // 整数/指针使用 mv 伪指令（实际上是 addi rd, rs1, 0）
        moveOpcode = RISCVOpcode::MV;
    }

    auto moveInst = RISCVInstruction::createPseudo(moveOpcode, destReg, srcReg);
    currentBB->addInstruction(moveInst);
}