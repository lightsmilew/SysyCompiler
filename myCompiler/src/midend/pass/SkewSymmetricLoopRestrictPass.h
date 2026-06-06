#pragma once
#include "MatrixStructureAnalysis.h"

namespace optimization
{
    /// 对已识别的斜对称矩阵 nest，将内层 j 下界从 0 收紧为 i，只写上三角与对角。
    class SkewSymmetricLoopRestrictPass : public Pass
    {
    public:
        SkewSymmetricLoopRestrictPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "SkewSymmetricLoopRestrict"; }

    private:
        bool restrictNest(const SkewSymmetricMatrixNest &nest);
    };
}
