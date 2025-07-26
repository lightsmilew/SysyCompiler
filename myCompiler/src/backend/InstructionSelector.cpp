#include "InstructionSelector.h"
using namespace RISCV;
// RISC-V 参数寄存器映射常量
const vector<RISCVRegister::PhysicalReg> INT_PARAM_REGS = {
    RISCVRegister::PhysicalReg::A0, RISCVRegister::PhysicalReg::A1,
    RISCVRegister::PhysicalReg::A2, RISCVRegister::PhysicalReg::A3,
    RISCVRegister::PhysicalReg::A4, RISCVRegister::PhysicalReg::A5,
    RISCVRegister::PhysicalReg::A6, RISCVRegister::PhysicalReg::A7};

const vector<RISCVRegister::PhysicalReg> FLOAT_PARAM_REGS = {
    RISCVRegister::PhysicalReg::FA0, RISCVRegister::PhysicalReg::FA1,
    RISCVRegister::PhysicalReg::FA2, RISCVRegister::PhysicalReg::FA3,
    RISCVRegister::PhysicalReg::FA4, RISCVRegister::PhysicalReg::FA5,
    RISCVRegister::PhysicalReg::FA6, RISCVRegister::PhysicalReg::FA7};

// RISC-V 临时寄存器映射常量
const vector<RISCVRegister::PhysicalReg> INT_TEMP_REGS = {
    RISCVRegister::PhysicalReg::T0, RISCVRegister::PhysicalReg::T1,
    RISCVRegister::PhysicalReg::T2, RISCVRegister::PhysicalReg::T3,
    RISCVRegister::PhysicalReg::T4, RISCVRegister::PhysicalReg::T5,
    RISCVRegister::PhysicalReg::T6};

const vector<RISCVRegister::PhysicalReg> FLOAT_TEMP_REGS = {
    RISCVRegister::PhysicalReg::FT0, RISCVRegister::PhysicalReg::FT1,
    RISCVRegister::PhysicalReg::FT2, RISCVRegister::PhysicalReg::FT3,
    RISCVRegister::PhysicalReg::FT4, RISCVRegister::PhysicalReg::FT5,
    RISCVRegister::PhysicalReg::FT6, RISCVRegister::PhysicalReg::FT7,
    RISCVRegister::PhysicalReg::FT8, RISCVRegister::PhysicalReg::FT9,
    RISCVRegister::PhysicalReg::FT10, RISCVRegister::PhysicalReg::FT11};

void InstructionSelector::selectInstructions(shared_ptr<RISCVFunction> func, Function *irFunc)
{
    currentFunc = func;
    irFunction = irFunc;

    // 创建虚拟寄存器映射表
    registerMap.clear();
    tempRegisters.clear();
    globalVarMap.clear();
    MoveArgMap.clear();

    buildControlFlowGraph();

    currentBB = func->getBasicBlocks().front(); // 从第一个基本块开始处理
    DealArgumentsInStart();

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
    auto &stack = currentFunc->getStackFrame();
    stack.allocateValueSpace(inst->getName(), inst->getAllocatedSize()); // 分配空间
    auto imm = LiInt(stack.getValueOffset(inst->getName()), true);
    currentFunc->addInstructionNeedReGetOffset(inst->getName(), currentLiInstruction);
    auto addrReg = getOrCreateVirtualReg(inst->getDest());

    auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);
    auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, addrReg, spReg, imm);
    currentBB->addInstruction(addInst);

    InitAllocaArray(addrReg, inst->getAllocatedSize());
}

void InstructionSelector::visitElementPtrInst(GetElementPtrInst *inst)
{
    auto baseAddr = getOrCreateVirtualReg(inst->getPointerOperand());
    auto destReg = getOrCreateVirtualReg(inst->getDest());
    auto offsetReg = LiInt(1, true);
    auto totalOffsetReg = LiInt(0, true);
    auto tmpReg = getTempReg(true);
    auto strideReg = getTempReg(true);

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
    currentBB->addInstruction(addInst);
}

