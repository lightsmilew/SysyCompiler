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
    vector<Value*> needToDelete; // 存储需要删除的值
    bool verbose;
    Pass(bool verbose = false) : verbose(verbose) {}
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
    DeadCodeEliminationPass(bool verbose = false) : Pass(verbose) {}
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
    CommonSubexpressionEliminationPass(bool verbose = false) : Pass(verbose) {}
    bool runOnFunction(Function *func) override;
    std::string getName() const override { return "CommonSubexpressionElimination"; }

private:
    std::pair<std::string, std::vector<std::string>> getExpressionKey(Instruction *inst);
    bool canBeCommonSubexpression(Instruction *inst);
};


// 3. 循环不变代码外提Pass
class LoopInvariantCodeMotionPass : public Pass
{
public:
    LoopInvariantCodeMotionPass(bool verbose = false) : Pass(verbose) {}
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
    bool canMoveToPreheader(Instruction *inst);
    BasicBlock *findPreheader(const Loop &loop);
};
// 4. 函数内联 Pass（将函数调用替换为函数体）
class FunctionInliningPass : public Pass {
public:
    FunctionInliningPass(bool verbose = false) : Pass(verbose) {}
    bool runOnFunction(Function *func) override;
    string getsuffix() { return "_inl"+to_string(inlineCount++); }
    std::string getName() const override { return "FunctionInlining"; }
private:
    int inlineCount=0;
    bool shouldInline(Function *callee);
    int inlineAt(CallInst *call, Function *caller, BasicBlock *bb, size_t insertPos);
};
// 5. 常量折叠 Pass（将常量表达式计算为常量值）函数内联时会产生常量二元表达式
class ConstantFoldingPass : public Pass {
public:
    ConstantFoldingPass(bool verbose = false) : Pass(verbose) {}
    std::string getName() const override { return "ConstantFoldingPass"; }
    bool runOnFunction(Function *func) override;
};
// 6. phi 消除 Pass（SSA转回普通IR，消除phi指令）
class PhiEliminationPass : public Pass
{
public:
    PhiEliminationPass(bool verbose = false) : Pass(verbose) {}
    bool runOnFunction(Function *func) override;
    string getName() const override { return "PhiElimination"; }
};
// 7. 活跃变量分析 Pass（Live Variable Analysis）
// liveIn
// 含义：在进入该基本块时，哪些变量是“活跃”的。
// 解释：这些变量在该基本块及其后继中会被使用，但在本基本块内还没有被重新定义。
// 用途：进入基本块前，这些变量的值必须是有效的（不能被覆盖或丢弃）。
// liveOut
// 含义：在离开该基本块时，哪些变量是“活跃”的。
// 解释：这些变量在该基本块的后继基本块中会被使用。
// 用途：离开基本块时，这些变量的值必须被保留，以便后继块使用。
class LiveVariableAnalysisPass : public Pass {
public:
    // 每个基本块的liveIn/liveOut集合
    std::unordered_map<BasicBlock*, std::set<Value*>> liveIn, liveOut;

    LiveVariableAnalysisPass(bool verbose = false) : Pass(verbose) {}
    bool runOnFunction(Function *func) override;
    std::string getName() const override { return "LiveVariableAnalysis"; }
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