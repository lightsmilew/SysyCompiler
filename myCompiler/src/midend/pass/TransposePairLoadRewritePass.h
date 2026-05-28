#pragma once
#include "Pass.h"
#include <unordered_set>

namespace optimization
{
    /// 识别 b[i][j]=a[j][i] 全矩阵转置后，将后续 load b[r][c]→a[c][r]、load a[r][c]→b[c][r] 以改善访存。
    class TransposePairLoadRewritePass : public Pass
    {
    public:
        TransposePairLoadRewritePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "TransposePairLoadRewrite"; }

    private:
        struct TransposeRelation
        {
            Value *arrayA = nullptr;
            Value *arrayB = nullptr;
            BasicBlock *outerExit = nullptr;
            std::unordered_set<BasicBlock *> skipBlocks;
        };

        static Value *stripCopy(Value *v);
        static bool sameValue(Value *a, Value *b);
        static bool sameArray(Value *a, Value *b);
        static bool sameBound(Value *a, Value *b);
        static bool feedsInductionVar(Value *from, Value *iv, unsigned depth = 0);
        static bool matchesLoopIV(Value *idx, Value *iv);
        static bool isKJMatrixAccess(Value *row, Value *col, Value *iIV, Value *jIV, Value *kIV);
        static bool isIKMatrixAccess(Value *row, Value *col, Value *iIV, Value *kIV);
        enum class KJLoadUseKind
        {
            ParityWithAIK,
            AccumWithBIK,
            Other
        };
        static KJLoadUseKind classifyKJLoadUser(Instruction *user, LoadInst *kjLoad, Value *iIV,
                                                Value *kIV, const TransposeRelation &rel);
        static const Loop *findParentLoop(const Loop &inner, const vector<Loop> &loops);
        static bool getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound, ICmpInst *&cmp);
        static bool parse2DAccess(Value *ptr, Value *&rowIdx, Value *&colIdx, Value *&arrayBase);
        static bool matchTransposeStore(StoreInst *store, Value *iIV, Value *jIV, Value *&arrayA,
                                        Value *&arrayB);
        static bool findFullMatrixTranspose(Function *func, TransposeRelation &rel);
        static bool findIJKInductionVars(BasicBlock *bb, const vector<Loop> &loops, Value *&iIV,
                                           Value *&jIV, Value *&kIV);
        static bool canRewriteInBlock(BasicBlock *bb, const TransposeRelation &rel);
        static LoadInst *materializeTransposedLoad(BasicBlock *bb, Value *rowIdx, Value *colIdx,
                                                     Value *newBase, unsigned insertIndex,
                                                     const std::string &tag);
        bool tryRewriteLoad(Function *func, LoadInst *load, BasicBlock *bb,
                            const TransposeRelation &rel);
    };
}
