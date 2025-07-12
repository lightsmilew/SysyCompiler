#pragma once
#include "RISCVDataStructure.h"
#include "../midend/irbuild/IRDataStructure.h"

namespace RISCV
{
    // 后端主流水线类
    class RISCVBuilder
    {
    private:
        shared_ptr<RISCVModule> riscvModule;
        shared_ptr<Module> irModule; // 保存IR模块引用

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

        // 全局变量初始化处理
        void processGlobalInitializer(shared_ptr<RISCVGlobalBlock> globalBlock, Constant *initializer);
        void processZeroInitializer(shared_ptr<RISCVGlobalBlock> globalBlock, Type *type);
    };

    // 指令选择器
    class InstructionSelector
    {
    private:
        shared_ptr<RISCVFunction> currentFunc;
        shared_ptr<RISCVBasicBlock> currentBB;
        unordered_map<Value *, shared_ptr<RISCVRegister>> registerMap; // IR值到寄存器的映射
        unordered_map<Value *, int> stackArguments;                    // 栈参数到偏移量的映射

        // 保存局部数组的indecs
        unordered_map<Value *, vector<int>> localArrayIndices; // 用于存储局部数组的索引

        // 临时寄存器轮换计数器，避免寄存器冲突
        mutable int generalTempCounter = 0;
        mutable int floatTempCounter = 0;

    public:
        InstructionSelector() = default;

        // 为函数生成指令
        void selectInstructions(shared_ptr<RISCVFunction> func, Function *irFunc);

    private:
        // 通用指令访问接口
        void visitInstruction(Instruction *inst);

        // IR指令到RISC-V指令的映射
        void visitBinaryOp(BinaryOperator *inst);
        // void visitUnaryOp(UnaryOperator *inst);
        void visitLoadInst(LoadInst *inst);
        void visitStoreInst(StoreInst *inst);
        void visitCallInst(CallInst *inst);
        void visitReturnInst(ReturnInst *inst);
        void visitBranchInst(BranchInst *inst);
        void visitAllocaInst(AllocaInst *inst);
        void visitElementPtrInst(GetElementPtrInst *inst);
        void visitFCmpInst(FCmpInst *inst);
        void visitICmpInst(ICmpInst *inst);
        void visitSIToFPInst(CastInst *inst);
        void visitFPToSIInst(CastInst *inst);
        void visitCopyInst(CopyInst *inst);

        // 辅助方法
        shared_ptr<RISCVRegister> getOrCreateVirtualReg(Value *value);
        void mapArguments(shared_ptr<RISCVFunction> func, Function *irFunc);

        // 栈帧管理相关方法
        void prescanFunction(shared_ptr<RISCVFunction> func, Function *irFunc);
        void generateFunctionPrologue(shared_ptr<RISCVFunction> func);
        void generateFunctionEpilogue(shared_ptr<RISCVFunction> func);

        // 栈偏移计算辅助方法
        void storeValueToStack(Value *value, shared_ptr<RISCVRegister> reg, int size = 4);
        void generateStackAccess(int offset, shared_ptr<RISCVRegister> reg, bool isStore, bool isDouble = false);
        bool isImmediateInRange(int immediate, int bits = 12);
        void generateConstantLoad(shared_ptr<RISCVRegister> reg, int64_t value);
        void generateFloatConstantLoad(shared_ptr<RISCVRegister> reg, float value);

        // 临时寄存器管理方法
        shared_ptr<RISCVRegister> getTempRegister(RegisterType type, int index = 0);
        shared_ptr<RISCVRegister> getGeneralTempRegister(int index = 0);
        shared_ptr<RISCVRegister> getFloatTempRegister(int index = 0);
    };

    // // 寄存器分配器
    // class RegisterAllocator
    // {
    // public:
    //     struct LiveInterval
    //     {
    //         shared_ptr<RISCVRegister> virtualReg;
    //         int start;
    //         int end;
    //         shared_ptr<RISCVRegister> assignedReg;
    //         bool isSpilled;

    //         LiveInterval(shared_ptr<RISCVRegister> reg, int s, int e)
    //             : virtualReg(reg), start(s), end(e), assignedReg(nullptr), isSpilled(false) {}
    //     };

    // private:
    //     shared_ptr<RISCVFunction> currentFunc;
    //     vector<LiveInterval> intervals;

    //     // 可用的物理寄存器
    //     static const vector<shared_ptr<RISCVRegister>> availableGeneralRegs;
    //     static const vector<shared_ptr<RISCVRegister>> availableFloatRegs;

    // public:
    //     RegisterAllocator() = default;

    //     // 主要接口
    //     void allocateRegisters(shared_ptr<RISCVFunction> func);

    // private:
    //     // 活跃变量分析
    //     void computeLiveness();
    //     void computeLiveIntervals();

    //     // 线性扫描寄存器分配
    //     void linearScanAllocation();
    //     void expireOldIntervals(const LiveInterval &current,
    //                             vector<LiveInterval *> &active);
    //     void spillAtInterval(LiveInterval &current,
    //                          vector<LiveInterval *> &active);

    //     // 插入溢出代码
    //     void insertSpillCode();
    // };

    // // 窥孔优化器
    // class PeepholeOptimizer
    // {
    // private:
    //     shared_ptr<RISCVFunction> currentFunc;

    // public:
    //     PeepholeOptimizer() = default;

    //     void optimize(shared_ptr<RISCVFunction> func);

    // private:
    //     // 优化模式
    //     bool optimizeRedundantMoves(shared_ptr<RISCVBasicBlock> bb);
    //     bool optimizeConstantFolding(shared_ptr<RISCVBasicBlock> bb);
    //     bool optimizeDeadCode(shared_ptr<RISCVBasicBlock> bb);
    // };

    // 汇编生成器
    class AssemblyEmitter
    {
    public:
        string emit(shared_ptr<RISCVModule> module);

    private:
        string emitGlobals(const vector<shared_ptr<RISCVGlobalBlock>> &globals);
        string emitFunction(shared_ptr<RISCVFunction> func);
        string emitBasicBlock(shared_ptr<RISCVBasicBlock> bb);
        bool isLibraryFunction(const string &funcName);
    };
}
