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
        riscvFunc->addBasicBlock(make_shared<RISCVBasicBlock>("prologue_" + func->getName(), riscvFunc));

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

    // 不需要生成函数尾声
}

// 当基本块中使用alloca指令访问函数参数时，我应该将该块空间与寄存器联合起来
void InstructionSelector::visitInstruction(Instruction *inst)
{
    // 清理上一条指令的临时寄存器
    releaseAllCurrentTemps();

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

    // 指令处理完成后，自动释放本条指令使用的所有临时寄存器
    // 由于结果已经存储到栈上，可以安全释放
    releaseAllCurrentTemps();
}

void InstructionSelector::visitBinaryOp(BinaryOperator *inst)
{
    auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
    auto rhsReg = getOrCreateVirtualReg(inst->getRHS());

    // 创建临时寄存器存储计算结果，避免与操作数冲突
    RegisterType regType = inst->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::GENERAL;
    auto tempReg = allocateTempRegister(regType, "binary_result_" + inst->getDest()->getName());

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
    auto riscvInst = RISCVInstruction::createRType(opcode, tempReg, lhsReg, rhsReg);
    currentBB->addInstruction(riscvInst);

    // 将结果存储到栈中
    storeValueToStack(inst->getDest(), tempReg, 4);
}

void InstructionSelector::visitLoadInst(LoadInst *inst)
{
    // Load指令从内存地址加载数据
    // 对于数组元素访问：从GEP计算得到的元素地址加载数据
    // 对于标量变量：从变量地址加载数据

    auto ptrReg = getOrCreateVirtualReg(inst->getPointer());

    // 创建临时寄存器存储加载的数据
    RegisterType regType = inst->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::GENERAL;
    auto valueReg = allocateTempRegister(regType, "phi_value");

    // 根据数据类型选择合适的加载指令
    RISCVOpcode loadOpcode;
    if (inst->getType()->isFloatTy())
    {
        loadOpcode = RISCVOpcode::FLW; // 浮点数加载
    }
    else if (inst->getType()->isIntegerTy())
    {
        loadOpcode = RISCVOpcode::LW; // 整数加载
    }
    else if (inst->getType()->isPointerTy())
    {
        loadOpcode = RISCVOpcode::LD; // 指针加载
    }

    else
    {
        loadOpcode = RISCVOpcode::LW; // 默认使用字加载
    }

    // 生成加载指令：lw/flw rd, 0(addr)
    // 这里偏移量为0，因为ptrReg已经是确切的目标地址
    auto loadInst = RISCVInstruction::createIType(loadOpcode, valueReg, ptrReg, 0);
    currentBB->addInstruction(loadInst);

    // 将加载的数据存储到栈中供后续指令使用
    storeValueToStack(inst->getDest(), valueReg);
}

void InstructionSelector::visitStoreInst(StoreInst *inst)
{
    // Store指令将数据存储到内存地址
    // 对于数组元素赋值：将数据存储到GEP计算得到的元素地址
    // 对于标量变量赋值：将数据存储到变量地址

    auto valueReg = getOrCreateVirtualReg(inst->getValueToStore());
    auto ptrReg = getOrCreateVirtualReg(inst->getPointer());

    // 根据要存储的数据类型选择合适的存储指令
    RISCVOpcode storeOpcode;
    if (inst->getValueToStore()->getType()->isFloatTy())
    {
        storeOpcode = RISCVOpcode::FSW; // 浮点数存储
    }
    else if (inst->getValueToStore()->getType()->isIntegerTy())
    {
        storeOpcode = RISCVOpcode::SW; // 整数存储
    }
    else if (inst->getValueToStore()->getType()->isPointerTy())
    {
        storeOpcode = RISCVOpcode::SD; // 指针存储
    }
    else
    {
        storeOpcode = RISCVOpcode::SW; // 默认使用字存储
    }

    // 生成存储指令：sw/fsw rs2, 0(addr)
    // 这里偏移量为0，因为ptrReg已经是确切的目标地址
    auto storeInst = RISCVInstruction::createSType(storeOpcode, ptrReg, valueReg, 0);
    currentBB->addInstruction(storeInst);

    // Store指令没有返回值，不需要存储结果到栈
}

