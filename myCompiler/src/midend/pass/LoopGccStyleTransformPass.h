#pragma once
#include "Pass.h"

namespace optimization
{
    // 将 LLVM 风格循环 (preheader -> cond -> body/exit, body 跳回 cond)
    // 变换为 GCC 风格 (preheader 入库判断, body 末尾条件回边到 body 或 exit)。
    class LoopGccStyleTransformPass : public Pass
    {
    public:
        LoopGccStyleTransformPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopGccStyleTransform"; }

    private:
        bool tryTransform(Function *func, const Loop &loop);
        void replacePhiIncomingBlock(BasicBlock *succBlock, BasicBlock *oldPred, BasicBlock *newPred);
    };
}
