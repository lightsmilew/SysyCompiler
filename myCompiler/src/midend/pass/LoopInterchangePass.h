#pragma once
#include "Pass.h"

namespace optimization
{
    /// 将 i-j-k 点积循环交换为 i-k-j，改善矩阵乘等访存模式。
    class LoopInterchangePass : public Pass
    {
    public:
        LoopInterchangePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopInterchange"; }

    private:
        struct NestPattern
        {
            const Loop *iLoop = nullptr;
            const Loop *jLoop = nullptr;
            const Loop *kLoop = nullptr;
            Value *iIV = nullptr;
            Value *jIV = nullptr;
            PhiInst *kPhi = nullptr;
            PhiInst *sumPhi = nullptr;
            Value *bound = nullptr;
            Value *cArray = nullptr;
            Value *aArray = nullptr;
            BasicBlock *jHeader = nullptr;
            BasicBlock *kHeader = nullptr;
            BasicBlock *kBody = nullptr;
            BasicBlock *kExit = nullptr;
            BasicBlock *jExit = nullptr;
            BasicBlock *iBody = nullptr;
            LoadInst *cLoad = nullptr;
            LoadInst *aLoad = nullptr;
            StoreInst *aStore = nullptr;
        };

        static Value *stripCopy(Value *v);
        static bool sameValue(Value *a, Value *b);
        static const Loop *findParentLoop(const Loop &inner, const vector<Loop> &loops);
        static BasicBlock *getLoopLatch(const Loop &loop);
        static BasicBlock *getLoopExit(const Loop &loop);
        static bool isSimpleTwoBlockLoop(const Loop &loop);
        static bool getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound, ICmpInst *&cmp);
        static PhiInst *findPhiAtHeader(BasicBlock *header, Value *iv);
        static bool parse2DAccess(Value *ptr, Value *&rowIdx, Value *&colIdx, Value *&arrayBase);
        static bool storeUsesSum(StoreInst *store, PhiInst *sumPhi, BasicBlock *kHeader);
        static bool matchDotProductNest(Function *func, const vector<Loop> &loops, NestPattern &pat);
        static AllocaInst *getOrCreateAccBuffer(Function *func);
        bool applyInterchange(Function *func, NestPattern &pat);
    };
}