void InstructionSelector::visitCallInst(CallInst *inst)
{
    if (!inst->getCalledFunction())
        return;

    // 1. 处理参数传递 - 严格按照RISC-V ABI规范
    // 前8个整数/指针参数使用a0-a7，前8个浮点参数使用fa0-fa7
    // 超出的参数按顺序存放在调用者栈帧的参数区域

    int intArgIndex = 0;
    int floatArgIndex = 0;
    int stackArgIndex = 0;

    // 逐个处理每个参数，避免同时占用过多临时寄存器
    for (size_t i = 0; i < inst->getArguments().size(); ++i)
    {
        auto arg = inst->getArguments()[i];
        // 为当前参数获取寄存器（每次只处理一个参数）
        auto argReg = getOrCreateVirtualReg(arg);

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

        releaseTempRegister(argReg); // 释放当前参数的临时寄存器
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
        storeValueToStack(inst->getDest(), returnPhysReg);
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

    // 生成函数结尾（恢复栈帧）
    generateFunctionEpilogue(currentFunc);

    if (currentFunc->getName() == "main")
    {
        // li a0, 0
        // call exit
        auto liRetInst = RISCVInstruction::createPseudoLI(make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A0), (int64_t)0);
        currentBB->addInstruction(liRetInst);
        auto callExitInst = RISCVInstruction::createPseudoCALL("exit");
        currentBB->addInstruction(callExitInst);
    }
    else
    {
        // 对于其他函数，直接返回
        auto retInst = RISCVInstruction::createPseudoRET();
        currentBB->addInstruction(retInst);
    }
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
            auto intCondReg = allocateTempRegister(RegisterType::GENERAL, "branch_cond");
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

void InstructionSelector::visitAllocaInst(AllocaInst *inst)
{
    // Alloca指令用于在栈上分配数组或变量的内存空间
    // 返回分配内存的首地址（对于数组，即第一个元素的地址）

    StackFrame &stackFrame = currentFunc->getStackFrame();

    // 检查是否已经为该alloca分配了栈空间
    string allocaName = inst->getName();
    if (!stackFrame.hasAllocation(allocaName))
    {
        // 计算需要分配的内存大小
        Type *allocatedType = inst->AllocatedType;
        // 数组类型：大小 = 元素数量 × 元素大小
        auto arrayType = static_cast<ArrayType *>(allocatedType);
        int elementSize = 4; // 假设int/float都是4字节

        int allocatedSize = arrayType->getArrayLength() * elementSize;

        stackFrame.allocateSpace(inst->getDest()->getName(), allocatedSize);
    }

    // 获取alloca分配的内存在栈帧中的偏移
    int varOffset = stackFrame.getOffset(inst->getDest()->getName());

    // 创建临时寄存器保存数组/变量的首地址
    auto addressReg = allocateTempRegister(RegisterType::GENERAL, "store_addr");

    // 计算首地址：sp + offset
    if (isImmediateInRange(varOffset))
    {
        // 直接使用addi计算地址
        auto addiInst = RISCVInstruction::createIType(RISCVOpcode::ADDI, addressReg,
                                                      make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP),
                                                      varOffset);
        currentBB->addInstruction(addiInst);
    }
    else
    {
        // 偏移量太大，使用li + add组合
        auto liInst = RISCVInstruction::createPseudoLI(addressReg, varOffset);
        currentBB->addInstruction(liInst);

        auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, addressReg,
                                                     make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP),
                                                     addressReg);
        currentBB->addInstruction(addInst);
    }

    // 将数组/变量的首地址存储到栈中供后续使用stridegetNumElements
    // alloca指令的结果是一个指针，指向分配的内存区域
    storeValueToStack(inst->getDest(), addressReg, 8);
}

