#pragma once
#include "Pass.h"
#include "MatrixStructureAnalysis.h"

namespace optimization
{
    // 将矩阵循环的规整模式（scaled-row 更新、通用逐元素表达式）向量化：
    // 生成 strip-mining 向量循环（vecsetvl / vecload / vecsplat / vecbinary / vecstore）。
    // 仅在 CompilerConfig::enableRVV 为 true 时生效。
    class LoopVectorizePass : public Pass
    {
    public:
        LoopVectorizePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopVectorize"; }

    private:
        // C[i][j] = C[i][j] * A[i][k] + B[k][j] 的 j 内层循环 → 向量 strip-mining 循环
        bool vectorizeScaledRowUpdate(Function *func, const ScaledRowUpdateNest &nest);
    };
}
