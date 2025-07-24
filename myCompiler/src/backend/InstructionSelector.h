#pragma once
#include "RISCVBuilder.h"
using namespace RISCV;

// 指令选择器
class InstructionSelector
{
private:
    Function *irFunction; // 保存IR函数引用
    shared_ptr<RISCVFunction> currentFunc;
    shared_ptr<RISCVBasicBlock> currentBB;
    unordered_map<string, shared_ptr<RISCVRegister>> registerMap;  // IR值名字到寄存器的映射
    vector<shared_ptr<RISCVRegister>> tempRegisters;               // temp寄存器
    unordered_map<string, shared_ptr<RISCVRegister>> globalVarMap; // 全局变量映射
    unordered_map<string, shared_ptr<RISCVRegister>> MoveArgMap;   // 临时寄存器到参数寄存器的映射
    shared_ptr<RISCVInstruction> currentLiInstruction;             // 当前正在处理的指令

    bool isTempRegOrZero(shared_ptr<RISCVRegister> reg) const
    {
        return find(tempRegisters.begin(), tempRegisters.end(), reg) != tempRegisters.end() ||
               reg->getPhysicalReg() == RISCVRegister::PhysicalReg::ZERO && reg->isPhysical();
    }

    int tempRegCount = 0;
    int tempFloatRegCount = 0;

public:
    InstructionSelector() {}
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

    // 获取虚拟寄存器
    shared_ptr<RISCVRegister> getOrCreateVirtualReg(Value *value);
    shared_ptr<RISCVRegister> LiInt(int intValue, bool isPhysical = false);
    shared_ptr<RISCVRegister> LiFloat(float floatValue, bool isPhysical = false);
    shared_ptr<RISCVRegister> LaGlobl(GlobalVariable *globlvar);
    shared_ptr<RISCVRegister> getTempReg(bool isPhysical = false);
    shared_ptr<RISCVRegister> getTempFloatReg(bool isPhysical = false);
    shared_ptr<RISCVRegister> getTempPhysicalReg();
    shared_ptr<RISCVRegister> getTempPhysicalFloatReg();

    // alloca需要初始化数组
    void InitAllocaArray(shared_ptr<RISCVRegister> addrReg, int size);

    // 参数传递解耦函数
    void DealArgumentsInStart(); // 处理函数参数
    unordered_map<string, shared_ptr<RISCVRegister>> moveCallerArgsTwoPhase();
    void move2RestoreArgs(unordered_map<string, shared_ptr<RISCVRegister>> &registerMap);
    shared_ptr<RISCVRegister> getCallerArgReg(Argument *arg, size_t index);
    shared_ptr<RISCVRegister> getArgReg(const string &argName, RegisterType regType);

    // 数据流分析
    // Step 1: 计算每个基本块的 USE 和 DEF 集合
    // Step 2: 计算每个基本块的 liveIn 和 liveOut 集合
    // Step 3: 基于 liveIn/liveOut 计算每个寄存器的活跃区间
    // Step 4: 优化和合并重叠的活跃区间
    void buildControlFlowGraph()
    {
        // 遍历IR函数的所有基本块，构建RISC-V基本块的前驱后继关系
        for (size_t i = 0; i < irFunction->BasicBlocks.size(); ++i)
        {
            auto irBB = irFunction->BasicBlocks[i].get();
            auto riscvBB = currentFunc->getBasicBlock(irBB->getName());

            if (!riscvBB)
                continue;

            // 获取IR基本块的后继，并在RISC-V基本块中建立相应关系
            const auto &irSuccessors = irBB->getSuccessors();
            for (auto irSuccBB : irSuccessors)
            {
                auto riscvSuccBB = currentFunc->getBasicBlock(irSuccBB->getName());
                if (riscvSuccBB)
                {
                    // 建立后继关系
                    riscvBB->addSuccessor(riscvSuccBB);
                    // 建立前驱关系
                    riscvSuccBB->addPredecessor(riscvBB);
                }
            }
        }
    }

    void printAllLiveRanges(const RISCV::LivenessInfo &livenessInfo)
    {
        // std::cout << "==== Live use and def information ====" << std::endl;
        // for (const auto &[reg, uses] : livenessInfo.usePoints)
        // {
        //     std::cout << reg->toString() << " uses: ";
        //     for (int use : uses)
        //     {
        //         std::cout << use << " ";
        //     }
        //     std::cout << std::endl;
        // }
        // for (const auto &[reg, defs] : livenessInfo.defPoints)
        // {
        //     std::cout << reg->toString() << " defs: ";
        //     for (int def : defs)
        //     {
        //         std::cout << def << " ";
        //     }
        //     std::cout << std::endl;
        // }
        // std::cout << "Total instructions: " << livenessInfo.totalInstructions << std::endl;
        // std::cout << "======================================" << std::endl;
        // // livein和liveout信息
        // std::cout << "==== Basic Block Live In/Out Information ====" << std::endl;
        // for (const auto &bb : currentFunc->getBasicBlocks())
        // {
        //     std::cout << "Basic Block: " << bb->getLabel() << std::endl;
        //     std::cout << "  Live In: ";
        //     for (const auto &reg : bb->getLiveIn())
        //     {
        //         std::cout << reg->toString() << " ";
        //     }
        //     std::cout << std::endl;
        //     std::cout << "  Live Out: ";
        //     for (const auto &reg : bb->getLiveOut())
        //     {
        //         std::cout << reg->toString() << " ";
        //     }
        //     std::cout << std::endl;
        // }
        // std::cout << "======================================" << std::endl;
        // 打印所有寄存器的活跃区间
        std::cout << "==== All Register Live Ranges ====" << std::endl;
        for (const auto &[reg, ranges] : livenessInfo.liveRanges)
        {
            if (!reg)
                continue;
            std::cout << reg->toString() << ": ";
            for (const auto &range : ranges)
            {
                std::cout << "[" << range.start << ", " << range.end << "] ";
            }
            std::cout << std::endl;
        }
        std::cout << "==================================" << std::endl;
    }
};