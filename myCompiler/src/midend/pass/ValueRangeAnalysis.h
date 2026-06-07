#pragma once
#include "../irbuild/IRDataStructure.h"
#include <cstdint>
#include <string>
#include <unordered_set>

namespace optimization
{
    struct RangeInfo
    {
        bool hasLower = false;
        bool hasUpper = false;
        int64_t lower = 0;
        int64_t upper = 0; // inclusive
    };

    class ValueRangeAnalysis
    {
    public:
        static RangeInfo analyze(Value *v, Function *func, int modHint = 0, BasicBlock *useBB = nullptr);

        static bool proveNonNegative(Value *v, Function *func, BasicBlock *useBB = nullptr,
                                     int modHint = 0);

        static bool tryProveModuloAccumulateAddRange(Value *lhs, int mod, Function *func,
                                                     BasicBlock *useBB, RangeInfo &out);
    };
}
