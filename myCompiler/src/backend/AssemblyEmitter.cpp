#include "AssemblyEmitter.h"
#include <sstream>
#include <set>
using namespace RISCV;
using std::set;
using std::stringstream;

namespace
{
    constexpr int kImm12Min = -2048;
    constexpr int kImm12Max = 2047;

    bool fitsInSignedImm12(int value)
    {
        return value >= kImm12Min && value <= kImm12Max;
    }

    void emitAdjustSp(stringstream &ss, int delta)
    {
        if (fitsInSignedImm12(delta))
        {
            ss << "        addi sp, sp, " << delta << "\n";
            return;
        }

        ss << "        li t0, " << (delta < 0 ? -delta : delta) << "\n";
        if (delta < 0)
        {
            ss << "        sub sp, sp, t0\n";
        }
        else
        {
            ss << "        add sp, sp, t0\n";
        }
    }

    void emitStore(stringstream &ss, const string &reg, int offset, bool isFloat = false)
    {
        const string op = isFloat ? "fsd" : "sd";
        if (fitsInSignedImm12(offset))
        {
            ss << "        " << op << " " << reg << ", " << offset << "(sp)\n";
            return;
        }

        ss << "        li t0, " << offset << "\n";
        ss << "        add t0, sp, t0\n";
        ss << "        " << op << " " << reg << ", 0(t0)\n";
    }

    void emitLoad(stringstream &ss, const string &reg, int offset, bool isFloat = false)
    {
        const string op = isFloat ? "fld" : "ld";
        if (fitsInSignedImm12(offset))
        {
            ss << "        " << op << " " << reg << ", " << offset << "(sp)\n";
            return;
        }

        ss << "        li t0, " << offset << "\n";
        ss << "        add t0, sp, t0\n";
        ss << "        " << op << " " << reg << ", 0(t0)\n";
    }
}

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
        emitAdjustSp(ss, -stackSize);
    }

    if (stack.raStackSize)
    {
        emitStore(ss, "ra", stack.getRaOffset());
    }

    if (func->getUsedCalleeSavedRegs().size() > 0)
    {
        auto offset = stack.getValueOffset("usedCalleeSavedRegs");
        for (size_t i = 0; i < func->getUsedCalleeSavedRegs().size(); i++)
        {
            const auto &reg = func->getUsedCalleeSavedRegs()[i];
            emitStore(ss, reg->toString(), offset + static_cast<int>(i * 8),
                      reg->getType() == RegisterType::FLOAT);
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
        for (size_t i = 0; i < func->getUsedCalleeSavedRegs().size(); i++)
        {
            const auto &reg = func->getUsedCalleeSavedRegs()[i];
            emitLoad(ss, reg->toString(), offset + static_cast<int>(i * 8),
                     reg->getType() == RegisterType::FLOAT);
        }
    }

    // 2. 恢复返回地址（ra）并释放栈空间
    if (stack.raStackSize)
    {
        emitLoad(ss, "ra", stack.getRaOffset());
        emitAdjustSp(ss, stackSize);
    }
    else if (stackSize > 0)
    {
        emitAdjustSp(ss, stackSize);
    }

    return ss.str();
}