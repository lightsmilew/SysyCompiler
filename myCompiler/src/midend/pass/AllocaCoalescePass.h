#pragma once
#include "Pass.h"

namespace optimization
{
    // 将元素总数相同的 alloca 合并到同一支配块，便于后端集中生成初始化代码
    class AllocaCoalescePass : public Pass
    {
    public:
        AllocaCoalescePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "AllocaCoalesce"; }
    };
}