void InstructionSelector::visitElementPtrInst(GetElementPtrInst *inst)
{
    // GetElementPtr指令用于计算数组元素的地址
    // 支持多维数组的地址计算
    // 输入：数组首地址（由alloca返回）+ 多个索引
    // 输出：指定元素的地址

    // 获取数组的基地址,数组的基地址通常是alloca指令的指针
    auto baseAddressReg = getOrCreateVirtualReg(inst->getPointerOperand());

    // 如果不是全局变量，可能需要调整基地址
    auto globalVar = dynamic_cast<GlobalVariable *>(inst->getPointerOperand());
    if (!globalVar)
    {
        auto addiInst = RISCVInstruction::createIType(RISCVOpcode::ADDI, baseAddressReg,
                                                      baseAddressReg, 8);
        currentBB->addInstruction(addiInst);
    }

    auto elementAddressReg = allocateTempRegister(RegisterType::GENERAL, "element_addr");

    auto indices = inst->getIndices();       // 访问下标
    auto stridePtr = inst->getArrayStride(); // 获取数组的步长信息

    if (!indices.empty())
    {
        if (stridePtr != nullptr && !stridePtr->empty())
        {
            // 多维数组：计算元素地址：base + sum(index[i] * stride[i] * 4)
            auto totalOffsetReg = allocateTempRegister(RegisterType::GENERAL, "total_offset");

            // 初始化偏移量为0
            auto zeroReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO);
            auto initInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, totalOffsetReg, zeroReg);
            currentBB->addInstruction(initInst);

            auto offsetReg = allocateTempRegister(RegisterType::GENERAL, "index_offset");
            auto liInst = RISCVInstruction::createPseudoLI(offsetReg, 1);
            currentBB->addInstruction(liInst);

            // 处理每个维度的索引
            for (int i = static_cast<int>(indices.size()) - 1; i >= 0; --i)
            {
                auto indexReg = getOrCreateVirtualReg(indices[i]);
                auto tmpReg = allocateTempRegister(RegisterType::GENERAL, "index_calc");

                // totalOffset += offset * index * 4
                auto mulInst = RISCVInstruction::createRType(RISCVOpcode::MUL, tmpReg, indexReg, offsetReg);
                currentBB->addInstruction(mulInst);
                auto shiftInst = RISCVInstruction::createIType(RISCVOpcode::SLLI, tmpReg, tmpReg, 2); // 左移2位，相当于乘以4
                currentBB->addInstruction(shiftInst);
                auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, totalOffsetReg, totalOffsetReg, tmpReg);
                currentBB->addInstruction(addInst);

                releaseTempRegister(tmpReg);   // 释放临时寄存器
                releaseTempRegister(indexReg); // 释放偏移寄存器

                // offset *= stride
                if (i == 0)
                {
                    // 最后一个维度的偏移量不需要乘以stride
                    break;
                }

                int stride = (*stridePtr)[i - 1];
                if (stride != 1)
                {
                    auto strideReg = allocateTempRegister(RegisterType::GENERAL, "stride_calc");
                    auto liStrideInst = RISCVInstruction::createPseudoLI(strideReg, stride);
                    currentBB->addInstruction(liStrideInst);
                    auto mulStrideInst = RISCVInstruction::createRType(RISCVOpcode::MUL, offsetReg, offsetReg, strideReg);
                    currentBB->addInstruction(mulStrideInst);

                    releaseTempRegister(strideReg); // 释放stride寄存器
                }
            }

            // 基地址加上总偏移量得到最终地址
            auto finalAddInst = RISCVInstruction::createRType(RISCVOpcode::ADD, elementAddressReg, baseAddressReg, totalOffsetReg);
            currentBB->addInstruction(finalAddInst);
        }
        else
        {
            // 一维数组或简单情况：base + index * 4
            auto indexReg = getOrCreateVirtualReg(indices[0]);
            auto shiftReg = allocateTempRegister(RegisterType::GENERAL, "shift_calc");

            // 将索引左移2位（相当于乘以4）
            auto shiftInst = RISCVInstruction::createIType(RISCVOpcode::SLLI, shiftReg, indexReg, 2);
            currentBB->addInstruction(shiftInst);

            // 基地址加上偏移量
            auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, elementAddressReg, baseAddressReg, shiftReg);
            currentBB->addInstruction(addInst);
        }
    }
    else
    {
        // 没有索引，直接复制基地址
        auto moveInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, elementAddressReg, baseAddressReg);
        currentBB->addInstruction(moveInst);
    }

    // 将计算出的元素地址存储到栈中供load/store指令使用
    storeValueToStack(inst->getDest(), elementAddressReg, 8);
}

void InstructionSelector::visitICmpInst(ICmpInst *inst)
{
    auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
    auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
    auto destReg = allocateTempRegister(RegisterType::GENERAL, "icmp_result");

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

    // 将结果存储到栈中
    storeValueToStack(inst->getDest(), destReg);
}

void InstructionSelector::visitFCmpInst(FCmpInst *inst)
{
    // 修改：使用浮点寄存器类型
    auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
    auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
    auto destReg = allocateTempRegister(RegisterType::GENERAL, "fcmp_result"); // 结果仍存储在通用寄存器

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

    // 将结果存储到栈中
    storeValueToStack(inst->getDest(), destReg);
}

