#include "LICM.h"
#include <iostream>

void LICM::runLICM(shared_ptr<RISCVFunction> function)
{
    currentFunction = function;
    analyseLoops();
    hoistLoopInvariantInstructions();
}

void LICM::analyseLoops()
{
    loops.clear();
    auto &loopinfo = currentFunction->getLoopInfo();
    for (const auto &loop : loopinfo.getLoops())
    {
        if (!loop->getParentLoop())
        {
            loops.push_back(loop);
        }
    }
}

bool isTerminatorOpcode(RISCVOpcode op)
{
    return op == RISCVOpcode::BNE ||
           op == RISCVOpcode::BEQ ||
           op == RISCVOpcode::JAL ||
           op == RISCVOpcode::JALR ||
           op == RISCVOpcode::RET ||
           op == RISCVOpcode::CALL;
};

bool LICM::isLoopInvariant(shared_ptr<RISCVInstruction> inst)
{
    // 目前只能处理LA
    if (inst->getOpcode() == RISCVOpcode::LA)
    {
        return true;
    }

    return false;
}

void LICM::insertLAInst(shared_ptr<RISCVInstruction> laInst, shared_ptr<RISCVBasicBlock> preHeader)
{

    auto &preInsts = preHeader->getInstructions();
    size_t insertPos = preInsts.size();
    while (insertPos > 0)
    {
        auto &lastInst = preInsts[insertPos - 1];
        if (lastInst && isTerminatorOpcode(lastInst->getOpcode()))
        {
            --insertPos;
        }
        else if (lastInst && !isTerminatorOpcode(lastInst->getOpcode()))
        {
            break;
        }
    }

    preInsts.insert(preInsts.begin() + insertPos, laInst);
}

void LICM::mergeLAReg(shared_ptr<RISCVInstruction> keep, vector<shared_ptr<RISCVInstruction>> merges)
{
    for (auto inst : merges)
    {
        if (!inst)
            continue;

        if (inst->getOpcode() == RISCVOpcode::ADD || inst->getOpcode() == RISCVOpcode::ADDI)
        {
            inst->replaceOperand(1, keep->getOperands()[0]);
        }
        else if (inst->getOpcode() == RISCVOpcode::SW || inst->getOpcode() == RISCVOpcode::SD || inst->getOpcode() == RISCVOpcode::FSW || inst->getOpcode() == RISCVOpcode::FSD)
        {
            inst->replaceOperand(0, keep->getOperands()[0]);
        }
        else if (inst->getOpcode() == RISCVOpcode::LW || inst->getOpcode() == RISCVOpcode::LD || inst->getOpcode() == RISCVOpcode::FLW || inst->getOpcode() == RISCVOpcode::FLD)
        {
            inst->replaceOperand(1, keep->getOperands()[0]);
        }
    }
}

