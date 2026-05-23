#pragma once
#include "Pass.h"

namespace optimization
{
    // 基本块内指令重排：纯计算可在无数据/内存依赖时越过锚点；锚点之间保持原序
    class InstructionReorderPass : public Pass
    {
    public:
        InstructionReorderPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "InstructionReorder"; }
    };
}
