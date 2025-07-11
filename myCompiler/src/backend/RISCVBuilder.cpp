#include "RISCVBuilder.h"
#include <sstream>
#include <iostream>
#include <algorithm>
#include <cstring>
#include <iomanip>
#include <set>
#include <vector>

using namespace RISCV;
using std::endl;
using std::max;
using std::set;
using std::stringstream;
using std::vector;

shared_ptr<RISCVModule> RISCVBuilder::generateRISCVCode(shared_ptr<Module> irModule)
{
    // 保存IR模块引用
    this->irModule = irModule;

    // 流水线各阶段
    initializeModule(irModule);
    generateInstructions();
    // allocateRegisters();
    // optimizeCode();

    return riscvModule;
}

string RISCVBuilder::generateAssembly(shared_ptr<RISCVModule> module)
{
    AssemblyEmitter emitter;
    return emitter.emit(module);
}

void RISCVBuilder::initializeModule(shared_ptr<Module> irModule)
{
    riscvModule = make_shared<RISCVModule>(irModule->Name);

    // 初始化全局变量块
    for (const auto &globalVar : irModule->GlobalVariables)
    {
        auto globalBlock = riscvModule->createGlobalBlock(globalVar->getName());
        // 初始化全局变量的数据
        if (globalVar->Initializer)
        {
            processGlobalInitializer(globalBlock, globalVar->Initializer);
        }
        else
        {
            // 没有初始化器，添加零初始化
            processZeroInitializer(globalBlock, globalVar->getType());
        }
    }

    // 初始化函数
    for (const auto &func : irModule->Functions)
    {
        auto riscvFunc = make_shared<RISCVFunction>(func->getName(), riscvModule);
        riscvModule->addFunction(riscvFunc);

        // 为函数添加prologue基本块
        riscvFunc->addBasicBlock(make_shared<RISCVBasicBlock>("prologue", riscvFunc));

        // 为每个IR基本块创建对应的RISC-V基本块
        for (const auto &bb : func->BasicBlocks)
        {
            auto riscvBB = make_shared<RISCVBasicBlock>(bb->getName(), riscvFunc);
            riscvFunc->addBasicBlock(riscvBB);
        }

        // 为函数添加epilogue基本块
        riscvFunc->addBasicBlock(make_shared<RISCVBasicBlock>("epilogue", riscvFunc));
    }
}

void RISCVBuilder::generateInstructions()
{
    // 为每个函数生成指令
    for (const auto &func : irModule->Functions)
    {
        auto riscvFunc = riscvModule->getFunction(func->getName());
        if (!riscvFunc)
            continue;

        InstructionSelector selector;
        selector.selectInstructions(riscvFunc, func.get());
    }
}

// void RISCVBuilder::allocateRegisters()
// {
//     // 为每个函数进行寄存器分配
//     for (const auto &func : riscvModule->getFunctions())
//     {
//         RegisterAllocator allocator;
//         allocator.allocateRegisters(func);
//     }
// }

// void RISCVBuilder::optimizeCode()
// {
//     // 为每个函数进行窥孔优化
//     // TODO: 实现窥孔优化
//     /*
//     for (const auto &func : riscvModule->getFunctions())
//     {
//         PeepholeOptimizer optimizer;
//         optimizer.optimize(func);
//     }
//     */
// }

// ===== InstructionSelector 实现 =====

