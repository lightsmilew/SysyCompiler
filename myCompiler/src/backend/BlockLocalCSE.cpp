#include "BlockLocalCSE.h"
#include <algorithm>

using std::optional;
using std::unordered_map;
using std::vector;

namespace RISCV
{
    bool BlockLocalCSE::ExprKey::operator==(const ExprKey &o) const
    {
        return opcode == o.opcode && op1 == o.op1 && op2 == o.op2 && hasImm == o.hasImm &&
               (!hasImm || imm == o.imm) && usesSp == o.usesSp;
    }

    size_t BlockLocalCSE::ExprKeyHash::operator()(const ExprKey &k) const
    {
        size_t h = std::hash<int>()(static_cast<int>(k.opcode));
        h ^= std::hash<std::string>()(k.op1) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>()(k.op2) + 0x9e3779b9 + (h << 6) + (h >> 2);
        if (k.hasImm)
            h ^= std::hash<int64_t>()(k.imm) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<bool>()(k.usesSp);
        return h;
    }

    bool BlockLocalCSE::MaterialKey::operator==(const MaterialKey &o) const
    {
        if (opcode != o.opcode)
            return false;
        if (opcode == RISCVOpcode::LI)
            return hasImm == o.hasImm && (!hasImm || imm == o.imm);
        if (opcode == RISCVOpcode::LA)
            return label == o.label;
        return false;
    }

    size_t BlockLocalCSE::MaterialKeyHash::operator()(const MaterialKey &k) const
    {
        size_t h = std::hash<int>()(static_cast<int>(k.opcode));
        if (k.hasImm)
            h ^= std::hash<int64_t>()(k.imm) + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<std::string>()(k.label) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }

    shared_ptr<RISCVRegister> BlockLocalCSE::getDestReg(const shared_ptr<RISCVInstruction> &inst)
    {
        if (!inst)
            return nullptr;
        auto ops = inst->getOperands();
        if (ops.empty() || !ops[0]->getReg())
            return nullptr;
        return ops[0]->getReg();
    }

    string BlockLocalCSE::regKey(const shared_ptr<RISCVRegister> &reg)
    {
        return reg ? reg->toString() : "";
    }

    bool BlockLocalCSE::isSpRegister(const shared_ptr<RISCVRegister> &reg)
    {
        return reg && reg->isPhysical() &&
               reg->getPhysicalReg() == RISCVRegister::PhysicalReg::SP;
    }

