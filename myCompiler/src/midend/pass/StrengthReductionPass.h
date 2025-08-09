#pragma once
#include "Pass.h"
namespace optimization
{
    // 10.强度削弱
    class StrengthReductionPass : public Pass
    {
    public:
        StrengthReductionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "StrengthReduction"; }
    private:
       std::pair<int64_t,int>compute_magic(int32_t d);
    };
}