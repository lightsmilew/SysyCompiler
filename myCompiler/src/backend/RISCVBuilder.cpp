#include "RISCVBuilder.h"
#include <sstream>
#include <iostream>
#include <algorithm>

using namespace RISCV;
using std::endl;
using std::max;
using std::stringstream;

shared_ptr<RISCVModule> RISCVBuilder::generateRISCVCode(shared_ptr<Module> irModule)
{
    // 保存IR模块引用
    this->irModule = irModule;

    // 流水线各阶段
    initializeModule(irModule);
    generateInstructions();
    allocateRegisters();
    optimizeCode();

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
        auto globalBlock = riscvModule->createGlobalBlock(globalVar->getName(), globalVar->IsConstant);
        // 初始化全局变量的数据
        if (globalVar->Initializer)
        {
            // TODO: 处理全局变量的初始化数据
        }
        riscvModule->addGlobalBlock(globalBlock);
    }

    // 初始化函数
    for (const auto &func : irModule->Functions)
    {
        auto riscvFunc = make_shared<RISCVFunction>(func->getName(), riscvModule);
        riscvModule->addFunction(riscvFunc);

        // 为每个IR基本块创建对应的RISC-V基本块
        for (const auto &bb : func->BasicBlocks)
        {
            auto riscvBB = make_shared<RISCVBasicBlock>(bb->getName(), riscvFunc);
            riscvFunc->addBasicBlock(riscvBB);
        }
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

void RISCVBuilder::allocateRegisters()
{
    // 为每个函数进行寄存器分配
    for (const auto &func : riscvModule->getFunctions())
    {
        RegisterAllocator allocator;
        allocator.allocateRegisters(func);
    }
}

void RISCVBuilder::optimizeCode()
{
    // 为每个函数进行窥孔优化
    for (const auto &func : riscvModule->getFunctions())
    {
        PeepholeOptimizer optimizer;
        optimizer.optimize(func);
    }
}

// ===== InstructionSelector 实现 =====

void InstructionSelector::selectInstructions(shared_ptr<RISCVFunction> func, Function *irFunc)
{
    currentFunc = func;

    // 创建虚拟寄存器映射表
    registerMap.clear();

    // 处理函数参数
    mapArguments(func, irFunc);

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
    case Opcode::FPToSI:
        // TODO: 处理类型转换指令
        break;
    default:
        // 其他指令暂时忽略
        break;
    }
}

void InstructionSelector::visitBinaryOp(BinaryOperator *inst)
{
    auto destReg = getOrCreateVirtualReg(inst);
    auto lhsReg = getOrCreateVirtualReg(inst->LHS);
    auto rhsReg = getOrCreateVirtualReg(inst->RHS);

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

    auto riscvInst = RISCVInstruction::createRType(opcode, destReg, lhsReg, rhsReg);
    currentBB->addInstruction(riscvInst);
}

void InstructionSelector::visitLoadInst(LoadInst *inst)
{
    auto destReg = getOrCreateVirtualReg(inst);
    auto ptrReg = getOrCreateVirtualReg(inst->Pointer);

    RISCVOpcode opcode = inst->getType()->isFloatTy() ? RISCVOpcode::FLW : RISCVOpcode::LW;

    // 生成加载指令 (假设偏移为0)
    auto riscvInst = RISCVInstruction::createIType(opcode, destReg, ptrReg, 0);
    currentBB->addInstruction(riscvInst);
}

void InstructionSelector::visitStoreInst(StoreInst *inst)
{
    auto valueReg = getOrCreateVirtualReg(inst->ValueToStore);
    auto ptrReg = getOrCreateVirtualReg(inst->Pointer);

    RISCVOpcode opcode = inst->ValueToStore->getType()->isFloatTy() ? RISCVOpcode::FSW : RISCVOpcode::SW;

    // 生成存储指令 (假设偏移为0)
    auto riscvInst = RISCVInstruction::createSType(opcode, valueReg, ptrReg, 0);
    currentBB->addInstruction(riscvInst);
}

