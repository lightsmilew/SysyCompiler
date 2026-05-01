#pragma once
#include "Pass.h"

namespace optimization
{

    /**
     * 多面体启发式中端 Pass（受限完整版）
     *
     * 仅处理 canonical perfect 2-level while-nest，收集近似依赖向量并在
     * interchange legality 成立时执行可并行化改写入口（当前工程语义安全子集）。
     */
    class PolyhedralLoopOptimizePass : public Pass
    {
    public:
        PolyhedralLoopOptimizePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "PolyhedralLoopOptimize"; }

    private:
        struct CanonicalLoopShape
        {
            BasicBlock *preheader{nullptr};
            BasicBlock *header{nullptr};
            BasicBlock *body{nullptr};
            BasicBlock *latch{nullptr};
            BasicBlock *exit{nullptr};
            PhiInst *indPhi{nullptr};
            Value *bound{nullptr};
            int step{0};
            bool isInc{true};
            bool valid{false};
        };
        struct InterchangePlan
        {
            Value *innerInit{nullptr};
            Value *outerInit{nullptr};
            Instruction *outerStepInst{nullptr};
            Instruction *innerStepInst{nullptr};
            bool valid{false};
        };

        struct Affine1D
        {
            Value *phi{nullptr}; // 归纳变量对应的 phi / 等价 value
            int offset{0};
            bool valid{false};
        };

        /// 在同一基本块仿射索引上尽量识别 v = phi + const
        static bool tryParseAffine1D(Value *v, Affine1D &out);
        /// 从 GEP 取 2D 下标（要求为两维索引；一维线性化由其他 pass 处理时本分析会跳过）
        static bool tryGet2DIndices(GetElementPtrInst *gep, Affine1D &row, Affine1D &col);

        static void findNestedLoopPairs(const std::vector<Loop> &loops,
                                        std::vector<std::pair<const Loop *, const Loop *>> &out);

        /// 识别 loop header icmp 上与边界比较的归纳 Phi（单边 Phi 或 phi±const）
        static PhiInst *findInductionPhi(BasicBlock *header);
        /// 交换嵌套后在字典序下仍为正向：(Δ_outer,Δ_inner) → 检验 lex+(Δ_inner,Δ_outer)
        static bool interchangeLegalLex(const std::vector<std::pair<int, int>> &depVectors);

        bool maybeOptimizeNest(const Loop *outer, const Loop *inner);
        bool matchCanonicalLoopShape(const Loop *loop, CanonicalLoopShape &out) const;
        static bool isPerfectTwoLevelNest(const CanonicalLoopShape &outer, const CanonicalLoopShape &inner);
        static bool buildInterchangePlan(const CanonicalLoopShape &outer, const CanonicalLoopShape &inner,
                                         InterchangePlan &plan);
        static bool addLoopTag(BasicBlock *bb, const std::string &tag);
        static int extractStepConst(PhiInst *phi, Instruction *stepInst, bool &ok);
        static void swapPhiUsesInBlock(BasicBlock *bb, PhiInst *a, PhiInst *b);
        bool tryInterchangeToExposeParallel(const Loop *outer, const Loop *inner,
                                            const std::vector<std::pair<int, int>> &depVectors,
                                            PhiInst *outerPhi, PhiInst *innerPhi);

        /// 在同一块内收集 Store–Load 对的近似依赖向量；verbose 侧打印沿用旧 analyzeNest 格式
        void collectDepsVerbose(const Loop *outer, const Loop *inner,
                                std::vector<std::pair<int, int>> &depVectors);

        // 保守 tiling：对 canonical 1D loop 做 strip-mining，并插入 tile loop
        bool tryTileLoop(Function *func, const Loop *loop, int tileSize = 16);
        // 保守 fusion：将两个相邻 canonical loop 融合为单循环
        bool tryFuseAdjacentLoops(Function *func, const std::vector<Loop> &loops);

    };

} // namespace optimization
