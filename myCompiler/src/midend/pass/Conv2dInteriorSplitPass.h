#pragma once
#include "Pass.h"

namespace optimization
{
    /// 将 conv2d 拆为 interior（无边界检查）与 border（保留 guard）两段。
    /// 通过循环嵌套与 IR 结构匹配，不依赖基本块名称。
    class Conv2dInteriorSplitPass : public Pass
    {
    public:
        static constexpr int kPad = 2;
        static constexpr int kKernelSize = 5;

        Conv2dInteriorSplitPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "Conv2dInteriorSplit"; }

    private:
        struct Conv2dPattern
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

        static bool matchConv2dNest(Function *func, const vector<Loop> &loops, Conv2dPattern &pat);
        static bool applySplit(Function *func, Conv2dPattern &pat, bool verbose, std::stringstream &dbg);
    };
}
