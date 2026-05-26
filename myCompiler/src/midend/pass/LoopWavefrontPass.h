#pragma once
#include "Pass.h"

namespace optimization
{
    /// 将 i-j-k 原地 stencil 循环变换为 t=i+j+k 波前形式，暴露波前内无 RAW 依赖的迭代。
    class LoopWavefrontPass : public Pass
    {
    public:
        LoopWavefrontPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopWavefront"; }

    private:
        struct WavefrontPattern
        {
            const Loop *iLoop = nullptr;
            const Loop *jLoop = nullptr;
            const Loop *kLoop = nullptr;
            Value *iIV = nullptr;
            Value *jIV = nullptr;
            Value *kIV = nullptr;
            Value *bound = nullptr;
            GlobalVariable *stencilArray = nullptr;
            BasicBlock *iHeader = nullptr;
            BasicBlock *iBody = nullptr;
            BasicBlock *iExit = nullptr;
            BasicBlock *jHeader = nullptr;
            BasicBlock *jBody = nullptr;
            BasicBlock *jExit = nullptr;
            BasicBlock *kHeader = nullptr;
            BasicBlock *kBody = nullptr;
            BasicBlock *iPreheader = nullptr;
            std::vector<Instruction *> iSetupInsts;
            std::vector<Instruction *> jSetupInsts;
            std::vector<Instruction *> kStencilInsts;
        };

        static Value *stripCopy(Value *v);
        static bool sameValue(Value *a, Value *b);
        static const Loop *findParentLoop(const Loop &inner, const vector<Loop> &loops);
        static BasicBlock *getLoopLatch(const Loop &loop);
        static BasicBlock *getLoopExit(const Loop &loop);
        static bool isSimpleTwoBlockLoop(const Loop &loop);
        static bool getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound, ICmpInst *&cmp);
        static PhiInst *findPhiAtHeader(BasicBlock *header, Value *iv);
        static bool isLoopIncrementByOne(BasicBlock *latch, Value *iv, Value *&incResult);
        static BasicBlock *getLoopBodyBlock(const Loop &loop);
        static GlobalVariable *getRootGlobal(Value *ptr);
        static bool isStencilKBody(BasicBlock *body, Value *kIV, GlobalVariable *&arrayOut);
        static void collectCloneableInsts(BasicBlock *bb, Value *iv, std::vector<Instruction *> &out);
        static bool matchStencilNest(Function *func, const vector<Loop> &loops, WavefrontPattern &pat);
        bool applyWavefront(Function *func, WavefrontPattern &pat);
    };
}