void InstructionSelector::selectInstructions(shared_ptr<RISCVFunction> func, Function *irFunc)
{
    currentFunc = func;

    // 创建虚拟寄存器映射表
    registerMap.clear();
    stackArguments.clear();

    // 预扫描函数，分配栈空间
    prescanFunction(func, irFunc);

    // 处理函数参数
    mapArguments(func, irFunc);

    // 生成函数序言
    generateFunctionPrologue(func);

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

    // 生成函数尾声
    generateFunctionEpilogue(func);
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

    // 创建临时寄存器存储计算结果
    RegisterType regType = inst->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::GENERAL;
    auto tempReg = getTempRegister(regType, 0);

    RISCVOpcode opcode;
    bool isFloat = inst->getType()->isFloatTy();

    switch (inst->Op)
    {
    case Opcode::Add:
        opcode = isFloat ? RISCVOpcode::FADD_S : RISCVOpcode::ADD;
        break;
    case Opcode::Sub:
        opcode = isFloat ? RISCVOpcode::FSUB_S : RISCVOpcode::SUB;
        break;
    case Opcode::Mul:
        opcode = isFloat ? RISCVOpcode::FMUL_S : RISCVOpcode::MUL;
        break;
    case Opcode::SDiv:
        opcode = isFloat ? RISCVOpcode::FDIV_S : RISCVOpcode::DIV;
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
    auto riscvInst = RISCVInstruction::createRType(opcode, tempReg, lhsReg, rhsReg);
    currentBB->addInstruction(riscvInst);

    // 将结果存储到栈中
    storeValueToStack(inst, tempReg);
}

void InstructionSelector::visitLoadInst(LoadInst *inst)
{
    auto ptrReg = getOrCreateVirtualReg(inst->getPointer());

    // 创建临时寄存器存储加载结果
    RegisterType regType = inst->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::GENERAL;
    auto tempReg = getTempRegister(regType, 0);

    RISCVOpcode opcode = inst->getType()->isFloatTy() ? RISCVOpcode::FLW : RISCVOpcode::LW;

    // 生成加载指令 (假设偏移为0)
    auto riscvInst = RISCVInstruction::createIType(opcode, tempReg, ptrReg, 0);
    currentBB->addInstruction(riscvInst);

    // 将加载的结果存储到栈中
    storeValueToStack(inst, tempReg);
}

void InstructionSelector::visitStoreInst(StoreInst *inst)
{
    auto valueReg = getOrCreateVirtualReg(inst->getValueToStore());
    auto ptrReg = getOrCreateVirtualReg(inst->getPointer());

    RISCVOpcode opcode = inst->getValueToStore()->getType()->isFloatTy() ? RISCVOpcode::FSW : RISCVOpcode::SW;

    // 生成存储指令 (假设偏移为0)
    auto riscvInst = RISCVInstruction::createSType(opcode, ptrReg, valueReg, 0);
    currentBB->addInstruction(riscvInst);

    // Store指令本身没有返回值，所以不需要存储到栈
}

void InstructionSelector::visitCallInst(CallInst *inst)
{
    if (!inst->getCalledFunction())
        return;

    // 1. 处理参数传递 - 严格按照RISC-V ABI规范
    // 前8个整数/指针参数使用a0-a7，前8个浮点参数使用fa0-fa7
    // 超出的参数按顺序存放在调用者栈帧的参数区域

    vector<shared_ptr<RISCVRegister>> argRegs;
    argRegs.reserve(inst->getArguments().size());

    // 首先为所有参数加载值到临时寄存器
    for (auto arg : inst->getArguments())
    {
        auto argReg = getOrCreateVirtualReg(arg);
        argRegs.push_back(argReg);
    }

    int intArgIndex = 0;
    int floatArgIndex = 0;
    int stackArgIndex = 0;

    // 处理每个参数
    for (size_t i = 0; i < inst->getArguments().size(); ++i)
    {
        auto arg = inst->getArguments()[i];
        auto argReg = argRegs[i];

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
                // 超出fa0-fa7的浮点参数写入栈
                auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);
                int stackOffset = stackArgIndex * 4; // 每个参数4字节

                auto storeInst = RISCVInstruction::createSType(RISCVOpcode::FSW, spReg, argReg, stackOffset);
                currentBB->addInstruction(storeInst);
                stackArgIndex++;
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
                // 超出a0-a7的整数参数写入栈
                auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);
                int stackOffset = stackArgIndex * 4; // 每个参数4字节

                auto storeInst = RISCVInstruction::createSType(RISCVOpcode::SW, spReg, argReg, stackOffset);
                currentBB->addInstruction(storeInst);
                stackArgIndex++;
            }
        }
    }

    // 2. 生成函数调用指令
    auto callInst = RISCVInstruction::createJType(RISCVOpcode::JAL,
                                                  make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::RA),
                                                  inst->getCalledFunction()->getName());
    currentBB->addInstruction(callInst);

    // 3. 处理返回值 - 从a0/fa0中获取返回值并存储到栈
    if (!inst->getType()->isVoidTy())
    {
        auto returnPhysReg = make_shared<RISCVRegister>(
            inst->getType()->isFloatTy() ? RISCVRegister::PhysicalReg::FA0 : RISCVRegister::PhysicalReg::A0);

        // 将返回值存储到栈中
        storeValueToStack(inst, returnPhysReg);
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

        // 如果是main函数，需要调用putint输出返回值
        if (currentFunc->getName() == "main")
        {
            // 保存返回值到a0寄存器（如果还没有的话）
            if (returnReg->getPhysicalReg() != RISCVRegister::PhysicalReg::A0)
            {
                auto a0Reg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A0);
                auto moveToA0 = RISCVInstruction::createPseudo(RISCVOpcode::MV, a0Reg, valueReg);
                currentBB->addInstruction(moveToA0);
            }

            // 调用putint函数输出返回值
            auto callPutint = RISCVInstruction::createJType(RISCVOpcode::JAL,
                                                            make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::RA),
                                                            "putint");
            currentBB->addInstruction(callPutint);

            // 添加程序退出代码
            auto a7Reg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A7);
            auto loadExit = RISCVInstruction::createIType(RISCVOpcode::ADDI, a7Reg,
                                                          make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO),
                                                          93); // sys_exit系统调用号
            currentBB->addInstruction(loadExit);

            // 在退出前恢复栈帧
            generateFunctionEpilogue(currentFunc);

            // 标准的ecall指令
            auto ecallInst = make_shared<RISCVInstruction>(RISCVOpcode::ECALL, InstructionType::I_TYPE);
            currentBB->addInstruction(ecallInst);

            // main函数不需要常规的return，因为程序直接退出
            return;
        }
    }

    // 生成函数结尾（恢复栈帧）
    generateFunctionEpilogue(currentFunc);

    // 生成返回指令
    auto retInst = RISCVInstruction::createIType(RISCVOpcode::JALR,
                                                 make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO),
                                                 make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::RA),
                                                 0);
    currentBB->addInstruction(retInst);
}

