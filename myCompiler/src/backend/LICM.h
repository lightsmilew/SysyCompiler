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

        // 只基于操作码和标签，忽略寄存器
        string key = "";
        key += static_cast<int>(inst->getOpcode());
        if (inst->getOperands().size() > 1 && inst->getOperands()[1]->hasLabel())
        {
            key += inst->getOperands()[1]->getLabel();
        }
        return hash<string>()(key);
    }
};
struct InstructionEqual
{
    bool operator()(const shared_ptr<RISCVInstruction> &a,
                    const shared_ptr<RISCVInstruction> &b) const
    {
        return a->toString() == b->toString();
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
