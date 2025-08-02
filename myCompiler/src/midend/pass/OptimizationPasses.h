#pragma once
#include "../irbuild/IRDataStructure.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <memory>
#include <set>
#include <sstream>

namespace optimization
{

    void dfs(BasicBlock *bb, std::unordered_map<BasicBlock *, int> &dfn,
             vector<BasicBlock *> &order, int &idx,
             std::unordered_map<BasicBlock *, int> &inStack,
             std::vector<std::pair<BasicBlock *, BasicBlock *>> &backedges);
    vector<Loop> findLoops(Function *func);

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
        using ExprKey = std::pair<std::string, std::vector<std::string>>;

    private:
        struct ExpressionHash
        {
            std::size_t operator()(const ExprKey &expr) const;
        };

        std::unordered_map<ExprKey, std::pair<Instruction *, BasicBlock *>, ExpressionHash> exprMap;
        std::unordered_map<BasicBlock *, std::unordered_set<BasicBlock *>> dom;

    public:
        CommonSubexpressionEliminationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "CommonSubexpressionElimination"; }

    private:
        std::unordered_map<BasicBlock *, BasicBlock *> idom;
        std::pair<std::string, std::vector<std::string>> getExpressionKey(Instruction *inst);
        bool canBeCommonSubexpression(Instruction *inst, BasicBlock *bb);
        bool CanLoadCSE(Instruction *inst, Instruction *map_inst, BasicBlock *bb);
        bool dominates(BasicBlock *dom, BasicBlock *node);
        std::unordered_map<BasicBlock *, BasicBlock *> computeIDom_LengauerTarjan(Function *func);
        // 检查Load指令的地址是否只被唯一Store且无其他写
    };

    // 3. 循环不变代码外提Pass
    class LoopInvariantCodeMotionPass : public Pass
    {
    public:
        LoopInvariantCodeMotionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        string getName() const override { return "LoopInvariantCodeMotion"; }

    private:
        bool isLoopInvariant(Instruction *inst, const Loop &loop);
        bool canMoveToPreheader(Instruction *inst, const Loop &loop);
        BasicBlock *findPreheader(const Loop &loop);
    };
    // 4. 函数内联 Pass（将函数调用替换为函数体）
    // 内联之后要更新循环块信息
    class FunctionInliningPass : public Pass
    {
    public:
        FunctionInliningPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        string getsuffix(string funcname) 
        { 
            int count = inlineCountMap[funcname]++;
            return "_inl_" +funcname+"_"+to_string(count); 
        }
        std::string getName() const override { return "FunctionInlining"; }

    private:
        int inlineCount = 0;
        unordered_map<string,int> inlineCountMap; // 记录每个函数的内联次数
        bool shouldInline(Function *callee);
        int inlineAt(CallInst *call, Function *caller, BasicBlock *bb, size_t insertPos);
        // debug
        void verifyCFG(Function *func);
    };
    // 5. 常量折叠 Pass（将常量表达式计算为常量值）函数内联时会产生常量二元表达式
    class ConstantFoldingPass : public Pass
    {
    public:
        ConstantFoldingPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "ConstantFoldingPass"; }
    };
    // 6. phi 消除 Pass（SSA转回普通IR，消除phi指令）
    class PhiEliminationPass : public Pass
    {
    public:
        PhiEliminationPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "PhiElimination"; }
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
    class LiveVariableAnalysisPass : public Pass
    {
    public:
        // 每个基本块的liveIn/liveOut集合
        std::unordered_map<BasicBlock *, std::set<std::string>> liveIn, liveOut;

        LiveVariableAnalysisPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "LiveVariableAnalysis"; }
    };
    // 8.展开getelementptr
    class GEPExpansionPass : public Pass
    {
    public:
        GEPExpansionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "GEPExpansion"; }
    };
    // 9.链式加法转乘法
    class AddChainReductionPass : public Pass
    {
    public:
        AddChainReductionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "AddChainReduction"; }
    };
    // 10.强度削弱
    class StrengthReductionPass : public Pass
    {
    public:
        StrengthReductionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "StrengthReduction"; }
    };
    // 11.替换无用的gep指令为bitcast
    class GEPToBitCastPass : public Pass
    {
    public:
        GEPToBitCastPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "GEPToBitCast"; }
    };
    // // 12.数组消除
    // class ArrayEliminationPass : public Pass
    // {
    // public:
    //     ArrayEliminationPass(bool verbose = false) : Pass(verbose) {}
    //     std::string getName() const override { return "ArrayEliminationPass"; }
    //     bool runOnFunction(Function *func) override;

    //     bool isAffineStore(StoreInst *store, Value *index, double &A, double &b);
    //     // 构造循环比较指令
    //     Instruction *constructLoopCondition(const Loop &loop);
    // };
    // 优化级别枚举
    enum class OptimizationLevel
    {
        O0, // 无优化
        O1, // 基本优化
        O2, // 更多优化

        // 以下是调试内容
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