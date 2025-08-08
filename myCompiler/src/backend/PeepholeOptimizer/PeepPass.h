#pragma once

#include "PeepOptimizationManager.h"
using std::dynamic_pointer_cast;
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

// 指令强度削弱
class StrengthReductionPass : public PeepPass
{
public:
    StrengthReductionPass() : PeepPass("StrengthReduction") {}

    PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) override;

private:
    bool isPowerOfTwoMul(shared_ptr<RISCVInstruction> instr, int &shiftAmount);
    shared_ptr<RISCVInstruction> createShiftInstruction(shared_ptr<RISCVRegister> dst, shared_ptr<RISCVRegister> src, int shiftAmount);
};