void InstructionSelector::visitCallInst(CallInst *inst)
{
    // 1. 使用两阶段参数传递解耦方法处理参数
    const auto &callerArgsVec = irFunction->getArguments();
    vector<Argument *> callerArgs;
    for (const auto &argPtr : callerArgsVec)
    {
        callerArgs.push_back(argPtr.get());
    }

    unordered_map<string, shared_ptr<RISCVRegister>> tempMoveArgMap;
    if (!callerArgs.empty())
    {
        // 如果有参数，先处理参数传递
        tempMoveArgMap = moveCallerArgsTwoPhase();
    }

    // 2. 处理栈参数传递（超过8个寄存器参数的情况）
    auto arguments = inst->getArguments();
    int intArgIndex = 0;
    int floatArgIndex = 0;
    int argNum = 0;
    auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);
    auto &stack = currentFunc->getStackFrame();
    stack.allocateRaSpace(8);

    // 处理超出寄存器范围的参数（通过栈传递）
    for (auto arg : arguments)
    {
        bool isFloat = arg->getType()->isFloatTy();
        bool needStackPass = false;

        if (isFloat)
        {
            if (floatArgIndex >= 8)
            {
                needStackPass = true;
            }
            else if (tempMoveArgMap.find(arg->getName()) != tempMoveArgMap.end())
            {
                // 如果是两阶段传递的参数，直接使用临时寄存器
                auto tempReg = tempMoveArgMap[arg->getName()];
                auto destReg = make_shared<RISCVRegister>(FLOAT_PARAM_REGS[floatArgIndex]);
                auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_S, destReg, tempReg);
                currentBB->addInstruction(mvInst);
            }
            else
            {
                // 使用寄存器传递参数
                auto argReg = getOrCreateVirtualReg(arg);
                auto destReg = make_shared<RISCVRegister>(FLOAT_PARAM_REGS[floatArgIndex]);
                auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_S, destReg, argReg);
                currentBB->addInstruction(mvInst);
            }
            floatArgIndex++;
        }
        else
        {
            if (intArgIndex >= 8)
            {
                needStackPass = true;
            }
            else if (tempMoveArgMap.find(arg->getName()) != tempMoveArgMap.end())
            {
                // 如果是两阶段传递的参数，直接使用临时寄存器
                auto tempReg = tempMoveArgMap[arg->getName()];
                auto destReg = make_shared<RISCVRegister>(INT_PARAM_REGS[intArgIndex]);
                auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, destReg, tempReg);
                currentBB->addInstruction(mvInst);
            }
            else
            {
                // 使用寄存器传递参数
                auto argReg = getOrCreateVirtualReg(arg);
                auto destReg = make_shared<RISCVRegister>(INT_PARAM_REGS[intArgIndex]);
                auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, destReg, argReg);
                currentBB->addInstruction(mvInst);
            }
            intArgIndex++;
        }

        if (needStackPass)
        {
            shared_ptr<RISCVRegister> argReg = getOrCreateVirtualReg(arg);
            if (tempMoveArgMap.find(arg->getName()) != tempMoveArgMap.end())
            {
                // 如果是两阶段传递的参数，直接使用临时寄存器
                argReg = tempMoveArgMap[arg->getName()];
            }

            if (isFloat)
            {
                stack.allocateCalleeArgSpace(argNum);
                int offset = stack.getCalleeArgOffset(argNum);

                auto tempReg = getTempReg(true);
                auto liInst = RISCVInstruction::createPseudoLI(tempReg, offset);
                currentBB->addInstruction(liInst);
                auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, spReg, tempReg);
                currentBB->addInstruction(addInst);
                auto storeInst = RISCVInstruction::createSType(RISCVOpcode::FSW, tempReg, argReg, 0);
                currentBB->addInstruction(storeInst);
            }
            else
            {
                bool isPtr = arg->getType()->isPointerTy();
                stack.allocateCalleeArgSpace(argNum, isPtr ? 8 : 4);
                int offset = stack.getCalleeArgOffset(argNum);

                auto tempReg = getTempReg(true);
                auto liInst = RISCVInstruction::createPseudoLI(tempReg, offset);
                currentBB->addInstruction(liInst);
                auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, spReg, tempReg);
                currentBB->addInstruction(addInst);
                auto storeInst = RISCVInstruction::createSType(isPtr ? RISCVOpcode::SD : RISCVOpcode::SW, tempReg, argReg, 0);
                currentBB->addInstruction(storeInst);
            }
        }
        argNum++;
    }

    // 3. 生成函数调用指令
    auto callInst = RISCVInstruction::createPseudoCALL(inst->getCalledFunction()->getName());
    currentBB->addInstruction(callInst);

    // 4. 处理返回值
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

    // 5. 回复caller参数寄存器
    move2RestoreArgs(tempMoveArgMap);
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
    RegisterType regType = inst->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::INT;
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