unordered_map<shared_ptr<RISCVInstruction>, vector<shared_ptr<RISCVInstruction>>> LICM::getInvariantMap(shared_ptr<RISCVLoop> loop)
{
    unordered_map<shared_ptr<RISCVInstruction>, vector<shared_ptr<RISCVInstruction>>> laMap;

    if (loop->getChildLoops().empty())
    {
        for (auto &bb : loop->getBlocks())
        {
            auto &insts = bb->getInstructions();
            for (size_t idx = 0; idx < insts.size(); ++idx)
            {
                if (isLoopInvariant(insts[idx]))
                {
                    auto nextInsts = vector<shared_ptr<RISCVInstruction>>();
                    for (int i = 1; i < 5; i++)
                    {
                        if (idx + i < bb->getInstructions().size())
                        {
                            auto inst = bb->getInstructions()[idx + i];
                            if (inst->getOpcode() == RISCVOpcode::ADD || inst->getOpcode() == RISCVOpcode::ADDI)
                            {
                                if (*insts[idx]->getOperands()[0]->getReg() == *inst->getOperands()[1]->getReg())
                                {
                                    nextInsts.push_back(bb->getInstructions()[idx + i]);
                                }
                            }
                            else if (inst->getOpcode() == RISCVOpcode::SW || inst->getOpcode() == RISCVOpcode::SD || inst->getOpcode() == RISCVOpcode::FSW || inst->getOpcode() == RISCVOpcode::FSD)
                            {
                                if (*insts[idx]->getOperands()[0]->getReg() == *inst->getOperands()[0]->getReg())
                                {
                                    nextInsts.push_back(bb->getInstructions()[idx + i]);
                                }
                            }
                            else if (inst->getOpcode() == RISCVOpcode::LW || inst->getOpcode() == RISCVOpcode::LD || inst->getOpcode() == RISCVOpcode::FLW || inst->getOpcode() == RISCVOpcode::FLD)
                            {
                                if (*insts[idx]->getOperands()[0]->getReg() == *inst->getOperands()[1]->getReg())
                                {
                                    nextInsts.push_back(bb->getInstructions()[idx + i]);
                                }
                            }
                        }
                    }

                    bool overLap = false;
                    for (auto &la : laMap)
                    {
                        if (la.first->getOperands()[1]->getLabel() == insts[idx]->getOperands()[1]->getLabel())
                        {
                            overLap = true;
                            la.second.insert(la.second.end(), nextInsts.begin(), nextInsts.end());
                        }
                    }

                    if (!overLap)
                    {
                        laMap[insts[idx]] = nextInsts;
                    }

                    insts.erase(insts.begin() + idx);
                    --idx;
                }
            }
        }
    }
    else
    {
        // 非子循环的blocks
        for (auto &bb : loop->getBlocksWithoutChildren())
        {
            auto &insts = bb->getInstructions();
            for (size_t idx = 0; idx < insts.size(); ++idx)
            {
                if (isLoopInvariant(insts[idx]))
                {
                    auto nextInsts = vector<shared_ptr<RISCVInstruction>>();
                    for (int i = 1; i < 5; i++)
                    {
                        if (idx + i < bb->getInstructions().size())
                        {
                            auto inst = bb->getInstructions()[idx + i];
                            if (inst->getOpcode() == RISCVOpcode::ADD || inst->getOpcode() == RISCVOpcode::ADDI)
                            {
                                if (*insts[idx]->getOperands()[0]->getReg() == *inst->getOperands()[1]->getReg())
                                {
                                    nextInsts.push_back(bb->getInstructions()[idx + i]);
                                }
                            }
                            else if (inst->getOpcode() == RISCVOpcode::SW || inst->getOpcode() == RISCVOpcode::SD || inst->getOpcode() == RISCVOpcode::FSW || inst->getOpcode() == RISCVOpcode::FSD)
                            {
                                if (*insts[idx]->getOperands()[0]->getReg() == *inst->getOperands()[0]->getReg())
                                {
                                    nextInsts.push_back(bb->getInstructions()[idx + i]);
                                }
                            }
                            else if (inst->getOpcode() == RISCVOpcode::LW || inst->getOpcode() == RISCVOpcode::LD || inst->getOpcode() == RISCVOpcode::FLW || inst->getOpcode() == RISCVOpcode::FLD)
                            {
                                if (*insts[idx]->getOperands()[0]->getReg() == *inst->getOperands()[1]->getReg())
                                {
                                    nextInsts.push_back(bb->getInstructions()[idx + i]);
                                }
                            }
                        }
                    }

                    bool overLap = false;
                    for (auto &la : laMap)
                    {
                        if (la.first->getOperands()[1]->getLabel() == insts[idx]->getOperands()[1]->getLabel())
                        {
                            overLap = true;
                            la.second.insert(la.second.end(), nextInsts.begin(), nextInsts.end());
                        }
                    }

                    if (!overLap)
                    {
                        laMap[insts[idx]] = nextInsts;
                    }

                    insts.erase(insts.begin() + idx);
                    --idx;
                }
            }
        }

        for (auto &l : loop->getChildLoops())
        {
            auto currentMap = getInvariantMap(l);
            for (auto &cm : currentMap)
            {
                bool overLap = false;
                for (auto &la : laMap)
                {
                    if (la.first->getOperands()[1]->getLabel() == cm.first->getOperands()[1]->getLabel())
                    {
                        overLap = true;
                        for (auto &inst : cm.second)
                        {
                            la.second.push_back(inst);
                        }
                    }
                }

                if (!overLap)
                {
                    laMap[cm.first] = cm.second;
                }
            }
        }
    }

    return laMap;
}

void printMap(unordered_map<shared_ptr<RISCVInstruction>, vector<shared_ptr<RISCVInstruction>>> &laMap)
{
    for (const auto &pair : laMap)
    {
        cout << "LA: " << pair.first->toString() << " -> ";
        for (const auto &inst : pair.second)
        {
            cout << inst->toString() << ", ";
        }
        cout << endl;
    }
}

void LICM::hoistLoopInvariantInstructions()
{
    for (auto &loop : loops)
    {
        auto invariantMap = getInvariantMap(loop);
        auto preHeader = loop->getPreHeader();

        // printMap(invariantMap);
        for (auto &la : invariantMap)
        {
            insertLAInst(la.first, preHeader);
            mergeLAReg(la.first, la.second);
        }
    }
}