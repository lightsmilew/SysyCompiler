#pragma once
#include "RISCVDataStructure.h"

using namespace RISCV;
using std::cout;
using std::endl;
using std::set;
using std::unordered_map;

struct InstructionHash
{
    size_t operator()(const shared_ptr<RISCVInstruction> &inst) const
    {
        if (!inst)
            return 0;

        // 基于操作码计算哈希
        string key = to_string(static_cast<int>(inst->getOpcode()));

        // LA/LI 均按 rd + 标签/立即数
        if (inst->getOpcode() == RISCVOpcode::LA || inst->getOpcode() == RISCVOpcode::LI)
        {
            auto operands = inst->getOperands();
            if (operands.size() >= 1 && operands[0]->getReg())
            {
                key += operands[0]->getReg()->toString();
            }
            if (operands.size() >= 2)
            {
                if (operands[1]->hasLabel())
                {
                    key += operands[1]->getLabel();
                }
                else if (operands[1]->hasImm())
                {
                    key += to_string(operands[1]->getImmediate());
                }
            }
        }
        else
        {
            // 对于其他指令，使用完整的toString
            key += inst->toString();
        }

        return hash<string>()(key);
    }
};
struct InstructionEqual
{
    bool operator()(const shared_ptr<RISCVInstruction> &a,
                    const shared_ptr<RISCVInstruction> &b) const
    {
        if (!a && !b)
            return true;
        if (!a || !b)
            return false;
        if (a->getOpcode() != b->getOpcode())
            return false;

        if (a->getOpcode() == RISCVOpcode::LA || a->getOpcode() == RISCVOpcode::LI)
        {
            auto aOperands = a->getOperands();
            auto bOperands = b->getOperands();

            if (aOperands.size() != bOperands.size())
                return false;

            auto aRd = aOperands.size() >= 1 ? aOperands[0]->getReg() : nullptr;
            auto bRd = bOperands.size() >= 1 ? bOperands[0]->getReg() : nullptr;
            if (!aRd || !bRd || !(*aRd == *bRd))
                return false;

            if (aOperands.size() >= 2)
            {
                if (aOperands[1]->hasLabel() && bOperands[1]->hasLabel())
                {
                    return aOperands[1]->getLabel() == bOperands[1]->getLabel();
                }
                else if (aOperands[1]->getType() == RISCVOperand::Type::IMMEDIATE &&
                         bOperands[1]->getType() == RISCVOperand::Type::IMMEDIATE)
                {
                    return aOperands[1]->getImmediate() == bOperands[1]->getImmediate();
                }
            }
        }

        return false;
    }
};

class LICM
{
private:
    shared_ptr<RISCVFunction> currentFunction;
    vector<shared_ptr<RISCVLoop>> loops; // 从内到外存储所有的循环
    void analyseLoops();
    void hoistLoopInvariantInstructions();
    void hoistForLoop(shared_ptr<RISCVLoop> loop);
    static bool isLoopInvariant(shared_ptr<RISCVInstruction> inst);
    void insertHoistedInst(shared_ptr<RISCVInstruction> inst, shared_ptr<RISCVBasicBlock> preHeader);
    static bool sameInvariantKey(const shared_ptr<RISCVInstruction> &a,
                                 const shared_ptr<RISCVInstruction> &b);
    static bool isOnlyDefOfDestInBlocks(const shared_ptr<RISCVInstruction> &inst,
                                        const vector<shared_ptr<RISCVBasicBlock>> &blocks);
    static bool canHoistInvariantInst(const shared_ptr<RISCVInstruction> &inst,
                                      const shared_ptr<RISCVBasicBlock> &bb,
                                      const shared_ptr<RISCVLoop> &loop);
    void mergeLAReg(shared_ptr<RISCVInstruction> keep, vector<shared_ptr<RISCVInstruction>> merges);
    void collectInvariantsInBlocks(
        const vector<shared_ptr<RISCVBasicBlock>> &scopeBlocks,
        const shared_ptr<RISCVLoop> &loop,
        unordered_map<shared_ptr<RISCVInstruction>,
                      vector<shared_ptr<RISCVInstruction>>,
                      InstructionHash, InstructionEqual> &laMap);
    unordered_map<shared_ptr<RISCVInstruction>,
                  vector<shared_ptr<RISCVInstruction>>,
                  InstructionHash, InstructionEqual>
    getInvariantMap(shared_ptr<RISCVLoop> loop);

public:
    LICM() = default;

    void runLICM(shared_ptr<RISCVFunction> function);
};