void InstructionSelector::visitCallInst(CallInst *inst)
{
    // 函数调用比较复杂，需要处理参数传递、调用约定等
    // 这里先生成一个简单的JAL指令

    if (inst->CalledFunction)
    {
        auto returnReg = inst->getType()->isVoidTy() ? nullptr : getOrCreateVirtualReg(inst);

        // 参数传递 (简化处理)
        // TODO: 实现完整的调用约定

        // 生成函数调用指令
        auto riscvInst = RISCVInstruction::createJType(RISCVOpcode::JAL,
                                                       make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::RA),
                                                       inst->CalledFunction->getName());
        currentBB->addInstruction(riscvInst);
    }
}

void InstructionSelector::visitReturnInst(ReturnInst *inst)
{
    if (inst->ReturnValue)
    {
        // 有返回值，将值移动到返回寄存器
        auto valueReg = getOrCreateVirtualReg(inst->ReturnValue);
        auto returnReg = make_shared<RISCVRegister>(
            inst->ReturnValue->getType()->isFloatTy() ? RISCVRegister::PhysicalReg::FA0 : RISCVRegister::PhysicalReg::A0);

        auto moveInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, returnReg, valueReg);
        currentBB->addInstruction(moveInst);
    }

    // 生成返回指令
    auto retInst = RISCVInstruction::createIType(RISCVOpcode::JALR,
                                                 make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO),
                                                 make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::RA),
                                                 0);
    currentBB->addInstruction(retInst);
}

void InstructionSelector::visitBranchInst(BranchInst *inst)
{
    if (inst->Condition)
    {
        // 条件分支
        auto condReg = getOrCreateVirtualReg(inst->Condition);

        // 生成条件分支指令
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
    // 这里简化处理，只分配虚拟寄存器来保存地址
    auto destReg = getOrCreateVirtualReg(inst);

    // TODO: 实际应该在栈帧中分配空间，这里先用简化版本
    currentFunc->getStackFrame().localVarSize += 8; // 假设分配8字节
}

void InstructionSelector::visitElementPtrInst(GetElementPtrInst *inst)
{
    auto destReg = getOrCreateVirtualReg(inst);
    auto ptrReg = getOrCreateVirtualReg(inst->PointerOperand);

    // 简化的GEP处理，假设只有一个索引
    if (!inst->Indices.empty())
    {
        auto indexReg = getOrCreateVirtualReg(inst->Indices[0]);

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
}

void InstructionSelector::visitICmpInst(ICmpInst *inst)
{
    auto destReg = getOrCreateVirtualReg(inst);
    auto lhsReg = getOrCreateVirtualReg(inst->LHS);
    auto rhsReg = getOrCreateVirtualReg(inst->RHS);

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
        return;
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
        return;
    default:
        return;
    }

    if (inst->Pred == ICmpInst::ICMP_SLT || inst->Pred == ICmpInst::ICMP_SGT)
    {
        auto cmpInst = RISCVInstruction::createRType(opcode, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(cmpInst);
    }
}

void InstructionSelector::visitFCmpInst(FCmpInst *inst)
{
    auto destReg = getOrCreateVirtualReg(inst);
    auto lhsReg = getOrCreateVirtualReg(inst->LHS);
    auto rhsReg = getOrCreateVirtualReg(inst->RHS);

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
        return;
    default:
        return;
    }

    auto cmpInst = RISCVInstruction::createRType(opcode, destReg, lhsReg, rhsReg);
    currentBB->addInstruction(cmpInst);
}

shared_ptr<RISCVRegister> InstructionSelector::getOrCreateVirtualReg(Value *value)
{
    // 处理常量
    if (auto constInt = dynamic_cast<ConstantInt *>(value))
    {
        // 对于常量，创建临时寄存器并生成LI指令
        auto tempReg = make_shared<RISCVRegister>(RegisterType::GENERAL);
        auto liInst = RISCVInstruction::createPseudoLI(tempReg, constInt->Value);
        currentBB->addInstruction(liInst);
        return tempReg;
    }

    if (auto constFloat = dynamic_cast<ConstantFloat *>(value))
    {
        // 对于浮点常量，需要更复杂的处理
        auto tempReg = make_shared<RISCVRegister>(RegisterType::FLOAT);
        // TODO: 实现浮点常量加载
        return tempReg;
    }

    // 查找已存在的映射
    auto it = registerMap.find(value);
    if (it != registerMap.end())
    {
        return it->second;
    }

    // 创建新的虚拟寄存器
    RegisterType regType = value->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::GENERAL;
    auto virtualReg = make_shared<RISCVRegister>(regType);
    registerMap[value] = virtualReg;

    return virtualReg;
}

void InstructionSelector::mapArguments(shared_ptr<RISCVFunction> func, Function *irFunc)
{
    vector<shared_ptr<RISCVRegister>> argRegs;

    for (size_t i = 0; i < irFunc->Arguments.size(); ++i)
    {
        auto arg = irFunc->Arguments[i].get();
        bool isFloat = arg->getType()->isFloatTy();

        shared_ptr<RISCVRegister> argReg;
        if (i < 8)
        { // 前8个参数通过寄存器传递
            if (isFloat)
            {
                argReg = make_shared<RISCVRegister>(
                    static_cast<RISCVRegister::PhysicalReg>(
                        static_cast<int>(RISCVRegister::PhysicalReg::FA0) + i));
            }
            else
            {
                argReg = make_shared<RISCVRegister>(
                    static_cast<RISCVRegister::PhysicalReg>(
                        static_cast<int>(RISCVRegister::PhysicalReg::A0) + i));
            }
        }
        else
        {
            // 超过8个参数通过栈传递
            argReg = make_shared<RISCVRegister>(isFloat ? RegisterType::FLOAT : RegisterType::GENERAL);
            // TODO: 处理栈参数
        }

        registerMap[arg] = argReg;
        argRegs.push_back(argReg);
    }

    func->setArgRegs(argRegs);
}

void InstructionSelector::handleFunctionPrologue(shared_ptr<RISCVFunction> func)
{
    // TODO: 实现函数序言
}

void InstructionSelector::handleFunctionEpilogue(shared_ptr<RISCVFunction> func)
{
    // TODO: 实现函数尾声
}

// ===== RegisterAllocator 实现 =====

// 可用的物理寄存器定义
const vector<shared_ptr<RISCVRegister>> RegisterAllocator::availableGeneralRegs = {
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T0),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T1),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T2),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S0),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S1),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S2),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S3),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S4),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S5),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S6),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S7),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S8),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S9),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S10),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S11),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T3),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T4),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T5),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T6)};

