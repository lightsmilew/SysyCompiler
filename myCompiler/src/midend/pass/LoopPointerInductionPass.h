#pragma once
#include "Pass.h"

namespace optimization
{
    /// 将循环归纳变量参与的 GEP 提升为指针 phi + latch 上 addd 字节步进。
    /// 须在 PhiElimination 之前运行（此时 header phi / latch 回边仍完整，便于分析）。
    class LoopPointerInductionPass : public Pass
    {
    public:
        LoopPointerInductionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopPointerInduction"; }

    private:
        struct InductionVarInfo
        {
            Value *iv = nullptr;
            Value *init = nullptr;
            int64_t step = 1;
            BasicBlock *preheader = nullptr;
            BasicBlock *latch = nullptr;
            PhiInst *phi = nullptr;
            BinaryOperator *inc = nullptr;
        };

        struct GepPromoKey
        {
            Value *base = nullptr; // 归一化后的根指针（剥掉本环不变的 GEP 链）
            int varyPos = -1;
            // 先存基址链上的不变下标，再存本 GEP 的下标（变化维跳过）
            vector<pair<bool, int64_t>> constSlots;
            vector<Value *> varSlots;

            bool operator==(const GepPromoKey &o) const
            {
                return base == o.base && varyPos == o.varyPos && constSlots == o.constSlots &&
                       varSlots == o.varSlots;
            }
        };

        struct GepPromoKeyHash
        {
            size_t operator()(const GepPromoKey &k) const
            {
                size_t h = std::hash<Value *>()(k.base) ^ (std::hash<int>()(k.varyPos) << 1);
                for (size_t i = 0; i < k.constSlots.size(); ++i)
                {
                    h = h * 131u + (k.constSlots[i].first ? 1u : 0u);
                    h = h * 131u + static_cast<size_t>(k.constSlots[i].second);
                    h = h * 131u + std::hash<Value *>()(k.varSlots[i]);
                }
                return h;
            }
        };

        static void appendIndexToKey(GepPromoKey &key, Value *idx);
        /// 剥掉对本环不变的 GEP 链，根指针写入 key.base，链上下标 append 到 key。
        bool flattenInvariantBase(Value *base, const Loop &loop, GepPromoKey &key) const;

        static Value *stripCopy(Value *v);
        static bool sameLoopValue(Value *a, Value *b);
        bool isLoopInvariant(Value *val, const Loop &loop) const;
        bool findBasicIV(const Loop &loop, InductionVarInfo &info) const;
        BinaryOperator *findIVIncrement(BasicBlock *latch, Value *iv, int64_t &step) const;
        static int getElemSizeBytes(Type *ty);
        static int64_t strideBytesForVaryingIndex(GetElementPtrInst *gep, int varyPos);
        /// 识别 index = iv + constOffset（含纯 iv）；其它仿射拒绝。
        bool matchIVIndex(Value *index, const InductionVarInfo &iv, int64_t &constOffset) const;
        /// 将循环不变的地址表达式（含体内冗余重算的不变 GEP 链）物化到 preheader。
        Value *materializeInvariantInPreheader(Value *v, BasicBlock *preheader, const Loop &loop,
                                               const string &namePrefix);
        bool tryPromoteGep(const Loop &loop, const InductionVarInfo &iv, GetElementPtrInst *gep,
                           unordered_map<GepPromoKey, PhiInst *, GepPromoKeyHash> &cache);
        BasicBlock *findInstructionBlock(const Loop &loop, Instruction *inst) const;
        static void eraseInstruction(BasicBlock *bb, Instruction *inst, vector<Value *> &needToDelete);
    };
}
