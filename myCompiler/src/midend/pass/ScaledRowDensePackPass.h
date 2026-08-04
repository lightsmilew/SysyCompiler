#pragma once
#include "MatrixStructureAnalysis.h"
#include "Pass.h"

namespace optimization
{
    /// 将 k-i-j scaled-row 内核搬到 pitch-n 稠密缓冲上执行，再写回原矩阵。
    class ScaledRowDensePackPass : public Pass
    {
    public:
        ScaledRowDensePackPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "ScaledRowDensePack"; }

    private:
        static GlobalVariable *getOrCreateDenseBuffer(Function *func, const string &name);
        bool applyFunctionGroup(Function *func, vector<ScaledRowUpdateNest> &nests);
    };
}
