#pragma once
#include "Pass.h"

namespace optimization
{
    // 外层循环线性迭代折叠：
    // 1) 迭代不变：每轮效果相同、结果不随 trip 变化 → 外层 trip 压成 1
    // 2) 线性累加器：acc' = acc + beta（beta 不依赖 acc）→ 单轮 + 出口乘以 tripCount
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

        // countUp: i=0; i<N; i++   / countDown: n=N; n>0; n--
        struct CountableLoopInfo
        {
            ICmpInst *cmp = nullptr;
            Value *iv = nullptr;
            Value *bound = nullptr;
            int constTripCount = -1;
            bool countDown = false;
            Instruction *initInst = nullptr; // countDown: copy/const that sets iv to N outside loop
        };

        bool getCountableOuterLoopInfo(const Loop &loop, CountableLoopInfo &info) const;
        bool isOuterIvUnusedInBody(const Loop &outer, Value *iv) const;
        bool allLoopCarriedValuesIterationInvariant(const Loop &outer, Value *iv) const;
        bool allLoopCarriedValuesOverwrittenOrInvariant(const Loop &outer, Value *iv) const;
        bool provePerElementFirstStoreFresh(const Loop &outer) const;
        bool proveNoReadWriteGlobalLoadBeforeFirstStore(const Loop &outer) const;
        // 允许标量 RW 全局在首次 store 前 load（如 huffman 的 buf），数组仍须 store-before-load
        bool proveEarlyLoadsOnlyScalarRWGlobals(const Loop &outer) const;
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
        bool tryFoldIterationInvariantOuterLoop(Function *func,
                                                const Loop &outer,
                                                const CountableLoopInfo &info);
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
