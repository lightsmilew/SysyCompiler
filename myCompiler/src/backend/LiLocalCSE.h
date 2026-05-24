#pragma once
#include "RISCVDataStructure.h"
#include <optional>
#include <unordered_map>

namespace RISCV
{
    /// 基本块内公共子表达式消除：li + 纯算术（不做 la：循环中指针别名不安全）；avail 不跨块延续。
    /// 循环归纳/规约相关指令跳过（与 LICM 相同判定）；命中时用已有 rd 替换后续 use 并删除重复计算，不生成 MV。
    class LiLocalCSE
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

        static bool optimizeFunction(shared_ptr<RISCVFunction> function);
        static shared_ptr<RISCVRegister> getSpRegister();
        static shared_ptr<RISCVRegister> getDestReg(const shared_ptr<RISCVInstruction> &inst);
        static std::string regKey(const shared_ptr<RISCVRegister> &reg);
        static bool isSpRegister(const shared_ptr<RISCVRegister> &reg);
        static bool isCSEableOpcode(RISCVOpcode op);
        static shared_ptr<RISCVLoop> findInnermostLoop(const LoopInfo &loopInfo,
                                                       shared_ptr<RISCVBasicBlock> bb);
        static bool isOnlyDefOfDestInBlocks(const shared_ptr<RISCVInstruction> &inst,
                                            const vector<shared_ptr<RISCVBasicBlock>> &blocks);
        static bool isLoopInductionRelated(shared_ptr<RISCVBasicBlock> bb,
                                           const shared_ptr<RISCVInstruction> &inst,
                                           const shared_ptr<RISCVLoop> &loop);
        static std::string operandKey(const vector<shared_ptr<RISCVInstruction>> &insts,
                                      size_t idx, const shared_ptr<RISCVRegister> &reg);
        static std::optional<ExprKey> buildExprKey(const vector<shared_ptr<RISCVInstruction>> &insts,
                                                   size_t idx,
                                                   const shared_ptr<RISCVInstruction> &inst,
                                                   shared_ptr<RISCVRegister> &outRd);
        static bool isRegDefinedSince(const vector<shared_ptr<RISCVInstruction>> &insts, size_t fromIdx,
                                      size_t toIdx, const shared_ptr<RISCVRegister> &reg);
        static bool isAvailEntryLive(const vector<shared_ptr<RISCVInstruction>> &insts, size_t useIdx,
                                     const AvailEntry &entry);
        static bool hasUseOutsideBlock(shared_ptr<RISCVFunction> function,
                                       shared_ptr<RISCVBasicBlock> bb,
                                       const shared_ptr<RISCVRegister> &reg);
        static bool allUsesReplaceable(const vector<shared_ptr<RISCVInstruction>> &insts,
                                       size_t dupIdx, size_t canonDefIdx,
                                       const shared_ptr<RISCVRegister> &dupRd,
                                       const shared_ptr<RISCVRegister> &canon);
        static void replaceUsesWithCanon(const vector<shared_ptr<RISCVInstruction>> &insts,
                                         size_t dupIdx, size_t canonDefIdx,
                                         const shared_ptr<RISCVRegister> &dupRd,
                                         const shared_ptr<RISCVRegister> &canon);
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
