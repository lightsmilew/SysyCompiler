#pragma once
#include "Pass.h"
#include <optional>
#include <unordered_set>
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

    /// 转置写建立的双缓冲关系：dst[i][j] = src[j][i]
    struct TransposeBufferRelation
    {
        bool valid = false;
        Value *srcBuffer = nullptr;
        Value *dstBuffer = nullptr;
        BasicBlock *regionEntry = nullptr;
        std::unordered_set<BasicBlock *> transposeLoopBlocks;
        StoreInst *witnessStore = nullptr;
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

    /// 通用：纯 init + 仅原地拷贝更新 + 对前缀的纯归约 ⇒ 可用起源追踪消去拷贝链。
    /// 三角仿射写 M[j*C+i]=M[i*R+j]（C=n/R）是该类拷贝的一种。
    struct InPlaceCopyOriginChain
    {
        bool valid = false;
        Value *matrix = nullptr;
        Value *shapeVec = nullptr;
        Value *extentN = nullptr;
        Value *reduceBound = nullptr;

        const Loop *shapeLoop = nullptr;
        const Loop *rowLoop = nullptr;
        const Loop *colLoop = nullptr;
        Value *rowIV = nullptr;
        Value *colIV = nullptr;
        Value *shapeIV = nullptr;
        Value *rowSize = nullptr;
        Value *colSize = nullptr;

        StoreInst *copyWitness = nullptr;
        BasicBlock *computeEntry = nullptr;

        /// 原 shape 循环是否用 shapeVec[bound-1-t]（逆序）；起源追踪应对原序取逆。
        bool shapeLoadReversed = false;
        ArrayPureInitKind initKind = ArrayPureInitKind::Identity;
        int initMask = 0;
        int initReplace = 0;
        /// 归约为 sum_k k*k*M[k] 后是否再做 abs
        bool reductionAbs = false;

        /// CFG 拼接锚点
        BasicBlock *replaceEntry = nullptr; // 原计算区域入口（init/拷贝起点）
        BasicBlock *continueBB = nullptr;   // 区域之后的延续块
        Value *oldResult = nullptr;        // 原归约结果，供 RAUW
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
        std::optional<TransposeBufferRelation> transposePair;
        std::vector<SkewSymmetricMatrixNest> skewSymmetricNests;
        std::vector<MatMulDotProductNest> matMulDotProductNests;
        std::optional<InPlaceCopyOriginChain> inPlaceCopyOriginChain;
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
        bool isTransposeWitnessStore(StoreInst *store, Value *iIV, Value *jIV, Value *&srcBuffer,
                                     Value *&dstBuffer);
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

        enum class KJLoadUseKind
        {
            ParityWithAIK,
            AccumWithBIK,
            Other
        };
        KJLoadUseKind classifyKJLoadUser(Instruction *user, LoadInst *kjLoad, Value *iIV, Value *kIV,
                                         const TransposeBufferRelation &rel);

        bool isReachableFrom(BasicBlock *from, BasicBlock *to,
                             const std::unordered_set<BasicBlock *> &forbidden);
        CopyInst *findJZeroInitCopy(const SquareIJLoopNest &nest, Value *jIV);
        bool hasJPhiZeroInit(const Loop &jLoop, Value *jIV);

        bool valueDependsOn(Value *expr, Value *target, unsigned depth = 0);
        bool parseFlatAffine2(Value *idx, Value *&termA, Value *&termB);
        bool isTriangularCopyWitnessStore(StoreInst *store, Value *rowIV, Value *colIV, Value *rowSize,
                                          Value *colSize, Value *&matrixOut);
        bool refineCopyOriginSemantics(Function *func, InPlaceCopyOriginChain &chain);
        std::optional<InPlaceCopyOriginChain> analyzeInPlaceCopyOriginChain(Function *func);

        MatrixFunctionAnalysis analyzeFunction(Function *func);
        const MatrixFunctionAnalysis *getAnalysis(Function *func);
        void clearAnalysis(Function *func);
    } // namespace matrixStructure

    /// 识别函数内矩阵循环 nest、转置双缓冲与斜对称结构，供后续结构型变换读取。
    class MatrixStructureAnalysisPass : public Pass
    {
    public:
        MatrixStructureAnalysisPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "MatrixStructureAnalysis"; }
    };
}
