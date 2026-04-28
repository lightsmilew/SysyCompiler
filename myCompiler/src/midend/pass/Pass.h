#pragma once
#include "../irbuild/IRDataStructure.h"
#include "ControlFlowAnalysis.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <sstream>
#include <iostream>
#include <string>
#include <memory>
#include <set>
namespace optimization
{
    // 优化Pass的基类
    class Pass
    {
    public:
        bool verbose;
        vector<Value *> needToDelete; // 存储需要删除的值
        std::stringstream debugInfo;  // 用于调试输出
        Pass(bool verbose = false) : verbose(verbose) {}
        virtual ~Pass() = default;
        virtual bool runOnFunction(Function *func) = 0;
        virtual std::string getName() const = 0;
        std::string toString() const { return debugInfo.str(); } // 返回调试信息;

    protected:
        // 删除 CFG 边 predBlock -> succBlock 后，去掉 succBlock 上 Phi 中来自 predBlock 的 incoming
        void removePhiIncomingFromPredecessor(BasicBlock *succBlock, BasicBlock *predBlock);
    };

    inline void Pass::removePhiIncomingFromPredecessor(BasicBlock *succBlock, BasicBlock *predBlock)
    {
        if (!succBlock || !predBlock)
            return;
        auto &SuccInsts = succBlock->getInstructions();
        for (auto pit = SuccInsts.begin(); pit != SuccInsts.end();)
        {
            auto *phi = dynamic_cast<PhiInst *>(pit->get());
            if (!phi)
            {
                ++pit;
                continue;
            }
            const unsigned n = phi->getNumIncomingValues();
            unsigned idx = n;
            for (unsigned i = 0; i < n; ++i)
            {
                if (phi->getIncomingBlock(i) == predBlock)
                {
                    idx = i;
                    break;
                }
            }
            if (idx == n)
            {
                ++pit;
                continue;
            }
            if (n > 1)
            {
                phi->removeIncoming(idx);
                ++pit;
            }
            else
            {
                Value *v = phi->getIncomingValue(0);
                phi->replaceAllUsesWith(v);
                phi->removeThisFromOperands();
                needToDelete.push_back(pit->release());
                pit = SuccInsts.erase(pit);
            }
        }
    }
}