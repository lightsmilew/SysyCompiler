#pragma once
#include "Pass.h"

namespace optimization
{
    // 固定/常量 trip 的外层循环：若每轮对累加器的作用可证明为对 N 线性的仿射映射
    //   acc' = acc + beta（beta 不依赖 acc），则压成单轮并在出口乘以 tripCount 补偿。
    // beta=0 的恒等情形另由纯拷贝折叠处理（删内层 copy、外层 trip→1）。
    // acc += acc 等 acc 依赖自身的更新属于指数增长，不在此 pass 处理。
    class LoopLinearIterationFoldPass : public Pass
    {
    public:
        LoopLinearIterationFoldPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopLinearIterationFold"; }

    private:
        struct LinearIterationMap
        {
            bool valid = false;
            bool weakIncrement = false;
            Value *addend = nullptr;
        };

        bool getFixedTripCountLoopInfo(const Loop &loop,
                                       ICmpInst *&cmp,
                                       ConstantInt *&boundConst,
                                       int &tripCount) const;
        bool getCountableOuterLoopInfo(const Loop &loop,
                                       ICmpInst *&cmp,
                                       Value *&iv,
                                       Value *&bound,
                                       int &constTripCount) const;
        bool isPureCopyLoop(const Loop &loop, Value *&srcArray, Value *&dstArray) const;
        bool findLoopAccumulator(const Loop &outer, Value *iv, Value *&acc) const;
        bool proveLinearIterationMap(const Loop &outer, Value *acc, Value *iv, LinearIterationMap &map);
        bool isLinearFoldableOuterBody(const Loop &outer,
                                       Value *iv,
                                       Value *acc,
                                       const std::vector<Loop> &allLoops) const;
        Instruction *buildLinearCompensation(Value *acc, Value *tripScale, const LinearIterationMap &map) const;
        void replaceValueInFunction(Function *func,
                                    Value *oldValue,
                                    Value *newValue,
                                    const std::set<BasicBlock *> &skipBlocks,
                                    const std::set<Instruction *> &skipInsts = {}) const;
        void redirectAndRemoveLoop(Function *func, const Loop &loop);
        bool tryFoldIdentityCopyNest(Function *func,
                                     const Loop &outer,
                                     ICmpInst *cmp,
                                     int tripCount,
                                     const std::vector<Loop> &loops);
        bool tryFoldLinearAccumulator(Function *func,
                                      const Loop &outer,
                                      ICmpInst *cmp,
                                      Value *iv,
                                      Value *bound,
                                      Value *acc,
                                      Value *tripScale,
                                      const LinearIterationMap &map);
    };
}
