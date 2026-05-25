#pragma once
#include "Pass.h"

namespace optimization
{
    struct DirectIndex2ArgPlan;

    // Function-entry memoization (V2): unified cache table with argument verification.
    class MemoizationV2Pass : public Pass
    {
    public:
        static constexpr int CACHE_SIZE = 4096; // 2^12 slots

        MemoizationV2Pass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "MemoizationV2"; }

    private:
        static constexpr int MIN_RECURSIVE_CALLS = 2;

        bool analyzeFunctionForMemoization(Function *func);
        DirectIndex2ArgPlan analyzeDirectIndex2Arg(Function *func);
        void addMemoizationToFunction(Function *func);
    };
}
