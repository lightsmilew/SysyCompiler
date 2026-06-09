#pragma once

#include "PeepOptimizationManager.h"
#include <optional>
#include <tuple>
#include <cmath>
using std::dynamic_pointer_cast;
using std::nullopt;
using std::optional;
using std::tuple;
// 删除 addw rd, rd, zero（visitTruncInst 在 rd==src 时的 i64→i32 截断，语义为恒等）
class RemoveRedundantAddwZeroPass : public PeepPass
{
public:
    RemoveRedundantAddwZeroPass() : PeepPass("RemoveRedundantAddwZero") {}

    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;
};

// 删除多余move指令
class RemoveRedundantMovePass : public PeepPass
{
public:
    RemoveRedundantMovePass() : PeepPass("RemoveRedundantMove") {}

    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;

private:
    bool isRedundantMove(shared_ptr<RISCVInstruction> instr);
    /// mv rd, x 后紧跟 mv rd, y：前一条结果被完全覆盖，可删
    bool isDeadConsecutiveMoveToSameDest(shared_ptr<RISCVInstruction> instr,
                                         shared_ptr<RISCVBasicBlock> bb);
    /// mv a, b 后紧跟 mv b, a：第二条多余（b 未被改写，a 已等于 b 的原值）
    bool isRedundantPingPongMove(shared_ptr<RISCVInstruction> instr,
                                 shared_ptr<RISCVBasicBlock> bb);
};

// 融合相邻的li+mv和地址计算+load/store
class FoldAdjacentMoveAndAddressPass : public PeepPass
{
public:
    FoldAdjacentMoveAndAddressPass() : PeepPass("FoldAdjacentMoveAndAddress") {}

    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;

private:
    bool isLegalMemoryImmediate(int64_t value) const;
    bool isAddressCalcOpcode(RISCVOpcode opcode) const;
    bool isLoadOpcode(RISCVOpcode opcode) const;
    bool isStoreOpcode(RISCVOpcode opcode) const;
    
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

// 折叠比较 + 条件跳转
class FoldCompareBranchPass : public PeepPass
{
public:
    FoldCompareBranchPass() : PeepPass("FoldCompareBranch") {}

    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;

private:
    bool isZeroRegister(shared_ptr<RISCVOperand> operand) const;
    bool canCombine(shared_ptr<RISCVInstruction> instr,
                    shared_ptr<RISCVInstruction> nextInstr,
                    shared_ptr<RISCVRegister> &lhsReg,
                    shared_ptr<RISCVRegister> &rhsReg,
                    RISCVOpcode &branchOpcode,
                    string &branchLabel) const;
};

// 折叠「退出分支 + jal 回边」为反向比较回边，删除 jal
class FoldLoopContinueBranchPass : public PeepPass
{
public:
    FoldLoopContinueBranchPass() : PeepPass("FoldLoopContinueBranch") {}

    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;
};

// 消除 icmp-ne-0 遗留的 xori rd,rs,0 + 分支，改为直接对 rs 分支
class FoldXorZeroBranchPass : public PeepPass
{
public:
    FoldXorZeroBranchPass() : PeepPass("FoldXorZeroBranch") {}

    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;
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
// 仍存在优化空间 eg:6=4+2
class StrengthReductionPass : public PeepPass
{
public:
    StrengthReductionPass() : PeepPass("StrengthReduction") {}
    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;

private:
    // 检查是否是2的幂次
    bool isPowerOfTwo(int64_t n);

    optional<tuple<vector<shared_ptr<RISCVInstruction>>::iterator, int64_t>>
    findLIInstruction(shared_ptr<RISCVRegister> targetReg,
                      vector<shared_ptr<RISCVInstruction>>::iterator currentIt,
                      vector<shared_ptr<RISCVInstruction>> &instrs);

    // 执行强度削减优化
    PeepOptiState performStrengthReduction(
        shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb,
        vector<shared_ptr<RISCVInstruction>>::iterator currentIt,
        vector<shared_ptr<RISCVInstruction>>::iterator liIt,
        int64_t constant, bool isOp1,
        shared_ptr<RISCVOperand> mulOp1, shared_ptr<RISCVOperand> mulOp2);
};

// 立即数传播优化
class ImmediatePropagationPass : public PeepPass
{
public:
    ImmediatePropagationPass() : PeepPass("ImmediatePropagation") {}

    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;

private:
    bool canUseImmediateForm(RISCVOpcode opcode);
    bool canPropagateSecondOperand(RISCVOpcode opcode);
    bool isValidImmediate(int64_t value, RISCVOpcode opcode);
    RISCVOpcode getImmediateOpcode(RISCVOpcode opcode);

    PeepOptiState performImmediatePropagation(
        shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb,
        vector<shared_ptr<RISCVInstruction>>::iterator currentIt,
        vector<shared_ptr<RISCVInstruction>>::iterator liIt,
        int64_t constant, RISCVOpcode immediateOpcode, int operandIndex);

    optional<tuple<vector<shared_ptr<RISCVInstruction>>::iterator, int64_t>>
    findLIInstruction(shared_ptr<RISCVRegister> targetReg,
                      vector<shared_ptr<RISCVInstruction>>::iterator currentIt,
                      vector<shared_ptr<RISCVInstruction>> &instrs);
};

// 融合连续的 I 型二元计算指令（例如: addi/andi/ori/xori/slli/srai ...）
// 支持将两条连续的同类 I 指令合并为一条（需保证第一个定义只有单一使用）
class FoldShiftSequencePass : public PeepPass
{
public:
    FoldShiftSequencePass() : PeepPass("FoldShiftSequence") {}

    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;

private:
    bool isArithmeticShiftOpcode(RISCVOpcode opc) const;
    bool isFusibleITypeOpcode(RISCVOpcode opc) const;
};
