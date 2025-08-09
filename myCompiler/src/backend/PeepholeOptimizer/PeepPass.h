#pragma once

#include "PeepOptimizationManager.h"
#include <optional>
#include <cmath>
using std::dynamic_pointer_cast;
using std::nullopt;
using std::optional;
// 删除多余move指令
class RemoveRedundantMovePass : public PeepPass
{
public:
    RemoveRedundantMovePass() : PeepPass("RemoveRedundantMove") {}

    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;

private:
    bool isRedundantMove(shared_ptr<RISCVInstruction> instr);
};

// 删除多余jal指令
class RemoveRedundantJalPass : public PeepPass
{
public:
    RemoveRedundantJalPass() : PeepPass("RemoveRedundantJal") {}

    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;

private:
    bool isRedundantJal(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb);
};

// 冗余代码删除
class DeadCodeEliminationPass : public PeepPass
{
public:
    DeadCodeEliminationPass() : PeepPass("DeadCodeElimination") {}

    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;

private:
    bool isDeadCode(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb);
    bool hasSideEffects(shared_ptr<RISCVInstruction> instr);
    bool isRegisterRedefined(shared_ptr<RISCVRegister> reg, shared_ptr<RISCVInstruction> startInstr, shared_ptr<RISCVBasicBlock> bb);
};

// 强度减弱
class StrengthReductionPass : public PeepPass
{
public:
    StrengthReductionPass() : PeepPass("StrengthReduction") {}
    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;

private:
    bool isPowerOfTwo(int64_t n);
};

// 立即数传播优化
class ImmediatePropagationPass : public PeepPass
{
public:
    ImmediatePropagationPass() : PeepPass("ImmediatePropagation") {}

    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;

private:
    // 检查指令是否可以使用立即数形式
    bool canUseImmediateForm(RISCVOpcode opcode);

    // 查找寄存器的常量定义
    optional<int64_t> findConstantValue(shared_ptr<RISCVRegister> reg,
                                        shared_ptr<RISCVInstruction> currentInstr,
                                        shared_ptr<RISCVBasicBlock> bb);

    // 检查立即数是否在有效范围内
    bool isValidImmediate(int64_t value, RISCVOpcode opcode);

    // 创建立即数形式的指令
    shared_ptr<RISCVInstruction> createImmediateInstruction(
        shared_ptr<RISCVInstruction> instr,
        int operandIndex,
        int64_t immediateValue);

    // 获取指令对应的立即数操作码
    RISCVOpcode getImmediateOpcode(RISCVOpcode opcode);
};