void InstructionSelector::visitBranchInst(BranchInst *inst)
{
    if (inst->getCondition())
    {
        // 条件分支
        auto condReg = getOrCreateVirtualReg(inst->getCondition());

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

void InstructionSelector::visitAllocaInst(AllocaInst *inst)
{
    // Alloca指令在RISC-V中通常对应栈空间分配
    // 在预扫描阶段已经分配了实际的栈空间，这里需要计算地址

    StackFrame &stackFrame = currentFunc->getStackFrame();

    // 检查是否已经为该alloca分配了栈空间
    if (!stackFrame.hasAllocation(inst))
    {
        // 如果没有分配（虽然预扫描应该已经分配），现在分配
        Type *allocatedType = inst->AllocatedType;
        int allocatedSize = 4; // 默认值

        if (allocatedType->isArrayTy())
        {
            auto arrayType = static_cast<ArrayType *>(allocatedType);
            int elementSize = arrayType->getElementType()->isFloatTy() ? 4 : 4;
            allocatedSize = arrayType->getNumElements() * elementSize;
        }
        else if (allocatedType->isIntegerTy() || allocatedType->isFloatTy())
        {
            allocatedSize = 4;
        }
        else if (allocatedType->isPointerTy())
        {
            allocatedSize = 8; // 64位系统中指针为8字节
        }

        stackFrame.allocateSpace(inst, allocatedSize);
    }

    // 获取alloca的栈偏移
    int varOffset = stackFrame.getOffset(inst);

    // 创建临时寄存器保存地址
    auto tempReg = getGeneralTempRegister(0);

    // 计算地址：sp + offset
    if (isImmediateInRange(varOffset))
    {
        auto addiInst = RISCVInstruction::createIType(RISCVOpcode::ADDI, tempReg,
                                                      make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP),
                                                      varOffset);
        currentBB->addInstruction(addiInst);
    }
    else
    {
        // 使用li + add组合
        auto liInst = RISCVInstruction::createPseudoLI(tempReg, varOffset);
        currentBB->addInstruction(liInst);

        auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg,
                                                     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP),
                                                     tempReg);
        currentBB->addInstruction(addInst);
    }

    // 将地址存储到栈中（alloca的结果是指针）
    storeValueToStack(inst, tempReg);
}

void InstructionSelector::visitElementPtrInst(GetElementPtrInst *inst)
{
    auto ptrReg = getOrCreateVirtualReg(inst->getPointerOperand());
    auto destReg = getGeneralTempRegister(0);

    // 简化的GEP处理，假设只有一个索引
    auto indices = inst->getIndices();
    if (!indices.empty())
    {
        auto indexReg = getOrCreateVirtualReg(indices[0]);

        // 计算地址：ptr + index * sizeof(element)
        // 这里简化为 ptr + index
        auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, destReg, ptrReg, indexReg);
        currentBB->addInstruction(addInst);
    }
    else
    {
        // 没有索引，直接复制指针
        auto moveInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, destReg, ptrReg);
        currentBB->addInstruction(moveInst);
    }

    // 将结果存储到栈中
    storeValueToStack(inst, destReg);
}

void InstructionSelector::visitICmpInst(ICmpInst *inst)
{
    auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
    auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
    auto destReg = getGeneralTempRegister(0);

    RISCVOpcode opcode;
    switch (inst->Pred)
    {
    case ICmpInst::ICMP_EQ:
        // eq: sub + seqz
        {
            auto subInst = RISCVInstruction::createRType(RISCVOpcode::SUB, destReg, lhsReg, rhsReg);
            currentBB->addInstruction(subInst);

            // seqz rd, rs1 等价于 sltiu rd, rs1, 1
            auto seqzInst = RISCVInstruction::createIType(RISCVOpcode::SLTIU, destReg, destReg, 1);
            currentBB->addInstruction(seqzInst);
        }
        break;
    case ICmpInst::ICMP_NE:
        // ne: sub + snez
        {
            auto subInst = RISCVInstruction::createRType(RISCVOpcode::SUB, destReg, lhsReg, rhsReg);
            currentBB->addInstruction(subInst);

            // snez rd, rs1 等价于 sltu rd, zero, rs1
            auto snezInst = RISCVInstruction::createRType(RISCVOpcode::SLTU, destReg,
                                                          make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO),
                                                          destReg);
            currentBB->addInstruction(snezInst);
        }
        break;
    case ICmpInst::ICMP_SLT:
        opcode = RISCVOpcode::SLT;
        break;
    case ICmpInst::ICMP_SLE:
        // le: slt + xori
        {
            auto sltInst = RISCVInstruction::createRType(RISCVOpcode::SLT, destReg, rhsReg, lhsReg);
            currentBB->addInstruction(sltInst);
            auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, destReg, 1);
            currentBB->addInstruction(xoriInst);
        }
        break;
    case ICmpInst::ICMP_SGT:
        opcode = RISCVOpcode::SLT;
        std::swap(lhsReg, rhsReg); // gt: slt with swapped operands
        break;
    case ICmpInst::ICMP_SGE:
        // ge: slt + xori with swapped operands
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

    if (inst->Pred == ICmpInst::ICMP_SLT || inst->Pred == ICmpInst::ICMP_SGT)
    {
        auto cmpInst = RISCVInstruction::createRType(opcode, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(cmpInst);
    }

    // 将结果存储到栈中
    storeValueToStack(inst, destReg);
}

