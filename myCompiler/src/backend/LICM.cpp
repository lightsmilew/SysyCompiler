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
    return op == RISCVOpcode::BEQ ||
           op == RISCVOpcode::BNE ||
           op == RISCVOpcode::BLT ||
           op == RISCVOpcode::BGE ||
           op == RISCVOpcode::BLTU ||
           op == RISCVOpcode::BGEU ||
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
    {
        auto operands = inst->getOperands();
        auto rd = operands.size() >= 1 ? operands[0]->getReg() : nullptr;
        return rd && rd->isVirtual() && operands.size() >= 2 && operands[1]->hasLabel();
    }
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

bool LICM::isChildLoopPreheader(const shared_ptr<RISCVLoop> &loop,
                                const shared_ptr<RISCVBasicBlock> &bb)
{
    if (!loop || !bb)
        return false;
    for (const auto &child : loop->getChildLoops())
    {
        if (!child)
            continue;
        auto childPre = child->getPreHeader();
        if (childPre && childPre == bb)
            return true;
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

namespace
{
    bool laInstUsesRegAsAddressOperand(const shared_ptr<RISCVInstruction> &inst,
                                       const shared_ptr<RISCVRegister> &laReg)
    {
        if (!inst || !laReg)
            return false;
        auto ops = inst->getOperands();
        switch (inst->getOpcode())
        {
        case RISCVOpcode::ADD:
        case RISCVOpcode::ADDI:
            return ops.size() >= 2 && ops[1]->getReg() && *ops[1]->getReg() == *laReg;
        case RISCVOpcode::SW:
        case RISCVOpcode::SD:
        case RISCVOpcode::FSW:
        case RISCVOpcode::FSD:
            return ops.size() >= 1 && ops[0]->getReg() && *ops[0]->getReg() == *laReg;
        case RISCVOpcode::LW:
        case RISCVOpcode::LD:
        case RISCVOpcode::FLW:
        case RISCVOpcode::FLD:
            return ops.size() >= 2 && ops[1]->getReg() && *ops[1]->getReg() == *laReg;
        case RISCVOpcode::MV:
            return ops.size() >= 2 && ops[1]->getReg() && *ops[1]->getReg() == *laReg;
        default:
            return false;
        }
    }

    void collectLaMergeUsers(const vector<shared_ptr<RISCVInstruction>> &insts,
                             size_t laIdx,
                             const shared_ptr<RISCVRegister> &laReg,
                             vector<shared_ptr<RISCVInstruction>> &out)
    {
        for (size_t i = laIdx + 1; i < insts.size(); ++i)
        {
            auto inst = insts[i];
            if (!inst)
                continue;
            if (!laInstUsesRegAsAddressOperand(inst, laReg))
                continue;
            out.push_back(inst);
            for (const auto &def : inst->getDefRegisters())
            {
                if (def && *def == *laReg)
                    return;
            }
        }
    }

    bool hasDefOfRegLaterInSameBlock(const shared_ptr<RISCVBasicBlock> &bb,
                                     const shared_ptr<RISCVInstruction> &inst,
                                     const shared_ptr<RISCVRegister> &reg)
    {
        if (!bb || !inst || !reg)
            return false;
        bool seen = false;
        for (const auto &other : bb->getInstructions())
        {
            if (!other)
                continue;
            if (other == inst)
            {
                seen = true;
                continue;
            }
            if (!seen)
                continue;
            for (const auto &def : other->getDefRegisters())
            {
                if (def && *def == *reg)
                    return true;
            }
        }
        return false;
    }
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

    if (inst->getOpcode() == RISCVOpcode::LI || inst->getOpcode() == RISCVOpcode::LA)
    {
        auto defs = inst->getDefRegisters();
        if (defs.empty() || !defs[0])
            return false;
        // 同块内 li/la 之后若还有指令改写 rd（如 la t2,a; add t2,t2,off），不能外提
        if (hasDefOfRegLaterInSameBlock(bb, inst, defs[0]))
            return false;
    }

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
        // 子循环 preheader 不在子循环块集合内，会被父循环误当作“非子循环块”；
        // 若在此收集 la 会外提到祖先 preheader，基址寄存器跨子循环存活并被踩坏（mm/matmul）。
        if (isChildLoopPreheader(loop, bb))
            continue;
        auto &insts = bb->getInstructions();
        for (size_t idx = 0; idx < insts.size(); ++idx)
        {
            if (!canHoistInvariantInst(insts[idx], bb, loop))
                continue;

            // li 只做单条外提；la 才收集紧随其后的地址计算/访存以便 mergeLAReg
            if (insts[idx]->getOpcode() == RISCVOpcode::LI)
            {
                laMap[insts[idx]] = {};
                insts.erase(insts.begin() + idx);
                --idx;
                continue;
            }

            auto nextInsts = vector<shared_ptr<RISCVInstruction>>();
            collectLaMergeUsers(insts, idx, insts[idx]->getOperands()[0]->getReg(), nextInsts);

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
