#pragma once
#include "../RISCVDataStructure.h"
#include "../../midend/irbuild/IRDataStructure.h"

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
        void allocateRegisters();

        // 全局变量初始化处理
        void processGlobalInitializer(shared_ptr<RISCVGlobalBlock> globalBlock, Constant *initializer);
        void processZeroInitializer(shared_ptr<RISCVGlobalBlock> globalBlock, GlobalVariable *globalVar);
    };
}
