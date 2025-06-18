#include "OptimizationPasses.h"
#include <iostream>
#include <algorithm>
#include <queue>

namespace optimization
{

    // PassManager 实现
    void PassManager::addPass(std::unique_ptr<Pass> pass)
    {
        passes.push_back(std::move(pass));
    }

    bool PassManager::runOnModule(Module *module)
    {
        bool changed = false;

        for (auto &func : module->getFunctions())
        {
            if (func->isDeclaration())
                continue;

            for (auto &pass : passes)
            {
                if (verbose)
                {
                    std::cout << "Running " << pass->getName() << " on function " << func->getName() << std::endl;
                }

                bool funcChanged = pass->runOnFunction(func.get());
                changed |= funcChanged;

                if (verbose && funcChanged)
                {
                    std::cout << "  - Function modified" << std::endl;
                }
            }
        }

        return changed;
    }

    // 死代码消除Pass实现
    bool DeadCodeEliminationPass::runOnFunction(Function *func)
    {
        std::unordered_set<Instruction *> liveInsts;
        std::vector<Instruction *> allInsts;

        // 收集所有指令
        for (auto &bb : func->getBasicBlocks())
        {
            for (auto &inst : bb->getInstructions())
            {
                allInsts.push_back(inst.get());
            }
        }

        // 标记活跃指令
        markLiveInstructions(func, liveInsts);

        // 删除死代码
        bool changed = false;
        for (auto *inst : allInsts)
        {
            if (liveInsts.find(inst) == liveInsts.end())
            {
                inst->eraseFromParent();
                changed = true;
            }
        }

        return changed;
    }

    void DeadCodeEliminationPass::markLiveInstructions(Function *func, std::unordered_set<Instruction *> &liveInsts)
    {
        std::queue<Instruction *> workList;

        // 标记关键指令
        for (auto &bb : func->getBasicBlocks())
        {
            for (auto &inst : bb->getInstructions())
            {
                if (isInstructionCritical(inst.get()))
                {
                    liveInsts.insert(inst.get());
                    workList.push(inst.get());
                }
            }
        }

        // 反向传播活跃性
        while (!workList.empty())
        {
            Instruction *inst = workList.front();
            workList.pop();

            for (auto *operand : inst->getOperands())
            {
                if (auto *defInst = dynamic_cast<Instruction *>(operand))
                {
                    if (liveInsts.find(defInst) == liveInsts.end())
                    {
                        liveInsts.insert(defInst);
                        workList.push(defInst);
                    }
                }
            }
        }
    }

    bool DeadCodeEliminationPass::isInstructionCritical(Instruction *inst)
    {
        // 返回指令、存储指令、调用指令等都是关键的
        return inst->isTerminator() || inst->mayHaveSideEffects();
    }

    // 常量折叠Pass实现
    bool ConstantFoldingPass::runOnFunction(Function *func)
    {
        bool changed = false;

        for (auto &bb : func->getBasicBlocks())
        {
            for (auto it = bb->getInstructions().begin(); it != bb->getInstructions().end();)
            {
                auto &inst = *it;
                ++it;

                Value *foldedValue = nullptr;

                if (auto *binOp = dynamic_cast<BinaryOperator *>(inst.get()))
                {
                    foldedValue = foldBinaryOperation(binOp);
                }
                else if (auto *cmpInst = dynamic_cast<CompareInst *>(inst.get()))
                {
                    foldedValue = foldComparison(cmpInst);
                }

                if (foldedValue)
                {
                    inst->replaceAllUsesWith(foldedValue);
                    inst->eraseFromParent();
                    changed = true;
                }
            }
        }

        return changed;
    }

