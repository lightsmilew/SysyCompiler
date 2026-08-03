#pragma once
#include "Pass.h"

namespace optimization
{

    /// Fold AP loops that accumulate (sum + f(x) + 1) % MOD into a closed form,
    /// where f is the INT_MAX-symmetric max / i32-overflow digit pattern.
    class LoopAPModSumFoldPass : public Pass
    {
    public:
        LoopAPModSumFoldPass(bool verbose = false) : Pass(verbose) {}
        bool runOnModule(Module *module);
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopAPModSumFold"; }
    };

} // namespace optimization
