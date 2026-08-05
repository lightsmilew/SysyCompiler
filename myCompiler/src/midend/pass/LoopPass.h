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
        struct PowDivPattern
        {
            int radix = 0;        // 基（2 的幂），如 16
            int posShiftLog2 = 0; // log2(radix)
            int radixMask = 0;    // radix - 1
            int maxPos = 0;       // 32 / posShiftLog2，超出则结果为 0
        };

        PowDivLoopReductionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "PowDivLoopReduction"; }

    private:
        // 本轮已成功改写的 callee → 模式（供调用点内联替换，因改写后原 sdiv 循环已消失）
        unordered_map<Function *, PowDivPattern> reducedCallees;

        bool rewriteDivLoopCallee(Function *func);
        bool replaceDivLoopCalls(Function *func);
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