void InstructionSelector::visitSIToFPInst(CastInst *inst)
{
    // 处理有符号整数到浮点数的转换指令
    auto srcReg = getOrCreateVirtualReg(inst->getOperand());

    // 创建目标浮点寄存器 - 使用临时寄存器管理
    auto destReg = allocateTempRegister(RegisterType::FLOAT, "sitofp_result"); // 类型转换

    // 生成 RISC-V 的 fcvt.s.w 指令（整数到单精度浮点）
    auto fcvtInst = RISCVInstruction::createPseudo(RISCVOpcode::FCVT_S_W, destReg, srcReg);
    currentBB->addInstruction(fcvtInst);

    // 将结果存储到栈中
    storeValueToStack(inst->getDest(), destReg);
}

void InstructionSelector::visitFPToSIInst(CastInst *inst)
{
    // 处理浮点数到有符号整数的转换指令
    auto srcReg = getOrCreateVirtualReg(inst->getOperand());

    // 创建目标整数寄存器 - 使用临时寄存器管理
    auto destReg = allocateTempRegister(RegisterType::GENERAL, "fptosi_result"); // 类型转换

    // 生成 RISC-V 的 fcvt.w.s 指令（单精度浮点到整数）
    // 使用RTZ（Round toward Zero）舍入模式，这是C语言标准的行为
    auto fcvtInst = RISCVInstruction::createPseudo(RISCVOpcode::FCVT_W_S, destReg, srcReg);
    currentBB->addInstruction(fcvtInst);

    // 将结果存储到栈中
    storeValueToStack(inst->getDest(), destReg);
}

void InstructionSelector::visitCopyInst(CopyInst *inst)
{
    // Copy指令用于将源值复制到目标位置
    // 在我们的"所有变量溢出到栈上"策略中，这实际上是从源位置加载值，然后存储到目标位置

    // 获取源值的寄存器
    auto srcReg = getOrCreateVirtualReg(inst->getSource());

    // 创建临时寄存器进行复制操作
    RegisterType regType = inst->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::GENERAL;
    auto destReg = allocateTempRegister(regType, "copy_result");

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
    storeValueToStack(inst->getDest(), destReg);
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
    auto tempIntReg = allocateTempRegister(RegisterType::GENERAL, "float_const_bits"); // 用于浮点常量加载

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

shared_ptr<RISCVRegister> InstructionSelector::getOrCreateVirtualReg(Value *value)
{
    // 处理常量
    if (auto constInt = dynamic_cast<ConstantInt *>(value))
    {
        // 根据常量大小优化加载指令
        auto tempReg = allocateTempRegister(RegisterType::GENERAL, "const_" + std::to_string(constInt->Value));
        generateConstantLoad(tempReg, constInt->Value);
        return tempReg;
    }
    else if (auto constFloat = dynamic_cast<ConstantFloat *>(value))
    {
        // 对于浮点常量，需要更复杂的处理
        auto tempReg = allocateTempRegister(RegisterType::FLOAT, "constf_" + std::to_string(constFloat->Value));
        generateFloatConstantLoad(tempReg, constFloat->Value);
        return tempReg;
    }

    // 获取Value的名字作为映射的key
    string valueName = value->getName();
    if (valueName.empty())
    {
        // 如果没有名字，抛出错误，因为我们需要名字来建立映射
        throw std::runtime_error("Unable to find register or stack allocation for Value: " + value->getName() + " (type: " + value->getType()->toString() + ")");
    }

    // 查找已存在的映射（函数参数）
    auto it = registerMap.find(valueName);
    if (it != registerMap.end())
    {
        auto reg = allocateTempRegister(it->second->getType(), "reuse_" + valueName);

        // 如果是浮点寄存器，使用浮点move指令
        auto op = it->second->getType() == RegisterType::FLOAT ? RISCVOpcode::FMV_S : RISCVOpcode::MV;
        auto moveInst = RISCVInstruction::createPseudo(op, reg, it->second);
        currentBB->addInstruction(moveInst);
        return reg;
    }

    // 检查是否是栈参数
    auto stackIt = stackArguments.find(valueName);
    if (stackIt != stackArguments.end())
    {
        // 这是一个栈参数，需要从调用者栈帧中加载
        // 栈参数存储在调用者栈帧顶部，被调用者通过SP+偏移量访问
        auto loadReg = allocateTempRegister(RegisterType::GENERAL, "stackarg_" + valueName);

        // 计算栈参数的实际偏移量：
        // argOffset = 被调用者栈帧大小 + 参数在调用者栈帧中的偏移
        StackFrame &stackFrame = currentFunc->getStackFrame();
        int argOffset = stackFrame.getAlignedSize() + stackIt->second;

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
            auto addrReg = allocateTempRegister(RegisterType::GENERAL, "stack_addr");
            auto liInst = RISCVInstruction::createPseudoLI(addrReg, argOffset);
            currentBB->addInstruction(liInst);

            auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, addrReg, spReg, addrReg);
            currentBB->addInstruction(addInst);

            // 目标是否为指针
            auto loadOpcode = value->getType()->isPointerTy() ? RISCVOpcode::LD : RISCVOpcode::LW;

            auto loadInst = RISCVInstruction::createIType(loadOpcode, loadReg, addrReg, 0);
            currentBB->addInstruction(loadInst);
        }

        // 如果目标是浮点类型，需要转移到浮点寄存器
        if (value->getType()->isFloatTy())
        {
            auto floatReg = allocateTempRegister(RegisterType::FLOAT, "stackarg_float_" + valueName);
            auto moveInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_W_X, floatReg, loadReg);
            currentBB->addInstruction(moveInst);
            return floatReg;
        }

        return loadReg;
    }

    // 对于需要从栈加载的值（如指令的结果）
    StackFrame &stackFrame = currentFunc->getStackFrame();
    if (stackFrame.hasAllocation(valueName))
    {
        // 检查是否已经有加载过的寄存器
        auto existing = registerMap.find(valueName);
        if (existing != registerMap.end())
        {
            return existing->second;
        }

        // 从栈中加载值到新的虚拟寄存器
        RegisterType regType = value->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::GENERAL;
        auto virtualReg = allocateTempRegister(regType, "load_" + valueName);

        auto isPtr = value->getType()->isPointerTy();

        // 生成从栈加载的指令
        int offset = stackFrame.getOffset(valueName);
        generateStackAccess(offset, virtualReg, false, isPtr); // false表示load

        return virtualReg;
    }

    // 如果是全局变量
    if (value->isGlobal())
    {
        // 全局变量需要先从从全局内存加载地址
        auto globalReg = allocateTempRegister(RegisterType::GENERAL, "global_" + valueName);

        auto laInst = RISCVInstruction::createPseudoLA(globalReg, value->getName());
        currentBB->addInstruction(laInst);

        return globalReg;
    }

    // 错误情况：未能找到Value的寄存器或栈位置
    throw std::runtime_error("Cannot allocate register for value: " + valueName);
}

