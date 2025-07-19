#include "RISCVBuilder.h"
using namespace RISCV;

// 指令选择器
class InstructionSelector
{
private:
    Function *irFunction; // 保存IR函数引用
    shared_ptr<RISCVFunction> currentFunc;
    shared_ptr<RISCVBasicBlock> currentBB;
    unordered_map<string, shared_ptr<RISCVRegister>> registerMap; // IR值名字到寄存器的映射
    vector<shared_ptr<RISCVRegister>> tempRegisters;              // temp寄存器

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

    // 获取虚拟寄存器
    shared_ptr<RISCVRegister> getOrCreateVirtualReg(Value *value);
    shared_ptr<RISCVRegister> LiInt(int intValue);
    shared_ptr<RISCVRegister> LiFloat(float floatValue);
    shared_ptr<RISCVRegister> LaGlobl(GlobalVariable *globlvar);
    shared_ptr<RISCVRegister> getTempReg();
};