void InstructionSelector::visitFCmpInst(FCmpInst *inst)
{
    auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
    auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
    auto destReg = getGeneralTempRegister(0); // 浮点比较结果存储在通用寄存器中

    RISCVOpcode opcode;
    switch (inst->Pred)
    {
    case FCmpInst::FCMP_OEQ:
        opcode = RISCVOpcode::FEQ_S;
        break;
    case FCmpInst::FCMP_OLT:
        opcode = RISCVOpcode::FLT_S;
        break;
    case FCmpInst::FCMP_OLE:
        opcode = RISCVOpcode::FLE_S;
        break;
    case FCmpInst::FCMP_OGT:
        opcode = RISCVOpcode::FLT_S;
        std::swap(lhsReg, rhsReg); // gt: flt with swapped operands
        break;
    case FCmpInst::FCMP_OGE:
        opcode = RISCVOpcode::FLE_S;
        std::swap(lhsReg, rhsReg); // ge: fle with swapped operands
        break;
    case FCmpInst::FCMP_ONE:
        // ne: !feq
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

    if (inst->Pred != FCmpInst::FCMP_ONE)
    {
        auto cmpInst = RISCVInstruction::createRType(opcode, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(cmpInst);
    }

    // 将结果存储到栈中
    storeValueToStack(inst, destReg);
}

void InstructionSelector::visitSIToFPInst(CastInst *inst)
{
    // 处理有符号整数到浮点数的转换指令
    auto srcReg = getOrCreateVirtualReg(inst->getOperand());

    // 创建目标浮点寄存器 - 使用临时寄存器管理
    auto destReg = getFloatTempRegister(0); // 使用FT0进行类型转换

    // 生成 RISC-V 的 fcvt.s.w 指令（整数到单精度浮点）
    auto fcvtInst = RISCVInstruction::createPseudo(RISCVOpcode::FCVT_S_W, destReg, srcReg);
    currentBB->addInstruction(fcvtInst);

    // 将结果存储到栈中
    storeValueToStack(inst, destReg);
}

void InstructionSelector::visitFPToSIInst(CastInst *inst)
{
    // 处理浮点数到有符号整数的转换指令
    auto srcReg = getOrCreateVirtualReg(inst->getOperand());

    // 创建目标整数寄存器 - 使用临时寄存器管理
    auto destReg = getGeneralTempRegister(0); // 使用T0进行类型转换

    // 生成 RISC-V 的 fcvt.w.s 指令（单精度浮点到整数）
    // 使用RTZ（Round toward Zero）舍入模式，这是C语言标准的行为
    auto fcvtInst = RISCVInstruction::createPseudo(RISCVOpcode::FCVT_W_S, destReg, srcReg);
    currentBB->addInstruction(fcvtInst);

    // 将结果存储到栈中
    storeValueToStack(inst, destReg);
}

void InstructionSelector::visitCopyInst(CopyInst *inst)
{
    // Copy指令用于将源值复制到目标位置
    // 在我们的"所有变量溢出到栈上"策略中，这实际上是从源位置加载值，然后存储到目标位置

    // 获取源值的寄存器
    auto srcReg = getOrCreateVirtualReg(inst->getSource());

    // 创建临时寄存器进行复制操作
    RegisterType regType = inst->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::GENERAL;
    auto destReg = getTempRegister(regType, 0);

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

    // 将复制的结果存储到栈中（Copy指令的结果）
    storeValueToStack(inst, destReg);
}

shared_ptr<RISCVRegister> InstructionSelector::getOrCreateVirtualReg(Value *value)
{
    // 处理常量
    if (auto constInt = dynamic_cast<ConstantInt *>(value))
    {
        // 根据常量大小优化加载指令
        auto tempReg = getGeneralTempRegister(0);
        generateConstantLoad(tempReg, constInt->Value);
        return tempReg;
    }

    if (auto constFloat = dynamic_cast<ConstantFloat *>(value))
    {
        // 对于浮点常量，需要更复杂的处理
        auto tempReg = getFloatTempRegister(0);
        generateFloatConstantLoad(tempReg, constFloat->Value);
        return tempReg;
    }

    // 查找已存在的映射（函数参数）
    auto it = registerMap.find(value);
    if (it != registerMap.end())
    {
        return it->second;
    }

    // 检查是否是栈参数
    auto stackIt = stackArguments.find(value);
    if (stackIt != stackArguments.end())
    {
        // 这是一个栈参数，需要从调用者栈帧中加载
        // 栈参数存储在调用者栈帧顶部，被调用者通过SP+偏移量访问
        auto loadReg = getGeneralTempRegister(0);

        // 计算栈参数的实际偏移量：
        // argOffset = 被调用者栈帧大小 + 参数在调用者栈帧中的偏移
        StackFrame &stackFrame = currentFunc->getStackFrame();
        int argOffset = stackFrame.totalAlignedSize + stackIt->second;

        // 生成从栈加载指令
        auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);
        if (isImmediateInRange(argOffset))
        {
            auto loadInst = RISCVInstruction::createIType(RISCVOpcode::LW, loadReg, spReg, argOffset);
            currentBB->addInstruction(loadInst);
        }
        else
        {
            // 偏移量太大，先计算地址
            auto addrReg = getGeneralTempRegister(3);
            auto liInst = RISCVInstruction::createPseudoLI(addrReg, argOffset);
            currentBB->addInstruction(liInst);

            auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, addrReg, spReg, addrReg);
            currentBB->addInstruction(addInst);

            auto loadInst = RISCVInstruction::createIType(RISCVOpcode::LW, loadReg, addrReg, 0);
            currentBB->addInstruction(loadInst);
        }

        // 如果目标是浮点类型，需要转移到浮点寄存器
        if (value->getType()->isFloatTy())
        {
            auto floatReg = getFloatTempRegister(0);
            auto moveInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_W_X, floatReg, loadReg);
            currentBB->addInstruction(moveInst);
            return floatReg;
        }

        return loadReg;
    }

    // 对于需要从栈加载的值（如指令的结果）
    StackFrame &stackFrame = currentFunc->getStackFrame();
    if (stackFrame.hasAllocation(value))
    {
        // 检查是否已经有加载过的寄存器
        auto existing = registerMap.find(value);
        if (existing != registerMap.end())
        {
            return existing->second;
        }

        // 从栈中加载值到新的虚拟寄存器
        RegisterType regType = value->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::GENERAL;
        auto virtualReg = getTempRegister(regType, 0);

        // 先保存映射，避免递归调用
        registerMap[value] = virtualReg;

        // 生成从栈加载的指令
        int offset = stackFrame.getOffset(value);
        generateStackAccess(offset, virtualReg, false); // false表示load

        return virtualReg;
    }

    // 默认情况：创建新的虚拟寄存器
    RegisterType regType = value->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::GENERAL;
    auto newReg = getTempRegister(regType, 0);

    // 保存映射
    registerMap[value] = newReg;

    return newReg;
}

