#pragma once
#include "Pass.h"
#include <optional>
#include <vector>

namespace optimization
{
    /// 方阵二重循环 nest（i 外层、j 内层，同一边界）
    struct SquareIJLoopNest
    {
        const Loop *iLoop = nullptr;
        const Loop *jLoop = nullptr;
        Value *iIV = nullptr;
        Value *jIV = nullptr;
        Value *bound = nullptr;
    };

    /// 斜对称（反对称）矩阵 nest：c[i][j] = -c[j][i]，j 初值为 0
    struct SkewSymmetricMatrixNest
    {
        bool valid = false;
        SquareIJLoopNest nest;
        Value *matrix = nullptr;
        StoreInst *witnessStore = nullptr;
        bool jInitFromZero = false;
    };

    /// 数组纯初始化形态（由 init 循环上的 store 推断）
    enum class ArrayPureInitKind
    {
        Identity,             // M[i] = i
        IdentityUnlessMasked  // M[i] = ((i & mask) == 0) ? repl : i
    };


    /// k-i-j scaled-row 更新：
    ///   add: C[i][j] = C[i][j] * A[i][k] + B[k][j]（可选 A[i][k]==1 跳过）
    ///   sub: B[j][k] = B[j][k] - A[j][i] * B[i][k]（TRSM rank-1 / float）
    struct ScaledRowUpdateNest
    {
        bool valid = false;
        const Loop *kLoop = nullptr;
        const Loop *iLoop = nullptr;
        const Loop *jLoop = nullptr;
        Value *kIV = nullptr;
        Value *iIV = nullptr;
        Value *jIV = nullptr;
        Value *bound = nullptr;
        Value *aArray = nullptr;
        Value *bArray = nullptr;
        Value *cArray = nullptr;
        Type *elemTy = nullptr;
        BasicBlock *kHeader = nullptr;
        BasicBlock *iHeader = nullptr;
        BasicBlock *jHeader = nullptr;
        BasicBlock *kBodyOrIEntry = nullptr;
        LoadInst *aLoad = nullptr;
        StoreInst *cStore = nullptr;
        ICmpInst *skipCmp = nullptr;
        bool hasSkipGuard = false;
        bool isSubtract = false;
    };

    /// i-j-k 点积 nest：out[i][j] += (可选奇偶条件) * lhs[i][k] * rhs[k][j]
    struct MatMulDotProductNest
    {
        bool valid = false;
        const Loop *iLoop = nullptr;
        const Loop *jLoop = nullptr;
        const Loop *kLoop = nullptr;
        Value *iIV = nullptr;
        Value *jIV = nullptr;
        PhiInst *kPhi = nullptr;
        PhiInst *sumPhi = nullptr;
        Value *bound = nullptr;
        Value *lhsArray = nullptr; // lhs[i][k]
        Value *rhsArray = nullptr; // rhs[k][j]
        Value *outArray = nullptr; // out[i][j]
        /// CondGuarded 后的形式：再乘以「两操作数不同时为奇」的 0/1 条件
        bool hasParityGuard = false;
        Value *parityIkArray = nullptr; // P[i][k]
        Value *parityKjArray = nullptr; // Q[k][j]
        BasicBlock *jHeader = nullptr;
        BasicBlock *kHeader = nullptr;
        BasicBlock *kBody = nullptr;
        BasicBlock *kExit = nullptr;
        BasicBlock *jExit = nullptr;
        BasicBlock *iBody = nullptr;
        LoadInst *lhsLoad = nullptr;
        LoadInst *rhsLoad = nullptr;
        StoreInst *outputStore = nullptr;
    };

    struct MatrixFunctionAnalysis
    {
        std::vector<SkewSymmetricMatrixNest> skewSymmetricNests;
        std::vector<MatMulDotProductNest> matMulDotProductNests;
        std::vector<ScaledRowUpdateNest> scaledRowUpdateNests;
    };

    namespace matrixStructure
    {
        Value *stripCopy(Value *v);
        bool sameValue(Value *a, Value *b);
        bool sameArray(Value *a, Value *b);
        bool sameBound(Value *a, Value *b);
        bool feedsInductionVar(Value *from, Value *iv, unsigned depth = 0);
        bool matchesLoopIV(Value *idx, Value *iv);

        const Loop *findParentLoop(const Loop &inner, const vector<Loop> &loops);
        const Loop *findInnermostLoopContaining(BasicBlock *bb, const vector<Loop> &loops);
        bool getHeaderBoundCmp(BasicBlock *header, Value *&iv, Value *&bound, ICmpInst *&cmp);
        bool parse2DAccess(Value *ptr, Value *&rowIdx, Value *&colIdx, Value *&arrayBase);

        bool findSquareIJNest(const Loop &jLoop, const vector<Loop> &loops, SquareIJLoopNest &out);
        bool findIJKInductionVars(BasicBlock *bb, const vector<Loop> &loops, Value *&iIV, Value *&jIV,
                                  Value *&kIV);

        BasicBlock *getLoopLatch(const Loop &loop);
        BasicBlock *getLoopExit(const Loop &loop);
        bool isSimpleTwoBlockLoop(const Loop &loop);
        PhiInst *findPhiAtHeader(BasicBlock *header, Value *iv);
        bool storeUsesSum(StoreInst *store, PhiInst *sumPhi, BasicBlock *kHeader);

        bool isZeroInit(Value *v);
        bool isNegatedLoad(Value *val, LoadInst *&loadOut);
        bool isSkewSymmetricWitnessStore(StoreInst *store, Value *iIV, Value *jIV, Value *matrix);

        bool isKJMatrixAccess(Value *row, Value *col, Value *iIV, Value *jIV, Value *kIV);
        bool isIKMatrixAccess(Value *row, Value *col, Value *iIV, Value *kIV);

        bool isMatMulAccumWitness(BasicBlock *kBody, PhiInst *kPhi, PhiInst *sumPhi, Value *iIV,
                                  Value *jIV, LoadInst *&lhsLoad, LoadInst *&rhsLoad,
                                  Value *&lhsArray, Value *&rhsArray, bool &hasParityGuard,
                                  Value *&parityIkArray, Value *&parityKjArray);
        bool isMatMulOutputStore(StoreInst *store, PhiInst *sumPhi, BasicBlock *kHeader, Value *iIV,
                                 Value *jIV, Value *&outArray);
        bool findMatMulDotProductNest(const Loop &kLoop, const vector<Loop> &loops,
                                      MatMulDotProductNest &out);
        bool findScaledRowUpdateNest(const Loop &jLoop, const vector<Loop> &loops,
                                     ScaledRowUpdateNest &out);

        CopyInst *findJZeroInitCopy(const SquareIJLoopNest &nest, Value *jIV);
        bool hasJPhiZeroInit(const Loop &jLoop, Value *jIV);

        bool valueDependsOn(Value *expr, Value *target, unsigned depth = 0);

        MatrixFunctionAnalysis analyzeFunction(Function *func);
        const MatrixFunctionAnalysis *getAnalysis(Function *func);
        void clearAnalysis(Function *func);
    } // namespace matrixStructure

    /// 识别函数内矩阵循环 nest 与斜对称结构，供后续结构型变换读取。
    class MatrixStructureAnalysisPass : public Pass
    {
    public:
        MatrixStructureAnalysisPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "MatrixStructureAnalysis"; }
    };
}
