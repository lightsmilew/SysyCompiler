#pragma once
#include "Pass.h"

namespace optimization
{
    /// 循环归纳变量强度削弱：将 iv*coeff、base+iv*coeff 转为递推 phi（init, init+step, ...）
    class LoopInductionStrengthReductionPass : public Pass
    {
    public:
        LoopInductionStrengthReductionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopInductionStrengthReduction"; }

    private:
        struct InductionVarInfo
        {
            Value *iv = nullptr;
            Value *init = nullptr;
            int64_t step = 1;
            BasicBlock *preheader = nullptr;
            BasicBlock *latch = nullptr;
            BinaryOperator *inc = nullptr;
            PhiInst *phi = nullptr;
        };

        static Value *stripCopy(Value *v);
        static bool sameIV(Value *a, Value *b);
        bool isLoopInvariant(Value *val, const Loop &loop) const;
        bool isInCurrentLoopBodyOnly(const Loop &loop, BasicBlock *bb, Function *func) const;
        bool findBasicIV(const Loop &loop, InductionVarInfo &info) const;
        BinaryOperator *findIVIncrement(BasicBlock *latch, Value *iv, int64_t &step) const;
        bool feedsIVUpdate(Value *val, Value *iv, BasicBlock *latch) const;
        bool tryReduceMulIV(Function *func, const Loop &loop, const InductionVarInfo &iv);
        bool tryReduceAffineAddIV(Function *func, const Loop &loop, const InductionVarInfo &iv);
        Value *materializeAffineInit(BasicBlock *preheader, Value *ivInit, Value *base, Value *coeff,
                                     const string &namePrefix);
        Value *materializeAffineStep(BasicBlock *bb, Value *coeff, int64_t ivStep,
                                       const string &namePrefix);
        PhiInst *insertAffinePhi(const Loop &loop, const InductionVarInfo &iv, Value *initVal,
                                 Value *stepVal, const string &name);
    };
}
