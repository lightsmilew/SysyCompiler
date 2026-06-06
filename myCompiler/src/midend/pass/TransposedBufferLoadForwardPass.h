#pragma once
#include "MatrixStructureAnalysis.h"

namespace optimization
{
    /// 在已建立转置双缓冲关系的区域内，将 load 转发到转置后的等价访存。
    class TransposedBufferLoadForwardPass : public Pass
    {
    public:
        TransposedBufferLoadForwardPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "TransposedBufferLoadForward"; }

    private:
        static LoadInst *materializeTransposedLoad(BasicBlock *bb, Value *rowIdx, Value *colIdx,
                                                   Value *newBase, unsigned insertIndex,
                                                   const std::string &tag);
        bool tryForwardLoad(Function *func, LoadInst *load, BasicBlock *bb,
                            const TransposeBufferRelation &rel);
    };
}
