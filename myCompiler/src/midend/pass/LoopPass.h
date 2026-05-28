#pragma once
#include "Pass.h"
namespace optimization
{
    // 3. 循环不变代码外提Pass
    class LoopInvariantCodeMotionPass : public Pass
    {
    public:
        LoopInvariantCodeMotionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        string getName() const override { return "LoopInvariantCodeMotion"; }

    private:
        bool isLoopInvariant(Instruction *inst, const Loop &loop);
        bool canMoveToPreheader(Instruction *inst, const Loop &loop);
    };
    // 14.移除无用的while循环
    class RemoveUselessWhilePass : public Pass
    {
    public:
        RemoveUselessWhilePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "RemoveUselessWhile"; }
    };
    // 15.循环求和规约
    class LoopSumReductionPass : public Pass
    {
    public:
        LoopSumReductionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopSumReduction"; }
    };
    // 19.加法取模循环规约
    class ModLoopReductionPass : public Pass
    {
    public:
        ModLoopReductionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "ModLoopReduction"; }
    };
    // 21.循环展开
    class LoopUnrollingPass : public Pass
    {
    public:
        LoopUnrollingPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopUnrolling"; }

    private:
        // 用于记录循环展开次数
        size_t LoopUnrollingCount = 0;
    };

    // 折叠固定次数的外层拷贝+调用循环
    class LoopCopyCallCollapsePass : public Pass
    {
    public:
        LoopCopyCallCollapsePass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopCopyCallCollapse"; }

    private:
        bool getFixedTripCountLoopInfo(const Loop &loop,
                                       ICmpInst *&cmp,
                                       ConstantInt *&boundConst,
                                       int &tripCount) const;
        bool getRepeatOuterLoopInfo(const Loop &loop,
                                    ICmpInst *&cmp,
                                    Value *&iv,
                                    Value *&bound,
                                    int &constTripCount) const;
        bool isPureCopyLoop(const Loop &loop, Value *&srcArray, Value *&dstArray) const;
        bool isRepeatAccumulateOuterLoop(const Loop &outer,
                                         Value *iv,
                                         Value *acc,
                                         const std::vector<Loop> &allLoops);
        bool findRepeatAccumulator(const Loop &outer, Value *iv, Value *&acc) const;
        CallInst *findDominantCallInLoop(const Loop &outerLoop, const Loop &copyLoop, Value *dstArray) const;
        void replaceValueInFunction(Function *func,
                                  Value *oldValue,
                                  Value *newValue,
                                  const std::set<BasicBlock *> &skipBlocks,
                                  const std::set<Instruction *> &skipInsts = {}) const;
        void redirectAndRemoveLoop(Function *func, const Loop &loop);
        bool tryCollapseRepeatAccumulate(Function *func,
                                         const Loop &outer,
                                         ICmpInst *cmp,
                                         Value *iv,
                                         Value *bound,
                                         Value *acc,
                                         int constTripCount);
    };

}