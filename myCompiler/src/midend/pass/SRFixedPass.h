#pragma once
#include "Pass.h"
namespace optimization
{
    // 26.强度削弱修复版
    class SRFixedPass : public Pass
    {
    public:
        SRFixedPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "SRFixed"; }
    private:
        std::pair<int64_t,int>compute_magic(int32_t d);
    };
}