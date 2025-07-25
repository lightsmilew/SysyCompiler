#include "RISCVBuilder.h"
#include <memory>
using namespace RISCV;

shared_ptr<RISCVModule> RISCVBuilder::generateRISCVCode(shared_ptr<Module> irModule)
{
    this->irModule = irModule;
    initializeModule(irModule);
    generateInstructions();
    allocateRegisters();
    reallocOffsetForInstructions();
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

        // 为函数添加prologue基本块
        auto prologueBB = make_shared<RISCVBasicBlock>("prologue_" + func->getName(), riscvFunc);
        riscvFunc->addBasicBlock(prologueBB);

        // 为每个IR基本块创建对应的RISC-V基本块
        for (const auto &bb : func->BasicBlocks)
        {
            auto riscvBB = make_shared<RISCVBasicBlock>(bb->getName(), riscvFunc);
            riscvFunc->addBasicBlock(riscvBB);
        }

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

void RISCVBuilder::allocateRegisters()
{
    // 为每个函数进行寄存器分配
    for (const auto &func : riscvModule->getFunctions())
    {
        GraphColorRegisterAllocator allocator;
        allocator.allocateRegisters(func);
    }
}

void RISCVBuilder::reallocOffsetForInstructions()
{
    // 遍历所有函数，重新计算指令的偏移量
    for (const auto &func : riscvModule->getFunctions())
    {
        auto &stackFrame = func->getStackFrame();
        for (const auto &instrPair : func->getInstructionNeedReGetOffset())
        {
            const string &argName = instrPair.first;
            shared_ptr<RISCVInstruction> instr = instrPair.second;

            // 重新计算偏移量并更新指令
            int offset = stackFrame.getValueOffset(argName);
            if (offset != -1)
            {
                // 更新指令的偏移量
                instr->setOffsetForLiInstruction(offset);
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