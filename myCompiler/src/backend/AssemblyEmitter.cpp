#include "AssemblyEmitter.h"
#include <sstream>
#include <set>
using namespace RISCV;
using std::set;
using std::stringstream;

// AssemblyEmitter 实现
string AssemblyEmitter::emit(shared_ptr<RISCVModule> module)
{
    stringstream ss;

    // 生成全局变量段
    if (!module->getGlobalBlocks().empty())
    {
        ss << emitGlobals(module->getGlobalBlocks());
    }

    // 生成文本段
    ss << ".text\n";

    // 生成函数
    for (const auto &func : module->getFunctions())
    {
        ss << emitFunction(func) << "\n";
    }

    return ss.str();
}

string AssemblyEmitter::emitGlobals(const vector<shared_ptr<RISCVGlobalBlock>> &globals)
{
    stringstream ss;
    int bssCounter = 0; // 用于生成唯一的bss标签
    int dataCounter = 0; // 用于生成唯一的数据标签
    for (const auto &global : globals)
    {
        if (!global->isInitialized())
        {
            // 未初始化的全局变量放在 .bss 段
            if (bssCounter == 0)
            {
                ss << ".section .bss\n";
                bssCounter++;
            }
            ss << global->getLabel() << ":\n";
            ss << "    .skip " << global->getSize() << "\n";
        }
        else
        {
            // 已初始化的全局变量放在 .data 段
            if (dataCounter == 0)
            {
                ss << ".section .data\n";
                dataCounter++;
            }
            ss << global->toString() << "\n";
        }
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

        if (bb->getLabel() == "prologue_" + bb->getParentFunc()->getName())
        {
            ss << getPrologue(bb->getParentFunc());
        }
    }

    // 遍历指令，特殊处理RET指令
    for (const auto &inst : bb->getInstructions())
    {
        if (inst->getOpcode() == RISCVOpcode::RET)
        {
            ss << getEpilogue(bb->getParentFunc());

            // 3. 执行RET指令（必须在恢复后）
            ss << "    " << inst->toString() << "\n";
        }
        else
        {
            ss << "    " << inst->toString() << "\n";
        }
    }

    return ss.str();
}

string AssemblyEmitter::getPrologue(const shared_ptr<RISCVFunction> func)
{
    stringstream ss;

    auto stack = func->getStackFrame();
    auto stackSize = stack.getAlignedSize();

    // 1. 调整栈指针（分配栈空间）
    if (stackSize > 0)
    {
        ss << "        li t0, " << stackSize << "\n";
        ss << "        sub sp, sp, t0\n";
    }

    if (stack.raStackSize)
    {
        ss << "        addi t0, t0, -8\n";
        ss << "        add t0, sp, t0\n"; // 确保sp指向正确位置
        ss << "        sd ra, 0(t0)\n";   // 保存ra寄存器到栈顶
    }

    if (func->getUsedCalleeSavedRegs().size() > 0)
    {
        auto offset = stack.getValueOffset("usedCalleeSavedRegs");
        ss << "        li t0, " << offset << "\n";
        ss << "        add t1, sp, t0\n"; // 确保sp指向正确位置
        // 保存被调用函数使用的保存寄存器
        for (size_t i = 0; i < func->getUsedCalleeSavedRegs().size(); i++)
        {
            if (func->getUsedCalleeSavedRegs()[i]->getType() == RegisterType::FLOAT)
            {
                ss << "        fsd " << func->getUsedCalleeSavedRegs()[i]->toString() << ", " << (i * 8) << "(t1)\n";
            }
            else
            {
                // 确保是整数寄存器
                ss << "        sd " << func->getUsedCalleeSavedRegs()[i]->toString() << ", " << (i * 8) << "(t1)\n";
            }
        }
    }

    return ss.str();
}

string AssemblyEmitter::getEpilogue(const shared_ptr<RISCVFunction> func)
{
    stringstream ss;
    auto stack = func->getStackFrame();
    auto stackSize = stack.getAlignedSize();

    // 1. 恢复被调用函数使用的保存寄存器（callee-saved）
    if (func->getUsedCalleeSavedRegs().size() > 0)
    {
        auto offset = stack.getValueOffset("usedCalleeSavedRegs");
        ss << "        li t0, " << offset << "\n";
        ss << "        add t1, sp, t0\n";
        for (size_t i = 0; i < func->getUsedCalleeSavedRegs().size(); i++)
        {
            if (func->getUsedCalleeSavedRegs()[i]->getType() == RegisterType::FLOAT)
            {
                ss << "        fld " << func->getUsedCalleeSavedRegs()[i]->toString() << ", " << (i * 8) << "(t1)\n";
            }
            else
            {
                ss << "        ld " << func->getUsedCalleeSavedRegs()[i]->toString() << ", " << (i * 8) << "(t1)\n";
            }
        }
    }

    // 2. 恢复返回地址（ra）
    if (stack.raStackSize)
    {
        ss << "        li t0, " << stack.getRaOffset() << "\n";
        ss << "        add t1, sp, t0\n";
        ss << "        ld ra, 0(t1)\n";
        ss << "        addi t0, t0, 8\n";
        ss << "        add sp, sp, t0\n";
    }
    else if (stackSize > 0)
    {
        ss << "        li t0, " << stackSize << "\n";
        ss << "        add sp, sp, t0\n";
    }

    return ss.str();
}