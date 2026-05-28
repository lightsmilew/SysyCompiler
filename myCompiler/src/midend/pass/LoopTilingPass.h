#pragma once
#include "Pass.h"

namespace optimization
{
    /// 对 i-j-k 条件累加循环做 cache blocking（默认 tile=50）。
    class LoopTilingPass : public Pass
    {
    public:
        static constexpr int kTileSize = 50;

        LoopTilingPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopTiling"; }

    private:
        struct NestPattern
        {
            const Loop *iLoop = nullptr;
            const Loop *jLoop = nullptr;
            const Loop *kLoop = nullptr;
            Value *iIV = nullptr;
            Value *jIV = nullptr;
            Value *kIV = nullptr;
            Value *acc = nullptr;
            Value *bound = nullptr;
            int boundConst = 0;
            BasicBlock *iHeader = nullptr;
            BasicBlock *iBody = nullptr;
            BasicBlock *iExit = nullptr;
            BasicBlock *iPreheader = nullptr;
            BasicBlock *jHeader = nullptr;
            BasicBlock *jBody = nullptr;
            BasicBlock *jExit = nullptr;
            BasicBlock *kHeader = nullptr;
            BasicBlock *kBody = nullptr;
            BasicBlock *kExit = nullptr;
            StoreInst *cStore = nullptr;
            Value *cRowGepBase = nullptr;
            Value *cArray = nullptr;
        };

        static Value *stripCopy(Value *v);
        static bool sameValue(Value *a, Value *b);
        static bool sameBound(Value *a, Value *b);
        static bool sameIndex(Value *a, Value *b);
        static const Loop *findParentLoop(const Loop &inner, const vector<Loop> &loops);
        static BasicBlock *getLoopLatch(const Loop &loop);
        static BasicBlock *getLoopExit(const Loop &loop);
        static bool isSimpleTwoBlockLoop(const Loop &loop);
        static bool getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound, ICmpInst *&cmp);
        static bool parse2DAccess(Value *ptr, Value *&rowIdx, Value *&colIdx, Value *&arrayBase);
        static bool matchGuardedAccumulateNest(const vector<Loop> &loops, NestPattern &pat,
                                               bool verbose, std::stringstream &dbg);
        static Value *makeTileUpperBound(Value *tileStart, Value *globalBound, int tileSize,
                                         BasicBlock *bb, const string &name);
        static void moveKBodyCompute(BasicBlock *src, BasicBlock *dst,
                                     unordered_map<Value *, Value *> &vmap, Value *oldKIV,
                                     Value *oldAcc);
        bool applyTiling(Function *func, NestPattern &pat);
    };
}
