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
    };
}