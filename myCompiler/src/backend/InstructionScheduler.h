#pragma once
#include "RISCVDataStructure.h"
#include <vector>
#include <queue>
#include <unordered_map>
#include <unordered_set>
#include <memory>

// #define RISCV_SCHEDULER_DEBUG

namespace RISCV
{

    // 功能单元类型
    enum class FunctionalUnitType
    {
        LOAD_STORE, // 内存访问单元
        MUL_DIV,    // 乘除法单元
        BRANCH,     // 分支跳转单元
        INTEGER,    // 整数运算单元
        FLOAT       // 浮点运算单元
    };

    // 指令延迟信息
    struct InstructionLatency
    {
        int latency;             // 指令延迟周期数
        int repeatRate;          // 重复执行间隔（主要用于浮点除法）
        FunctionalUnitType unit; // 所需功能单元
    };

    // 指令依赖节点
    class DependencyNode
    {
    public:
        std::shared_ptr<RISCVInstruction> instruction;
        std::vector<DependencyNode *> predecessors; // 前驱节点
        std::vector<DependencyNode *> successors;   // 后继节点
        int readyTime;                              // 最早可调度时间
        int scheduledTime;                          // 实际调度时间
        bool scheduled;                             // 是否已调度
        InstructionLatency latencyInfo;             // 延迟信息
        int criticalPath;                           // 预计算的关键路径长度

        DependencyNode(std::shared_ptr<RISCVInstruction> instr)
            : instruction(instr), readyTime(0), scheduledTime(-1), scheduled(false), criticalPath(0) {}
    };

    // 功能单元状态
    struct FunctionalUnit
    {
        FunctionalUnitType type;
        int busyUntil;        // 忙碌直到第几个周期
        int lastFloatDivTime; // 最后一次浮点除法的时间（用于repeat rate）

        FunctionalUnit(FunctionalUnitType t) : type(t), busyUntil(0), lastFloatDivTime(-1) {}
    };

    // 指令调度器
    class InstructionScheduler
    {
    private:
        // 功能单元配置
        std::vector<FunctionalUnit> units;

        // 延迟信息映射
        std::unordered_map<RISCVOpcode, InstructionLatency> latencyMap;

        // 初始化延迟信息
        void initializeLatencyMap();

        // 获取指令的延迟信息
        InstructionLatency getInstructionLatency(RISCVOpcode opcode) const;

        // 构建数据依赖图
        std::vector<DependencyNode> buildDependencyGraph(
            const std::vector<std::shared_ptr<RISCVInstruction>> &instructions);

        // 添加数据依赖
        void addDataDependencies(std::vector<DependencyNode> &nodes);

        // 内存依赖分析辅助函数
        bool isMemoryLoadInstruction(RISCVOpcode opcode) const;
        bool isMemoryStoreInstruction(RISCVOpcode opcode) const;
        bool mayAlias(const std::shared_ptr<RISCVInstruction> &inst1,
                      const std::shared_ptr<RISCVInstruction> &inst2) const; // 计算指令的最早可调度时间
        void calculateReadyTimes(std::vector<DependencyNode> &nodes);

        // 检查功能单元可用性
        bool canScheduleInstruction(const DependencyNode &node, int currentTime);

        // 调度指令到功能单元
        void scheduleInstruction(DependencyNode &node, int currentTime);

        // 选择下一个要调度的指令
        DependencyNode *selectNextInstruction(
            const std::vector<DependencyNode *> &readyList, int currentTime);

        // 计算关键路径长度（旧版本，保留兼容性）
        int calculateCriticalPath(DependencyNode *node) const;

        // 预计算所有节点的关键路径（新版本，O(n)复杂度）
        void precomputeCriticalPaths(std::vector<DependencyNode> &nodes);

        // 计算指令调度优先级评分（使用预计算的关键路径，O(1)复杂度）
        int calculateSimpleScore(DependencyNode *node) const;

        // 检查双发射约束
        bool canDualIssue(const DependencyNode &inst1, const DependencyNode &inst2);

        // 控制流指令处理相关方法
        std::vector<std::vector<std::shared_ptr<RISCVInstruction>>>
        splitByCallInstructions(const std::vector<std::shared_ptr<RISCVInstruction>> &instructions);
        bool containsNonSchedulableInstructions(const std::vector<std::shared_ptr<RISCVInstruction>> &instructions);
        bool isNonSchedulableInstruction(RISCVOpcode opcode) const;
        std::pair<std::vector<std::shared_ptr<RISCVInstruction>>, std::vector<std::shared_ptr<RISCVInstruction>>>
        separateControlFlowInstructions(const std::vector<std::shared_ptr<RISCVInstruction>> &instructions);
        bool isReturnValueSetupInstruction(
            std::shared_ptr<RISCVInstruction> instr,
            size_t currentIndex,
            const std::vector<std::shared_ptr<RISCVInstruction>> &instructions) const;

    public:
        InstructionScheduler();

        // 对指令序列进行调度
        std::vector<std::shared_ptr<RISCVInstruction>> scheduleInstructions(
            const std::vector<std::shared_ptr<RISCVInstruction>> &instructions);

        // 对基本块进行调度
        void scheduleBasicBlock(std::shared_ptr<RISCVBasicBlock> bb);

        // 对整个函数进行调度
        void scheduleFunction(std::shared_ptr<RISCVFunction> func);

        // 对整个模块进行调度
        void scheduleModule(std::shared_ptr<RISCVModule> module);

        // 配置选项
        void setEnableDualIssue(bool enable) { enableDualIssue = enable; }
        void setEnableFloatOptimization(bool enable) { enableFloatOptimization = enable; }
        void setEnablePreciseMemoryDependency(bool enable) { enablePreciseMemoryDependency = enable; }
        void setPostRegisterAllocation(bool enable) { postRegisterAllocation = enable; }

        // 调试和分析功能
        void printSchedulingStatistics() const;
        int estimateCycles(const std::vector<std::shared_ptr<RISCVInstruction>> &instructions) const;
        void printSchedulingComparison(
            const std::vector<std::shared_ptr<RISCVInstruction>> &originalInstructions,
            const std::vector<std::shared_ptr<RISCVInstruction>> &scheduledInstructions,
            int originalCycles,
            int scheduledCycles) const;

    private:
        // 配置选项
        bool enableDualIssue;
        bool enableFloatOptimization;
        bool enablePreciseMemoryDependency; // 启用精确内存依赖分析
        bool postRegisterAllocation;        // 是否在寄存器分配后进行调度

        // 统计信息
        mutable int totalInstructionsScheduled;
        mutable int totalCyclesSaved;
    };

} // namespace RISCV
