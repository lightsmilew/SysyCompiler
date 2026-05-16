#include "RISCVBuilder.h"
#include <memory>
using namespace RISCV;

shared_ptr<RISCVModule> RISCVBuilder::generateRISCVCode(shared_ptr<Module> irModule)
{
    this->irModule = irModule;
    initializeModule(irModule);
    generateInstructions();
    FirstPeep();
    runLICMPass();
    // instructionScheduler();
    allocateRegisters();
    SecondPeep();
    reallocOffsetForInstructions();
    return riscvModule;
}

string RISCVBuilder::generateAssembly(shared_ptr<RISCVModule> module)
{
    AssemblyEmitter emitter;
    return emitter.emit(module);
}

void RISCVBuilder::printInstructions()
{
    for (const auto &func : riscvModule->getFunctions())
    {
        cout << "Function: " << func->getName() << endl;
        for (const auto &bb : func->getBasicBlocks())
        {
            cout << "  BasicBlock: " << bb->getLabel() << endl;
            for (const auto &instr : bb->getInstructions())
            {
                cout << "    Instruction: " << instr->toString() << endl;
            }
        }
    }
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
            processZeroInitializer(globalBlock, globalVar.get());
        }
    }

    // 初始化函数
    for (const auto &func : irModule->Functions)
    {
        if (isLibraryFunction(func->getName()))
        {
            continue;
        }

        auto riscvFunc = make_shared<RISCVFunction>(func->getName(), riscvModule);
        riscvModule->addFunction(riscvFunc);

        // 建立IR基本块到RISC-V基本块的映射表
        unordered_map<BasicBlock *, shared_ptr<RISCVBasicBlock>> blockMapping;

        // 为函数添加prologue基本块
        auto prologueBB = make_shared<RISCVBasicBlock>("prologue_" + func->getName(), riscvFunc);
        riscvFunc->addBasicBlock(prologueBB);

        // 为每个IR基本块创建对应的RISC-V基本块
        for (const auto &bb : func->BasicBlocks)
        {
            auto riscvBB = make_shared<RISCVBasicBlock>(bb->getName(), riscvFunc);
            riscvFunc->addBasicBlock(riscvBB);
            blockMapping[bb.get()] = riscvBB; // 建立映射关系
        }

        // 构造后端循环信息
        buildBackendLoopInfo(riscvFunc, func.get(), blockMapping);

        prologueBB->addSuccessor(riscvFunc->getBasicBlocks()[1]); // 将prologue连接到第一个基本块
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
        // 数组常量：递归处理所有元素
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
                else if (auto elemArray = dynamic_cast<ConstantArray *>(element))
                {
                    // 递归处理嵌套数组
                    // 创建临时块来收集嵌套数组的数据
                    auto tempBlock = make_shared<RISCVGlobalBlock>("temp");
                    processGlobalInitializer(tempBlock, elemArray);

                    // 将临时块的数据添加到当前数组
                    auto tempData = tempBlock->getData();
                    arrayElements.insert(arrayElements.end(), tempData.begin(), tempData.end());
                }
                else
                {
                    // 其他未知类型用零初始化
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

void RISCVBuilder::processZeroInitializer(shared_ptr<RISCVGlobalBlock> globalBlock, GlobalVariable *globalVar)
{
    // 处理零初始化
    if (globalVar->isArray())
    {
        int numElements = globalVar->getTotallength();

        // 对于大数组使用优化的零初始化方法，避免创建巨大的vector
        if (numElements > 1000) // 当数组元素超过1000个时使用优化方法
        {
            globalBlock->addZeroData(numElements);
        }
        else
        {
            // 小数组仍使用原来的方法
            vector<string> zeroArray(numElements, "0");
            globalBlock->addData(zeroArray);
        }
    }
    else
    {
        // 标量类型的零初始化
        globalBlock->addData("0");
    }
}

void RISCVBuilder::generateInstructions()
{
    // 创建指令选择器（全局共享，用于处理全局数组信息）
    InstructionSelector selector;

    // 为每个函数生成指令
    for (const auto &func : irModule->Functions)
    {
        if (isLibraryFunction(func->getName()))
            continue;

        auto riscvFunc = riscvModule->getFunction(func->getName());
        if (!riscvFunc)
            continue;

        selector.selectInstructions(riscvFunc, func.get());
    }
}

void RISCVBuilder::instructionSheduler()
{
    // 创建指令调度器
    InstructionScheduler scheduler;
    scheduler.scheduleModule(riscvModule);
}

void RISCVBuilder::allocateRegisters()
{
    // 为每个函数进行寄存器分配
    for (const auto &func : riscvModule->getFunctions())
    {
        GraphColorRegisterAllocator allocator;
        allocator.allocateRegisters(func, irModule);
    }
}

void RISCVBuilder::FirstPeep()
{
    PeepOptimizationManager peep;
    peep.addPass(make_shared<FoldCompareBranchPass>());
    peep.addPass(make_shared<DeadCodeEliminationPass>());
    peep.addPass(make_shared<RemoveRedundantJalPass>());
    peep.addPass(make_shared<StrengthReductionPass>());
    peep.optimizeModule(riscvModule);
}

void RISCVBuilder::SecondPeep()
{
    PeepOptimizationManager peep;
    // 地址融合目前还有问题，先关闭
    // peep.addPass(make_shared<FoldAdjacentMoveAndAddressPass>());
    peep.addPass(make_shared<RemoveRedundantMovePass>());
    peep.optimizeModule(riscvModule);
}

void RISCVBuilder::reallocOffsetForInstructions()
{
    // 遍历所有函数，重新计算指令的偏移量
    for (const auto &func : riscvModule->getFunctions())
    {
        auto &stackFrame = func->getStackFrame();
        auto usedCalleeSavedRegs = func->getUsedCalleeSavedRegs();
        if (!usedCalleeSavedRegs.empty())
        {
            func->getStackFrame().allocateValueSpace("usedCalleeSavedRegs", usedCalleeSavedRegs.size() * 8);
        }

        for (const auto &instrPair : func->getInstructionNeedReGetOffset())
        {
            const string &argName = instrPair.first;
            shared_ptr<RISCVInstruction> instr = instrPair.second;

            // 重新计算偏移量并更新指令
            int offset = stackFrame.getValueOffset(argName);
            if (offset != -1)
            {
                if (instr->getOpcode() == RISCVOpcode::LI)
                    instr->setOffsetForLiInstruction(offset);
                else if (instr->getOpcode() == RISCVOpcode::ADDI)
                {
                    instr->setOffsetForAddiInstruction(offset);
                }
            }
            else
            {
                offset = stackFrame.getCallerArgOffset(argName);
                instr->setOffsetForLiInstruction(offset);
            }
        }
    }
}

bool RISCVBuilder::isLibraryFunction(const string &funcName)
{
    // 检查是否是库函数 - 包括SysY运行时库函数
    static const set<string> libFuncs = {
        // SysY运行时库函数
        "getint", "getch", "getfloat", "getarray", "getfarray",
        "putint", "putch", "putfloat", "putarray", "putfarray", "putf",
        "_sysy_starttime", "_sysy_stoptime"};
    return libFuncs.count(funcName) > 0;
}

// 构造后端循环信息
void RISCVBuilder::buildBackendLoopInfo(shared_ptr<RISCVFunction> riscvFunc,
                                        Function *irFunc,
                                        const unordered_map<BasicBlock *, shared_ptr<RISCVBasicBlock>> &blockMapping)
{
    LoopInfo backendLoopInfo;

    // 遍历IR函数中的所有循环
    for (const auto &irLoop : irFunc->getLoops())
    {
        auto riscvLoop = make_shared<RISCVLoop>();

        // 映射循环头部
        if (blockMapping.find(irLoop.header) != blockMapping.end())
        {
            riscvLoop->setHeader(blockMapping.at(irLoop.header));
        }

        // 映射循环体
        for (auto irBlock : irLoop.blocks)
        {
            if (blockMapping.find(irBlock) != blockMapping.end())
            {
                riscvLoop->addBodyBlock(blockMapping.at(irBlock));
            }
        }

        // 映射出口块
        for (auto irExit : irLoop.exits)
        {
            if (blockMapping.find(irExit) != blockMapping.end())
            {
                riscvLoop->addExitBlock(blockMapping.at(irExit));
            }
        }

        // 计算循环深度
        int depth = calculateLoopDepth(irLoop, irFunc->getLoops());
        riscvLoop->setDepth(depth);

        backendLoopInfo.addLoop(riscvLoop);
    }

    // 建立循环的父子关系
    establishLoopHierarchy(backendLoopInfo, irFunc->getLoops(), blockMapping);

    // 将循环信息设置到RISC-V函数中
    riscvFunc->setLoopInfo(backendLoopInfo);
}

// 计算循环深度
int RISCVBuilder::calculateLoopDepth(const Loop &targetLoop, const vector<Loop> &allLoops)
{
    int depth = 1;

    // 检查是否被其他循环包含
    for (const auto &otherLoop : allLoops)
    {
        if (&otherLoop == &targetLoop)
            continue;

        // 如果targetLoop的头部在otherLoop的循环体中，说明targetLoop被包含
        if (std::find(otherLoop.blocks.begin(), otherLoop.blocks.end(), targetLoop.header) != otherLoop.blocks.end())
        {
            depth++;
        }
    }

    return depth;
}

// 建立循环的父子关系
void RISCVBuilder::establishLoopHierarchy(LoopInfo &backendLoopInfo,
                                          const vector<Loop> &irLoops,
                                          const unordered_map<BasicBlock *, shared_ptr<RISCVBasicBlock>> &blockMapping)
{
    auto &riscvLoops = backendLoopInfo.getLoops();

    // 为每个循环找到其父循环
    for (size_t i = 0; i < riscvLoops.size(); i++)
    {
        for (size_t j = 0; j < riscvLoops.size(); j++)
        {
            if (i == j)
                continue;

            auto &potentialChild = riscvLoops[i];
            auto &potentialParent = riscvLoops[j];

            // 检查潜在子循环的头部是否在潜在父循环的循环体中
            if (isBlockInLoop(potentialChild->getHeader(), potentialParent))
            {
                // 确保这是直接的父子关系（不是祖父母关系）
                bool isDirectParent = true;
                for (size_t k = 0; k < riscvLoops.size(); k++)
                {
                    if (k == i || k == j)
                        continue;

                    auto &intermediate = riscvLoops[k];
                    if (isBlockInLoop(potentialChild->getHeader(), intermediate) &&
                        isBlockInLoop(intermediate->getHeader(), potentialParent))
                    {
                        isDirectParent = false;
                        break;
                    }
                }

                if (isDirectParent)
                {
                    potentialChild->setParentLoop(potentialParent);
                    potentialParent->addChildLoop(potentialChild);
                }
            }
        }
    }
}

// 检查基本块是否在循环中
bool RISCVBuilder::isBlockInLoop(shared_ptr<RISCVBasicBlock> block, shared_ptr<RISCVLoop> loop)
{
    return loop->containsBlock(block);
}

// 运行LICM优化pass
void RISCVBuilder::runLICMPass()
{
    // 对每个函数运行LICM优化
    for (auto &func : riscvModule->getFunctions())
    {
        // 跳过库函数
        if (isLibraryFunction(func->getName()))
        {
            continue;
        }

        LICM licm;
        licm.runLICM(func);
    }
}