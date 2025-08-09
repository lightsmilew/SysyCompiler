#pragma once
#include "Pass.h"
#include <vector>
namespace optimization
{

    // 2. 公共子表达式消除Pass
    class CommonSubexpressionEliminationPass : public Pass
    {
        using ExprKey = std::pair<std::string, std::vector<std::string>>;

    public:
        CommonSubexpressionEliminationPass(bool verbose = false) : Pass(verbose), recognizeMode(0) {}
        CommonSubexpressionEliminationPass(int mode, bool verbose = false) : Pass(verbose), recognizeMode(mode) {}
        // 设置识别模式，0代表严格识别，只要store基址相同
        // 就不消除，1代表宽松识别，允许load和store基址相同
        void setRecognizeMode(int mode) { recognizeMode = mode; }
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "CommonSubexpressionElimination"; }

    private:
        struct ExpressionHash
        {
            std::size_t operator()(const ExprKey &expr) const;
        };
        int recognizeMode; // 0代表严格识别，只要store|load基址相同就不消除
        std::unordered_map<ExprKey, std::pair<Instruction *, BasicBlock *>, ExpressionHash> exprMap;
        std::pair<std::string, std::vector<std::string>> getExpressionKey(Instruction *inst);
        bool canBeCommonSubexpression(Instruction *inst, BasicBlock *bb);
        bool CanLoadCSE(Instruction *inst, Instruction *map_inst, BasicBlock *bb);
    };
}