void InstructionSelector::DealArgumentsInStart()
{
    const auto &args = irFunction->getArguments();
    vector<Argument *> argsVec;
    for (const auto &argPtr : args)
    {
        argsVec.push_back(argPtr.get());
    }

    if (argsVec.empty())
    {
        return; // 无参数函数，直接返回
    }

    auto intArgIndex = 0;
    auto floatArgIndex = 0;
    for (auto arg : argsVec)
    {
        bool isFloat = arg->getType()->isFloatTy();
        if (isFloat)
        {
            getCallerArgReg(arg, floatArgIndex);
            floatArgIndex++;
        }
        else
        {
            getCallerArgReg(arg, intArgIndex);
            intArgIndex++;
        }
    }
}

unordered_map<string, shared_ptr<RISCVRegister>> InstructionSelector::moveCallerArgsTwoPhase()
{
    const auto &callerArgsVec = irFunction->getArguments();
    vector<Argument *> callerArgs;
    for (const auto &argPtr : callerArgsVec)
    {
        callerArgs.push_back(argPtr.get());
    }

    if (callerArgs.empty())
    {
        return {}; // 无参数函数，直接返回
    }

    // 创建临时寄存器数组，用于存储参数
    vector<shared_ptr<RISCVRegister>> tempRegs;
    unordered_map<string, shared_ptr<RISCVRegister>> tempMoveArgMap;

    // 为每个参数创建一个临时寄存器
    for (size_t i = 0; i < callerArgs.size(); i++)
    {
        if (callerArgs[i]->getType()->isFloatTy())
        {
            // 浮点参数使用浮点临时寄存器
            tempRegs.push_back(getTempFloatReg());
        }
        else
        {
            // 整数/指针参数使用整数临时寄存器
            tempRegs.push_back(getTempReg());
        }
    }

    // 将参数移动到临时寄存器
    for (size_t i = 0; i < callerArgs.size(); i++)
    {
        auto argReg = getOrCreateVirtualReg(callerArgs[i]);
        RISCVOpcode moveOpcode = callerArgs[i]->getType()->isFloatTy() ? RISCVOpcode::FMV_S : RISCVOpcode::MV;
        auto moveInst = RISCVInstruction::createPseudo(moveOpcode, tempRegs[i], argReg);
        currentBB->addInstruction(moveInst);
        // 更新临时寄存器映射
        tempMoveArgMap[callerArgs[i]->getName()] = tempRegs[i];
    }

    return tempMoveArgMap; // 返回临时寄存器映射
}
void InstructionSelector::move2RestoreArgs(unordered_map<string, shared_ptr<RISCVRegister>> &registerMap)
{
    // 1. 解析 IR Function 的形参列表
    const auto &arguments = irFunction->getArguments();
    if (arguments.empty())
    {
        return; // 无参数函数，直接返回
    }

    int intArgIndex = 0;
    int floatArgIndex = 0;

    // 2. 为每个形参创建虚拟寄存器 xi 并生成 mv xi, ai 指令
    for (const auto &arg : arguments)
    {
        // 确定参数类型和对应的参数寄存器
        bool isFloat = arg->getType()->isFloatTy();
        shared_ptr<RISCVRegister> paramReg;

        if (isFloat)
        {
            if (floatArgIndex < 8)
            {
                auto tempReg = registerMap[arg->getName()];
                paramReg = make_shared<RISCVRegister>(FLOAT_PARAM_REGS[floatArgIndex]);
                auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_S, paramReg, tempReg);
                currentBB->addInstruction(mvInst);
                floatArgIndex++;
            }
        }
        else
        {
            if (intArgIndex < 8)
            {
                auto tempReg = registerMap[arg->getName()];
                paramReg = make_shared<RISCVRegister>(INT_PARAM_REGS[intArgIndex]);
                auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, paramReg, tempReg);
                currentBB->addInstruction(mvInst);
                intArgIndex++;
            }
        }
    }
}

