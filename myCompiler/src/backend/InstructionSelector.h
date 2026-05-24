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
    void visitStorePairInst(StorePairInst *inst);
    shared_ptr<RISCVRegister> packI64FromHalves(Value *hi, Value *lo, bool isPhysical = false);
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
    void visitBitCastInst(CastInst *inst);
    void visitXnorInst(BinaryOperator *inst);
    void visitSExtInst(CastInst *inst);
    void visitTruncInst(CastInst *inst);
    void visitSelectInst(SelectInst *inst);

    // 获取虚拟寄存器
    // 常量默认用虚拟寄存器，供寄存器分配与 LICM 外提；仅局部 scratch 显式传 isPhysical=true
    shared_ptr<RISCVRegister> getOrCreateVirtualReg(Value *value, bool isPhysical = true);
    shared_ptr<RISCVRegister> LiInt(int intValue, bool isPhysical = false);
    shared_ptr<RISCVRegister> LiFloat(float floatValue, bool isPhysical = false);
    shared_ptr<RISCVRegister> LiLong(long longValue, bool isPhysical = false);
    shared_ptr<RISCVRegister> LaGlobl(GlobalVariable *globlvar);
    shared_ptr<RISCVRegister> getTempReg(bool isPhysical = false);
    shared_ptr<RISCVRegister> getTempFloatReg(bool isPhysical = false);
    shared_ptr<RISCVRegister> getTempPhysicalReg();
    shared_ptr<RISCVRegister> getTempPhysicalFloatReg();

    bool isValidImmediate(int64_t value, Opcode opcode);

    // alloca需要初始化数组
    void InitAllocaArray(shared_ptr<RISCVRegister> addrReg, int size);

    // 参数传递解耦函数
    void DealArgumentsInStart(); // 处理函数参数
    unordered_map<string, shared_ptr<RISCVRegister>> moveCallerArgsTwoPhase();
    void move2RestoreArgs(unordered_map<string, shared_ptr<RISCVRegister>> &registerMap, const string &funcName);
    shared_ptr<RISCVRegister> getCallerArgReg(Argument *arg, size_t index);
    shared_ptr<RISCVRegister> getArgReg(const string &argName, RegisterType regType);

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
};