#include "LiLocalCSE.h"
#include <algorithm>

using std::optional;
using std::unordered_map;
using std::vector;

namespace RISCV
{
    bool LiLocalCSE::ExprKey::operator==(const ExprKey &o) const
    {
        return opcode == o.opcode && op1 == o.op1 && op2 == o.op2 && hasImm == o.hasImm &&
               (!hasImm || imm == o.imm) && usesSp == o.usesSp;
    }

    size_t LiLocalCSE::ExprKeyHash::operator()(const ExprKey &k) const
    {
        size_t h = std::hash<int>()(static_cast<int>(k.opcode));
        h ^= std::hash<std::string>()(k.op1) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>()(k.op2) + 0x9e3779b9 + (h << 6) + (h >> 2);
        if (k.hasImm)
            h ^= std::hash<int64_t>()(k.imm) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>()(k.usesSp);
        return h;
    }

    shared_ptr<RISCVRegister> LiLocalCSE::getSpRegister()
    {
        static auto sp = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);
        return sp;
    }

    shared_ptr<RISCVRegister> LiLocalCSE::getDestReg(const shared_ptr<RISCVInstruction> &inst)
    {
        if (!inst)
            return nullptr;
        auto ops = inst->getOperands();
        if (ops.empty() || !ops[0]->getReg())
            return nullptr;
        return ops[0]->getReg();
    }

    string LiLocalCSE::regKey(const shared_ptr<RISCVRegister> &reg)
    {
        return reg ? reg->toString() : "";
    }

    bool LiLocalCSE::isSpRegister(const shared_ptr<RISCVRegister> &reg)
    {
        return reg && reg->isPhysical() &&
               reg->getPhysicalReg() == RISCVRegister::PhysicalReg::SP;
    }

    bool LiLocalCSE::isCSEableOpcode(RISCVOpcode op)
    {
        switch (op)
        {
        case RISCVOpcode::LI:
        case RISCVOpcode::ADD:
        case RISCVOpcode::ADDI:
        case RISCVOpcode::ADDW:
        case RISCVOpcode::ADDIW:
        case RISCVOpcode::SUB:
        case RISCVOpcode::SUBW:
        case RISCVOpcode::MUL:
        case RISCVOpcode::MULW:
        case RISCVOpcode::AND:
        case RISCVOpcode::ANDI:
        case RISCVOpcode::OR:
        case RISCVOpcode::ORI:
        case RISCVOpcode::XOR:
        case RISCVOpcode::XORI:
        case RISCVOpcode::SLL:
        case RISCVOpcode::SLLI:
        case RISCVOpcode::SLLW:
        case RISCVOpcode::SLLIW:
        case RISCVOpcode::SRL:
        case RISCVOpcode::SRLI:
        case RISCVOpcode::SRLW:
        case RISCVOpcode::SRA:
        case RISCVOpcode::SRAI:
        case RISCVOpcode::SRAW:
        case RISCVOpcode::SLTI:
        case RISCVOpcode::SLTIU:
            return true;
        default:
            return false;
        }
    }

    shared_ptr<RISCVLoop> LiLocalCSE::findInnermostLoop(const LoopInfo &loopInfo,
                                                        shared_ptr<RISCVBasicBlock> bb)
    {
        shared_ptr<RISCVLoop> innermost;
        int maxDepth = -1;
        for (const auto &loop : loopInfo.getLoops())
        {
            if (!loop || !bb || !loop->containsBlock(bb))
                continue;
            const int depth = loop->getDepth();
            if (depth > maxDepth)
            {
                maxDepth = depth;
                innermost = loop;
            }
        }
        return innermost;
    }

    bool LiLocalCSE::isOnlyDefOfDestInBlocks(const shared_ptr<RISCVInstruction> &inst,
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

    bool LiLocalCSE::isLoopInductionRelated(shared_ptr<RISCVBasicBlock> bb,
                                            const shared_ptr<RISCVInstruction> &inst,
                                            const shared_ptr<RISCVLoop> &loop)
    {
        if (!inst || !loop || !bb)
            return false;

        if (!isOnlyDefOfDestInBlocks(inst, loop->getBlocks()))
            return true;

        // 循环头内的 li 每轮迭代都会执行，不能当作可复用的不变量
        if (inst->getOpcode() == RISCVOpcode::LI && loop->getHeader() && bb == loop->getHeader())
            return true;

        return false;
    }

    string LiLocalCSE::operandKey(const vector<shared_ptr<RISCVInstruction>> &insts, size_t idx,
                                  const shared_ptr<RISCVRegister> &reg)
    {
        if (!reg)
            return "";
        if (idx > 0)
        {
            const auto &prev = insts[idx - 1];
            if (prev && prev->getOpcode() == RISCVOpcode::MV)
            {
                auto ops = prev->getOperands();
                if (ops.size() >= 2 && ops[0]->getReg() && ops[1]->getReg() &&
                    *ops[0]->getReg() == *reg)
                {
                    auto src = ops[1]->getReg();
                    // 勿穿透到 a0 等物理寄存器：call 后其值已变，且 getDefRegisters(call) 不含 caller-saved
                    if (src && src->isVirtual())
                        return operandKey(insts, idx - 1, src);
                }
            }
        }
        return regKey(reg);
    }

    optional<LiLocalCSE::ExprKey> LiLocalCSE::buildExprKey(
        const vector<shared_ptr<RISCVInstruction>> &insts, size_t idx,
        const shared_ptr<RISCVInstruction> &inst, shared_ptr<RISCVRegister> &outRd)
    {
        outRd = nullptr;
        if (!inst || !isCSEableOpcode(inst->getOpcode()))
            return std::nullopt;

        auto ops = inst->getOperands();
        if (ops.empty() || !ops[0]->getReg())
            return std::nullopt;

        outRd = ops[0]->getReg();
        if (!outRd->isVirtual())
            return std::nullopt;

        ExprKey key;
        key.opcode = inst->getOpcode();

        if (key.opcode == RISCVOpcode::LI)
        {
            if (ops.size() < 2 || ops[1]->getType() != RISCVOperand::Type::IMMEDIATE)
                return std::nullopt;
            key.hasImm = true;
            key.imm = ops[1]->getImmediate();
            return key;
        }

        switch (inst->getInstrType())
        {
        case InstructionType::R_TYPE:
            if (ops.size() < 3)
                return std::nullopt;
            if (!ops[1]->getReg() || !ops[2]->getReg())
                return std::nullopt;
            key.op1 = operandKey(insts, idx, ops[1]->getReg());
            key.op2 = operandKey(insts, idx, ops[2]->getReg());
            key.usesSp = isSpRegister(ops[1]->getReg()) || isSpRegister(ops[2]->getReg());
            return key;

        case InstructionType::I_TYPE:
            if (ops.size() < 3)
                return std::nullopt;
            if (!ops[1]->getReg())
                return std::nullopt;
            key.op1 = operandKey(insts, idx, ops[1]->getReg());
            key.op2.clear();
            if (ops[2]->getType() == RISCVOperand::Type::IMMEDIATE)
            {
                key.hasImm = true;
                key.imm = ops[2]->getImmediate();
            }
            else
            {
                return std::nullopt;
            }
            key.usesSp = isSpRegister(ops[1]->getReg());
            return key;

        default:
            return std::nullopt;
        }
    }

    bool LiLocalCSE::isRegDefinedSince(const vector<shared_ptr<RISCVInstruction>> &insts, size_t fromIdx,
                                       size_t toIdx, const shared_ptr<RISCVRegister> &reg)
    {
        if (!reg || fromIdx >= toIdx)
            return false;
        const size_t end = std::min(toIdx, insts.size());
        for (size_t i = fromIdx + 1; i < end; ++i)
        {
            const auto &inst = insts[i];
            if (!inst)
                continue;
            for (const auto &def : inst->getDefRegisters())
            {
                if (def && *def == *reg)
                    return true;
            }
        }
        return false;
    }

    bool LiLocalCSE::isAvailEntryLive(const vector<shared_ptr<RISCVInstruction>> &insts, size_t useIdx,
                                      const AvailEntry &entry)
    {
        if (!entry.defInst)
            return false;
        auto canon = getDestReg(entry.defInst);
        if (!canon || !canon->isVirtual())
            return false;
        return !isRegDefinedSince(insts, entry.defIdx, useIdx, canon);
    }

    bool LiLocalCSE::allUsesReplaceable(const vector<shared_ptr<RISCVInstruction>> &insts,
                                        size_t dupIdx, size_t canonDefIdx,
                                        const shared_ptr<RISCVRegister> &dupRd,
                                        const shared_ptr<RISCVRegister> &canon)
    {
        if (!dupRd || !canon)
            return false;

        for (size_t j = dupIdx + 1; j < insts.size(); ++j)
        {
            const auto &user = insts[j];
            if (!user)
                continue;
            for (const auto &use : user->getUseRegisters())
            {
                if (!use || !(*use == *dupRd))
                    continue;
                if (isRegDefinedSince(insts, canonDefIdx, j, canon))
                    return false;
            }
        }
        return true;
    }

    bool LiLocalCSE::hasUseOutsideBlock(shared_ptr<RISCVFunction> function,
                                        shared_ptr<RISCVBasicBlock> bb,
                                        const shared_ptr<RISCVRegister> &reg)
    {
        if (!function || !reg)
            return false;
        for (auto &otherBB : function->getBasicBlocks())
        {
            if (!otherBB || otherBB == bb)
                continue;
            for (const auto &inst : otherBB->getInstructions())
            {
                if (!inst)
                    continue;
                for (const auto &use : inst->getUseRegisters())
                {
                    if (use && *use == *reg)
                        return true;
                }
            }
        }
        return false;
    }

    void LiLocalCSE::replaceUsesWithCanon(const vector<shared_ptr<RISCVInstruction>> &insts,
                                          size_t dupIdx, size_t canonDefIdx,
                                          const shared_ptr<RISCVRegister> &dupRd,
                                          const shared_ptr<RISCVRegister> &canon)
    {
        for (size_t j = dupIdx + 1; j < insts.size(); ++j)
        {
            auto &user = insts[j];
            if (!user)
                continue;

            if (isRegDefinedSince(insts, canonDefIdx, j, canon))
                continue;

            for (const auto &use : user->getUseRegisters())
            {
                if (!use || !dupRd || !(*use == *dupRd))
                    continue;
                user->replaceUseRegister(use, canon);
            }
        }
    }

    void LiLocalCSE::decrementDefIdxAfter(unordered_map<ExprKey, AvailEntry, ExprKeyHash> &avail,
                                          size_t erasedIdx)
    {
        for (auto &entry : avail)
        {
            if (entry.second.defIdx > erasedIdx)
                --entry.second.defIdx;
        }
    }

    void LiLocalCSE::invalidateDefsOfInst(unordered_map<ExprKey, AvailEntry, ExprKeyHash> &avail,
                                        const shared_ptr<RISCVInstruction> &producer)
    {
        if (!producer)
            return;
        for (const auto &reg : producer->getDefRegisters())
        {
            if (!reg)
                continue;
            const string rk = regKey(reg);
            for (auto it = avail.begin(); it != avail.end();)
            {
                const auto &k = it->first;
                auto canon = getDestReg(it->second.defInst);
                if (k.op1 == rk || k.op2 == rk)
                {
                    it = avail.erase(it);
                    continue;
                }
                // 结果寄存器被改写：若改写者就是该表达式的定义指令本身，则值刚写入，不失效
                if (canon && *canon == *reg)
                {
                    if (it->second.defInst == producer)
                    {
                        ++it;
                        continue;
                    }
                    it = avail.erase(it);
                    continue;
                }
                ++it;
            }
        }
    }

    void LiLocalCSE::invalidateForSp(unordered_map<ExprKey, AvailEntry, ExprKeyHash> &avail,
                                     int &spVersion)
    {
        ++spVersion;
        for (auto it = avail.begin(); it != avail.end();)
        {
            if (it->first.usesSp)
                it = avail.erase(it);
            else
                ++it;
        }
    }

    bool LiLocalCSE::isSpDefinedBy(const shared_ptr<RISCVInstruction> &inst)
    {
        if (!inst)
            return false;
        for (const auto &def : inst->getDefRegisters())
        {
            if (isSpRegister(def))
                return true;
        }
        return false;
    }

    bool LiLocalCSE::optimizeFunction(shared_ptr<RISCVFunction> function)
    {
        if (!function)
            return false;

        bool changed = false;
        const LoopInfo &loopInfo = function->getLoopInfo();
        unordered_map<shared_ptr<RISCVBasicBlock>, shared_ptr<RISCVLoop>> bbInnerLoop;
        for (auto &bb : function->getBasicBlocks())
        {
            if (bb)
                bbInnerLoop[bb] = findInnermostLoop(loopInfo, bb);
        }

        for (auto &bb : function->getBasicBlocks())
        {
            unordered_map<ExprKey, AvailEntry, ExprKeyHash> avail;
            int spVersion = 0;
            auto &insts = bb->getInstructions();
            const shared_ptr<RISCVLoop> innerLoop = bb ? bbInnerLoop[bb] : nullptr;

            for (size_t idx = 0; idx < insts.size(); ++idx)
            {
                auto &inst = insts[idx];
                if (!inst)
                    continue;

                if (idx > 0 && insts[idx - 1])
                {
                    invalidateDefsOfInst(avail, insts[idx - 1]);
                }

                if (isSpDefinedBy(inst))
                {
                    invalidateForSp(avail, spVersion);
                }

                if (inst->getOpcode() == RISCVOpcode::CALL)
                {
                    avail.clear();
                }

                if (isLoopInductionRelated(bb, inst, innerLoop))
                    continue;

                shared_ptr<RISCVRegister> rd;
                auto keyOpt = buildExprKey(insts, idx, inst, rd);
                bool handled = false;
                if (keyOpt && rd)
                {
                    auto it = avail.find(*keyOpt);
                    if (it != avail.end())
                    {
                        if (isAvailEntryLive(insts, idx, it->second) &&
                            (!keyOpt->usesSp || it->second.spVersion == spVersion))
                        {
                            auto canon = getDestReg(it->second.defInst);
                            if (canon && !hasUseOutsideBlock(function, bb, rd) &&
                                allUsesReplaceable(insts, idx, it->second.defIdx, rd, canon))
                            {
                                replaceUsesWithCanon(insts, idx, it->second.defIdx, rd, canon);
                                insts.erase(insts.begin() + static_cast<long>(idx));
                                decrementDefIdxAfter(avail, idx);
                                changed = true;
                                handled = true;
                                --idx;
                            }
                            else
                            {
                                avail.erase(it);
                            }
                        }
                        else
                        {
                            avail.erase(it);
                        }
                    }

                    if (!handled)
                    {
                        AvailEntry entry;
                        entry.defInst = inst;
                        entry.defIdx = idx;
                        entry.spVersion = spVersion;
                        avail[*keyOpt] = entry;
                    }
                }
            }

            if (!insts.empty() && insts.back())
            {
                invalidateDefsOfInst(avail, insts.back());
            }
        }

        return changed;
    }

    void LiLocalCSE::run(shared_ptr<RISCVFunction> function)
    {
        if (!function)
            return;
        // 参考 CSEPass::do-while(localChanged)：反复扫描直至不动点
        while (optimizeFunction(function))
        {
        }
    }
}