    Value *ConstantFoldingPass::foldBinaryOperation(BinaryOperator *binOp)
    {
        Value *lhs = binOp->getOperand(0);
        Value *rhs = binOp->getOperand(1);

        if (!isConstant(lhs) || !isConstant(rhs))
        {
            return nullptr;
        }

        int lhsVal = getConstantValue(lhs);
        int rhsVal = getConstantValue(rhs);
        int result;

        switch (binOp->getOpcode())
        {
        case BinaryOperator::Add:
            result = lhsVal + rhsVal;
            break;
        case BinaryOperator::Sub:
            result = lhsVal - rhsVal;
            break;
        case BinaryOperator::Mul:
            result = lhsVal * rhsVal;
            break;
        case BinaryOperator::Div:
            if (rhsVal == 0)
                return nullptr;
            result = lhsVal / rhsVal;
            break;
        case BinaryOperator::Mod:
            if (rhsVal == 0)
                return nullptr;
            result = lhsVal % rhsVal;
            break;
        default:
            return nullptr;
        }

        return ConstantInt::get(result);
    }

    Value *ConstantFoldingPass::foldComparison(CompareInst *cmpInst)
    {
        Value *lhs = cmpInst->getOperand(0);
        Value *rhs = cmpInst->getOperand(1);

        if (!isConstant(lhs) || !isConstant(rhs))
        {
            return nullptr;
        }

        int lhsVal = getConstantValue(lhs);
        int rhsVal = getConstantValue(rhs);
        bool result;

        switch (cmpInst->getPredicate())
        {
        case CompareInst::EQ:
            result = lhsVal == rhsVal;
            break;
        case CompareInst::NE:
            result = lhsVal != rhsVal;
            break;
        case CompareInst::LT:
            result = lhsVal < rhsVal;
            break;
        case CompareInst::LE:
            result = lhsVal <= rhsVal;
            break;
        case CompareInst::GT:
            result = lhsVal > rhsVal;
            break;
        case CompareInst::GE:
            result = lhsVal >= rhsVal;
            break;
        default:
            return nullptr;
        }

        return ConstantInt::get(result ? 1 : 0);
    }

    bool ConstantFoldingPass::isConstant(Value *val)
    {
        return dynamic_cast<ConstantInt *>(val) != nullptr;
    }

    int ConstantFoldingPass::getConstantValue(Value *val)
    {
        if (auto *constInt = dynamic_cast<ConstantInt *>(val))
        {
            return constInt->getValue();
        }
        return 0;
    }

    // 公共子表达式消除Pass实现
    bool CommonSubexpressionEliminationPass::runOnFunction(Function *func)
    {
        bool changed = false;
        exprMap.clear();

        for (auto &bb : func->getBasicBlocks())
        {
            for (auto it = bb->getInstructions().begin(); it != bb->getInstructions().end();)
            {
                auto &inst = *it;
                ++it;

                if (!canBeCommonSubexpression(inst.get()))
                {
                    continue;
                }

                auto exprKey = getExpressionKey(inst.get());
                auto mapIt = exprMap.find(exprKey);

                if (mapIt != exprMap.end())
                {
                    // 找到公共子表达式，替换当前指令
                    inst->replaceAllUsesWith(mapIt->second);
                    inst->eraseFromParent();
                    changed = true;
                }
                else
                {
                    // 记录新的表达式
                    exprMap[exprKey] = inst.get();
                }
            }
        }

        return changed;
    }

    std::pair<std::string, std::vector<Value *>> CommonSubexpressionEliminationPass::getExpressionKey(Instruction *inst)
    {
        std::string opcode = inst->getOpcodeName();
        std::vector<Value *> operands;

        for (auto *operand : inst->getOperands())
        {
            operands.push_back(operand);
        }

        return {opcode, operands};
    }

    bool CommonSubexpressionEliminationPass::canBeCommonSubexpression(Instruction *inst)
    {
        // 只处理纯计算指令，不包括有副作用的指令
        return dynamic_cast<BinaryOperator *>(inst) != nullptr ||
               dynamic_cast<CompareInst *>(inst) != nullptr;
    }

    std::string CommonSubexpressionEliminationPass::ExpressionHash::operator()(
        const std::pair<std::string, std::vector<Value *>> &expr) const
    {
        std::string result = expr.first;
        for (auto *val : expr.second)
        {
            result += "_" + std::to_string(reinterpret_cast<uintptr_t>(val));
        }
        return result;
    }

