#pragma once
#include "Pass.h"

namespace optimization
{
    // Function-entry memoization: always dense table (no hash).
    // Layout by arity (each cell = [result, valid]):
    //   1 arg: idx = a0                         (size 65536)
    //   2 arg: idx = a0 * 512 + a1               (64 x 512)
    //   3 arg: idx = a0 * (128*32) + a1 * 32 + a2 (32 x 128 x 32)
    class MemoizationV2Pass : public Pass
    {
    public:
        MemoizationV2Pass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "MemoizationV2"; }

        // Dense table extents (public so helpers in .cpp can share them).
        static constexpr int DENSE1_SIZE = 524288; // 1-arg: idx = a0
        static constexpr int DENSE2_ROWS = 64;    // 2-arg: idx = a0*COLS+a1
        static constexpr int DENSE2_COLS = 512;
        static constexpr int DENSE3_D0 = 32; // 3-arg: idx = a0*(D1*D2)+a1*D2+a2
        static constexpr int DENSE3_D1 = 128;
        static constexpr int DENSE3_D2 = 32;

    private:
        static constexpr int MIN_RECURSIVE_CALLS = 2;

        bool analyzeFunctionForMemoization(Function *func);
        void addMemoizationToFunction(Function *func);
    };
}