void InstructionSelector::storeValueToStack(Value *value, shared_ptr<RISCVRegister> reg, int size)
{
    StackFrame &stackFrame = currentFunc->getStackFrame();
    string valueName = value->getName();

    if (!stackFrame.hasAllocation(valueName))
    {
        // 如果还没有分配空间，现在分配
        stackFrame.allocateSpace(valueName, size);
    }

    int offset = stackFrame.getOffset(valueName);
    generateStackAccess(offset, reg, true, size == 8); // true表示store
}

void InstructionSelector::generateStackAccess(int offset, shared_ptr<RISCVRegister> reg, bool isStore, bool isDouble)
{
    auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);

    if (isImmediateInRange(offset))
    {
        // 直接使用偏移量
        if (isStore)
        {
            RISCVOpcode opcode = (reg->getType() == RegisterType::FLOAT) ? (isDouble ? RISCVOpcode::FSD : RISCVOpcode::FSW) : (isDouble ? RISCVOpcode::SD : RISCVOpcode::SW);
            auto inst = RISCVInstruction::createSType(opcode, spReg, reg, offset);
            currentBB->addInstruction(inst);
        }
        else
        {
            RISCVOpcode opcode = (reg->getType() == RegisterType::FLOAT) ? (isDouble ? RISCVOpcode::FLD : RISCVOpcode::FLW) : (isDouble ? RISCVOpcode::LD : RISCVOpcode::LW);
            auto inst = RISCVInstruction::createIType(opcode, reg, spReg, offset);
            currentBB->addInstruction(inst);
        }
    }
    else
    {
        // 偏移量超出范围，需要计算地址
        auto tempReg = allocateTempRegister(RegisterType::GENERAL, "addr_calc"); // 地址计算

        // li temp, offset
        auto liInst = RISCVInstruction::createPseudoLI(tempReg, offset);
        currentBB->addInstruction(liInst);

        // add temp, sp, temp
        auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, spReg, tempReg);
        currentBB->addInstruction(addInst);

        // lw/sw reg, 0(temp)
        if (isStore)
        {
            RISCVOpcode opcode = (reg->getType() == RegisterType::FLOAT) ? (isDouble ? RISCVOpcode::FSD : RISCVOpcode::FSW) : (isDouble ? RISCVOpcode::SD : RISCVOpcode::SW);
            auto memInst = RISCVInstruction::createSType(opcode, tempReg, reg, 0);
            currentBB->addInstruction(memInst);
        }
        else
        {
            RISCVOpcode opcode = (reg->getType() == RegisterType::FLOAT) ? (isDouble ? RISCVOpcode::FLD : RISCVOpcode::FLW) : (isDouble ? RISCVOpcode::LD : RISCVOpcode::LW);
            auto memInst = RISCVInstruction::createIType(opcode, reg, tempReg, 0);
            currentBB->addInstruction(memInst);
        }
    }
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
            // 指针参数需要8字节，整数和浮点参数需要4字节
            // 指针和整数共用八个参数寄存器，浮点数用八个参数寄存器
            if (instr->Op == Opcode::Call)
            {
                hasCall = true;
                if (auto callInst = dynamic_cast<CallInst *>(instr))
                {
                    int extraIntArgs = callInst->getIntArguments().size() - 8;
                    int extraFloatArgs = callInst->getFloatArguments().size() - 8;
                    int extraPtrArgs = extraIntArgs < 0 ? callInst->getPtrArguments().size() + extraIntArgs : callInst->getPtrArguments().size();

                    callArgSizes.push_back(std::max(extraIntArgs, 0) * 4 + std::max(extraFloatArgs, 0) * 4 + std::max(extraPtrArgs, 0) * 8);
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
            case Opcode::FCmp:
            case Opcode::FDiv:
            case Opcode::ICmp:
            case Opcode::Load:
            case Opcode::Call:
            case Opcode::Copy:
            case Opcode::SIToFP:
            case Opcode::FPToSI:
                hasResult = !instr->getType()->isVoidTy();
                break;
            case Opcode::Alloca:
                if (auto allocaInst = dynamic_cast<AllocaInst *>(instr))
                {
                    Type *allocatedType = allocaInst->AllocatedType;
                    if (allocatedType->isArrayTy())
                    {
                        auto arrayType = static_cast<ArrayType *>(allocatedType);
                        allocSize = arrayType->getArrayLength() * 4 + 8; // 数组元素大小 + 指针大小
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
                // 对于其他指令类型，检查是否有返回值
                hasResult = !instr->getType()->isVoidTy();
                break;
            }

            if (hasResult)
            {
                Value *destValue = nullptr;

                // 根据指令类型获取目标值
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
                    if (auto binOp = dynamic_cast<BinaryOperator *>(instr))
                        destValue = binOp->getDest();
                    break;
                case Opcode::ICmp:
                    if (auto icmpInst = dynamic_cast<ICmpInst *>(instr))
                        destValue = icmpInst->getDest();
                    break;
                case Opcode::FCmp:
                    if (auto fcmpInst = dynamic_cast<FCmpInst *>(instr))
                        destValue = fcmpInst->getDest();
                    break;
                case Opcode::Load:
                    if (auto loadInst = dynamic_cast<LoadInst *>(instr))
                        destValue = loadInst->getDest();
                    break;
                case Opcode::Call:
                    if (auto callInst = dynamic_cast<CallInst *>(instr))
                        destValue = callInst->getDest();
                    break;
                case Opcode::Copy:
                    if (auto copyInst = dynamic_cast<CopyInst *>(instr))
                        destValue = copyInst->getDest();
                    break;
                case Opcode::Alloca:
                    if (auto allocaInst = dynamic_cast<AllocaInst *>(instr))
                        destValue = allocaInst->getDest();
                    break;
                case Opcode::GetElementPtr:
                    if (auto gepInst = dynamic_cast<GetElementPtrInst *>(instr))
                        destValue = gepInst->getDest();
                    break;
                case Opcode::SIToFP:
                case Opcode::FPToSI:
                    if (auto castInst = dynamic_cast<CastInst *>(instr))
                        destValue = castInst->getDest();
                    break;
                default:
                    destValue = instr; // 回退到原来的行为
                    break;
                }

                if (destValue)
                {
                    stackFrame.allocateSpace(destValue->getName(), allocSize);
                    S += allocSize;
                }
            }
        }
    }

    // 计算R：如果有call指令则需要保存ra
    R = hasCall ? 4 : 0;
    stackFrame.currentOffset += 4; // ra寄存器的栈空间

    // 计算A：传参需要的栈空间
    // A = max{max(len_i - 8, 0)} * 4，其中len_i是第i个call的参数个数
    A = callArgSizes.empty() ? 0 : *std::max_element(callArgSizes.begin(), callArgSizes.end());

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

            releaseTempRegister(tempReg); // 释放临时寄存器
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

                auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, spReg, tempReg);
                func->getBasicBlocks()[0]->addInstruction(addInst);

                auto swInst = RISCVInstruction::createSType(RISCVOpcode::SW, tempReg, raReg, 0);
                func->getBasicBlocks()[0]->addInstruction(swInst);

                releaseTempRegister(tempReg); // 释放临时寄存器
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

                releaseTempRegister(tempReg); // 释放临时寄存器
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

            releaseTempRegister(tempReg); // 释放临时寄存器
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

    for (const auto &arg : irFunc->getArguments())
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
                registerMap[arg->getName()] = reg;
                intArgIndex++;
            }
            else
            {
                // 超出范围的整数参数从栈获取
                stackArguments[arg->getName()] = (intArgIndex + floatArgIndex) * 4 + ptrArgIndex * 8;
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
                registerMap[arg->getName()] = reg;
                floatArgIndex++;
            }
            else
            {
                // 超出范围的浮点参数从栈获取
                stackArguments[arg->getName()] = (intArgIndex + floatArgIndex) * 4 + ptrArgIndex * 8;
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
                registerMap[arg->getName()] = reg;
                intArgIndex++;
            }
            else
            {
                // 超出范围的指针参数从栈获取
                stackArguments[arg->getName()] = (intArgIndex + floatArgIndex) * 4 + ptrArgIndex * 8;
                ptrArgIndex++;
            }
        }
    }
}

