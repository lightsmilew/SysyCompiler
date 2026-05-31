#pragma once
#include "Pass.h"
namespace optimization
{
    // 8.展开getelementptr
    class GEPExpansionPass : public Pass
    {
    public:
        GEPExpansionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "GEPExpansion"; }
    };
    // 11.替换无用的gep指令为bitcast
    class GEPToBitCastPass : public Pass
    {
    public:
        GEPToBitCastPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "GEPToBitCast"; }
    };

    // 12. 合并同基址、偏移呈递推关系的 GEP 链为 1 个锚点 GEP + 64 位字节偏移加法；
    //     单条常量索引一维 GEP 直接折叠为 addd(base, idx*elemSize)
    class GEPChainFoldPass : public Pass
    {
    public:
        GEPChainFoldPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "GEPChainFold"; }
    };

}