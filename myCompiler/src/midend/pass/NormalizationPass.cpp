#include "NormalizationPass.h"

using namespace std;
using namespace optimization;

namespace
{
    ICmpInst::Predicate flipICmpPredicate(ICmpInst::Predicate pred)
    {
        switch (pred)
        {
        case ICmpInst::ICMP_SLT:
            return ICmpInst::ICMP_SGT;
        case ICmpInst::ICMP_SLE:
            return ICmpInst::ICMP_SGE;
        case ICmpInst::ICMP_SGT:
            return ICmpInst::ICMP_SLT;
        case ICmpInst::ICMP_SGE:
            return ICmpInst::ICMP_SLE;
        case ICmpInst::ICMP_EQ:
        case ICmpInst::ICMP_NE:
            return pred;
        default:
            return pred;
        }
    }

    bool matchConstantInt(Value *v, int &out)
    {
        if (auto *c = dynamic_cast<ConstantInt *>(v))
        {
            out = c->Value;
            return true;
        }
        return false;
    }

    // 仅匹配 add/sub 与常量：sub 只处理 var - c，add 允许常量在任一侧。
    bool matchAddSubExpr(Value *v, Value *&var, int &c1, bool &isSub)
    {
        auto *bin = dynamic_cast<BinaryOperator *>(v);
        if (!bin)
        {
            return false;
        }
        if (dynamic_cast<ConstantInt *>(bin->getLHS()) && dynamic_cast<ConstantInt *>(bin->getRHS()))
        {
            return false;
        }
        if (bin->getOpcode() == Opcode::Sub)
        {
            if (dynamic_cast<ConstantInt *>(bin->getLHS()))
            {
                return false;
            }
            auto *c = dynamic_cast<ConstantInt *>(bin->getRHS());
            if (!c)
            {
                return false;
            }
            var = bin->getLHS();
            c1 = c->Value;
            isSub = true;
            return true;
        }
        if (bin->getOpcode() == Opcode::Add)
        {
            auto *cL = dynamic_cast<ConstantInt *>(bin->getLHS());
            auto *cR = dynamic_cast<ConstantInt *>(bin->getRHS());
            if (cL && !cR)
            {
                var = bin->getRHS();
                c1 = cL->Value;
                isSub = false;
                return true;
            }
            if (cR && !cL)
            {
                var = bin->getLHS();
                c1 = cR->Value;
                isSub = false;
                return true;
            }
        }
        return false;
    }

    int adjustBoundForLhsExpr(bool isSub, int c1, int c2)
    {
        return isSub ? (c2 + c1) : (c2 - c1);
    }

    // 简单变量：非常量，且不是 add/sub 与常量的组合表达式。
    bool isSimpleVariable(Value *v)
    {
        if (!v || dynamic_cast<ConstantInt *>(v))
        {
            return false;
        }
        Value *dummy = nullptr;
        int c = 0;
        bool isSub = false;
        return !matchAddSubExpr(v, dummy, c, isSub);
    }

    BinaryOperator *insertAddSubBeforeICmp(std::vector<std::unique_ptr<Instruction>> &insts, size_t icmpIndex,
                                         Opcode op, Value *lhs, int rhsConst,
                                         const std::string &nameSuffix)
    {
        auto *c = new ConstantInt(IntegerType::getInstance(), rhsConst);
        auto *bin = new BinaryOperator(op, lhs, c, nameSuffix);
        insts.insert(insts.begin() + static_cast<ptrdiff_t>(icmpIndex),
                     std::unique_ptr<Instruction>(bin));
        return bin;
    }

    // 已是「简单变量 op 简单变量」形式：一侧为变量，另一侧为 add/sub(变量, 常量)
    bool isICmpVarVsAddSubForm(ICmpInst *icmp)
    {
        Value *lhs = icmp->getLHS();
        Value *rhs = icmp->getRHS();
        Value *y = nullptr;
        int c1 = 0;
        bool isSub = false;
        if (isSimpleVariable(lhs) && matchAddSubExpr(rhs, y, c1, isSub))
        {
            return true;
        }
        return isSimpleVariable(rhs) && matchAddSubExpr(lhs, y, c1, isSub);
    }

