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
        ss << ".data\n";
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

    ss << getPrologue(func->getStackFrame());

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

    // 遍历指令，特殊处理RET指令
    for (const auto &inst : bb->getInstructions())
    {
        if (inst->getOpcode() == RISCVOpcode::RET)
        {
            ss << getEpilogue(bb->getParentFunc()->getStackFrame());

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

string AssemblyEmitter::getPrologue(const StackFrame &stack)
{
    stringstream ss;
    auto stackSize = stack.getAlignedSize();

    // 1. 调整栈指针（分配栈空间）
    if (stackSize > 0)
    {
        ss << "    li t0, " << stackSize << "\n";
        ss << "    sub sp, sp, t0\n";
    }

    // 2. 保存返回地址（ra）
    int raOffset = stack.getRaOffset();
    if (raOffset == stackSize - 4)
    {
        ss << "    subi t0, to, -4\n";
        ss << "    add t0, sp, t0\n"; // 确保sp指向正确位置
        ss << "    sd ra, 0(t0)\n";   // 保存ra寄存器到栈顶
    }

    return ss.str();
}

string AssemblyEmitter::getEpilogue(const StackFrame &stack)
{
    stringstream ss;
    auto stackSize = stack.getAlignedSize();

    // 2. 恢复返回地址（ra）
    int raOffset = stack.getRaOffset();
    if (raOffset == stackSize - 4)
    {
        ss << "    li t0, " << raOffset << "\n";
        ss << "    add t1, sp, t0\n"; // 确
        ss << "    ld ra, 0(t1)\n";   // 恢复ra寄存器
    }
    else
    {
        // 3. 释放栈空间（恢复sp）
        if (stackSize > 0)
        {
            ss << "    addi t0, t0, 4\n"; // 恢复栈指针
            ss << "    add sp, sp, t0\n";
        }

        return ss.str();
    }
}