void InstructionSelector::storeValueToStack(Value *value, shared_ptr<RISCVRegister> reg)
{
    StackFrame &stackFrame = currentFunc->getStackFrame();

    if (!stackFrame.hasAllocation(value))
    {
        // 如果还没有分配空间，现在分配
        stackFrame.allocateSpace(value, 4);
    }

    int offset = stackFrame.getOffset(value);
    generateStackAccess(offset, reg, true); // true表示store
}

void InstructionSelector::generateStackAccess(int offset, shared_ptr<RISCVRegister> reg, bool isStore)
{
    auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);

    if (isImmediateInRange(offset))
    {
        // 直接使用偏移量
        if (isStore)
        {
            RISCVOpcode opcode = (reg->getType() == RegisterType::FLOAT) ? RISCVOpcode::FSW : RISCVOpcode::SW;
            auto inst = RISCVInstruction::createSType(opcode, spReg, reg, offset);
            currentBB->addInstruction(inst);
        }
        else
        {
            RISCVOpcode opcode = (reg->getType() == RegisterType::FLOAT) ? RISCVOpcode::FLW : RISCVOpcode::LW;
            auto inst = RISCVInstruction::createIType(opcode, reg, spReg, offset);
            currentBB->addInstruction(inst);
        }
    }
    else
    {
        // 偏移量超出范围，需要计算地址
        auto tempReg = getGeneralTempRegister(1); // 使用T1用于地址计算

        // li temp, offset
        auto liInst = RISCVInstruction::createPseudoLI(tempReg, offset);
        currentBB->addInstruction(liInst);

        // add temp, sp, temp
        auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, spReg, tempReg);
        currentBB->addInstruction(addInst);

        // lw/sw reg, 0(temp)
        if (isStore)
        {
            RISCVOpcode opcode = (reg->getType() == RegisterType::FLOAT) ? RISCVOpcode::FSW : RISCVOpcode::SW;
            auto memInst = RISCVInstruction::createSType(opcode, tempReg, reg, 0);
            currentBB->addInstruction(memInst);
        }
        else
        {
            RISCVOpcode opcode = (reg->getType() == RegisterType::FLOAT) ? RISCVOpcode::FLW : RISCVOpcode::LW;
            auto memInst = RISCVInstruction::createIType(opcode, reg, tempReg, 0);
            currentBB->addInstruction(memInst);
        }
    }
}

void InstructionSelector::generateConstantLoad(shared_ptr<RISCVRegister> reg, int64_t value)
{
    // 优化常量加载：根据常量大小选择最高效的指令序列

    if (value == 0)
    {
        // 常量0：直接使用zero寄存器
        auto moveInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, reg,
                                                       make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO));
        currentBB->addInstruction(moveInst);
    }
    else if (isImmediateInRange(value, 12))
    {
        // 12位立即数范围内：使用addi rd, zero, imm
        auto addiInst = RISCVInstruction::createIType(RISCVOpcode::ADDI, reg,
                                                      make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO),
                                                      static_cast<int>(value));
        currentBB->addInstruction(addiInst);
    }
    else if ((value & 0xFFF) == 0)
    {
        // 值是4096的倍数：只需要lui指令
        int upper = static_cast<int>(value >> 12);
        if (isImmediateInRange(upper, 20))
        {
            auto luiInst = RISCVInstruction::createUType(RISCVOpcode::LUI, reg, upper);
            currentBB->addInstruction(luiInst);
        }
        else
        {
            // 超出lui范围，使用li伪指令
            auto liInst = RISCVInstruction::createPseudoLI(reg, value);
            currentBB->addInstruction(liInst);
        }
    }
    else if (isImmediateInRange(value, 32))
    {
        // 32位常量：使用lui + addi组合
        int upper = static_cast<int>((value + 0x800) >> 12); // 考虑符号扩展
        int lower = static_cast<int>(value & 0xFFF);

        // 调整下半部分为有符号数
        if (lower > 2047)
        {
            lower -= 4096;
        }

        if (isImmediateInRange(upper, 20))
        {
            // lui rd, upper
            auto luiInst = RISCVInstruction::createUType(RISCVOpcode::LUI, reg, upper);
            currentBB->addInstruction(luiInst);

            // addi rd, rd, lower (如果lower不为0)
            if (lower != 0)
            {
                auto addiInst = RISCVInstruction::createIType(RISCVOpcode::ADDI, reg, reg, lower);
                currentBB->addInstruction(addiInst);
            }
        }
        else
        {
            // 超出范围，使用li伪指令
            auto liInst = RISCVInstruction::createPseudoLI(reg, value);
            currentBB->addInstruction(liInst);
        }
    }
    else
    {
        // 超大常量：使用li伪指令（让汇编器处理）
        auto liInst = RISCVInstruction::createPseudoLI(reg, value);
        currentBB->addInstruction(liInst);
    }
}

void InstructionSelector::generateFloatConstantLoad(shared_ptr<RISCVRegister> reg, float value)
{
    // 浮点常量加载：RISC-V没有直接的浮点立即数指令
    // 需要通过整数寄存器加载浮点数的位表示，然后转换到浮点寄存器

    // 将浮点数转换为32位整数位表示
    uint32_t bits;
    std::memcpy(&bits, &value, sizeof(float));

    // 创建临时的整数寄存器来加载位表示
    auto tempIntReg = getGeneralTempRegister(2); // 使用T2用于浮点常量加载

    // 特殊情况：0.0
    if (value == 0.0f)
    {
        // 直接使用fmv.w.x将zero寄存器的值移动到浮点寄存器
        auto fmvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_W_X, reg,
                                                      make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO));
        currentBB->addInstruction(fmvInst);
        return;
    }

    // 将浮点数的位表示作为整数加载到整数寄存器
    generateConstantLoad(tempIntReg, static_cast<int64_t>(bits));

    // 使用fmv.w.x指令将整数寄存器的值移动到浮点寄存器
    auto fmvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_W_X, reg, tempIntReg);
    currentBB->addInstruction(fmvInst);
}

