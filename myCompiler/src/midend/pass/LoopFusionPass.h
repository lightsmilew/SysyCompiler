#pragma once
#include "Pass.h"

namespace optimization
{

    /// 保守相邻循环融合：trip 一致、glue 路径、IO 互斥分区等条件下合并 canonical while 循环。
    class LoopFusionPass : public Pass
    {
    public:
        LoopFusionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopFusion"; }

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

        enum class FusionLinkKind
        {
            DirectAdjacent,      // 共享边界或 phi 相连的相邻循环
            GlueToSecondHeader   // 顺序兄弟内层（经外层 glue 块连接）
        };

        struct SequentialSiblingGlueInfo
        {
            bool active{false};
            CanonicalLoopShape outerFirst{};
            CanonicalLoopShape outerSecond{};
            const Loop *outerFirstLoop{nullptr};
            const Loop *outerSecondLoop{nullptr};
        };

        bool matchCanonicalLoopShape(const Loop *loop, CanonicalLoopShape &out) const;
        static bool matchFusionPartnerLoopShape(const Loop *loop, CanonicalLoopShape &out);
        static bool isWrapperLoopToInnerHeader(const CanonicalLoopShape &shape);
        static Value *headerInductionValue(const CanonicalLoopShape &shape);
        static Value *loopEntryInitValue(const CanonicalLoopShape &shape);
        static void stripSharedBoundarySecondInit(BasicBlock *junctionExit,
                                                  const CanonicalLoopShape &second);
        static void linkSharedBoundaryHalfBound(BasicBlock *entry, BasicBlock *sharedExit,
                                                const Loop &secondLoop,
                                                std::unordered_map<Value *, Value *> &valueMap);
        static void stripSkippedOuterLoopEntryInit(BasicBlock *junctionExit,
                                                   const CanonicalLoopShape &skippedOuter);
        static bool spliceExitInitsBeforeBranch(BasicBlock *dest, BasicBlock *source);
        static void retargetSkippedOuterPhiInits(BasicBlock *junctionExit, BasicBlock *skippedOuterExit,
                                                 BasicBlock *afterSecondOuter);
        static bool fusionInductionDimensionsCompatible(
            const Loop &firstLoop, const Loop &secondLoop, const CanonicalLoopShape &first,
            const CanonicalLoopShape &second, FusionLinkKind linkKind,
            const SequentialSiblingGlueInfo *seqGlue = nullptr);
        static bool ioInterleaveFusionSafe(const Loop &firstLoop, const Loop &secondLoop,
                                         const CanonicalLoopShape &first,
                                         const CanonicalLoopShape &second);
        static bool partitionHalfFusionSafe(const Loop &firstLoop, const Loop &secondLoop,
                                            const CanonicalLoopShape &first,
                                            const CanonicalLoopShape &second);
        static bool validateFuseLoopPair(const Loop &firstLoop, const Loop &secondLoop,
                                         const CanonicalLoopShape &first, const CanonicalLoopShape &second,
                                         FusionLinkKind linkKind, std::string &rejectReason,
                                         const std::vector<Loop> &allLoops,
                                         const SequentialSiblingGlueInfo *seqGlue = nullptr);

        bool findSequentialInnerGluePath(const std::vector<Loop> &allLoops, const Loop &firstInnerLoop,
                                         const Loop &secondInnerLoop, const CanonicalLoopShape &first,
                                         const CanonicalLoopShape &second,
                                         SequentialSiblingGlueInfo &seqGlue,
                                         std::vector<BasicBlock *> &glueBlocks, BasicBlock *&glueExitFrom,
                                         BasicBlock *&glueExitOldSucc) const;

        bool attemptFuseLoopPair(const Loop &firstLoop, const Loop &secondLoop, CanonicalLoopShape &first,
                                 CanonicalLoopShape &second, FusionLinkKind linkKind,
                                 const std::vector<BasicBlock *> &glueBlocks, BasicBlock *glueExitFrom,
                                 BasicBlock *glueExitOldSucc, std::string &rejectReason,
                                 const std::vector<Loop> &allLoops,
                                 const SequentialSiblingGlueInfo *seqGlue = nullptr);

        bool tryFuseAdjacentLoops(Function *func, const std::vector<Loop> &loops);

        void recordFusion(const CanonicalLoopShape &first, const CanonicalLoopShape &second,
                          FusionLinkKind linkKind, bool sharedBoundaryAdjacent, bool sequentialInnerGlue,
                          const std::vector<BasicBlock *> &glueBlocks, bool glueWrapperOuter = false);
    };

} // namespace optimization
