#pragma once
#include "Pass.h"

namespace optimization
{
    /// 基于循环嵌套与内存访问结构，将 guarded kernel 拆为 interior（无边界检查）
    class LoopNestInteriorSplitPass : public Pass
    {
    public:
        static constexpr int kPad = 2;
        static constexpr int kKernelSize = 5;

        LoopNestInteriorSplitPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopNestInteriorSplit"; }

    private:
        struct KernelNestInfo
        {
            const Loop *repeatLoop = nullptr;
            const Loop *rLoop = nullptr;
            const Loop *cLoop = nullptr;
            const Loop *krLoop = nullptr;
            const Loop *kcLoop = nullptr;

            Value *rIV = nullptr;
            Value *cIV = nullptr;
            Value *krIV = nullptr;
            Value *kcIV = nullptr;
            Value *nEff = nullptr;

            Value *inArray = nullptr;
            Value *outArray = nullptr;
            Value *kArray = nullptr;

            BasicBlock *repeatBody = nullptr;
            BasicBlock *rHeader = nullptr;
            BasicBlock *cBody = nullptr;
            BasicBlock *krHeader = nullptr;
            BasicBlock *krExitStore = nullptr;

            int pad = kPad;
            int kSize = kKernelSize;
        };

        static bool analyzeKernelNest(Function *func, const vector<Loop> &loops, KernelNestInfo &info);
        static bool applySplit(Function *func, KernelNestInfo &info, bool verbose, std::stringstream &dbg);
    };
}
