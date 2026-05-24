#pragma once
#include "RISCVDataStructure.h"
#include <optional>
#include <unordered_map>

namespace RISCV
{
    /// 基本块内公共子表达式消除：li + 纯算术（不做 la：循环中指针别名不安全）；avail 不跨块延续。
    /// li 仅在目的虚拟寄存器在函数内恰有一处 use 时可 CSE（否则可能是循环归纳初值）。
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
        using RegUseCountMap = unordered_map<std::string, size_t>;

        static RegUseCountMap buildRegUseCounts(shared_ptr<RISCVFunction> function);
        static bool canCseLiDest(const RegUseCountMap &useCounts,
                                 const shared_ptr<RISCVRegister> &rd);
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
        static void invalidateDefsOfInst(
            unordered_map<ExprKey, AvailEntry, ExprKeyHash> &avail,
            const shared_ptr<RISCVInstruction> &inst);
        static void invalidateForSp(unordered_map<ExprKey, AvailEntry, ExprKeyHash> &avail,
                                    int &spVersion);
        static bool isSpDefinedBy(const shared_ptr<RISCVInstruction> &inst);
    };
}