const vector<shared_ptr<RISCVRegister>> RegisterAllocator::availableFloatRegs = {
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT0),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT1),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT2),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT3),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT4),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT5),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT6),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT7),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS0),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS1),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS2),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS3),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS4),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS5),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS6),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS7),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS8),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS9),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS10),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS11),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT8),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT9),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT10),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT11)};

void RegisterAllocator::allocateRegisters(shared_ptr<RISCVFunction> func)
{
    currentFunc = func;

    // 计算活跃变量信息
    computeLiveness();

    // 计算活跃区间
    computeLiveIntervals();

    // 线性扫描寄存器分配
    linearScanAllocation();

    // 插入溢出代码
    insertSpillCode();
}

void RegisterAllocator::computeLiveness()
{
    // 简化版活跃变量分析
    // 在实际实现中，这里应该使用数据流分析算法

    // 遍历所有基本块
    for (auto bb : currentFunc->getBasicBlocks())
    {
        unordered_set<shared_ptr<RISCVRegister>> liveIn, liveOut;

        // 向后遍历指令来计算活跃变量
        auto instructions = bb->getInstructions();
        for (auto it = instructions.rbegin(); it != instructions.rend(); ++it)
        {
            auto inst = *it;

            // 简化处理：假设所有使用的寄存器都是活跃的
            // 实际实现需要更精确的分析
        }

        bb->setLiveIn(liveIn);
        bb->setLiveOut(liveOut);
    }
}

