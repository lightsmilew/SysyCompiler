#pragma once
#include "../irbuild/IRDataStructure.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>

namespace optimization
{

// 优化Pass的基类
class Pass
{
public:
    virtual ~Pass() = default;
    virtual bool runOnFunction(Function *func) = 0;
    virtual string getName() const = 0;
};

// Pass管理器
class PassManager
{
private:
    vector<std::unique_ptr<Pass>> passes;
    // 是否启用详细输出
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
    string getName() const override { return "DeadCodeElimination"; }
private:
    void markLiveInstructions(Function *func, std::unordered_set<Instruction *> &liveInsts);
    bool isInstructionCritical(Instruction *inst);
};

// 2. 常量折叠Pass
class ConstantFoldingPass : public Pass
{
public:
    bool runOnFunction(Function *func) override;
    string getName() const override { return "ConstantFolding"; }
private:
    Value *foldBinaryOperation(BinaryOperator *binOp);
    // 对比较指令进行常量折叠
    Value *foldComparison(ICmpInst *cmpInst);
    bool isConstant(Value *val);

};

// 3. 公共子表达式消除Pass
class CommonSubexpressionEliminationPass : public Pass
{
private:
    struct ExpressionHash
    {
        std::size_t operator()(const std::pair<string, vector<Value *>> &expr) const;
    };

    std::unordered_map<std::pair<string, vector<Value *>>, Value *, ExpressionHash> exprMap;

public:
    bool runOnFunction(Function *func) override;
    string getName() const override { return "CommonSubexpressionElimination"; }

private:
    std::pair<string, vector<Value *>> getExpressionKey(Instruction *inst);
    bool canBeCommonSubexpression(Instruction *inst);
};

// 4. 复制传播Pass
class CopyPropagationPass : public Pass
{
public:
    bool runOnFunction(Function *func) override;
    string getName() const override { return "CopyPropagation"; }

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
    string getName() const override { return "BasicBlockMerge"; }

private:
    bool canMergeBlocks(BasicBlock *bb1, BasicBlock *bb2);
    void mergeBlocks(BasicBlock *bb1, BasicBlock *bb2);
};

// 6. 循环不变代码外提Pass
class LoopInvariantCodeMotionPass : public Pass
{
public:
    bool runOnFunction(Function *func) override;
    string getName() const override { return "LoopInvariantCodeMotion"; }
    ~LoopInvariantCodeMotionPass()
    {
        for (auto *inst : needToDelete) 
        {
            delete inst; // 手动析构
        }
    }
private:
    struct Loop
    {
        BasicBlock *header;
        //blocks是循环体内的所有基本块
        //exits是循环的出口基本块（可能有多个）
        vector<BasicBlock *> blocks;
        vector<BasicBlock *> exits;
        bool contains(Instruction *inst) const
        {
            return std::any_of(blocks.begin(), blocks.end(),
                               [&](BasicBlock *bb) { return bb->contains(inst); });
        }
    };
    vector<Instruction *>needToDelete; // 存储需要删除的指令
    vector<Loop> findLoops(Function *func);
    bool isLoopInvariant(Instruction *inst, const Loop &loop);
    BasicBlock *findPreheader(const Loop &loop);
};
// 7. phi 消除 Pass（SSA转回普通IR，消除phi指令）
class PhiEliminationPass : public Pass
{
public:
    bool runOnFunction(Function *func) override;
    string getName() const override { return "PhiElimination"; }
    ~PhiEliminationPass()
    {
        for (auto *inst : needToDelete) 
        {
            delete inst; // 手动析构
        }
    }
private:
    vector<Instruction *>needToDelete; // 存储需要删除的phi指令

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