// 按ABI规范预扫描函数，计算S、R、A三个值
void InstructionSelector::prescanFunction(shared_ptr<RISCVFunction> func, Function *irFunc)
{
    StackFrame &stackFrame = func->getStackFrame();

    int S = 0; // 局部变量需要的栈空间
    int R = 0; // ra寄存器需要的栈空间
    int A = 0; // 传参预留的栈空间

    bool hasCall = false;
    vector<int> callArgSizes; // 记录每个call指令的参数个数

    // 扫描所有基本块和指令
    for (const auto &bb : irFunc->BasicBlocks)
    {
        for (const auto &inst : bb->Instructions)
        {
            Instruction *instr = inst.get();

            // 统计call指令和参数数量
            if (instr->Op == Opcode::Call)
            {
                hasCall = true;
                if (auto callInst = dynamic_cast<CallInst *>(instr))
                {
                    int extraIntArgs = callInst->getIntArguments().size() - 8;
                    int extraFloatArgs = callInst->getFloatArguments().size() - 8;
                    int extraPtrArgs = callInst->getPtrArguments().size();

                    callArgSizes.push_back(std::max(extraIntArgs, 0) * 4 + std::max(extraFloatArgs, 0) * 4 + extraPtrArgs * 8);
                }
            }

            // 为所有有结果值的指令分配栈空间
            bool hasResult = false;
            int allocSize = 4; // 默认4字节

            switch (instr->Op)
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
            case Opcode::Load:
            case Opcode::Call:
            case Opcode::Copy:
                hasResult = !instr->getType()->isVoidTy();
                break;
            case Opcode::Alloca:
                if (auto allocaInst = dynamic_cast<AllocaInst *>(instr))
                {
                    Type *allocatedType = allocaInst->AllocatedType;
                    if (allocatedType->isArrayTy())
                    {
                        auto arrayType = static_cast<ArrayType *>(allocatedType);
                        allocSize = arrayType->getNumElements() * 4;
                    }
                    else
                    {
                        allocSize = 4;
                    }
                    hasResult = true;
                }
                break;
            case Opcode::GetElementPtr:
                hasResult = !instr->getType()->isVoidTy();
                allocSize = 8; // GEP通常返回指针，64位系统为8字节
                break;
            default:
                hasResult = false;
                break;
            }

            if (hasResult)
            {
                stackFrame.allocateSpace(instr, allocSize);
                S += allocSize;
            }
        }
    }

    // 计算R：如果有call指令则需要保存ra
    R = hasCall ? 4 : 0;

    // 计算A：传参需要的栈空间
    // A = max{max(len_i - 8, 0)} * 4，其中len_i是第i个call的参数个数
    A = std::max(0, *std::max_element(callArgSizes.begin(), callArgSizes.end()));

    // 计算总栈空间，向上取整到16字节对齐
    int totalSize = S + R + A;

    // 更新栈帧信息
    stackFrame.valueStackSize = S;
    stackFrame.raStackSize = R;
    stackFrame.argStackSize = A;
}

// 按ABI规范生成函数序言
void InstructionSelector::generateFunctionPrologue(shared_ptr<RISCVFunction> func)
{
    StackFrame &stackFrame = func->getStackFrame();
    int totalSize = stackFrame.getAlignedSize(); // 使用ABI计算的对齐大小

    if (totalSize > 0)
    {
        auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);

        // 1. 调整栈指针：addi sp, sp, -S'
        if (isImmediateInRange(-totalSize))
        {
            auto inst = RISCVInstruction::createIType(RISCVOpcode::ADDI, spReg, spReg, -totalSize);
            func->getBasicBlocks()[0]->addInstruction(inst);
        }
        else
        {
            // 需要用li + add
            auto tempReg = getGeneralTempRegister(0);
            auto liInst = RISCVInstruction::createPseudoLI(tempReg, -totalSize);
            func->getBasicBlocks()[0]->addInstruction(liInst);

            auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, spReg, spReg, tempReg);
            func->getBasicBlocks()[0]->addInstruction(addInst);
        }

        // 2. 如果R不为0，保存ra寄存器到 sp + S' - 4
        if (stackFrame.raStackSize > 0)
        {
            auto raReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::RA);
            int raOffset = totalSize - 4;

            if (isImmediateInRange(raOffset))
            {
                auto swInst = RISCVInstruction::createSType(RISCVOpcode::SW, spReg, raReg, raOffset);
                func->getBasicBlocks()[0]->addInstruction(swInst);
            }
            else
            {
                // 偏移量太大，需要先计算地址
                auto tempReg = getGeneralTempRegister(0);
                auto liInst = RISCVInstruction::createPseudoLI(tempReg, raOffset);
                func->getBasicBlocks()[0]->addInstruction(liInst);

                auto swInst = RISCVInstruction::createSType(RISCVOpcode::SW, tempReg, raReg, 0);
                func->getBasicBlocks()[0]->addInstruction(swInst);
            }
        }
    }
}

