#pragma once
#include "RISCVDataStructure.h"
#include <optional>

namespace RISCV
{
    /// 函数内公共子表达式消除（参考中端 CSEPass）：li/la + 纯算术；按表达式键匹配，用首次
    /// 定义指令作 canonical，仅当操作数或 canonical 结果寄存器在两次使用间被改写时失效。
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
        static bool isInductionInitLi(const vector<shared_ptr<RISCVInstruction>> &insts, size_t liIdx,
                                      shared_ptr<RISCVRegister> rd);
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
