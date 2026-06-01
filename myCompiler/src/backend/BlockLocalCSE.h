#pragma once
#include "RISCVDataStructure.h"
#include <optional>
#include <unordered_map>

namespace RISCV
{
    /// 基本块内 CSE：li/la 只读物化（materialAvail）+ 纯算术（avail）。copy-init li 不参与物化。
    class BlockLocalCSE
    {
    public:
        void run(shared_ptr<RISCVFunction> function);

    private:
        struct ExprKey
        {
            RISCVOpcode opcode = RISCVOpcode::ADD;
            std::string op1;
            std::string op2;
            int64_t imm = 0;
            bool hasImm = false;
            bool usesSp = false;

            bool operator==(const ExprKey &o) const;
        };

        struct ExprKeyHash
        {
            size_t operator()(const ExprKey &k) const;
        };

        struct AvailEntry
        {
            shared_ptr<RISCVInstruction> defInst;
            size_t defIdx = 0;
            int spVersion = 0;
        };

        /// li/la 物化键：不含 rd，仅按立即数或全局符号
        struct MaterialKey
        {
            RISCVOpcode opcode = RISCVOpcode::LI;
            int64_t imm = 0;
            std::string label;
            bool hasImm = false;

            bool operator==(const MaterialKey &o) const;
        };

        struct MaterialKeyHash
        {
            size_t operator()(const MaterialKey &k) const;
        };

        static bool optimizeFunction(shared_ptr<RISCVFunction> function);
        static shared_ptr<RISCVRegister> getDestReg(const shared_ptr<RISCVInstruction> &inst);
        static std::string regKey(const shared_ptr<RISCVRegister> &reg);
        static bool isSpRegister(const shared_ptr<RISCVRegister> &reg);
        static bool isCSEableOpcode(RISCVOpcode op);
        static shared_ptr<RISCVLoop> findInnermostLoop(const LoopInfo &loopInfo,
                                                       shared_ptr<RISCVBasicBlock> bb);
        static bool isOnlyDefOfDestInBlocks(const shared_ptr<RISCVInstruction> &inst,
                                            const vector<shared_ptr<RISCVBasicBlock>> &blocks);
        static bool isLoopInductionRelated(const shared_ptr<RISCVInstruction> &inst,
                                           const shared_ptr<RISCVLoop> &loop);
        static std::optional<MaterialKey> buildMaterialKey(const shared_ptr<RISCVInstruction> &inst,
                                                             shared_ptr<RISCVRegister> &outRd);
        static void invalidateMaterialAvail(
            unordered_map<MaterialKey, AvailEntry, MaterialKeyHash> &materialAvail,
            const shared_ptr<RISCVInstruction> &producer);
        static void decrementMaterialDefIdxAfter(
            unordered_map<MaterialKey, AvailEntry, MaterialKeyHash> &materialAvail, size_t erasedIdx);
        static std::string operandKey(const vector<shared_ptr<RISCVInstruction>> &insts,
                                      size_t idx, const shared_ptr<RISCVRegister> &reg);
        static std::optional<ExprKey> buildExprKey(const vector<shared_ptr<RISCVInstruction>> &insts,
                                                   size_t idx,
                                                   const shared_ptr<RISCVInstruction> &inst,
                                                   shared_ptr<RISCVRegister> &outRd);
        static bool isRegDefinedSince(const vector<shared_ptr<RISCVInstruction>> &insts, size_t fromIdx,
                                      size_t toIdx, const shared_ptr<RISCVRegister> &reg);
        static bool areSourceOperandsLiveSinceDef(
            const vector<shared_ptr<RISCVInstruction>> &insts, size_t useIdx,
            const shared_ptr<RISCVInstruction> &defInst, size_t defIdx);
        static bool isAvailEntryLive(const vector<shared_ptr<RISCVInstruction>> &insts, size_t useIdx,
                                     const AvailEntry &entry);
        static bool hasUseOutsideBlock(shared_ptr<RISCVFunction> function,
                                       shared_ptr<RISCVBasicBlock> bb,
                                       const shared_ptr<RISCVRegister> &reg);
        static bool isSingleDefInFunction(shared_ptr<RISCVFunction> function,
                                          const shared_ptr<RISCVRegister> &reg);
        static void replaceUsesInFunction(shared_ptr<RISCVFunction> function,
                                          const shared_ptr<RISCVRegister> &dup,
                                          const shared_ptr<RISCVRegister> &canon);
        static bool allUsesReplaceable(const vector<shared_ptr<RISCVInstruction>> &insts,
                                       size_t dupIdx, size_t canonDefIdx,
                                       const shared_ptr<RISCVRegister> &dupRd,
                                       const shared_ptr<RISCVRegister> &canon);
        static void replaceUsesWithCanon(const vector<shared_ptr<RISCVInstruction>> &insts,
                                         size_t dupIdx, size_t canonDefIdx,
                                         const shared_ptr<RISCVRegister> &dupRd,
                                         const shared_ptr<RISCVRegister> &canon);
        static bool eraseMaterialDuplicate(
            shared_ptr<RISCVFunction> function, shared_ptr<RISCVBasicBlock> bb,
            vector<shared_ptr<RISCVInstruction>> &insts, size_t dupIdx, size_t canonDefIdx,
            const shared_ptr<RISCVRegister> &dupRd, const shared_ptr<RISCVRegister> &canon,
            unordered_map<ExprKey, AvailEntry, ExprKeyHash> &avail,
            unordered_map<MaterialKey, AvailEntry, MaterialKeyHash> &materialAvail);
        static void decrementDefIdxAfter(unordered_map<ExprKey, AvailEntry, ExprKeyHash> &avail,
                                         size_t erasedIdx);
        static void invalidateDefsOfInst(
            unordered_map<ExprKey, AvailEntry, ExprKeyHash> &avail,
            const shared_ptr<RISCVInstruction> &producer);
        static void invalidateForSp(unordered_map<ExprKey, AvailEntry, ExprKeyHash> &avail,
                                    int &spVersion);
        static bool isSpDefinedBy(const shared_ptr<RISCVInstruction> &inst);
    };
}