// 按ABI规范生成函数尾声
void InstructionSelector::generateFunctionEpilogue(shared_ptr<RISCVFunction> func)
{
    StackFrame &stackFrame = func->getStackFrame();
    int totalSize = stackFrame.getAlignedSize(); // 使用ABI计算的对齐大小

    if (totalSize > 0)
    {
        auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);

        // 1. 如果保存了ra，恢复ra寄存器
        if (stackFrame.raStackSize > 0)
        {
            auto raReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::RA);
            int raOffset = totalSize - 4;

            if (isImmediateInRange(raOffset))
            {
                auto lwInst = RISCVInstruction::createIType(RISCVOpcode::LW, raReg, spReg, raOffset);
                currentBB->addInstruction(lwInst);
            }
            else
            {
                // 偏移量太大，需要先计算地址
                auto tempReg = getGeneralTempRegister(2); // 使用T2计算ra加载地址
                auto liInst = RISCVInstruction::createPseudoLI(tempReg, raOffset);
                currentBB->addInstruction(liInst);

                auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, spReg, tempReg);
                currentBB->addInstruction(addInst);

                auto lwInst = RISCVInstruction::createIType(RISCVOpcode::LW, raReg, tempReg, 0);
                currentBB->addInstruction(lwInst);
            }
        }

        // 2. 恢复栈指针：addi sp, sp, S'
        if (isImmediateInRange(totalSize))
        {
            auto inst = RISCVInstruction::createIType(RISCVOpcode::ADDI, spReg, spReg, totalSize);
            currentBB->addInstruction(inst);
        }
        else
        {
            auto tempReg = getGeneralTempRegister(1); // 使用T1用于栈指针计算
            auto liInst = RISCVInstruction::createPseudoLI(tempReg, totalSize);
            currentBB->addInstruction(liInst);

            auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, spReg, spReg, tempReg);
            currentBB->addInstruction(addInst);
        }
    }
}

bool InstructionSelector::isImmediateInRange(int immediate, int bits)
{
    int minVal = -(1 << (bits - 1));
    int maxVal = (1 << (bits - 1)) - 1;
    return immediate >= minVal && immediate <= maxVal;
}

void InstructionSelector::mapArguments(shared_ptr<RISCVFunction> func, Function *irFunc)
{
    // 按照RISC-V ABI规范处理函数参数
    // 前8个整数参数使用a0-a7，前8个浮点参数使用fa0-fa7
    // 超出的参数从调用者栈帧获取

    int intArgIndex = 0;
    int floatArgIndex = 0;
    int ptrArgIndex = 0;

    auto args = irFunc->getArguments();

    for (const auto &arg : args)
    {
        // 根据参数类型分类
        if (arg->getType()->isIntegerTy())
        {
            // 整数参数
            if (intArgIndex < 8)
            {
                // 使用a0-a7寄存器
                auto reg = make_shared<RISCVRegister>(static_cast<RISCVRegister::PhysicalReg>(
                    static_cast<int>(RISCVRegister::PhysicalReg::A0) + intArgIndex));
                registerMap[arg] = reg;
                intArgIndex++;
            }
            else
            {
                // 超出范围的整数参数从栈获取
                stackArguments[arg] = (intArgIndex + floatArgIndex) * 4 + ptrArgIndex * 8;
                intArgIndex++;
            }
        }
        else if (arg->getType()->isFloatTy())
        {
            // 浮点参数
            if (floatArgIndex < 8)
            {
                // 使用fa0-fa7寄存器
                auto reg = make_shared<RISCVRegister>(static_cast<RISCVRegister::PhysicalReg>(
                    static_cast<int>(RISCVRegister::PhysicalReg::FA0) + floatArgIndex));
                registerMap[arg] = reg;
                floatArgIndex++;
            }
            else
            {
                // 超出范围的浮点参数从栈获取
                stackArguments[arg] = (intArgIndex + floatArgIndex) * 4 + ptrArgIndex * 8;
                floatArgIndex++;
            }
        }
        else if (arg->getType()->isPointerTy())
        {
            // 指针参数（假设指针也使用a0-a7）
            if (intArgIndex + ptrArgIndex < 8)
            {
                // 使用a0-a7寄存器
                auto reg = make_shared<RISCVRegister>(static_cast<RISCVRegister::PhysicalReg>(
                    static_cast<int>(RISCVRegister::PhysicalReg::A0) + intArgIndex));
                registerMap[arg] = reg;
                intArgIndex++;
            }
            else
            {
                // 超出范围的指针参数从栈获取
                stackArguments[arg] = (intArgIndex + floatArgIndex) * 4 + ptrArgIndex * 8;
                ptrArgIndex++;
            }
        }
    }
}

void RISCVBuilder::processGlobalInitializer(shared_ptr<RISCVGlobalBlock> globalBlock, Constant *initializer)
{
    // 处理全局变量初始化器 - 只添加数值，不添加.word前缀
    if (auto constInt = dynamic_cast<ConstantInt *>(initializer))
    {
        globalBlock->addData(std::to_string(constInt->Value));
    }
    else if (auto constFloat = dynamic_cast<ConstantFloat *>(initializer))
    {
        // 将浮点数转换为32位整数表示
        uint32_t bits;
        std::memcpy(&bits, &constFloat->Value, sizeof(float));
        globalBlock->addData(std::to_string(bits));
    }
    else
    {
        // 默认零初始化
        globalBlock->addData("0");
    }
}

void RISCVBuilder::processZeroInitializer(shared_ptr<RISCVGlobalBlock> globalBlock, Type *type)
{
    // 处理零初始化 - 只添加数值，不添加.word前缀
    if (type->isArrayTy())
    {
        auto arrayType = static_cast<ArrayType *>(type);
        int numElements = arrayType->getNumElements();
        for (int i = 0; i < numElements; ++i)
        {
            globalBlock->addData("0");
        }
    }
    else
    {
        globalBlock->addData("0");
    }
}

// // 简化的寄存器分配器实现
// void RegisterAllocator::allocateRegisters(shared_ptr<RISCVFunction> func)
// {
//     // 简单的寄存器分配：所有虚拟寄存器保持虚拟状态
//     // 在实际汇编生成时处理具体的物理寄存器分配

