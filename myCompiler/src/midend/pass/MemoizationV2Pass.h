#pragma once
#include "Pass.h"

namespace optimization
{
    // Function-entry memoization (V2): unified cache table with argument verification.
    class MemoizationV2Pass : public Pass
    {
    public:
        MemoizationV2Pass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "MemoizationV2"; }

    private:
        static constexpr int MIN_RECURSIVE_CALLS = 2;
        static constexpr int CACHE_SIZE = 8192; // 2^13 slots

        struct DirectIndex2ArgPlan
        {
            bool useDirectIndex = false;
            int stride = 0; // slot = arg0 * stride + arg1
        };

        bool analyzeFunctionForMemoization(Function *func);
        DirectIndex2ArgPlan analyzeDirectIndex2Arg(Function *func) const;
        void addMemoizationToFunction(Function *func);
    };
}
