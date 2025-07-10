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
    vector<Instruction*> needToDelete; // 存储需要删除的值
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


// 2. 公共子表达式消除Pass
class CommonSubexpressionEliminationPass : public Pass
{
private:
    struct ExpressionHash
    {
        std::size_t operator()(const std::pair<std::string, std::vector<std::string>> &expr) const;
    };
    //std::vector<Value *> needToDelete;
    std::unordered_map<std::pair<std::string, std::vector<std::string>>, Value*, ExpressionHash> exprMap;

public:
    bool runOnFunction(Function *func) override;
    std::string getName() const override { return "CommonSubexpressionElimination"; }

private:
    std::pair<std::string, std::vector<std::string>> getExpressionKey(Instruction *inst);
    bool canBeCommonSubexpression(Instruction *inst);
};

// 无效果可删除
// 3. 基本块合并Pass
class BasicBlockMergePass : public Pass
{
public:
    bool runOnFunction(Function *func) override;
    string getName() const override { return "BasicBlockMerge"; }

private:
    bool canMergeBlocks(BasicBlock *bb1, BasicBlock *bb2);
    void mergeBlocks(BasicBlock *bb1, BasicBlock *bb2);
};

// 4. 循环不变代码外提Pass
class LoopInvariantCodeMotionPass : public Pass
{
public:
    bool runOnFunction(Function *func) override;
    string getName() const override { return "LoopInvariantCodeMotion"; }
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
    //vector<Instruction *>needToDelete; // 存储需要删除的指令
    vector<Loop> findLoops(Function *func);
    bool isLoopInvariant(Instruction *inst, const Loop &loop);
    BasicBlock *findPreheader(const Loop &loop);
};
// 5. 函数内联 Pass（将函数调用替换为函数体）
class FunctionInliningPass : public Pass {
public:
    bool runOnFunction(Function *func) override;
    std::string getName() const override { return "FunctionInlining"; }
private:
    bool shouldInline(Function *callee);
    void inlineAt(CallInst *call, Function *caller, BasicBlock *bb, std::vector<std::unique_ptr<Instruction>>::iterator it);
};
// 6. phi 消除 Pass（SSA转回普通IR，消除phi指令）
class PhiEliminationPass : public Pass
{
public:
    bool runOnFunction(Function *func) override;
    string getName() const override { return "PhiElimination"; }
// private:
//     vector<Instruction *>needToDelete; // 存储需要删除的phi指令

};

// 优化级别枚举
enum class OptimizationLevel
{
    O0, // 无优化
    O1, // 基本优化
    O2,  // 更多优化

    //以下是调试内容
    O10,
    O11,
    O12,
    O13,
    O14
};

// 创建优化Pass管道的工厂函数
std::unique_ptr<PassManager> createOptimizationPipeline(OptimizationLevel level, bool verbose = false);

}