void RegisterAllocator::computeLiveIntervals()
{
    intervals.clear();

    // 为每个虚拟寄存器创建活跃区间
    // 这里使用简化的方法：每个虚拟寄存器的活跃区间为其定义到最后使用

    unordered_map<shared_ptr<RISCVRegister>, int> firstDef;
    unordered_map<shared_ptr<RISCVRegister>, int> lastUse;

    int instrCount = 0;
    for (auto bb : currentFunc->getBasicBlocks())
    {
        for (auto inst : bb->getInstructions())
        {
            // 简化处理：假设指令的操作数都是虚拟寄存器
            instrCount++;
        }
    }

    // 创建活跃区间
    for (auto &pair : firstDef)
    {
        auto reg = pair.first;
        int start = pair.second;
        int end = lastUse.count(reg) ? lastUse[reg] : start;

        intervals.emplace_back(reg, start, end);
    }

    // 按起始点排序
    sort(intervals.begin(), intervals.end(),
         [](const LiveInterval &a, const LiveInterval &b)
         {
             return a.start < b.start;
         });
}

void RegisterAllocator::linearScanAllocation()
{
    vector<LiveInterval *> active;

    for (auto &interval : intervals)
    {
        // 移除已结束的区间
        expireOldIntervals(interval, active);

        // 尝试分配寄存器
        bool isFloat = interval.virtualReg->getType() == RegisterType::FLOAT;
        const auto &availableRegs = isFloat ? availableFloatRegs : availableGeneralRegs;

        if (active.size() < availableRegs.size())
        {
            // 有可用寄存器
            interval.assignedReg = availableRegs[active.size()];
            active.push_back(&interval);
        }
        else
        {
            // 需要溢出
            spillAtInterval(interval, active);
        }
    }
}

void RegisterAllocator::expireOldIntervals(const LiveInterval &current,
                                           vector<LiveInterval *> &active)
{
    auto it = active.begin();
    while (it != active.end())
    {
        if ((*it)->end >= current.start)
        {
            ++it;
        }
        else
        {
            it = active.erase(it);
        }
    }
}

void RegisterAllocator::spillAtInterval(LiveInterval &current,
                                        vector<LiveInterval *> &active)
{
    // 找到结束最晚的区间
    auto spill = *max_element(active.begin(), active.end(),
                              [](const LiveInterval *a, const LiveInterval *b)
                              {
                                  return a->end < b->end;
                              });

    if (spill->end > current.end)
    {
        // 溢出选中的区间
        current.assignedReg = spill->assignedReg;
        spill->assignedReg = nullptr;
        spill->isSpilled = true;

        // 从active中移除被溢出的区间，添加当前区间
        active.erase(find(active.begin(), active.end(), spill));
        active.push_back(&current);
    }
    else
    {
        // 溢出当前区间
        current.isSpilled = true;
    }
}

void RegisterAllocator::insertSpillCode()
{
    // 为溢出的寄存器插入load/store指令
    int spillOffset = 0;

    for (auto &interval : intervals)
    {
        if (interval.isSpilled)
        {
            // 分配栈空间
            spillOffset += 8; // 假设8字节对齐
            currentFunc->getStackFrame().tempVarSize = max(
                currentFunc->getStackFrame().tempVarSize, spillOffset);

            // TODO: 在使用点插入load指令，在定义点插入store指令
        }
    }
}

// ===== PeepholeOptimizer 实现 =====

void PeepholeOptimizer::optimize(shared_ptr<RISCVFunction> func)
{
    currentFunc = func;

    for (auto bb : func->getBasicBlocks())
    {
        bool changed = true;
        while (changed)
        {
            changed = false;
            changed |= optimizeRedundantMoves(bb);
            changed |= optimizeConstantFolding(bb);
            changed |= optimizeDeadCode(bb);
        }
    }
}

