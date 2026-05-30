#pragma once
#include "Pass.h"

namespace optimization
{
    class MultiplyPass : public Pass
    {
    public:
        MultiplyPass(bool verbose = false) : Pass(verbose) {}

        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "Multiply"; }
    };
}