//     // 这里暂时不做复杂的寄存器分配，
//     // 因为我们的重点是测试ABI规范的实现
// }

// AssemblyEmitter 实现
string AssemblyEmitter::emit(shared_ptr<RISCVModule> module)
{
    stringstream ss;

    // 生成全局变量段
    if (!module->getGlobalBlocks().empty())
    {
        ss << ".data\n";
        ss << emitGlobals(module->getGlobalBlocks());
    }

    // 生成函数
    for (const auto &func : module->getFunctions())
    {
        // 检查是否是库函数，如果是则跳过生成
        if (isLibraryFunction(func->getName()))
        {
            continue;
        }

        else

            ss << emitFunction(func) << "\n";
    }

    return ss.str();
}

string AssemblyEmitter::emitGlobals(const vector<shared_ptr<RISCVGlobalBlock>> &globals)
{
    stringstream ss;
    for (const auto &global : globals)
    {
        ss << global->toString() << "\n";
    }
    return ss.str();
}

string AssemblyEmitter::emitFunction(shared_ptr<RISCVFunction> func)
{
    stringstream ss;

    // 函数标签
    ss << ".text\n";
    ss << ".align 2\n"; // 对齐到4字节

    ss << "\n";
    ss << ".globl " << func->getName() << "\n";

    ss << func->getName() << ":\n";

    // 生成每个基本块
    for (const auto &bb : func->getBasicBlocks())
    {
        ss << emitBasicBlock(bb);
    }

    return ss.str();
}

string AssemblyEmitter::emitBasicBlock(shared_ptr<RISCVBasicBlock> bb)
{
    stringstream ss;

    // 基本块标签（如果不是入口块）
    if (bb->getLabel() != bb->getParentFunc()->getName())
    {
        ss << bb->getLabel() << ":\n";
    }

    // 生成指令
    for (const auto &inst : bb->getInstructions())
    {
        ss << "    " << inst->toString() << "\n";
    }

    return ss.str();
}

bool AssemblyEmitter::isLibraryFunction(const string &funcName)
{
    // 检查是否是库函数 - 包括SysY运行时库函数
    static const set<string> libFuncs = {
        // 标准C库函数
        "printf", "scanf", "malloc", "free", "memcpy", "strlen",
        // SysY运行时库函数
        "getint", "getch", "getfloat", "getarray", "getfarray",
        "putint", "putch", "putfloat", "putarray", "putfarray", "putf",
        "starttime", "stoptime", "_sysy_starttime", "_sysy_stoptime"};
    return libFuncs.count(funcName) > 0;
}

// 临时寄存器管理方法的实现
shared_ptr<RISCVRegister> InstructionSelector::getTempRegister(RegisterType type, int index)
{
    if (type == RegisterType::GENERAL)
    {
        return getGeneralTempRegister(index);
    }
    else if (type == RegisterType::FLOAT)
    {
        return getFloatTempRegister(index);
    }
    return nullptr;
}

shared_ptr<RISCVRegister> InstructionSelector::getGeneralTempRegister(int index)
{
    // 预定义的通用临时寄存器，确保不会冲突
    static vector<RISCVRegister::PhysicalReg> tempRegs = {
        RISCVRegister::PhysicalReg::T0, // index 0: 主要临时寄存器
        RISCVRegister::PhysicalReg::T1, // index 1: 地址计算用
        RISCVRegister::PhysicalReg::T2, // index 2: 复杂操作用
        RISCVRegister::PhysicalReg::T3, // index 3: 备用
        RISCVRegister::PhysicalReg::T4, // index 4: 备用
        RISCVRegister::PhysicalReg::T5, // index 5: 备用
        RISCVRegister::PhysicalReg::T6  // index 6: 备用
    }; // 如果指定了特定的index，使用指定的寄存器
    if (index >= 1 && index < tempRegs.size())
    {
        return make_shared<RISCVRegister>(tempRegs[index]);
    }

    // 否则使用轮换策略避免冲突
    int rotatedIndex = generalTempCounter % tempRegs.size();
    generalTempCounter++;

    return make_shared<RISCVRegister>(tempRegs[rotatedIndex]);
}

shared_ptr<RISCVRegister> InstructionSelector::getFloatTempRegister(int index)
{
    // 预定义的浮点临时寄存器，确保不会冲突
    static vector<RISCVRegister::PhysicalReg> tempRegs = {
        RISCVRegister::PhysicalReg::FT0,  // index 0: 主要浮点临时寄存器
        RISCVRegister::PhysicalReg::FT1,  // index 1: 辅助浮点临时寄存器
        RISCVRegister::PhysicalReg::FT2,  // index 2: 复杂操作用
        RISCVRegister::PhysicalReg::FT3,  // index 3: 备用
        RISCVRegister::PhysicalReg::FT4,  // index 4: 备用
        RISCVRegister::PhysicalReg::FT5,  // index 5: 备用
        RISCVRegister::PhysicalReg::FT6,  // index 6: 备用
        RISCVRegister::PhysicalReg::FT7,  // index 7: 备用
        RISCVRegister::PhysicalReg::FT8,  // index 8: 备用
        RISCVRegister::PhysicalReg::FT9,  // index 9: 备用
        RISCVRegister::PhysicalReg::FT10, // index 10: 备用
        RISCVRegister::PhysicalReg::FT11  // index 11: 备用
    }; // 如果指定了特定的index，使用指定的寄存器
    if (index >= 1 && index < tempRegs.size())
    {
        return make_shared<RISCVRegister>(tempRegs[index]);
    }

    // 否则使用轮换策略避免冲突
    int rotatedIndex = floatTempCounter % tempRegs.size();
    floatTempCounter++;

    return make_shared<RISCVRegister>(tempRegs[rotatedIndex]);
}
