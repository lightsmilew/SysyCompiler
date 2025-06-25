#pragma once
#include "RISCVDataStructure.h"
#include "../midend/IRDataStructure.h"

namespace RISCV
{
    // 后端主流水线类
    class RISCVBuilder
    {
    private:
        shared_ptr<RISCVModule> riscvModule;

    public:
        RISCVBuilder() = default;

        // 主要接口：将IR模块转换为RISC-V模块
        shared_ptr<RISCVModule> generateRISCVCode(shared_ptr<Module> irModule);

        // 生成最终的汇编代码
        string generateAssembly(shared_ptr<RISCVModule> module);

    private:
        // 流水线各阶段
        void initializeModule(shared_ptr<Module> irModule);
        void generateInstructions();
        void allocateRegisters();
        void optimizeCode();
    };

    // 指令选择器
    class InstructionSelector
    {
    private:
        shared_ptr<RISCVFunction> currentFunc;
        shared_ptr<RISCVBasicBlock> currentBB;

    public:
        InstructionSelector() = default;

        // 为函数生成指令
        void selectInstructions(shared_ptr<RISCVFunction> func);

    private:
        // IR指令到RISC-V指令的映射
        void visitBinaryOp(shared_ptr<BinaryOperator> inst);
        void visitUnaryOp(shared_ptr<UnaryOperator> inst);
        void visitLoadInst(shared_ptr<LoadInst> inst);
        void visitStoreInst(shared_ptr<StoreInst> inst);
        void visitCallInst(shared_ptr<CallInst> inst);
        void visitReturnInst(shared_ptr<ReturnInst> inst);
        void visitBranchInst(shared_ptr<BranchInst> inst);
        void visitAllocaInst(shared_ptr<AllocaInst> inst);
        void visitElementPtrInst(shared_ptr<GetElementPtrInst> inst);
        void visitFCmpInst(shared_ptr<FCmpInst> inst);
        void visitICmpInst(shared_ptr<ICmpInst> inst);

        // 辅助方法
        shared_ptr<RISCVRegister> getOrCreateVirtualReg(shared_ptr<Value> value);
        void mapArguments(shared_ptr<RISCVFunction> func);
        void handleFunctionPrologue(shared_ptr<RISCVFunction> func);
        void handleFunctionEpilogue(shared_ptr<RISCVFunction> func);
    };

    // 寄存器分配器
    class RegisterAllocator
    {
    public:
        struct LiveInterval
        {
            shared_ptr<RISCVRegister> virtualReg;
            int start;
            int end;
            shared_ptr<RISCVRegister> assignedReg;
            bool isSpilled;

            LiveInterval(shared_ptr<RISCVRegister> reg, int s, int e)
                : virtualReg(reg), start(s), end(e), assignedReg(nullptr), isSpilled(false) {}
        };

    private:
        shared_ptr<RISCVFunction> currentFunc;
        vector<LiveInterval> intervals;

        // 可用的物理寄存器
        static const vector<shared_ptr<RISCVRegister>> availableGeneralRegs;
        static const vector<shared_ptr<RISCVRegister>> availableFloatRegs;

    public:
        RegisterAllocator() = default;

        // 主要接口
        void allocateRegisters(shared_ptr<RISCVFunction> func);

    private:
        // 活跃变量分析
        void computeLiveness();
        void computeLiveIntervals();

        // 线性扫描寄存器分配
        void linearScanAllocation();
        void expireOldIntervals(const LiveInterval &current,
                                vector<LiveInterval *> &active);
        void spillAtInterval(LiveInterval &current,
                             vector<LiveInterval *> &active);

        // 插入溢出代码
        void insertSpillCode();
    };

    // 窥孔优化器
    class PeepholeOptimizer
    {
    private:
        shared_ptr<RISCVFunction> currentFunc;

    public:
        PeepholeOptimizer() = default;

        void optimize(shared_ptr<RISCVFunction> func);

    private:
        // 优化模式
        bool optimizeRedundantMoves(shared_ptr<RISCVBasicBlock> bb);
        bool optimizeConstantFolding(shared_ptr<RISCVBasicBlock> bb);
        bool optimizeDeadCode(shared_ptr<RISCVBasicBlock> bb);
    };

    // 汇编生成器
    class AssemblyEmitter
    {
    public:
        string emit(shared_ptr<RISCVModule> module);

    private:
        string emitGlobals(const vector<shared_ptr<RISCVGlobalBlock>> &globals);
        string emitFunction(shared_ptr<RISCVFunction> func);
        string emitBasicBlock(shared_ptr<RISCVBasicBlock> bb);
    };
}