    // 复制传播Pass实现
    bool CopyPropagationPass::runOnFunction(Function *func)
    {
        copyMap.clear();
        collectCopies(func);

        bool changed = false;

        for (auto &bb : func->getBasicBlocks())
        {
            for (auto &inst : bb->getInstructions())
            {
                for (int i = 0; i < inst->getNumOperands(); ++i)
                {
                    Value *operand = inst->getOperand(i);
                    Value *replacement = followCopyChain(operand);
                    if (replacement != operand)
                    {
                        inst->setOperand(i, replacement);
                        changed = true;
                    }
                }
            }
        }

        return changed;
    }

    void CopyPropagationPass::collectCopies(Function *func)
    {
        for (auto &bb : func->getBasicBlocks())
        {
            for (auto &inst : bb->getInstructions())
            {
                // 简单的复制指令检测（这里需要根据您的IR结构调整）
                if (inst->isCopy())
                {
                    copyMap[inst.get()] = inst->getOperand(0);
                }
            }
        }
    }

    Value *CopyPropagationPass::followCopyChain(Value *val)
    {
        Value *current = val;
        std::unordered_set<Value *> visited;

        while (copyMap.find(current) != copyMap.end() && visited.find(current) == visited.end())
        {
            visited.insert(current);
            current = copyMap[current];
        }

        return current;
    }

    // 基本块合并Pass实现
    bool BasicBlockMergePass::runOnFunction(Function *func)
    {
        bool changed = false;

        auto &blocks = func->getBasicBlocks();
        for (auto it = blocks.begin(); it != blocks.end();)
        {
            auto &bb = *it;
            ++it;

            if (bb->getSuccessors().size() == 1)
            {
                BasicBlock *successor = bb->getSuccessors()[0];

                if (canMergeBlocks(bb.get(), successor))
                {
                    mergeBlocks(bb.get(), successor);
                    changed = true;
                }
            }
        }

        return changed;
    }

    bool BasicBlockMergePass::canMergeBlocks(BasicBlock *bb1, BasicBlock *bb2)
    {
        // 检查bb2是否只有bb1一个前驱
        return bb2->getPredecessors().size() == 1 &&
               bb2->getPredecessors()[0] == bb1;
    }

    void BasicBlockMergePass::mergeBlocks(BasicBlock *bb1, BasicBlock *bb2)
    {
        // 移除bb1的终结指令
        bb1->getTerminator()->eraseFromParent();

        // 将bb2的所有指令移动到bb1
        while (!bb2->getInstructions().empty())
        {
            auto &inst = bb2->getInstructions().front();
            inst->removeFromParent();
            bb1->getInstructions().push_back(std::move(inst));
        }

        // 更新后继关系
        bb1->replaceSuccessorWith(bb2, bb2->getSuccessors());

        // 删除bb2
        bb2->eraseFromParent();
    }

    // 创建优化Pass管道
    std::unique_ptr<PassManager> createOptimizationPipeline(OptimizationLevel level, bool verbose)
    {
        auto manager = std::make_unique<PassManager>(verbose);

        switch (level)
        {
        case OptimizationLevel::O0:
            // 无优化
            break;

        case OptimizationLevel::O1:
            // 基本优化
            manager->addPass(std::make_unique<ConstantFoldingPass>());
            manager->addPass(std::make_unique<CopyPropagationPass>());
            manager->addPass(std::make_unique<DeadCodeEliminationPass>());
            manager->addPass(std::make_unique<BasicBlockMergePass>());
            break;

        case OptimizationLevel::O2:
            // 更多优化
            manager->addPass(std::make_unique<ConstantFoldingPass>());
            manager->addPass(std::make_unique<CopyPropagationPass>());
            manager->addPass(std::make_unique<CommonSubexpressionEliminationPass>());
            manager->addPass(std::make_unique<DeadCodeEliminationPass>());
            manager->addPass(std::make_unique<BasicBlockMergePass>());
            manager->addPass(std::make_unique<LoopInvariantCodeMotionPass>());

            // 再次运行基础优化来清理
            manager->addPass(std::make_unique<ConstantFoldingPass>());
            manager->addPass(std::make_unique<DeadCodeEliminationPass>());
            break;
        }

        return manager;
    }

} // namespace optimization