bool PeepholeOptimizer::optimizeRedundantMoves(shared_ptr<RISCVBasicBlock> bb)
{
    // 移除冗余的移动指令 mv rd, rs (当rd == rs时)
    bool changed = false;
    auto &instructions = const_cast<vector<shared_ptr<RISCVInstruction>> &>(bb->getInstructions());

    for (auto it = instructions.begin(); it != instructions.end();)
    {
        auto inst = *it;
        if (inst->getOpcode() == RISCVOpcode::MV)
        {
            auto operands = inst->getOperands();
            if (operands.size() >= 2)
            {
                auto dst = operands[0]->getReg();
                auto src = operands[1]->getReg();
                if (dst && src && *dst == *src)
                {
                    it = instructions.erase(it);
                    changed = true;
                    continue;
                }
            }
        }
        ++it;
    }

    return changed;
}

bool PeepholeOptimizer::optimizeConstantFolding(shared_ptr<RISCVBasicBlock> bb)
{
    // 简单的常量折叠优化
    // 例如：addi rd, rs, 0 -> mv rd, rs
    bool changed = false;
    auto &instructions = const_cast<vector<shared_ptr<RISCVInstruction>> &>(bb->getInstructions());

    for (auto it = instructions.begin(); it != instructions.end(); ++it)
    {
        auto inst = *it;
        if (inst->getOpcode() == RISCVOpcode::ADDI)
        {
            auto operands = inst->getOperands();
            if (operands.size() >= 3 && operands[2]->getImmediate() == 0)
            {
                // addi rd, rs, 0 -> mv rd, rs
                auto newInst = RISCVInstruction::createPseudo(RISCVOpcode::MV,
                                                              operands[0]->getReg(),
                                                              operands[1]->getReg());
                *it = newInst;
                changed = true;
            }
        }
    }

    return changed;
}

bool PeepholeOptimizer::optimizeDeadCode(shared_ptr<RISCVBasicBlock> bb)
{
    // 移除死代码（未使用的指令）
    // 这里只做简单的死代码检测
    bool changed = false;

    // TODO: 实现更完整的死代码消除

    return changed;
}

// ===== AssemblyEmitter 实现 =====

string AssemblyEmitter::emit(shared_ptr<RISCVModule> module)
{
    stringstream ss;

    // 输出汇编头部
    ss << ".text" << endl;
    ss << ".globl main" << endl
       << endl;

    // 输出全局变量
    if (!module->getGlobalBlocks().empty())
    {
        ss << emitGlobals(module->getGlobalBlocks()) << endl;
    }

    // 输出函数
    for (auto func : module->getFunctions())
    {
        ss << emitFunction(func) << endl;
    }

    return ss.str();
}

string AssemblyEmitter::emitGlobals(const vector<shared_ptr<RISCVGlobalBlock>> &globals)
{
    stringstream ss;

    ss << ".data" << endl;
    for (auto global : globals)
    {
        ss << global->toString() << endl;
    }

    return ss.str();
}

string AssemblyEmitter::emitFunction(shared_ptr<RISCVFunction> func)
{
    stringstream ss;

    // 函数标签
    ss << func->getName() << ":" << endl;

    // 函数序言
    auto &stackFrame = func->getStackFrame();
    int totalStackSize = stackFrame.getTotalSize();

    if (totalStackSize > 0)
    {
        ss << "    addi sp, sp, -" << totalStackSize << endl;
        ss << "    sw ra, " << (totalStackSize - 4) << "(sp)" << endl;
    }

    // 输出基本块
    for (auto bb : func->getBasicBlocks())
    {
        ss << emitBasicBlock(bb) << endl;
    }

    // 函数尾声（如果没有显式的返回指令）
    if (totalStackSize > 0)
    {
        ss << "    lw ra, " << (totalStackSize - 4) << "(sp)" << endl;
        ss << "    addi sp, sp, " << totalStackSize << endl;
    }
    ss << "    ret" << endl;

    return ss.str();
}

string AssemblyEmitter::emitBasicBlock(shared_ptr<RISCVBasicBlock> bb)
{
    stringstream ss;

    // 基本块标签
    if (!bb->getLabel().empty())
    {
        ss << bb->getLabel() << ":" << endl;
    }

    // 输出指令
    for (auto inst : bb->getInstructions())
    {
        ss << "    " << inst->toString() << endl;
    }

    return ss.str();
}