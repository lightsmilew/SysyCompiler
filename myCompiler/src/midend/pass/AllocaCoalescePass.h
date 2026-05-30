#pragma once
#include "Pass.h"

namespace optimization
{
    // 将元素总数相同的 alloca 合并到同一支配块；并将带 {} 初始化的 alloca
    // 推迟到首次真正使用块（跳过早退路径），同时在目标块插入 array_init 空块供后端清零。
    class AllocaCoalescePass : public Pass
    {
    public:
        AllocaCoalescePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "AllocaCoalesce"; }
    };
}
