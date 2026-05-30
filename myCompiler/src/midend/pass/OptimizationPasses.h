#pragma once
#include "ControlFlowAnalysis.h"
#include "Pass.h"
#include "FunctionPass.h"
#include "ArrayPass.h"
#include "AllocaCoalescePass.h"
#include "LoopPass.h"
#include "LoopSkipContinueElimPass.h"
#include "LoopGccStyleTransformPass.h"
#include "LoopIfGuardHoistPass.h"
#include "LoopInductionStrengthReductionPass.h"
#include "LoopInterchangePass.h"
#include "LoopFusionPass.h"
#include "LoopTilingPass.h"
#include "SymmetricNegateUpperTrianglePass.h"
#include "TransposePairLoadRewritePass.h"
#include "CondGuardedAccumulatePass.h"
#include "LoopNestInteriorSplitPass.h"
#include "BasicBlockPass.h"
#include "GepPass.h"
#include "DCEPass.h"
#include "CSEPass.h"
#include "StrengthReductionPass.h"
#include "ConstantFoldingPass.h"
#include "PhiEliminationPass.h"
#include "IfConversionPass.h"
#include "InstructionReorderPass.h"
#include "InstructionCombinePass.h"
#include "MemoizationPass.h"
#include "MemoizationV2Pass.h"
#include "SRFixedPass.h"
#include "GlobalScalarPromotionPass.h"
#include "NormalizationPass.h"
#include "MultiplyPass.h"
#include "BitwiseLoopFusionPass.h"
#include <vector>
#include <string>
#include <memory>

namespace optimization
{
    // Pass管理器
    class PassManager
    {
    private:
        vector<std::unique_ptr<Pass>> passes;
        // 是否启用详细输出
        bool verbose;
        // 增加passmanager本身的调试输出
        std::stringstream debugInfo;

    public:
        PassManager(bool verbose = false) : verbose(verbose) {}
        void addPass(std::unique_ptr<Pass> pass);
        bool runOnModule(Module *module);
        void setVerbose(bool v); // 设置是否启用详细输出
        // 循环信息模块
        void initializeLoops(Module *module);
        // 输出调试信息
        std::string toString() const;
    };
   
    // 优化级别枚举
    enum class OptimizationLevel
    {
        O0, // 无优化
        O1, // 基本优化
        O2, // 更多优化

        // 以下是调试内容
        O15,
        O16,
        O17
    };

    // 创建优化Pass管道的工厂函数
    std::unique_ptr<PassManager> createOptimizationPipeline(OptimizationLevel level, bool verbose = false);

}