void RISCVBuilder::processGlobalInitializer(shared_ptr<RISCVGlobalBlock> globalBlock, Constant *initializer)
{
    // 处理全局变量初始化器 - 根据数据类型选择合适的输出方式
    if (auto constInt = dynamic_cast<ConstantInt *>(initializer))
    {
        // 整数常量：直接添加数值
        globalBlock->addData(std::to_string(constInt->Value));
    }
    else if (auto constFloat = dynamic_cast<ConstantFloat *>(initializer))
    {
        // 浮点常量：转换为32位整数表示
        uint32_t bits;
        std::memcpy(&bits, &constFloat->Value, sizeof(float));
        globalBlock->addData(std::to_string(bits));
    }
    else if (auto constStr = dynamic_cast<ConstantString *>(initializer))
    {
        // 字符串常量：使用专门的字符串处理方法
        globalBlock->addStringData(constStr->Value);
    }
    else if (auto constArray = dynamic_cast<ConstantArray *>(initializer))
    {
        // 数组常量：收集所有元素，然后作为数组添加
        vector<string> arrayElements;
        for (Constant *element : constArray->Elements)
        {
            if (element)
            {
                if (auto elemInt = dynamic_cast<ConstantInt *>(element))
                {
                    arrayElements.push_back(std::to_string(elemInt->Value));
                }
                else if (auto elemFloat = dynamic_cast<ConstantFloat *>(element))
                {
                    uint32_t bits;
                    std::memcpy(&bits, &elemFloat->Value, sizeof(float));
                    arrayElements.push_back(std::to_string(bits));
                }
                else
                {
                    // 未定义的元素用零初始化
                    arrayElements.push_back("0");
                }
            }
            else
            {
                // 空元素用零初始化
                arrayElements.push_back("0");
            }
        }

        // 使用数组专用方法添加数据
        globalBlock->addData(arrayElements);
    }
    else
    {
        // 其他情况：默认零初始化
        globalBlock->addData("0");
    }
}

