#pragma once
#include "IRDataStructure.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>

namespace optimization
{

    // 优化Pass的基类
    class Pass
    {
    public:
        virtual ~Pass() = default;
        virtual bool runOnFunction(Function *func) = 0;
        virtual std::string getName() const = 0;
    };

    // Pass管理器
    class PassManager
    {
    private:
        std::vector<std::unique_ptr<Pass>> passes;
        bool verbose;

    public:
        PassManager(bool verbose = false) : verbose(verbose) {}

        void addPass(std::unique_ptr<Pass> pass);
        bool runOnModule(Module *module);
        void setVerbose(bool v) { verbose = v; }
    };

    // 1. 死代码消除Pass
    class DeadCodeEliminationPass : public Pass
    {
    public:
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "DeadCodeElimination"; }

    private:
        void markLiveInstructions(Function *func, std::unordered_set<Instruction *> &liveInsts);
        bool isInstructionCritical(Instruction *inst);
    };

    // 2. 常量折叠Pass
    class ConstantFoldingPass : public Pass
    {
    public:
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "ConstantFolding"; }

    private:
        Value *foldBinaryOperation(BinaryOperator *binOp);
        Value *foldComparison(CompareInst *cmpInst);
        bool isConstant(Value *val);
        int getConstantValue(Value *val);
    };

    // 3. 公共子表达式消除Pass
    class CommonSubexpressionEliminationPass : public Pass
    {
    private:
        struct ExpressionHash
        {
            std::string operator()(const std::pair<std::string, std::vector<Value *>> &expr) const;
        };

        std::unordered_map<std::pair<std::string, std::vector<Value *>>, Value *, ExpressionHash> exprMap;

    public:
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "CommonSubexpressionElimination"; }

    private:
        std::pair<std::string, std::vector<Value *>> getExpressionKey(Instruction *inst);
        bool canBeCommonSubexpression(Instruction *inst);
    };

    // 4. 复制传播Pass
    class CopyPropagationPass : public Pass
    {
    public:
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "CopyPropagation"; }

    private:
        std::unordered_map<Value *, Value *> copyMap;
        void collectCopies(Function *func);
        Value *followCopyChain(Value *val);
    };

    // 5. 基本块合并Pass
    class BasicBlockMergePass : public Pass
    {
    public:
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "BasicBlockMerge"; }

    private:
        bool canMergeBlocks(BasicBlock *bb1, BasicBlock *bb2);
        void mergeBlocks(BasicBlock *bb1, BasicBlock *bb2);
    };

    // 6. 简单的循环不变代码外提Pass
    class LoopInvariantCodeMotionPass : public Pass
    {
    public:
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LoopInvariantCodeMotion"; }

    private:
        struct Loop
        {
            BasicBlock *header;
            std::vector<BasicBlock *> blocks;
            std::vector<BasicBlock *> exits;
        };

        std::vector<Loop> findLoops(Function *func);
        bool isLoopInvariant(Instruction *inst, const Loop &loop);
        BasicBlock *findPreheader(const Loop &loop);
    };

    // 优化级别枚举
    enum class OptimizationLevel
    {
        O0, // 无优化
        O1, // 基本优化
        O2  // 更多优化
    };

    // 创建优化Pass管道的工厂函数
    std::unique_ptr<PassManager> createOptimizationPipeline(OptimizationLevel level, bool verbose = false);

} // namespace optimization
