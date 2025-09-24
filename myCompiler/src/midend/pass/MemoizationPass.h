#pragma once
#include "Pass.h"
namespace optimization
{
    // 25.递归记忆化 Pass
    class MemoizationPass : public Pass
    {
    public:
        MemoizationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "Memoization"; }

    private:
        // 生成全局数组名
        std::string getMemoFlagArrayName(const std::string &funcName)
        {
            return "__memo_flag_" + funcName;
        }
        std::string getMemoValueArrayName(const std::string &funcName)
        {
            return "__memo_value_" + funcName;
        }
        std::string getMemoArgsArrayName(const std::string &funcName)
        {
            return "__memo_args_" + funcName;
        }
        // 生成参数索引（所有参数为int，用哈希计算对应下标，模数为65535）
        Value *getMemoIndex(const std::vector<Value *> &args, Function *func);

        // 判断是否适合记忆化
        bool isMemoizable(Function *func);
    };
}