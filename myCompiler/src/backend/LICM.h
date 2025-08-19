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

        // 对于LA和LI指令，只基于标签/立即数计算哈希，忽略寄存器
        if (inst->getOpcode() == RISCVOpcode::LA || inst->getOpcode() == RISCVOpcode::LI)
        {
            auto operands = inst->getOperands();
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

        // 对于LA和LI指令，只比较标签/立即数，忽略目标寄存器
        // LA指令格式: la rd, label
        // LI指令格式: li rd, imm
        if (a->getOpcode() == RISCVOpcode::LA || a->getOpcode() == RISCVOpcode::LI)
        {
            auto aOperands = a->getOperands();
            auto bOperands = b->getOperands();

            if (aOperands.size() != bOperands.size())
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
    bool isLoopInvariant(shared_ptr<RISCVInstruction> inst);
    void insertLAInst(shared_ptr<RISCVInstruction> laInst, shared_ptr<RISCVBasicBlock> preHeader);
    void mergeLAReg(shared_ptr<RISCVInstruction> keep, vector<shared_ptr<RISCVInstruction>> merges);
    unordered_map<shared_ptr<RISCVInstruction>,
                  vector<shared_ptr<RISCVInstruction>>,
                  InstructionHash, InstructionEqual>
    getInvariantMap(shared_ptr<RISCVLoop> loop);

public:
    LICM() = default;

    void runLICM(shared_ptr<RISCVFunction> function);
};
