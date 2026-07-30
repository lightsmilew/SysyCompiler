#pragma once
#include "Pass.h"

namespace optimization
{
    /// 将「声明为 D³、实际只用 [0,N)³」的全局数组访问改成 N-pitch 扁平寻址。
    /// 从首次写入起按 idx = ((i*N)+j)*N+k 紧排，不改 .bss 大小（仍按 D³ 分配，只用前 N³）。
    class RuntimePitchCompactPass : public Pass
    {
    public:
        RuntimePitchCompactPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "RuntimePitchCompact"; }
    };
}