shared_ptr<RISCVRegister> InstructionSelector::getCallerArgReg(Argument *arg, size_t index)
{
    if (arg->getType()->isFloatTy())
    {
        if (index < FLOAT_PARAM_REGS.size())
        {
            auto sourceReg = make_shared<RISCVRegister>(FLOAT_PARAM_REGS[index]);
            auto reg = getArgReg(arg->getName(), RegisterType::FLOAT);
            auto FmvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_S, reg, sourceReg);
            currentBB->addInstruction(FmvInst);
            return reg;
        }
        else
        {
            // 超出范围，从栈上获取参数
            auto offset = currentFunc->getStackFrame().allocateCallerArgSpace(arg->getName(), 4);
            auto tempReg = LiInt(offset, true);
            currentFunc->addInstructionNeedReGetOffset(arg->getName(), currentLiInstruction);
            auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP), tempReg);
            currentBB->addInstruction(addInst);
            // 从栈上加载浮点数参数
            auto reg = getArgReg(arg->getName(), RegisterType::FLOAT);
            auto loadInst = RISCVInstruction::createIType(RISCVOpcode::FLW, reg, tempReg, 0);
            currentBB->addInstruction(loadInst);
            return tempReg;
        }
    }
    else
    {
        if (index < INT_PARAM_REGS.size())
        {
            auto sourceReg = make_shared<RISCVRegister>(INT_PARAM_REGS[index]);
            auto reg = getArgReg(arg->getName(), RegisterType::INT);
            auto MvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, reg, sourceReg);
            currentBB->addInstruction(MvInst);
            return reg;
        }
        else
        {
            // 超出范围，从栈上获取参数
            RISCVOpcode op = arg->getType()->isPointerTy() ? RISCVOpcode::LD : RISCVOpcode::LW;
            auto offset = currentFunc->getStackFrame().allocateCallerArgSpace(arg->getName(), arg->getType()->isPointerTy() ? 8 : 4);
            auto tempReg = LiInt(offset, true);
            currentFunc->addInstructionNeedReGetOffset(arg->getName(), currentLiInstruction);
            auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP), tempReg);
            currentBB->addInstruction(addInst);
            auto reg = getArgReg(arg->getName(), RegisterType::INT);
            auto loadInst = RISCVInstruction::createIType(op, reg, tempReg, 0);
            currentBB->addInstruction(loadInst);
            return tempReg;
        }
    }
}

shared_ptr<RISCVRegister> InstructionSelector::getOrCreateVirtualReg(Value *value)
{
    // 立即数
    if (auto constantIntValue = dynamic_cast<ConstantInt *>(value))
    {
        return LiInt(constantIntValue->Value, true);
    }
    else if (auto constantFloatValue = dynamic_cast<ConstantFloat *>(value))
    {
        return LiFloat(constantFloatValue->Value, true);
    }
    // 全局变量
    else if (auto globlVar = dynamic_cast<GlobalVariable *>(value))
    {
        return LaGlobl(globlVar);
    }
    // 函数参数
    else if (auto arg = dynamic_cast<Argument *>(value))
    {
        RegisterType regType = arg->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::INT;
        return getArgReg(arg->getName(), regType);
    }

    // 变量
    auto valueName = value->getName();
    if (registerMap.find(valueName) != registerMap.end())
    {
        return registerMap[valueName];
    }
    else
    {
        auto dataType = value->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::INT;
        auto virtualReg = make_shared<RISCVRegister>(dataType);
        registerMap[valueName] = virtualReg;
        return virtualReg;
    }

    return nullptr;
}

