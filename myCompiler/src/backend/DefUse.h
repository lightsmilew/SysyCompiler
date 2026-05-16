#pragma once

#include "RISCVDataStructure.h"
#include <unordered_map>

namespace RISCV
{
    class DefUseAnalyzer
    {
    public:
        struct Info
        {
            std::unordered_map<std::shared_ptr<RISCVRegister>, std::vector<int>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> usePos;
            std::unordered_map<std::shared_ptr<RISCVRegister>, std::vector<int>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> defPos;
            int instrCount = 0;
        };

        // 构建并缓存基本块的 def/use 位置信息
        void buildForBasicBlock(std::shared_ptr<RISCVBasicBlock> bb);

        // 查询：从 startIdx (包含) 开始，是否存在对 reg 的使用且在遇到下一个定义之前
        bool isRegUsedBeforeRedef(std::shared_ptr<RISCVBasicBlock> bb, std::shared_ptr<RISCVRegister> reg, int startIdx) const;

    private:
        mutable std::unordered_map<RISCVBasicBlock *, Info> cache;
    };
}
