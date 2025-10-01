#include "NormalizationPass.h"
using namespace std;
using namespace optimization;
bool NormalizationPass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        for (size_t i = 0; i < insts.size(); ++i)
        {
            // 交换律操作交换左右操作数
            auto *inst = insts[i].get();
            if (inst->isCommutativeOp())
            {
                auto *binInst = dynamic_cast<BinaryOperator *>(inst);
                auto *lhsConst = dynamic_cast<Constant *>(binInst->getLHS());
                auto *rhsConst = dynamic_cast<Constant *>(binInst->getRHS());
                // 常量在右边
                if (lhsConst && !rhsConst)
                {
                    binInst->exchangeOperands();
                    changed = true;
                    if (verbose)
                        debugInfo << "Normalization: Exchanged operands of instruction " << binInst->toString()
                                  << " in block " << bb->getName() << "\n";
                }
            }
            else if (inst->getOpcode() == Opcode::ICmp)
            {
                auto *icmpinst = dynamic_cast<ICmpInst *>(inst);
                // 把>=转为<=，把>转为<
                auto pred = icmpinst->getPredicate();
                if (pred == ICmpInst::ICMP_SGE || pred == ICmpInst::ICMP_SGT)
                {
                    icmpinst->exchangeOperands();
                    changed = true;
                    if (verbose)
                        debugInfo << "Normalization: Exchanged operands of instruction " << icmpinst->toString()
                                  << " in block " << bb->getName() << "\n";
                }
            }
            else if (inst->getOpcode() == Opcode::FCmp)
            {
                auto *fcmpinst = dynamic_cast<FCmpInst *>(inst);
                // 把>=转为<=，把>转为<
                auto pred = fcmpinst->getPredicate();
                if (pred == FCmpInst::FCMP_OGE || pred == FCmpInst::FCMP_OGT)
                {
                    fcmpinst->exchangeOperands();
                    changed = true;
                    if (verbose)
                        debugInfo << "Normalization: Exchanged operands of instruction " << fcmpinst->toString()
                                  << " in block " << bb->getName() << "\n";
                }
            }
        }
    }
    return changed;
}