void RISCVBuilder::processZeroInitializer(shared_ptr<RISCVGlobalBlock> globalBlock, Type *type)
{
    // 处理零初始化
    if (type->isArrayTy())
    {
        auto arrayType = static_cast<ArrayType *>(type);
        int numElements = arrayType->getArrayLength();

        // 创建零初始化的数组
        vector<string> zeroArray(numElements, "0");
        globalBlock->addData(zeroArray);
    }
    else
    {
        // 标量类型的零初始化
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

    // 生成文本段
    ss << ".text\n";

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

// 保持与原有接口的兼容性
shared_ptr<RISCVRegister> InstructionSelector::getTempRegister(RegisterType type, int index)
{
    return allocateTempRegister(type, "temp_" + std::to_string(index));
}

shared_ptr<RISCVRegister> InstructionSelector::getGeneralTempRegister(int index)
{
    return allocateTempRegister(RegisterType::GENERAL, "general_temp_" + std::to_string(index));
}

shared_ptr<RISCVRegister> InstructionSelector::getFloatTempRegister(int index)
{
    return allocateTempRegister(RegisterType::FLOAT, "float_temp_" + std::to_string(index));
}

// 临时寄存器管理方法实现
shared_ptr<RISCVRegister> InstructionSelector::allocateTempRegister(RegisterType type, const string &purpose)
{
    vector<RISCVRegister::PhysicalReg> *availablePool;
    unordered_map<RISCVRegister::PhysicalReg, RegisterState> *stateMap;

    // 选择合适的寄存器池和状态映射
    if (type == RegisterType::GENERAL)
    {
        availablePool = &availableGeneralTemps;
        stateMap = &generalRegState;
    }
    else
    {
        availablePool = &availableFloatTemps;
        stateMap = &floatRegState;
    }

    // 首先尝试找到空闲的寄存器
    for (auto reg : *availablePool)
    {
        if (!(*stateMap)[reg].inUse)
        {
            // 标记为使用中
            (*stateMap)[reg].inUse = true;
            (*stateMap)[reg].occupiedBy = purpose;
            (*stateMap)[reg].allocationOrder = allocationCounter++;

            // 创建寄存器对象
            auto tempReg = make_shared<RISCVRegister>(reg, type);

            // 添加到当前指令的临时寄存器列表
            currentInstructionTemps.push_back(tempReg);

            return tempReg;
        }
    }

    // 如果没有空闲寄存器，使用LRU策略释放最旧的寄存器
    RISCVRegister::PhysicalReg oldestReg = (*availablePool)[0];
    int oldestOrder = (*stateMap)[oldestReg].allocationOrder;

    for (auto reg : *availablePool)
    {
        if ((*stateMap)[reg].allocationOrder < oldestOrder)
        {
            oldestOrder = (*stateMap)[reg].allocationOrder;
            oldestReg = reg;
        }
    }

    // 释放最旧的寄存器并重新分配
    (*stateMap)[oldestReg].inUse = true;
    (*stateMap)[oldestReg].occupiedBy = purpose;
    (*stateMap)[oldestReg].allocationOrder = allocationCounter++;

    auto tempReg = make_shared<RISCVRegister>(oldestReg, type);
    currentInstructionTemps.push_back(tempReg);

    return tempReg;
}

void InstructionSelector::releaseTempRegister(shared_ptr<RISCVRegister> reg)
{
    if (!reg || reg->getRegType() == RegisterType::VIRTUAL)
    {
        return; // 只释放物理临时寄存器
    }

    unordered_map<RISCVRegister::PhysicalReg, RegisterState> *stateMap;

    if (reg->getType() == RegisterType::GENERAL)
    {
        stateMap = &generalRegState;
    }
    else
    {
        stateMap = &floatRegState;
    }

    // 标记为空闲
    auto physReg = reg->getPhysicalReg();
    (*stateMap)[physReg].inUse = false;
    (*stateMap)[physReg].occupiedBy = "";

    // 从当前指令临时寄存器列表中移除
    auto it = std::find(currentInstructionTemps.begin(), currentInstructionTemps.end(), reg);
    if (it != currentInstructionTemps.end())
    {
        currentInstructionTemps.erase(it);
    }
}

void InstructionSelector::releaseAllCurrentTemps()
{
    // 释放当前指令使用的所有临时寄存器
    for (auto reg : currentInstructionTemps)
    {
        if (reg->getRegType() != RegisterType::VIRTUAL)
        {
            unordered_map<RISCVRegister::PhysicalReg, RegisterState> *stateMap;

            if (reg->getType() == RegisterType::GENERAL)
            {
                stateMap = &generalRegState;
            }
            else
            {
                stateMap = &floatRegState;
            }

            auto physReg = reg->getPhysicalReg();
            (*stateMap)[physReg].inUse = false;
            (*stateMap)[physReg].occupiedBy = "";
        }
    }

    // 清空当前指令的临时寄存器列表
    currentInstructionTemps.clear();
}
