#include "OptimizationPasses.h"
using namespace std;
using namespace optimization;
// ========== PassManager 实现 ==========
void PassManager::addPass(std::unique_ptr<Pass> pass)
{
    passes.push_back(std::move(pass));
}

bool PassManager::runOnModule(Module *module)
{
    bool changed = false;
    // 先对每个 pass，依次作用于所有函数
    for (auto &pass : passes)
    {
        for (auto &func : module->Functions)
        {
            if (!func->isLibraryFunction())
            {
                changed |= pass->runOnFunction(func.get());
            }
        }
        // 先不删除用于调试
        // 如果是函数内联pass，则在内联后删除内联的函数
        if (dynamic_cast<FunctionInliningPass *>(pass.get()))
        {
            module->Functions.erase(
                std::remove_if(
                    module->Functions.begin(),
                    module->Functions.end(),
                    [](const auto &func)
                    { return func->isDeletedFunction(); }),
                module->Functions.end());
        }
    }
    initializeLoops(module);
    return changed;
}
void PassManager::setVerbose(bool v)
{
    verbose = v;
    for (auto &pass : passes)
    {
        pass->verbose = v;
    }
}
void PassManager::initializeLoops(Module *module)
{
    for (auto &func : module->Functions)
    {
        if (func->isLibraryFunction())
            continue; // 跳过库函数
        func->setLoops(ControlFlowAnalysis::findLoops(func.get()));
        if (verbose)
        {
            debugInfo << "Function: " << func->getName() << "\n";
            for (const auto &loop : func->getLoops())
            {
                debugInfo << "  Loop Header: " << loop.header->getName() << "\n";
                debugInfo << "  Blocks: ";
                for (const auto &block : loop.blocks)
                {
                    debugInfo << block->getName() << " ";
                }
                debugInfo << "\n"
                          << "  Exits: ";
                for (const auto &block : loop.exits)
                {
                    debugInfo << block->getName() << " ";
                }
                debugInfo << "\n";
            }
        }
    }
}
std::string PassManager::toString() const
{
    std::stringstream ss;
    for (const auto &pass : passes)
    {
        ss << pass->getName() << ": \n"
           << pass->toString() << "\n";
    }
    if (verbose)
    {
        ss << "Debug Info:\n"
           << debugInfo.str();
    }
    return ss.str();
}
// ========== 优化管道工厂 ==========
std::unique_ptr<PassManager> optimization::createOptimizationPipeline(OptimizationLevel level, bool verbose)
{
    auto pm = std::make_unique<PassManager>(verbose);

    if (level == OptimizationLevel::O0)
    {
        //pm->addPass(std::make_unique<CFGSimplificationPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(1, verbose));
        pm->addPass(std::make_unique<RemoveRedundantStorePass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        //pm->addPass(std::make_unique<ArrayEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveOnlyWriteArrayPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveUselessWhilePass>(verbose));
        pm->addPass(std::make_unique<LoopSumReductionPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<ModLoopReductionPass>(verbose));
        pm->addPass(std::make_unique<LoopUnrollingPass>(verbose));
        pm->addPass(std::make_unique<InstructionCombinePass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<TailRecursionEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPToBitCastPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<StrengthReductionPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockReorderPass>(verbose));
    }
    else if (level == OptimizationLevel::O1)
    {
        //pm->addPass(std::make_unique<CFGSimplificationPass>(verbose));
        //pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(1, verbose));
        pm->addPass(std::make_unique<RemoveRedundantStorePass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        //pm->addPass(std::make_unique<ArrayEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveOnlyWriteArrayPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveUselessWhilePass>(verbose));
        pm->addPass(std::make_unique<LoopSumReductionPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<ModLoopReductionPass>(verbose));
        pm->addPass(std::make_unique<LoopUnrollingPass>(verbose));
        pm->addPass(std::make_unique<InstructionCombinePass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<TailRecursionEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPToBitCastPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<StrengthReductionPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockReorderPass>(verbose));
    }
    else if (level == OptimizationLevel::O2)
    {
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
    }
    // 测试优化
    else if (level == OptimizationLevel::O15)
    {
        // 先简化CFG，然后函数内联后可以暴露更多优化机会:删除数组，优化后再删除无用循环
        pm->addPass(std::make_unique<CFGSimplificationPass>(verbose));
        // 消除无用函数调用 这里还没进行函数内联和gep展开以及后面的优化，可以宽松判断
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(1, verbose));
        pm->addPass(std::make_unique<RemoveRedundantStorePass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<ArrayEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveOnlyWriteArrayPass>(verbose));
        // 消除数组消除pass后留下的gep指令，便于无用while消除
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // 删除无用的while循环后必须进行死代码消除
        pm->addPass(std::make_unique<RemoveUselessWhilePass>(verbose));
        pm->addPass(std::make_unique<LoopSumReductionPass>(verbose));
        // 合并基本块，便于后续操作
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<ModLoopReductionPass>(verbose));
        // 进行循环展开后再来一次合并基本块
        pm->addPass(std::make_unique<LoopUnrollingPass>(verbose));
        pm->addPass(std::make_unique<IfConversionPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));

        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        // 尾递归消除必须在函数内联之后
        pm->addPass(std::make_unique<TailRecursionEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPToBitCastPass>(verbose));
        // pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        //  phi指令限制了循环不变量外提，所以必须先消除phi指令
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<StrengthReductionPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockReorderPass>(verbose));
    }
    // 测试先遣版优化级别(最激进优化级别)
    else if (level == OptimizationLevel::O16)
    {
        // 先简化CFG，然后函数内联后可以暴露更多优化机会:删除数组，优化后再删除无用循环
        pm->addPass(std::make_unique<CFGSimplificationPass>(verbose));
        // 消除无用函数调用 这里还没进行函数内联和gep展开以及后面的优化，可以宽松判断
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(1, verbose));
        // 删除冗余store，如果store的值和原来load的值相同，则删除
        // 必须在函数内联之前，否则需要进行指针别名分析
        pm->addPass(std::make_unique<RemoveRedundantStorePass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<ArrayEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveOnlyWriteArrayPass>(verbose));
        // 消除数组消除pass后留下的gep指令，便于无用while消除
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // 删除无用的while循环后必须进行死代码消除
        pm->addPass(std::make_unique<RemoveUselessWhilePass>(verbose));
        pm->addPass(std::make_unique<LoopSumReductionPass>(verbose));
        // 合并基本块，便于后续操作
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<ModLoopReductionPass>(verbose));
        // 进行循环展开后再来一次合并基本块
        pm->addPass(std::make_unique<LoopUnrollingPass>(verbose));
        // 这里进行指令合并
        pm->addPass(std::make_unique<InstructionCombinePass>(verbose));
        // 消除简单ifelse
        // pm->addPass(std::make_unique<IfConversionPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        // 尾递归消除必须在函数内联之后
        pm->addPass(std::make_unique<TailRecursionEliminationPass>(verbose));
        pm->addPass(std::make_unique<GEPToBitCastPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        // phi指令限制了循环不变量外提，所以必须先消除phi指令
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<StrengthReductionPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockReorderPass>(verbose));
    }
    return pm;
}