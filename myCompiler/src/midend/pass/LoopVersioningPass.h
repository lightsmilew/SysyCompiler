#pragma once
#include "Pass.h"

namespace optimization
{
    /// 循环版本化：对含循环不变除数的 nest，按除数特化克隆一条快路径，
    /// 便于后续常数除法强度削弱（如魔数法）。
    class LoopVersioningPass : public Pass
    {
    public:
        LoopVersioningPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopVersioning"; }
    };
}
