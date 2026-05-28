#pragma once
#include "Pass.h"

namespace optimization
{
    /// 对 c[i][j] = -c[j][i] 型二重循环：j 从 0 改为从 i 起，只写上三角与对角。
    class SymmetricNegateUpperTrianglePass : public Pass
    {
    public:
        SymmetricNegateUpperTrianglePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "SymmetricNegateUpperTriangle"; }

    private:
        static Value *stripCopy(Value *v);
        static bool sameValue(Value *a, Value *b);
        static bool sameBound(Value *a, Value *b);
        static const Loop *findParentLoop(const Loop &inner, const vector<Loop> &loops);
        static bool getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound, ICmpInst *&cmp);
        static bool parse2DAccess(Value *ptr, Value *&rowIdx, Value *&colIdx, Value *&arrayBase);
        static bool isZeroInit(Value *v);
        static bool isNegatedLoad(Value *val, LoadInst *&loadOut);
        static bool matchSymmetricNegateStore(StoreInst *store, Value *iIV, Value *jIV, Value *cArray);
        static bool feedsInductionVar(Value *from, Value *iv, unsigned depth = 0);
        static CopyInst *findJInitCopyInOuterBody(const Loop &iLoop, const Loop &jLoop, Value *jIV);
        bool setJInitToOuterIV(const Loop &iLoop, const Loop &jLoop, Value *iIV, Value *jIV);
    };
}