    bool BlockLocalCSE::isCSEableOpcode(RISCVOpcode op)
    {
        switch (op)
        {
        case RISCVOpcode::ADD:
        case RISCVOpcode::ADDI:
        case RISCVOpcode::ADDW:
        case RISCVOpcode::ADDIW:
        case RISCVOpcode::SUB:
        case RISCVOpcode::SUBW:
        // 不 CSE mul/mulw：mul rd,rs,rd 与 stride 链上常重复 li+mul，误合并会删掉后续 la 仍用的偏移
        // case RISCVOpcode::MUL:
        // case RISCVOpcode::MULW:
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

    optional<BlockLocalCSE::MaterialKey> BlockLocalCSE::buildMaterialKey(
        const shared_ptr<RISCVInstruction> &inst, shared_ptr<RISCVRegister> &outRd)
    {
        outRd = nullptr;
        if (!inst)
            return std::nullopt;
        if (inst->isCopyInitLi())
            return std::nullopt;
        const auto op = inst->getOpcode();
        if (op != RISCVOpcode::LI && op != RISCVOpcode::LA)
            return std::nullopt;
        auto ops = inst->getOperands();
        if (ops.empty() || !ops[0]->getReg() || !ops[0]->getReg()->isVirtual())
            return std::nullopt;
        outRd = ops[0]->getReg();

        MaterialKey key;
        key.opcode = op;
        if (op == RISCVOpcode::LI)
        {
            if (ops.size() < 2 || ops[1]->getType() != RISCVOperand::Type::IMMEDIATE)
                return std::nullopt;
            key.hasImm = true;
            key.imm = ops[1]->getImmediate();
            return key;
        }
        if (ops.size() < 2 || !ops[1]->hasLabel())
            return std::nullopt;
        key.label = ops[1]->getLabel();
        return key;
    }

    void BlockLocalCSE::invalidateMaterialAvail(
        unordered_map<MaterialKey, AvailEntry, MaterialKeyHash> &materialAvail,
        const shared_ptr<RISCVInstruction> &producer)
    {
        if (!producer)
            return;
        for (const auto &reg : producer->getDefRegisters())
        {
            if (!reg)
                continue;
            for (auto it = materialAvail.begin(); it != materialAvail.end();)
            {
                auto canon = getDestReg(it->second.defInst);
                if (canon && *canon == *reg)
                {
                    if (it->second.defInst == producer)
                    {
                        ++it;
                        continue;
                    }
                    it = materialAvail.erase(it);
                    continue;
                }
                ++it;
            }
        }
    }

    void BlockLocalCSE::decrementMaterialDefIdxAfter(
        unordered_map<MaterialKey, AvailEntry, MaterialKeyHash> &materialAvail,
        shared_ptr<RISCVBasicBlock> bb, size_t erasedIdx)
    {
        for (auto &entry : materialAvail)
        {
            if (entry.second.defBB == bb && entry.second.defIdx > erasedIdx)
                --entry.second.defIdx;
        }
    }

    string BlockLocalCSE::operandKey(const vector<shared_ptr<RISCVInstruction>> &insts, size_t idx,
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

    optional<BlockLocalCSE::ExprKey> BlockLocalCSE::buildExprKey(
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

    bool BlockLocalCSE::isRegDefinedSince(const vector<shared_ptr<RISCVInstruction>> &insts, size_t fromIdx,
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

    bool BlockLocalCSE::areSourceOperandsLiveSinceDef(
        const vector<shared_ptr<RISCVInstruction>> &insts, size_t useIdx,
        const shared_ptr<RISCVInstruction> &defInst, size_t defIdx)
    {
        if (!defInst)
            return false;
        for (const auto &use : defInst->getUseRegisters())
        {
            if (!use || !use->isVirtual())
                continue;
            if (isRegDefinedSince(insts, defIdx, useIdx, use))
                return false;
        }
        return true;
    }

    bool BlockLocalCSE::isAvailEntryLive(const vector<shared_ptr<RISCVInstruction>> &insts, size_t useIdx,
                                      const AvailEntry &entry)
    {
        if (!entry.defInst)
            return false;
        auto canon = getDestReg(entry.defInst);
        if (!canon || !canon->isVirtual())
            return false;
        if (isRegDefinedSince(insts, entry.defIdx, useIdx, canon))
            return false;
        // 仅看 dest 会误判：la t2,a; add a3,t2,t3; la t2,b; add t5,t2,t3 的 key 相同但 t2 已变
        return areSourceOperandsLiveSinceDef(insts, useIdx, entry.defInst, entry.defIdx);
    }

    bool BlockLocalCSE::allUsesReplaceable(const vector<shared_ptr<RISCVInstruction>> &insts,
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

    bool BlockLocalCSE::isSingleDefInFunction(shared_ptr<RISCVFunction> function,
                                              const shared_ptr<RISCVRegister> &reg)
    {
        if (!function || !reg)
            return false;
        int defCount = 0;
        for (const auto &bb : function->getBasicBlocks())
        {
            if (!bb)
                continue;
            for (const auto &inst : bb->getInstructions())
            {
                if (!inst)
                    continue;
                for (const auto &def : inst->getDefRegisters())
                {
                    if (def && *def == *reg)
                        ++defCount;
                }
            }
        }
        return defCount == 1;
    }

    void BlockLocalCSE::replaceUsesInFunction(shared_ptr<RISCVFunction> function,
                                              const shared_ptr<RISCVRegister> &dup,
                                              const shared_ptr<RISCVRegister> &canon)
    {
        if (!function || !dup || !canon)
            return;
        for (const auto &bb : function->getBasicBlocks())
        {
            if (!bb)
                continue;
            for (const auto &inst : bb->getInstructions())
            {
                if (!inst)
                    continue;
                for (const auto &use : inst->getUseRegisters())
                {
                    if (use && *use == *dup)
                        inst->replaceUseRegister(use, canon);
                }
            }
        }
    }

    bool BlockLocalCSE::hasUseOutsideBlock(shared_ptr<RISCVFunction> function,
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

    bool BlockLocalCSE::allUsesDominatedByDefBB(shared_ptr<RISCVFunction> function,
                                                shared_ptr<RISCVBasicBlock> defBB,
                                                const shared_ptr<RISCVRegister> &reg)
    {
        if (!function || !defBB || !reg)
            return false;
        for (const auto &useBB : function->getBasicBlocks())
        {
            if (!useBB)
                continue;
            for (const auto &inst : useBB->getInstructions())
            {
                if (!inst)
                    continue;
                for (const auto &use : inst->getUseRegisters())
                {
                    if (!use || !(*use == *reg))
                        continue;
                    if (useBB != defBB && !function->dominates(defBB, useBB))
                        return false;
                }
            }
        }
        return true;
    }

    void BlockLocalCSE::replaceUsesWithCanon(const vector<shared_ptr<RISCVInstruction>> &insts,
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

    bool BlockLocalCSE::eraseMaterialDuplicate(
        shared_ptr<RISCVFunction> function, shared_ptr<RISCVBasicBlock> bb,
        vector<shared_ptr<RISCVInstruction>> &insts, size_t dupIdx, size_t canonDefIdx,
        shared_ptr<RISCVBasicBlock> canonBB, const shared_ptr<RISCVRegister> &dupRd,
        const shared_ptr<RISCVRegister> &canon,
        unordered_map<ExprKey, AvailEntry, ExprKeyHash> &avail,
        unordered_map<MaterialKey, AvailEntry, MaterialKeyHash> &materialAvail)
    {
        if (!function || !canon || !dupRd || !canonBB)
            return false;

        // la / 非 copy-init li 在寄存器分配前均为单定义虚拟寄存器
        if (!isSingleDefInFunction(function, canon) || !isSingleDefInFunction(function, dupRd))
            return false;
        if (!allUsesDominatedByDefBB(function, canonBB, dupRd))
            return false;

        if (canonBB == bb && !hasUseOutsideBlock(function, bb, dupRd) &&
            allUsesReplaceable(insts, dupIdx, canonDefIdx, dupRd, canon))
        {
            replaceUsesWithCanon(insts, dupIdx, canonDefIdx, dupRd, canon);
        }
        else
        {
            replaceUsesInFunction(function, dupRd, canon);
        }

        insts.erase(insts.begin() + static_cast<long>(dupIdx));
        decrementDefIdxAfter(avail, bb, dupIdx);
        decrementMaterialDefIdxAfter(materialAvail, bb, dupIdx);
        return true;
    }

    void BlockLocalCSE::decrementDefIdxAfter(unordered_map<ExprKey, AvailEntry, ExprKeyHash> &avail,
                                          shared_ptr<RISCVBasicBlock> bb, size_t erasedIdx)
    {
        for (auto &entry : avail)
        {
            if (entry.second.defBB == bb && entry.second.defIdx > erasedIdx)
                --entry.second.defIdx;
        }
    }

    void BlockLocalCSE::invalidateDefsOfInst(unordered_map<ExprKey, AvailEntry, ExprKeyHash> &avail,
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

    void BlockLocalCSE::invalidateForSp(unordered_map<ExprKey, AvailEntry, ExprKeyHash> &avail,
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

    bool BlockLocalCSE::isSpDefinedBy(const shared_ptr<RISCVInstruction> &inst)
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

    bool BlockLocalCSE::optimizeFunction(shared_ptr<RISCVFunction> function)
    {
        if (!function)
            return false;

        function->computeDominators();

        bool changed = false;
        bool localChanged = false;
        do
        {
            localChanged = false;
            unordered_map<ExprKey, AvailEntry, ExprKeyHash> avail;
            unordered_map<MaterialKey, AvailEntry, MaterialKeyHash> materialAvail;

            for (auto &bb : function->getBasicBlocks())
            {
                int spVersion = 0;
                auto &insts = bb->getInstructions();

                for (size_t idx = 0; idx < insts.size();)
                {
                    auto &inst = insts[idx];
                    if (!inst)
                    {
                        ++idx;
                        continue;
                    }

                    if (idx > 0 && insts[idx - 1])
                    {
                        invalidateDefsOfInst(avail, insts[idx - 1]);
                        invalidateMaterialAvail(materialAvail, insts[idx - 1]);
                    }

                    if (isSpDefinedBy(inst))
                        invalidateForSp(avail, spVersion);

                    // li/la 只读物化：同立即数/符号则删重复指令，后续 use 改用首次的 rd
                    shared_ptr<RISCVRegister> matRd;
                    if (auto matKey = buildMaterialKey(inst, matRd); matKey && matRd)
                    {
                        bool matHandled = false;
                        auto matIt = materialAvail.find(*matKey);
                        if (matIt != materialAvail.end())
                        {
                            const auto &entry = matIt->second;
                            const auto canonBB = entry.defBB;
                            bool canElim = false;
                            if (canonBB == bb)
                                canElim = isAvailEntryLive(insts, idx, entry);
                            else if (canonBB && function->dominates(canonBB, bb))
                                canElim = true;

                            if (canElim)
                            {
                                auto canon = getDestReg(entry.defInst);
                                if (eraseMaterialDuplicate(function, bb, insts, idx, entry.defIdx, canonBB,
                                                         matRd, canon, avail, materialAvail))
                                {
                                    changed = true;
                                    localChanged = true;
                                    matHandled = true;
                                    continue;
                                }
                                else
                                    materialAvail.erase(matIt);
                            }
                            else
                                materialAvail.erase(matIt);
                        }
                        if (!matHandled)
                        {
                            AvailEntry entry;
                            entry.defInst = inst;
                            entry.defBB = bb;
                            entry.defIdx = idx;
                            materialAvail[*matKey] = entry;
                        }
                    }

                    shared_ptr<RISCVRegister> rd;
                    auto keyOpt = buildExprKey(insts, idx, inst, rd);
                    if (keyOpt && rd)
                    {
                        bool eliminated = false;
                        auto it = avail.find(*keyOpt);
                        if (it != avail.end())
                        {
                            const auto &entry = it->second;
                            const auto canonBB = entry.defBB;
                            auto canon = getDestReg(entry.defInst);
                            bool canElim = false;
                            if (canonBB == bb)
                            {
                                canElim = isAvailEntryLive(insts, idx, entry) &&
                                          (!keyOpt->usesSp || entry.spVersion == spVersion);
                            }
                            else if (!keyOpt->usesSp && canonBB && function->dominates(canonBB, bb) &&
                                     canon && isSingleDefInFunction(function, canon))
                            {
                                canElim = true;
                            }

                            if (canElim && canon)
                            {
                                bool replaced = false;
                                if (canonBB == bb && !hasUseOutsideBlock(function, bb, rd) &&
                                    allUsesReplaceable(insts, idx, entry.defIdx, rd, canon))
                                {
                                    replaceUsesWithCanon(insts, idx, entry.defIdx, rd, canon);
                                    replaced = true;
                                }
                                else if (isSingleDefInFunction(function, rd) &&
                                         allUsesDominatedByDefBB(function, canonBB, rd))
                                {
                                    replaceUsesInFunction(function, rd, canon);
                                    replaced = true;
                                }

                                if (replaced)
                                {
                                    insts.erase(insts.begin() + static_cast<long>(idx));
                                    decrementDefIdxAfter(avail, bb, idx);
                                    decrementMaterialDefIdxAfter(materialAvail, bb, idx);
                                    changed = true;
                                    localChanged = true;
                                    eliminated = true;
                                }
                            }
                        }

                        if (eliminated)
                            continue;

                        AvailEntry entry;
                        entry.defInst = inst;
                        entry.defBB = bb;
                        entry.defIdx = idx;
                        entry.spVersion = spVersion;
                        avail[*keyOpt] = entry;
                    }

                    ++idx;
                }

                if (!insts.empty() && insts.back())
                {
                    invalidateDefsOfInst(avail, insts.back());
                    invalidateMaterialAvail(materialAvail, insts.back());
                }
            }
        } while (localChanged);

        return changed;
    }

    void BlockLocalCSE::run(shared_ptr<RISCVFunction> function)
    {
        if (!function)
            return;
        while (optimizeFunction(function))
        {
        }
    }
}
