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