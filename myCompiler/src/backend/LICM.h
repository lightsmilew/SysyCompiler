#pragma once
#include "RISCVDataStructure.h"

using namespace RISCV;
using std::cout;
using std::endl;
using std::set;
using std::unordered_map;

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
    unordered_map<shared_ptr<RISCVInstruction>, vector<shared_ptr<RISCVInstruction>>> getInvariantMap(shared_ptr<RISCVLoop> loop);

public:
    LICM() = default;

    void runLICM(shared_ptr<RISCVFunction> function);
};
