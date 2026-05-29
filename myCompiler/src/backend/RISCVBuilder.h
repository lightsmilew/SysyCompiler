#pragma once
#include "RISCVDataStructure.h"
#include "../midend/irbuild/IRDataStructure.h"
#include "AssemblyEmitter.h"
#include "InstructionSelector.h"
#include "GraphRegisterAllocator/GraphColorRegisterAllocator.h"
#include "InstructionScheduler.h"
#include "PeepholeOptimizer/PeepOptimizationManager.h"
#include "PeepholeOptimizer/PeepPass.h"
#include "LICM.h"
#include "BlockLocalCSE.h"
#include <unordered_map>
using std::cout;
using std::endl;
using std::set;
using std::unordered_map;
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
        void runLICMPass();     // 循环不变 la/li 外提
        void runBlockLocalCSEPass(); // 块内 li/la 物化与算术 CSE
        void instructionSheduler();
        void allocateRegisters();
        void FirstPeep();
        void SecondPeep();

        // 全局变量初始化处理
        void processGlobalInitializer(shared_ptr<RISCVGlobalBlock> globalBlock, Constant *initializer);
        void processZeroInitializer(shared_ptr<RISCVGlobalBlock> globalBlock, GlobalVariable *globalVar);

        // 循环信息构造
        void buildBackendLoopInfo(shared_ptr<RISCVFunction> riscvFunc,
                                  Function *irFunc,
                                  const unordered_map<BasicBlock *, shared_ptr<RISCVBasicBlock>> &blockMapping);
        int calculateLoopDepth(const Loop &targetLoop, const vector<Loop> &allLoops);
        void establishLoopHierarchy(LoopInfo &backendLoopInfo,
                                    const vector<Loop> &irLoops,
                                    const unordered_map<BasicBlock *, shared_ptr<RISCVBasicBlock>> &blockMapping);
        bool isBlockInLoop(shared_ptr<RISCVBasicBlock> block, shared_ptr<RISCVLoop> loop);

        void reallocOffsetForInstructions();
        bool isLibraryFunction(const string &funcName);
        void printInstructions();
    };
}