    // x pred (y - c) => (x + c) pred y；x pred (y + c) => (x - c) pred y（仅 add/sub）
    bool tryNormalizeICmpAddSubVarVar(ICmpInst *icmp, std::vector<std::unique_ptr<Instruction>> &insts,
                                      size_t icmpIndex, bool verbose, std::stringstream &debugInfo)
    {
        if (isICmpVarVsAddSubForm(icmp))
        {
            return false;
        }

        Value *lhs = icmp->getLHS();
        Value *rhs = icmp->getRHS();
        Value *x = nullptr;
        Value *y = nullptr;
        int c1 = 0;
        bool isSub = false;
        bool binopOnRhs = false;

        if (isSimpleVariable(lhs) && matchAddSubExpr(rhs, y, c1, isSub))
        {
            x = lhs;
            binopOnRhs = true;
        }
        else if (matchAddSubExpr(lhs, y, c1, isSub) && isSimpleVariable(rhs))
        {
            x = rhs;
            binopOnRhs = false;
        }
        else
        {
            return false;
        }

        if (!x || !y || dynamic_cast<ConstantInt *>(y))
        {
            return false;
        }

        BinaryOperator *normBin = nullptr;
        const std::string suffix = icmp->getName() + "_norm";

        if (binopOnRhs)
        {
            if (isSub)
            {
                normBin = insertAddSubBeforeICmp(insts, icmpIndex, Opcode::Add, x, c1, suffix);
            }
            else
            {
                normBin = insertAddSubBeforeICmp(insts, icmpIndex, Opcode::Sub, x, c1, suffix);
            }
            icmp->setOperandByIndex(0, normBin);
            icmp->setOperandByIndex(1, y);
        }
        else
        {
            if (isSub)
            {
                normBin = insertAddSubBeforeICmp(insts, icmpIndex, Opcode::Add, x, c1, suffix);
            }
            else
            {
                normBin = insertAddSubBeforeICmp(insts, icmpIndex, Opcode::Sub, x, c1, suffix);
            }
            icmp->setOperandByIndex(0, y);
            icmp->setOperandByIndex(1, normBin);
        }

        if (verbose)
        {
            debugInfo << "Normalization: Rewrote icmp add/sub var-var compare " << icmp->toString()
                      << "\n";
        }
        return true;
    }

    // (var - c1) pred c2 或 (var + c1) pred c2  =>  var pred' c2'
    bool tryNormalizeICmpAddSub(ICmpInst *icmp, bool verbose, std::stringstream &debugInfo)
    {
        Value *lhs = icmp->getLHS();
        Value *rhs = icmp->getRHS();
        Value *var = nullptr;
        int c1 = 0;
        int c2 = 0;
        bool isSub = false;
        bool binopOnLhs = false;

        if (matchAddSubExpr(lhs, var, c1, isSub) && matchConstantInt(rhs, c2))
        {
            binopOnLhs = true;
        }
        else if (matchConstantInt(lhs, c2) && matchAddSubExpr(rhs, var, c1, isSub))
        {
            binopOnLhs = false;
        }
        else
        {
            return false;
        }

        if (!var || dynamic_cast<ConstantInt *>(var))
        {
            return false;
        }

        ICmpInst::Predicate pred = icmp->getPredicate();
        int newC2 = adjustBoundForLhsExpr(isSub, c1, c2);
        if (!binopOnLhs)
        {
            pred = flipICmpPredicate(pred);
        }

        auto *newConst = new ConstantInt(IntegerType::getInstance(), newC2);
        icmp->setOperandByIndex(0, var);
        icmp->setOperandByIndex(1, newConst);
        icmp->Pred = pred;

        if (verbose)
        {
            debugInfo << "Normalization: Rewrote icmp add/sub compare to iv-on-side form "
                      << icmp->toString() << "\n";
        }
        return true;
    }
} // namespace

bool NormalizationPass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        for (size_t i = 0; i < insts.size(); ++i)
        {
            auto *inst = insts[i].get();
            if (inst->isCommutativeOp())
            {
                auto *binInst = dynamic_cast<BinaryOperator *>(inst);
                auto *lhsConst = dynamic_cast<Constant *>(binInst->getLHS());
                auto *rhsConst = dynamic_cast<Constant *>(binInst->getRHS());
                if (lhsConst && !rhsConst)
                {
                    binInst->exchangeOperands();
                    changed = true;
                    if (verbose)
                        debugInfo << "Normalization: Exchanged operands of instruction "
                                  << binInst->toString() << " in block " << bb->getName() << "\n";
                }
            }
            else if (inst->getOpcode() == Opcode::ICmp)
            {
                auto *icmpinst = dynamic_cast<ICmpInst *>(inst);
                if (tryNormalizeICmpAddSubVarVar(icmpinst, insts, i, verbose, debugInfo))
                {
                    changed = true;
                }
                else if (tryNormalizeICmpAddSub(icmpinst, verbose, debugInfo))
                {
                    changed = true;
                }
                auto pred = icmpinst->getPredicate();
                if (pred == ICmpInst::ICMP_SGE || pred == ICmpInst::ICMP_SGT)
                {
                    icmpinst->exchangeOperands();
                    changed = true;
                    if (verbose)
                        debugInfo << "Normalization: Exchanged operands of instruction "
                                  << icmpinst->toString() << " in block " << bb->getName() << "\n";
                }
            }
            else if (inst->getOpcode() == Opcode::FCmp)
            {
                auto *fcmpinst = dynamic_cast<FCmpInst *>(inst);
                auto pred = fcmpinst->getPredicate();
                if (pred == FCmpInst::FCMP_OGE || pred == FCmpInst::FCMP_OGT)
                {
                    fcmpinst->exchangeOperands();
                    changed = true;
                    if (verbose)
                        debugInfo << "Normalization: Exchanged operands of instruction "
                                  << fcmpinst->toString() << " in block " << bb->getName() << "\n";
                }
            }
        }
    }
    return changed;
}
