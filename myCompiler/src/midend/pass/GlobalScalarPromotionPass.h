#pragma once
#include "Pass.h"

namespace optimization
{
    /// 将循环内对全局标量变量的反复 load/store 提升为 SSA（phi 寄存器变量），
    /// 将循环内标量全局变量提升为 phi（每次 store 须基于 load @gv：d*=2、ans+=…、state 更新等）；
    /// 仅在循环出口块写回。须在函数内联之前运行。
    class GlobalScalarPromotionPass : public Pass
    {
    public:
        GlobalScalarPromotionPass(bool verbose = false) : Pass(verbose) {}
        bool runOnFunction(Function *func) override;
        std::string getName() const override { return "GlobalScalarPromotion"; }

    private:
        static GlobalVariable *getScalarGlobal(Value *ptr);
        static bool isDirectAccessToGlobal(Value *ptr, GlobalVariable *gv);
        static bool loopMayModifyViaCall(const Loop &loop, GlobalVariable *gv);
        static bool hasStoreToGlobal(const Loop &loop, GlobalVariable *gv);
        static bool hasLoadToGlobal(const Loop &loop, GlobalVariable *gv);
        static Value *mergePredValues(const std::vector<Value *> &vals);
        static Value *applyBlock(GlobalVariable *gv, BasicBlock *bb, Value *in);
        static void insertPhiAtHeader(BasicBlock *header, PhiInst *phi);
        static void insertBeforeTerminator(BasicBlock *bb, Instruction *inst);
        static bool branchesTo(BasicBlock *from, BasicBlock *to);
        static bool propagateGlobalOutsideLoop(
            Function *func, GlobalVariable *gv, const Loop &loop,
            const std::unordered_map<BasicBlock *, Value *> &loopOutVal,
            const std::set<BasicBlock *> &loopExitTargets);
        bool promoteGlobalInLoop(Function *func, Loop &loop, GlobalVariable *gv);
    };
}
