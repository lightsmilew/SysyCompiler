#include "LICM.h"
#include <iostream>
#include <map>
using std::map;

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
    if (!inst)
        return false;
    if (inst->getOpcode() == RISCVOpcode::LA)
        return true;
    if (inst->getOpcode() == RISCVOpcode::LI)
    {
        auto operands = inst->getOperands();
        auto rd = operands.size() >= 1 ? operands[0]->getReg() : nullptr;
        // 物理临时寄存器假定“加载后立即使用”，不能外提到 preheader
        return rd && rd->isVirtual() && operands.size() >= 2 &&
               operands[1]->getType() == RISCVOperand::Type::IMMEDIATE;
    }
    return false;
}

bool LICM::sameInvariantKey(const shared_ptr<RISCVInstruction> &a,
                            const shared_ptr<RISCVInstruction> &b)
{
    if (!a || !b || a->getOpcode() != b->getOpcode())
        return false;
    auto aOps = a->getOperands();
    auto bOps = b->getOperands();
    if (aOps.size() < 2 || bOps.size() < 2)
        return false;
    if (a->getOpcode() == RISCVOpcode::LA)
    {
        auto aRd = aOps[0]->getReg();
        auto bRd = bOps[0]->getReg();
        if (!aRd || !bRd || !(*aRd == *bRd))
            return false;
        return aOps[1]->hasLabel() && bOps[1]->hasLabel() &&
               aOps[1]->getLabel() == bOps[1]->getLabel();
    }
    if (a->getOpcode() == RISCVOpcode::LI)
    {
        auto aRd = aOps[0]->getReg();
        auto bRd = bOps[0]->getReg();
        if (!aRd || !bRd || !(*aRd == *bRd))
            return false;
        return aOps[1]->getType() == RISCVOperand::Type::IMMEDIATE &&
               bOps[1]->getType() == RISCVOperand::Type::IMMEDIATE &&
               aOps[1]->getImmediate() == bOps[1]->getImmediate();
    }
    return false;
}

bool LICM::isOnlyDefOfDestInBlocks(const shared_ptr<RISCVInstruction> &inst,
                                   const vector<shared_ptr<RISCVBasicBlock>> &blocks)
{
    if (!inst)
        return false;
    auto defs = inst->getDefRegisters();
    if (defs.empty() || !defs[0])
        return false;
    const auto &destReg = defs[0];

    for (const auto &bb : blocks)
    {
        if (!bb)
            continue;
        for (const auto &other : bb->getInstructions())
        {
            if (!other || other == inst)
                continue;
            for (const auto &d : other->getDefRegisters())
            {
                if (d && *d == *destReg)
                    return false;
            }
        }
    }
    return true;
}

bool LICM::canHoistInvariantInst(const shared_ptr<RISCVInstruction> &inst,
                                 const shared_ptr<RISCVBasicBlock> &bb,
                                 const shared_ptr<RISCVLoop> &loop)
{
    if (!inst || !loop || !bb)
        return false;
    if (!isLoopInvariant(inst))
        return false;

    const auto loopBlocks = loop->getBlocks();
    if (!isOnlyDefOfDestInBlocks(inst, loopBlocks))
        return false;

    // 循环头内的 li 每轮迭代都会执行；外提到 preheader 只执行一次，等同错误外提归纳变量初值
    if (inst->getOpcode() == RISCVOpcode::LI && loop->getHeader() && bb == loop->getHeader())
        return false;

    return true;
}

void LICM::insertHoistedInst(shared_ptr<RISCVInstruction> laInst, shared_ptr<RISCVBasicBlock> preHeader)
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
        else if (inst->getOpcode() == RISCVOpcode::MV)
        {
            inst->replaceOperand(1, keep->getOperands()[0]);
        }
    }
}

void LICM::collectInvariantsInBlocks(
    const vector<shared_ptr<RISCVBasicBlock>> &scopeBlocks,
    const shared_ptr<RISCVLoop> &loop,
    unordered_map<shared_ptr<RISCVInstruction>,
                  vector<shared_ptr<RISCVInstruction>>,
                  InstructionHash, InstructionEqual> &laMap)
{
    for (auto &bb : scopeBlocks)
    {
        if (!bb)
            continue;
        auto &insts = bb->getInstructions();
        for (size_t idx = 0; idx < insts.size(); ++idx)
        {
            if (!canHoistInvariantInst(insts[idx], bb, loop))
                continue;

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
                    else if (inst->getOpcode() == RISCVOpcode::SW || inst->getOpcode() == RISCVOpcode::SD ||
                             inst->getOpcode() == RISCVOpcode::FSW || inst->getOpcode() == RISCVOpcode::FSD)
                    {
                        if (*insts[idx]->getOperands()[0]->getReg() == *inst->getOperands()[0]->getReg())
                        {
                            nextInsts.push_back(bb->getInstructions()[idx + i]);
                        }
                    }
                    else if (inst->getOpcode() == RISCVOpcode::LW || inst->getOpcode() == RISCVOpcode::LD ||
                             inst->getOpcode() == RISCVOpcode::FLW || inst->getOpcode() == RISCVOpcode::FLD)
                    {
                        if (*insts[idx]->getOperands()[0]->getReg() == *inst->getOperands()[1]->getReg())
                        {
                            nextInsts.push_back(bb->getInstructions()[idx + i]);
                        }
                    }
                    else if (inst->getOpcode() == RISCVOpcode::MV)
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
                if (sameInvariantKey(la.first, insts[idx]))
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

unordered_map<shared_ptr<RISCVInstruction>,
              vector<shared_ptr<RISCVInstruction>>,
              InstructionHash, InstructionEqual>
LICM::getInvariantMap(shared_ptr<RISCVLoop> loop)
{
    unordered_map<shared_ptr<RISCVInstruction>,
                  vector<shared_ptr<RISCVInstruction>>,
                  InstructionHash, InstructionEqual>
        laMap;

    if (loop->getChildLoops().empty())
    {
        collectInvariantsInBlocks(loop->getBlocks(), loop, laMap);
    }
    else
    {
        collectInvariantsInBlocks(loop->getBlocksWithoutChildren(), loop, laMap);
    }

    return laMap;
}

void LICM::hoistForLoop(shared_ptr<RISCVLoop> loop)
{
    if (!loop)
        return;

    // 先处理子循环，各自外提到子循环 preheader
    for (const auto &child : loop->getChildLoops())
    {
        hoistForLoop(child);
    }

    auto preHeader = loop->getPreHeader();
    if (!preHeader)
        return;

    auto invariantMap = getInvariantMap(loop);
    for (auto &entry : invariantMap)
    {
        insertHoistedInst(entry.first, preHeader);
        if (entry.first->getOpcode() == RISCVOpcode::LA)
        {
            mergeLAReg(entry.first, entry.second);
        }
    }
}

void LICM::hoistLoopInvariantInstructions()
{
    for (auto &loop : loops)
    {
        hoistForLoop(loop);
    }
}