shared_ptr<RISCVRegister> InstructionSelector::getArgReg(const string &argName, RegisterType regType)
{
    // 获取当前函数的参数寄存器
    if (MoveArgMap.find(argName) != MoveArgMap.end())
    {
        return MoveArgMap[argName];
    }
    else
    {
        auto tempReg = regType == RegisterType::INT ? getTempReg() : getTempFloatReg();
        MoveArgMap[argName] = tempReg; // 临时寄存器到参数寄存器的映射
        return tempReg;
    }
}

shared_ptr<RISCVRegister> InstructionSelector::getTempReg(bool isPhysical)
{
    if (isPhysical)
    {
        return getTempPhysicalReg();
    }

    auto tempReg = make_shared<RISCVRegister>(RegisterType::INT);
    return tempReg;
}

shared_ptr<RISCVRegister> InstructionSelector::getTempFloatReg(bool isPhysical)
{
    if (isPhysical)
    {
        return getTempPhysicalFloatReg();
    }

    auto tempReg = make_shared<RISCVRegister>(RegisterType::FLOAT);
    return tempReg;
}

shared_ptr<RISCVRegister> InstructionSelector::getTempPhysicalReg()
{
    auto tempReg = make_shared<RISCVRegister>(INT_TEMP_REGS[tempRegCount++ % INT_TEMP_REGS.size()]);
    tempRegisters.push_back(tempReg);
    return tempReg;
}

shared_ptr<RISCVRegister> InstructionSelector::getTempPhysicalFloatReg()
{
    auto tempReg = make_shared<RISCVRegister>(FLOAT_TEMP_REGS[tempFloatRegCount++ % FLOAT_TEMP_REGS.size()]);
    tempRegisters.push_back(tempReg);
    return tempReg;
}

shared_ptr<RISCVRegister> InstructionSelector::LaGlobl(GlobalVariable *globlvar)
{
    auto globReg = getTempReg(true);
    globalVarMap[globlvar->getName()] = globReg;
    auto laInst = RISCVInstruction::createPseudoLA(globReg, globlvar->getName());
    currentBB->addInstruction(laInst);

    return globReg;
}

shared_ptr<RISCVRegister> InstructionSelector::LiInt(int value, bool isPhysical)
{

    auto destReg = getTempReg(isPhysical);
    auto LiInst = RISCVInstruction::createPseudoLI(destReg, value);
    currentLiInstruction = LiInst; // 保存当前的立即数指令
    currentBB->addInstruction(LiInst);

    return destReg;
}

shared_ptr<RISCVRegister> InstructionSelector::LiFloat(float floatValue, bool isPhysical)
{
    auto tmpReg = getTempReg(isPhysical);
    uint32_t hexValue;
    memcpy(&hexValue, &floatValue, sizeof(floatValue));
    auto LiInst = RISCVInstruction::createPseudoLI(tmpReg, hexValue);
    currentBB->addInstruction(LiInst);

    auto destReg = getTempFloatReg(isPhysical);
    auto FmvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_W_X, destReg, tmpReg);
    currentBB->addInstruction(FmvInst);

    return destReg;
}

void InstructionSelector::InitAllocaArray(shared_ptr<RISCVRegister> addrReg, int size)
{
    auto startReg = getTempReg(true);
    auto CounterReg = LiInt(size / 4, true);
    auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, startReg, addrReg);
    currentBB->addInstruction(mvInst);

    // loop 初始化数组为0
    auto zeroReg = std::make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO);
    auto nextBB = currentBB->getSuccessors()[0];
    auto swInst = RISCVInstruction::createSType(RISCVOpcode::SW, startReg, zeroReg, 0);
    nextBB->addInstruction(swInst);
    auto addiInst = RISCVInstruction::createIType(RISCVOpcode::ADDI, CounterReg, CounterReg, -1);
    nextBB->addInstruction(addiInst);
    auto addiInst2 = RISCVInstruction::createIType(RISCVOpcode::ADDI, startReg, startReg, 4);
    nextBB->addInstruction(addiInst2);
    auto bneInst = RISCVInstruction::createBType(RISCVOpcode::BNE, CounterReg, zeroReg, nextBB->getLabel());
    nextBB->addInstruction(bneInst);
}