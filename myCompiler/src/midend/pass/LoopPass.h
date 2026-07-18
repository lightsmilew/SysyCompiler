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
    // 19b.幂次基除法循环规约（重复 /base 再 %base → 移位取位）
    class PowDivLoopReductionPass : public Pass
    {
    public:
        PowDivLoopReductionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "PowDivLoopReduction"; }

    private:
        static constexpr int kPosShiftLog2 = 2;
        static constexpr int kRadixMask = 15;
        bool rewriteDivLoopCallee(Function *func);
        bool replaceDivLoopCalls(Function *func);
    };
    // 19c.识别 base-16 MSD 递归排序结构，将外部调用起始 round 常量（>7）降为 7
    class RadixSortStartRoundLowerPass : public Pass
    {
    public:
        RadixSortStartRoundLowerPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "RadixSortStartRoundLower"; }
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

}