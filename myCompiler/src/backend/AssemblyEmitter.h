#pragma once
#include "RISCVBuilder.h"
using namespace RISCV;

// 汇编生成器
class AssemblyEmitter
{
public:
    string emit(shared_ptr<RISCVModule> module);

private:
    string emitGlobals(const vector<shared_ptr<RISCVGlobalBlock>> &globals);
    string emitFunction(shared_ptr<RISCVFunction> func);
    string emitBasicBlock(shared_ptr<RISCVBasicBlock> bb);
    string getPrologue(const shared_ptr<RISCVFunction> func);
    string getEpilogue(const shared_ptr<RISCVFunction> func);
};