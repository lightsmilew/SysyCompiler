#pragma once
#include "Pass.h"

namespace optimization
{
    /// 对带边界 guard 的 stencil nest 做 interior/border 拆分。
    /// 从 icmp 抽出仿射下标（如 rr = r + kr - pad），按内层 IV 值域证明
    /// 外层 IV 落在 [lo, n-hiSub) 时 guard 恒真，再插入无检查的 interior 路径。
    class LoopNestInteriorSplitPass : public Pass
    {
    public:
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

            int pad = 0;
            int kSize = 0;
            // interior: r,c ∈ [interiorLo, nEff - interiorHiSub)
            int interiorLo = 0;
            int interiorHiSub = 0;
        };

        static bool analyzeKernelNest(Function *func, const vector<Loop> &loops, KernelNestInfo &info,
                                      std::string *failReason = nullptr);
        static bool applySplit(Function *func, KernelNestInfo &info, bool verbose, std::stringstream &dbg);
    };
}
