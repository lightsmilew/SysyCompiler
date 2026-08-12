#pragma once
#include "Pass.h"
#include <unordered_set>
#include <vector>

namespace optimization
{
    /// 在 LoopPointerInduction 之后：若整数归纳变量仅用于循环控制（icmp / 自增），
    /// 则改写为用对应的指针 phi 与终点指针比较，并删除原整数 IV。
    /// 展开环：先把余数入口的 gep(base, iv) / sub(n,iv) 改写成 live-out 指针，再删 IV。
    /// 须在 PhiElimination 之前运行。
    class LoopPointerControlIVPass : public Pass
    {
    public:
        LoopPointerControlIVPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopPointerControlIV"; }

    private:
        struct InductionVarInfo
        {
            Value *iv = nullptr;
            Value *init = nullptr;
            Value *bound = nullptr;
            int64_t step = 1;
            BasicBlock *header = nullptr;
            BasicBlock *preheader = nullptr;
            BasicBlock *latch = nullptr;
            PhiInst *phi = nullptr;
            BinaryOperator *inc = nullptr;
            ICmpInst *cmp = nullptr;
        };

        struct PtrIVInfo
        {
            PhiInst *ptrPhi = nullptr;
            Value *ptrInit = nullptr;
            BinaryOperator *ptrNext = nullptr;
            int64_t byteStep = 0;
            int64_t byteStride = 0;
        };

        static Value *stripCopy(Value *v);
        static bool sameLoopValue(Value *a, Value *b);
        static bool sameAddress(Value *a, Value *b);
        bool isLoopInvariant(Value *val, const Loop &loop) const;
        bool findBasicIV(const Loop &loop, InductionVarInfo &info) const;
        BinaryOperator *findIVIncrement(BasicBlock *latch, Value *iv, int64_t &step) const;
        void collectIVFamily(const InductionVarInfo &iv, std::unordered_set<Value *> &family) const;
        bool ivOnlyUsedForControl(const InductionVarInfo &iv) const;
        bool findAllPointerIVs(const Loop &loop, const InductionVarInfo &iv,
                               std::vector<PtrIVInfo> &out) const;
        const PtrIVInfo *matchPtrIVForGep(GetElementPtrInst *gep, const InductionVarInfo &iv,
                                          const std::vector<PtrIVInfo> &ptrs) const;
        bool rewriteRemainderExitUses(const Loop &loop, const InductionVarInfo &iv,
                                      const std::vector<PtrIVInfo> &ptrs);
        bool eraseDeadIVAddChains(const Loop &loop, const InductionVarInfo &iv);
        Value *materializePtrEnd(const InductionVarInfo &iv, const PtrIVInfo &ptr,
                                 const string &namePrefix);
        Value *ensureAvailableInPreheader(Value *val, BasicBlock *preheader, BasicBlock *header,
                                          const string &name);
        static void eraseInstruction(BasicBlock *bb, Instruction *inst,
                                     vector<Value *> &needToDelete);
        BasicBlock *findInstructionBlock(Function *func, Instruction *inst) const;
        bool tryRewriteLoop(Loop &loop);
    };
}
