#pragma once
#include "../irbuild/IRDataStructure.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <set>

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

    // 新增：获取寄存器分配结果接口
    const std::unordered_map<Value*, int>* getRegisterAssignment() const;
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
                               [&](BasicBlock *bb) { return bb->containsByName(inst); });
        }
    };
    vector<Loop> findLoops(Function *func);
    bool isLoopInvariant(Instruction *inst, const Loop &loop);
    BasicBlock *findPreheader(const Loop &loop);
};
// 5. 函数内联 Pass（将函数调用替换为函数体）
class FunctionInliningPass : public Pass {
public:
    bool runOnFunction(Function *func) override;
    string getsuffix() { return "_inl"+to_string(inlineCount++); }
    std::string getName() const override { return "FunctionInlining"; }
private:
    int inlineCount=0;
    bool shouldInline(Function *callee);
    int inlineAt(CallInst *call, Function *caller, BasicBlock *bb, size_t insertPos);
};
// 6. 常量折叠 Pass（将常量表达式计算为常量值）函数内联时会产生常量二元表达式
class ConstantFoldingPass : public Pass {
public:
    std::string getName() const override { return "ConstantFoldingPass"; }
    bool runOnFunction(Function *func) override;
};
// 7. phi 消除 Pass（SSA转回普通IR，消除phi指令）
class PhiEliminationPass : public Pass
{
public:
    bool runOnFunction(Function *func) override;
    string getName() const override { return "PhiElimination"; }
};
// 8. 寄存器分配 Pass（基于图的寄存器分配）
class RegisterAllocationPass : public Pass {
public:
    // 活跃变量分析结果
    std::unordered_map<BasicBlock*, std::set<Value*>> liveIn, liveOut;
    // 冲突图
    std::unordered_map<Value*, std::set<Value*>> interferenceGraph;
    // 分配结果
    std::unordered_map<Value*, int> regAssignment;
    bool runOnFunction(Function *func) override;
    // 获取寄存器分配结果
    string getName() const override{ return "RegisterAllocationPass"; }
};
// 优化级别枚举
enum class OptimizationLevel
{
    O0, // 无优化
    O1, // 基本优化
    O2, // 更多优化

    //以下是调试内容
    O10,
    O11,
    O12,
    O13,
    O14,
    O15,
    O16
};

// 创建优化Pass管道的工厂函数
std::unique_ptr<PassManager> createOptimizationPipeline(OptimizationLevel level, bool verbose = false);

}