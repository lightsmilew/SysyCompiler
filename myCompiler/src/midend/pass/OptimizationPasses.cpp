#include "OptimizationPasses.h"
using namespace std;
using namespace optimization;

namespace
{
bool functionHasRemainingCalls(Module *module, Function *target)
{
    if (!module || !target || target->isLibraryFunction())
        return false;

    const string targetName = target->getName();
    for (auto &funcPtr : module->Functions)
    {
        Function *func = funcPtr.get();
        if (!func || func->isLibraryFunction() || func == target || func->isDeletedFunction())
            continue;
        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            if (!bb)
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                Instruction *inst = instPtr.get();
                if (!inst)
                    continue;
                auto *call = dynamic_cast<CallInst *>(inst);
                if (!call)
                    continue;
                Function *callee = call->getCalledFunction();
                if (!callee)
                    continue;
                if (callee == target || callee->getName() == targetName)
                    return true;
            }
        }
    }
    return false;
}
} // namespace

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
        if (auto *helperRet = dynamic_cast<HelperReturnAnalysisPass *>(pass.get()))
        {
            changed |= helperRet->runOnModule(module);
            continue;
        }
        for (auto &func : module->Functions)
        {
            if (!func->isLibraryFunction())
            {
                changed |= pass->runOnFunction(func.get());
            }
        }
        // 先不删除用于调试
        // 如果是函数内联pass，则在内联后删除内联的函数
        if (dynamic_cast<FunctionInliningPass *>(pass.get()) ||
            dynamic_cast<RemoveUnusedGlobalAndFunctionPass *>(pass.get()))
        {
            module->Functions.erase(
                std::remove_if(
                    module->Functions.begin(),
                    module->Functions.end(),
                    [module](const auto &func)
                    {
                        return func->isDeletedFunction() &&
                               !functionHasRemainingCalls(module, func.get());
                    }),
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
            debugInfo << "Loops Found: " << func->getLoops().size() << "\n";
            int loopIdx = 0;
            for (const auto &loop : func->getLoops())
            {
                debugInfo << "Loop " << loopIdx++ << ":\n";
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
        ss << "Final Debug Info:\n"
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
        pm->addPass(std::make_unique<CFGSimplificationPass>(verbose));
        pm->addPass(std::make_unique<MemoizationV2Pass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(1, verbose));
        pm->addPass(std::make_unique<RemoveRedundantStorePass>(verbose));
        pm->addPass(std::make_unique<NormalizationPass>(verbose));
        pm->addPass(std::make_unique<GlobalScalarPromotionPass>(verbose));
        pm->addPass(std::make_unique<PowDivLoopReductionPass>(verbose));
        pm->addPass(std::make_unique<HighDigitStartClampPass>(verbose));
        pm->addPass(std::make_unique<HelperReturnAnalysisPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<ModLoopReductionPass>(verbose));
        pm->addPass(std::make_unique<AllocaCoalescePass>(verbose));
        pm->addPass(std::make_unique<ArrayEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveOnlyWriteArrayPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<LoopLinearIterationFoldPass>(verbose));
        pm->addPass(std::make_unique<ArrayCopyPropagationPass>(verbose));
        pm->addPass(std::make_unique<LoopSumReductionPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<LoopIfGuardHoistPass>(verbose));
        pm->addPass(std::make_unique<LoopNestedBoundTighteningPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<LoopInductionStrengthReductionPass>(verbose));
        pm->addPass(std::make_unique<CondGuardedAccumulatePass>(verbose));
        pm->addPass(std::make_unique<MatrixStructureAnalysisPass>(verbose));
        pm->addPass(std::make_unique<TransposedBufferLoadForwardPass>(verbose));
        pm->addPass(std::make_unique<SkewSymmetricLoopRestrictPass>(verbose));
        pm->addPass(std::make_unique<LoopInterchangePass>(verbose));
        //pm->addPass(std::make_unique<RelativeGepOffsetPass>(verbose));
        pm->addPass(std::make_unique<LoopUnrollingPass>(verbose));
        pm->addPass(std::make_unique<InPlaceCopyOriginReductionPass>(verbose));
        pm->addPass(std::make_unique<InstructionCombinePass>(verbose));
        pm->addPass(std::make_unique<ArrayStoreLoadForwardPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<ArrayStoreLoadForwardPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        pm->addPass(std::make_unique<GEPChainFoldPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<TailRecursionEliminationPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose, true));
        pm->addPass(std::make_unique<GEPToBitCastPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<SRFixedPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<RemoveRedundantStorePass>(verbose));
        pm->addPass(std::make_unique<BasicBlockReorderPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveUnusedGlobalAndFunctionPass>(verbose));
        pm->addPass(std::make_unique<LoopGccStyleTransformPass>(verbose));
    }
    else if (level == OptimizationLevel::O1)
    {
        pm->addPass(std::make_unique<CFGSimplificationPass>(verbose));
        pm->addPass(std::make_unique<MemoizationV2Pass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(1, verbose));
        pm->addPass(std::make_unique<RemoveRedundantStorePass>(verbose));
        pm->addPass(std::make_unique<NormalizationPass>(verbose));
        pm->addPass(std::make_unique<GlobalScalarPromotionPass>(verbose));
        pm->addPass(std::make_unique<PowDivLoopReductionPass>(verbose));
        pm->addPass(std::make_unique<HighDigitStartClampPass>(verbose));
        pm->addPass(std::make_unique<HelperReturnAnalysisPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<ModLoopReductionPass>(verbose));
        pm->addPass(std::make_unique<AllocaCoalescePass>(verbose));
        pm->addPass(std::make_unique<ArrayEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveOnlyWriteArrayPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<LoopLinearIterationFoldPass>(verbose));
        pm->addPass(std::make_unique<ArrayCopyPropagationPass>(verbose));
        pm->addPass(std::make_unique<LoopSumReductionPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<LoopIfGuardHoistPass>(verbose));
        pm->addPass(std::make_unique<LoopNestedBoundTighteningPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<LoopInductionStrengthReductionPass>(verbose));
        pm->addPass(std::make_unique<CondGuardedAccumulatePass>(verbose));
        pm->addPass(std::make_unique<MatrixStructureAnalysisPass>(verbose));
        pm->addPass(std::make_unique<TransposedBufferLoadForwardPass>(verbose));
        pm->addPass(std::make_unique<SkewSymmetricLoopRestrictPass>(verbose));
        pm->addPass(std::make_unique<LoopInterchangePass>(verbose));
        //pm->addPass(std::make_unique<RelativeGepOffsetPass>(verbose));
        pm->addPass(std::make_unique<LoopUnrollingPass>(verbose));
        pm->addPass(std::make_unique<InPlaceCopyOriginReductionPass>(verbose));
        pm->addPass(std::make_unique<InstructionCombinePass>(verbose));
        pm->addPass(std::make_unique<ArrayStoreLoadForwardPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<ArrayStoreLoadForwardPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        pm->addPass(std::make_unique<GEPChainFoldPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<TailRecursionEliminationPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose, true));
        pm->addPass(std::make_unique<GEPToBitCastPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        pm->addPass(std::make_unique<SRFixedPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<RemoveRedundantStorePass>(verbose));
        pm->addPass(std::make_unique<BasicBlockReorderPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveUnusedGlobalAndFunctionPass>(verbose));
        pm->addPass(std::make_unique<LoopGccStyleTransformPass>(verbose));
    }
    else if (level == OptimizationLevel::O2)
    {
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
    }
    // 测试优化
    else if (level == OptimizationLevel::O15)
    {
        // 先简化CFG，然后函数内联后可以暴露更多优化机会:删除数组，优化后再删除无用循环
        pm->addPass(std::make_unique<CFGSimplificationPass>(verbose));
        //  消除无用函数调用 这里还没进行函数内联和gep展开以及后面的优化，可以宽松判断
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(1, verbose));
        // 删除冗余store，如果store的值和原来load的值相同，则删除
        // 必须在函数内联之前，否则需要进行指针别名分析
        pm->addPass(std::make_unique<RemoveRedundantStorePass>(verbose));
        pm->addPass(std::make_unique<GlobalScalarPromotionPass>(verbose));
        // pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        // pm->addPass(std::make_unique<ModLoopReductionPass>(verbose));
        // pm->addPass(std::make_unique<ArrayEliminationPass>(verbose));
        // pm->addPass(std::make_unique<RemoveOnlyWriteArrayPass>(verbose));
        // //消除数组消除pass后留下的gep指令，便于无用while消除
        // pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // pm->addPass(std::make_unique<LoopLinearIterationFoldPass>(verbose));
        // // 删除无用的while循环后必须进行死代码消除
        // pm->addPass(std::make_unique<RemoveUselessWhilePass>(verbose));
        // pm->addPass(std::make_unique<LoopSumReductionPass>(verbose));
        // // 合并基本块，便于后续操作
        // pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        // pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        // pm->addPass(std::make_unique<LoopIfGuardHoistPass>(verbose));
        // // 进行循环展开后再来一次合并基本块
        // pm->addPass(std::make_unique<LoopNestedBoundTighteningPass>(verbose));
        // pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        // pm->addPass(std::make_unique<LoopUnrollingPass>(verbose));

        // // 这里进行指令合并
        // pm->addPass(std::make_unique<InstructionCombinePass>(verbose));
        // pm->addPass(std::make_unique<ArrayStoreLoadForwardPass>(verbose));
        // // 消除简单ifelse
        // pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        // pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        // pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        // // 尾递归消除必须在函数内联之后
        // pm->addPass(std::make_unique<TailRecursionEliminationPass>(verbose));
        // pm->addPass(std::make_unique<GEPToBitCastPass>(verbose));
        // // pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        // //  phi指令限制了循环不变量外提，所以必须先消除phi指令
        // pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        // pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        // pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        // pm->addPass(std::make_unique<StrengthReductionPass>(verbose));
        // pm->addPass(std::make_unique<BasicBlockReorderPass>(verbose));
    }
    // O16留作后端debug
    else if (level == OptimizationLevel::O16)
    {
        // 先简化CFG，然后函数内联后可以暴露更多优化机会:删除数组，优化后再删除无用循环
        pm->addPass(std::make_unique<CFGSimplificationPass>(verbose));
        pm->addPass(std::make_unique<MemoizationV2Pass>(verbose));
        // 消除无用函数调用 这里还没进行函数内联和gep展开以及后面的优化，可以宽松判断
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(1, verbose));
        // 删除冗余store，如果store的值和原来load的值相同，则删除
        // 必须在函数内联之前，否则需要进行指针别名分析
        pm->addPass(std::make_unique<RemoveRedundantStorePass>(verbose));
        // 归一化，把乘法和加法常数放到右操作数，>=转为<=, >转为<，便于后续优化
        pm->addPass(std::make_unique<NormalizationPass>(verbose));
        //pm->addPass(std::make_unique<InstructionReorderPass>(verbose));
        pm->addPass(std::make_unique<GlobalScalarPromotionPass>(verbose));
        pm->addPass(std::make_unique<PowDivLoopReductionPass>(verbose));
        pm->addPass(std::make_unique<HighDigitStartClampPass>(verbose));
        pm->addPass(std::make_unique<HelperReturnAnalysisPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // 须在 ArrayElimination 之前：modulo 消除会去掉 K 的 load，导致 kernel 嵌套结构分析失败
        pm->addPass(std::make_unique<ModLoopReductionPass>(verbose));
        pm->addPass(std::make_unique<AllocaCoalescePass>(verbose));
        pm->addPass(std::make_unique<ArrayEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveOnlyWriteArrayPass>(verbose));
        // 多面体循环变换尽量前置，避免 DCE 提前删除归纳 phi 影响循环识别与融合
        // 消除数组消除pass后留下的gep指令，便于无用while消除
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<LoopLinearIterationFoldPass>(verbose));
        pm->addPass(std::make_unique<ArrayCopyPropagationPass>(verbose));
        // 删除无用的while循环后必须进行死代码消除
        // pm->addPass(std::make_unique<RemoveUselessWhilePass>(verbose));
        pm->addPass(std::make_unique<LoopSumReductionPass>(verbose));
        // 合并基本块，便于后续操作
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        //这三个绑定
        pm->addPass(std::make_unique<LoopIfGuardHoistPass>(verbose));
        pm->addPass(std::make_unique<LoopNestedBoundTighteningPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));

        pm->addPass(std::make_unique<LoopInductionStrengthReductionPass>(verbose));
        pm->addPass(std::make_unique<CondGuardedAccumulatePass>(verbose));
        pm->addPass(std::make_unique<MatrixStructureAnalysisPass>(verbose));
        pm->addPass(std::make_unique<TransposedBufferLoadForwardPass>(verbose));
        pm->addPass(std::make_unique<SkewSymmetricLoopRestrictPass>(verbose));
        pm->addPass(std::make_unique<LoopInterchangePass>(verbose));
        pm->addPass(std::make_unique<RelativeGepOffsetPass>(verbose));
        pm->addPass(std::make_unique<LoopUnrollingPass>(verbose));
        pm->addPass(std::make_unique<InPlaceCopyOriginReductionPass>(verbose));
        pm->addPass(std::make_unique<InstructionCombinePass>(verbose));
        pm->addPass(std::make_unique<ArrayStoreLoadForwardPass>(verbose));
        // 这里基本块和死代码消除多次迭代保证完全消除和合并
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        // 再次折叠有条件跳转
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));


        // pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        // pm->addPass(std::make_unique<ArrayStoreLoadForwardPass>(verbose));
        // pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        // //gep折叠必须在加法链归约之后，否则会限制gep折叠
        // pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        // pm->addPass(std::make_unique<GEPChainFoldPass>(verbose));
        // //现在寄存器分配有点问题，折叠后必须接死代码消除，不然可能会覆盖结果
        // pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // // 尾递归消除必须在函数内联之后
        // pm->addPass(std::make_unique<TailRecursionEliminationPass>(verbose));
        // pm->addPass(std::make_unique<FunctionInliningPass>(verbose, true));
        // pm->addPass(std::make_unique<GEPToBitCastPass>(verbose));
        // pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        // // phi指令限制了循环不变量外提，所以必须先消除phi指令
        // pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        // pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        // pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        // pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        // //pm->addPass(std::make_unique<StrengthReductionPass>(verbose));
        // pm->addPass(std::make_unique<SRFixedPass>(verbose));
        // pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        // // 基本块合并/强度折叠后可能出现同块内被覆盖的 store
        // pm->addPass(std::make_unique<RemoveRedundantStorePass>(verbose));
        // pm->addPass(std::make_unique<BasicBlockReorderPass>(verbose));
        // //必须要有死代码消除，处理寄存器分配潜在问题
        // pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // pm->addPass(std::make_unique<RemoveUnusedGlobalAndFunctionPass>(verbose));
        // pm->addPass(std::make_unique<LoopGccStyleTransformPass>(verbose));
    }
    // 测试先遣版优化级别(最激进优化级别)
    else if (level == OptimizationLevel::O17)
    {
        // 先简化CFG，然后函数内联后可以暴露更多优化机会:删除数组，优化后再删除无用循环
        pm->addPass(std::make_unique<CFGSimplificationPass>(verbose));
        pm->addPass(std::make_unique<MemoizationV2Pass>(verbose));
        // 消除无用函数调用 这里还没进行函数内联和gep展开以及后面的优化，可以宽松判断
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(1, verbose));
        // 删除冗余store，如果store的值和原来load的值相同，则删除
        // 必须在函数内联之前，否则需要进行指针别名分析
        pm->addPass(std::make_unique<RemoveRedundantStorePass>(verbose));
        // 归一化，把乘法和加法常数放到右操作数，>=转为<=, >转为<，便于后续优化
        pm->addPass(std::make_unique<NormalizationPass>(verbose));
        //pm->addPass(std::make_unique<InstructionReorderPass>(verbose));
        pm->addPass(std::make_unique<GlobalScalarPromotionPass>(verbose));
        pm->addPass(std::make_unique<PowDivLoopReductionPass>(verbose));
        pm->addPass(std::make_unique<HighDigitStartClampPass>(verbose));
        pm->addPass(std::make_unique<HelperReturnAnalysisPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // 须在 ArrayElimination 之前：modulo 消除会去掉 K 的 load，导致 kernel 嵌套结构分析失败
        pm->addPass(std::make_unique<ModLoopReductionPass>(verbose));
        pm->addPass(std::make_unique<AllocaCoalescePass>(verbose));
        pm->addPass(std::make_unique<ArrayEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveOnlyWriteArrayPass>(verbose));
        // 多面体循环变换尽量前置，避免 DCE 提前删除归纳 phi 影响循环识别与融合
        // 消除数组消除pass后留下的gep指令，便于无用while消除
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<LoopLinearIterationFoldPass>(verbose));
        pm->addPass(std::make_unique<ArrayCopyPropagationPass>(verbose));
        // 删除无用的while循环后必须进行死代码消除
        // pm->addPass(std::make_unique<RemoveUselessWhilePass>(verbose));
        pm->addPass(std::make_unique<LoopSumReductionPass>(verbose));
        // 合并基本块，便于后续操作
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        //这三个绑定
        pm->addPass(std::make_unique<LoopIfGuardHoistPass>(verbose));
        pm->addPass(std::make_unique<LoopNestedBoundTighteningPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));

        pm->addPass(std::make_unique<LoopInductionStrengthReductionPass>(verbose));
        pm->addPass(std::make_unique<CondGuardedAccumulatePass>(verbose));
        pm->addPass(std::make_unique<MatrixStructureAnalysisPass>(verbose));
        pm->addPass(std::make_unique<TransposedBufferLoadForwardPass>(verbose));
        pm->addPass(std::make_unique<SkewSymmetricLoopRestrictPass>(verbose));
        pm->addPass(std::make_unique<LoopInterchangePass>(verbose));

        //pm->addPass(std::make_unique<RelativeGepOffsetPass>(verbose));

        pm->addPass(std::make_unique<LoopUnrollingPass>(verbose));
        pm->addPass(std::make_unique<InPlaceCopyOriginReductionPass>(verbose));
        pm->addPass(std::make_unique<InstructionCombinePass>(verbose));
        pm->addPass(std::make_unique<ArrayStoreLoadForwardPass>(verbose));
        // 这里基本块和死代码消除多次迭代保证完全消除和合并
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));
        // 再次折叠有条件跳转
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<BasicBlockMergePass>(verbose));

        pm->addPass(std::make_unique<GEPExpansionPass>(verbose));
        pm->addPass(std::make_unique<ArrayStoreLoadForwardPass>(verbose));
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        //gep折叠必须在加法链归约之后，否则会限制gep折叠
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        pm->addPass(std::make_unique<GEPChainFoldPass>(verbose));
        //现在寄存器分配有点问题，折叠后必须接死代码消除，不然可能会覆盖结果
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        // 尾递归消除必须在函数内联之后
        pm->addPass(std::make_unique<TailRecursionEliminationPass>(verbose));
        pm->addPass(std::make_unique<FunctionInliningPass>(verbose, true));
        pm->addPass(std::make_unique<GEPToBitCastPass>(verbose));
        pm->addPass(std::make_unique<PhiEliminationPass>(verbose));
        // phi指令限制了循环不变量外提，所以必须先消除phi指令
        pm->addPass(std::make_unique<AddChainReductionPass>(verbose));
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>(verbose));
        //pm->addPass(std::make_unique<StrengthReductionPass>(verbose));
        pm->addPass(std::make_unique<SRFixedPass>(verbose));
        pm->addPass(std::make_unique<ConstantFoldingPass>(verbose));
        // 基本块合并/强度折叠后可能出现同块内被覆盖的 store
        pm->addPass(std::make_unique<RemoveRedundantStorePass>(verbose));
        pm->addPass(std::make_unique<BasicBlockReorderPass>(verbose));
        //必须要有死代码消除，处理寄存器分配潜在问题
        pm->addPass(std::make_unique<DeadCodeEliminationPass>(verbose));
        pm->addPass(std::make_unique<RemoveUnusedGlobalAndFunctionPass>(verbose));
        pm->addPass(std::make_unique<LoopGccStyleTransformPass>(verbose));
    }
    return pm;
}