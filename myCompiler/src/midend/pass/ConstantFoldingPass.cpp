#include "ConstantFoldingPass.h"
using namespace std;
using namespace optimization;
// 常量折叠实现
bool ConstantFoldingPass::runOnFunction(Function *func)
{
    bool changed = false;
    bool localChanged;
    do
    {
        localChanged = false;
        for (auto &bb : func->getBasicBlocks())
        {
            auto &insts = bb->getInstructions();
            for (auto it = insts.begin(); it != insts.end();)
            {
                Instruction *inst = it->get();
                // 只处理二元运算且无副作用
                if (inst && inst->isBinaryOp())
                {
                    auto binaryOperator = dynamic_cast<BinaryOperator *>(inst);
                    if (!binaryOperator)
                    {
                        ++it;
                        continue;
                    }
                    Value *lhs = binaryOperator->getLHS();
                    Value *rhs = binaryOperator->getRHS();

                    // int常量折叠
                    if (auto *ci1 = dynamic_cast<ConstantInt *>(lhs))
                    {
                        if (auto *ci2 = dynamic_cast<ConstantInt *>(rhs))
                        {
                            int result = 0;
                            switch (inst->getOpcode())
                            {
                            case Opcode::Add:
                                result = ci1->Value + ci2->Value;
                                break;
                            case Opcode::Sub:
                                result = ci1->Value - ci2->Value;
                                break;
                            case Opcode::Mul:
                                result = ci1->Value * ci2->Value;
                                break;
                            case Opcode::SDiv:
                                result = ci2->Value != 0 ? ci1->Value / ci2->Value : 0;
                                break;
                            case Opcode::SRem:
                                result = ci2->Value != 0 ? ci1->Value % ci2->Value : 0;
                                break;
                            default:
                                throw std::runtime_error("Unsupported opcode for constant folding");
                            }
                            auto constVal = new ConstantInt(IntegerType::getInstance(), result);
                            inst->replaceAllUsesWith(constVal);
                            if (verbose)
                            {
                                debugInfo << "Constant folding: " << inst->getOpcodeName() << " "
                                          << ci1->Value << " and " << ci2->Value
                                          << " to " << result << "\n";
                            }
                            // 还要打印输出
                            inst->removeThisFromOperands();
                            needToDelete.push_back(it->release());
                            it = insts.erase(it);
                            localChanged = true;
                            changed = true;
                            continue;
                        }
                    }
                    // float常量折叠
                    if (auto *cf1 = dynamic_cast<ConstantFloat *>(lhs))
                    {
                        if (auto *cf2 = dynamic_cast<ConstantFloat *>(rhs))
                        {
                            float result = 0;
                            switch (inst->getOpcode())
                            {
                            case Opcode::FAdd:
                                result = cf1->Value + cf2->Value;
                                break;
                            case Opcode::FSub:
                                result = cf1->Value - cf2->Value;
                                break;
                            case Opcode::FMul:
                                result = cf1->Value * cf2->Value;
                                break;
                            case Opcode::FDiv:
                                result = cf2->Value != 0.0f ? cf1->Value / cf2->Value : 0.0f;
                                break;
                            default:
                                throw std::runtime_error("Unsupported opcode for constant folding");
                            }
                            auto constVal = new ConstantFloat(FloatType::getInstance(), result);
                            inst->replaceAllUsesWith(constVal);
                            if (verbose)
                            {
                                debugInfo << "Constant folding: " << inst->getOpcodeName() << " "
                                          << cf1->Value << " and " << cf2->Value
                                          << " to " << result << "\n";
                            }
                            // 还要打印输出
                            inst->removeThisFromOperands();
                            needToDelete.push_back(it->release());
                            it = insts.erase(it);
                            localChanged = true;
                            changed = true;
                            continue;
                        }
                    }
                    // 恒等消除：add x 0 => x, add 0 x => x
                    if (binaryOperator->getOpcode() == Opcode::Add)
                    {
                        if (auto *ci = dynamic_cast<ConstantInt *>(rhs))
                        {
                            if (ci->Value == 0)
                            {
                                inst->replaceAllUsesWith(lhs);
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                        }
                        else if (auto *ci = dynamic_cast<ConstantInt *>(lhs))
                        {
                            if (ci->Value == 0)
                            {
                                inst->replaceAllUsesWith(rhs);
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                        }
                    }
                    if (binaryOperator->getOpcode() == Opcode::FAdd)
                    {
                        if (auto *cf = dynamic_cast<ConstantFloat *>(rhs))
                        {
                            if (cf->Value == 0.0f)
                            {
                                inst->replaceAllUsesWith(lhs);
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                        }
                        else if (auto *cf = dynamic_cast<ConstantFloat *>(lhs))
                        {
                            if (cf->Value == 0.0f)
                            {
                                inst->replaceAllUsesWith(rhs);
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                        }
                    }
                    // 减0不变
                    if (binaryOperator->getOpcode() == Opcode::Sub)
                    {
                        if (auto *ci = dynamic_cast<ConstantInt *>(rhs))
                        {
                            if (ci->Value == 0)
                            {
                                inst->replaceAllUsesWith(lhs);
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                        }
                    }
                    if (binaryOperator->getOpcode() == Opcode::FSub)
                    {
                        if (auto *cf = dynamic_cast<ConstantFloat *>(rhs))
                        {
                            if (cf->Value == 0.0f)
                            {
                                inst->replaceAllUsesWith(lhs);
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                        }
                    }
                    // 乘以1不变
                    if (binaryOperator->getOpcode() == Opcode::Mul)
                    {
                        if (auto *ci = dynamic_cast<ConstantInt *>(rhs))
                        {
                            if (ci->Value == 1)
                            {
                                inst->replaceAllUsesWith(lhs);
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                            else if (ci->Value == 0)
                            {
                                // 乘以0等于0
                                auto constVal = new ConstantInt(IntegerType::getInstance(), 0);
                                inst->replaceAllUsesWith(constVal);
                                if (verbose)
                                {
                                    debugInfo << "Constant folding: " << inst->getOpcodeName() << " "
                                              << " to 0\n";
                                }
                                // 还要打印输出
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                localChanged = true;
                                changed = true;
                                continue;
                            }
                        }
                        else if (auto *ci = dynamic_cast<ConstantInt *>(lhs))
                        {
                            if (ci->Value == 1)
                            {
                                inst->replaceAllUsesWith(rhs);
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                            else if (ci->Value == 0)
                            {
                                // 乘以0等于0
                                auto constVal = new ConstantInt(IntegerType::getInstance(), 0);
                                inst->replaceAllUsesWith(constVal);
                                if (verbose)
                                {
                                    debugInfo << "Constant folding: " << inst->getOpcodeName() << " "
                                              << " to 0\n";
                                }
                                // 还要打印输出
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                localChanged = true;
                                changed = true;
                                continue;
                            }
                        }
                    }
                    if (binaryOperator->getOpcode() == Opcode::FMul)
                    {
                        if (auto *cf = dynamic_cast<ConstantFloat *>(rhs))
                        {
                            if (cf->Value == 1.0f)
                            {
                                inst->replaceAllUsesWith(lhs);
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                            else if (cf->Value == 0.0f)
                            {
                                // 乘以0等于0
                                auto constVal = new ConstantFloat(FloatType::getInstance(), 0.0f);
                                inst->replaceAllUsesWith(constVal);
                                if (verbose)
                                {
                                    debugInfo << "Constant folding: " << inst->getOpcodeName() << " "
                                              << " to 0.0\n";
                                }
                                // 还要打印输出
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                localChanged = true;
                                changed = true;
                                continue;
                            }
                        }
                        else if (auto *cf = dynamic_cast<ConstantFloat *>(lhs))
                        {
                            if (cf->Value == 1.0f)
                            {
                                inst->replaceAllUsesWith(rhs);
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                            else if (cf->Value == 0.0f)
                            {
                                // 乘以0等于0
                                auto constVal = new ConstantFloat(FloatType::getInstance(), 0.0f);
                                inst->replaceAllUsesWith(constVal);
                                if (verbose)
                                {
                                    debugInfo << "Constant folding: " << inst->getOpcodeName() << " "
                                              << " to 0.0\n";
                                }
                                // 还要打印输出
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                localChanged = true;
                                changed = true;
                                continue;
                            }
                        }
                    }
                    // 除以1不变
                    if (binaryOperator->getOpcode() == Opcode::SDiv)
                    {
                        if (auto *ci = dynamic_cast<ConstantInt *>(rhs))
                        {
                            if (ci->Value == 1)
                            {
                                inst->replaceAllUsesWith(lhs);
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                        }
                    }
                    if (binaryOperator->getOpcode() == Opcode::FDiv)
                    {
                        if (auto *cf = dynamic_cast<ConstantFloat *>(rhs))
                        {
                            if (cf->Value == 1.0f)
                            {
                                inst->replaceAllUsesWith(lhs);
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                        }
                    }
                    // 取模1不变
                    if (binaryOperator->getOpcode() == Opcode::SRem)
                    {
                        if (auto *ci = dynamic_cast<ConstantInt *>(rhs))
                        {
                            if (ci->Value == 1)
                            {
                                inst->replaceAllUsesWith(lhs);
                                inst->removeThisFromOperands();
                                needToDelete.push_back(it->release());
                                it = insts.erase(it);
                                changed = true;
                                continue;
                            }
                        }
                    }
                }
                // int比较指令
                if (inst && inst->getOpcode() == Opcode::ICmp)
                {
                    auto *icmp = dynamic_cast<ICmpInst *>(inst);
                    auto *ci1 = dynamic_cast<ConstantInt *>(icmp->getLHS());
                    auto *ci2 = dynamic_cast<ConstantInt *>(icmp->getRHS());
                    if (ci1 && ci2)
                    {
                        int result = 0;
                        switch (icmp->getPredicate())
                        {
                        case ICmpInst::ICMP_EQ:
                            result = (ci1->Value == ci2->Value);
                            break;
                        case ICmpInst::ICMP_NE:
                            result = (ci1->Value != ci2->Value);
                            break;
                        case ICmpInst::ICMP_SLT:
                            result = (ci1->Value < ci2->Value);
                            break;
                        case ICmpInst::ICMP_SLE:
                            result = (ci1->Value <= ci2->Value);
                            break;
                        case ICmpInst::ICMP_SGT:
                            result = (ci1->Value > ci2->Value);
                            break;
                        case ICmpInst::ICMP_SGE:
                            result = (ci1->Value >= ci2->Value);
                            break;
                        }
                        auto constVal = new ConstantInt(IntegerType::getInstance(), result);
                        inst->replaceAllUsesWith(constVal);
                        if (verbose)
                        {
                            debugInfo << "Constant folding: " << inst->getOpcodeName() << " "
                                      << ci1->Value << " and " << ci2->Value
                                      << " to " << result << "\n";
                        }
                        // 还要打印输出
                        inst->removeThisFromOperands();
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                        localChanged = true;
                        changed = true;
                        continue;
                    }
                }
                // float比较指令
                if (inst && inst->getOpcode() == Opcode::FCmp)
                {
                    auto *fcmp = dynamic_cast<FCmpInst *>(inst);
                    auto *cf1 = dynamic_cast<ConstantFloat *>(fcmp->getLHS());
                    auto *cf2 = dynamic_cast<ConstantFloat *>(fcmp->getRHS());
                    if (cf1 && cf2)
                    {
                        int result = 0;
                        switch (fcmp->getPredicate())
                        {
                        case FCmpInst::FCMP_OEQ:
                            result = (cf1->Value == cf2->Value);
                            break;
                        case FCmpInst::FCMP_ONE:
                            result = (cf1->Value != cf2->Value);
                            break;
                        case FCmpInst::FCMP_OLT:
                            result = (cf1->Value < cf2->Value);
                            break;
                        case FCmpInst::FCMP_OLE:
                            result = (cf1->Value <= cf2->Value);
                            break;
                        case FCmpInst::FCMP_OGT:
                            result = (cf1->Value > cf2->Value);
                            break;
                        case FCmpInst::FCMP_OGE:
                            result = (cf1->Value >= cf2->Value);
                            break;
                        }
                        auto constVal = new ConstantInt(IntegerType::getInstance(), result);
                        inst->replaceAllUsesWith(constVal);
                        if (verbose)
                        {
                            debugInfo << "Constant folding: " << inst->getOpcodeName() << " "
                                      << cf1->Value << " and " << cf2->Value
                                      << " to " << result << "\n";
                        }
                        // 还要打印输出
                        inst->removeThisFromOperands();
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                        localChanged = true;
                        changed = true;
                        continue;
                    }
                }
                // 有条件跳转指令替换为无条件跳转
                if (inst && inst->getOpcode() == Opcode::Br)
                {
                    auto *br = dynamic_cast<BranchInst *>(inst);
                    if (br->isConditional())
                    {
                        if (auto *cond = dynamic_cast<ConstantInt *>(br->getCondition()))
                        {
                            BasicBlock *targetBB = cond->Value ? br->TrueBlock : br->FalseBlock;

                            // 替换为无条件跳转
                            auto *newBr = new BranchInst(targetBB);
                            inst->replaceAllUsesWith(newBr);
                            // 从操作数中移除自己
                            inst->removeThisFromOperands();
                            needToDelete.push_back(it->release());
                            it = insts.erase(it);
                            bb->addInstruction(std::unique_ptr<Instruction>(newBr));
                            // 更新cfg 从后继中删除永假块
                            if (br->FalseBlock == targetBB)
                            {
                                bb->removeSuccessor(br->TrueBlock);
                                br->TrueBlock->removePredecessor(bb.get());
                            }
                            else if (br->TrueBlock == targetBB)
                            {
                                bb->removeSuccessor(br->FalseBlock);
                                br->FalseBlock->removePredecessor(bb.get());
                            }
                            if (verbose)
                            {
                                debugInfo << "Constant folding: Conditional branch to "
                                          << targetBB->getName() << "\n";
                            }
                            changed = true;
                            localChanged = true;
                            continue;
                        }
                    }
                }

                ++it;
            }
        }
    } while (localChanged);
    return changed;
}
bool AddChainReductionPass::runOnFunction(Function *func)
{
    // std::cout << func->toString() << std::endl;
    bool changed = false;
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        for (int i = insts.size() - 1; i >= 0; --i)
        {
            Instruction *inst = insts[i].get();
            if (!inst || inst->getOpcode() != Opcode::Add)
                continue;

            Value *lhs = inst->getOperands()[0];
            Value *rhs = inst->getOperands()[1];

            // 1. 链式常量加法归约（仅处理有且仅有一个常量加数的情况）
            auto *ciLhs = dynamic_cast<ConstantInt *>(lhs);
            auto *ciRhs = dynamic_cast<ConstantInt *>(rhs);
            bool isAllConstant = true;
            if ((ciLhs && ciRhs) || (!ciRhs && !ciLhs))
                isAllConstant = false; // 跳过两个都是常量或都不是常量的加法
            if (isAllConstant)
            {
                int totalConst = 0;
                std::vector<Instruction *> chainInsts = {inst};

                // 当前加法指令的常量加数
                if (ciLhs)
                    totalConst += ciLhs->Value;
                else
                    totalConst += ciRhs->Value;

                // 从链头往前遍历
                Value *next = (ciLhs ? rhs : lhs); // 非常量的那个
                while (auto *prevAdd = dynamic_cast<BinaryOperator *>(next))
                {
                    if (prevAdd->getOpcode() != Opcode::Add || prevAdd->getUsers().size() > 1)
                        break;
                    Value *prevLhs = prevAdd->getOperands()[0];
                    Value *prevRhs = prevAdd->getOperands()[1];
                    auto *ciPrevLhs = dynamic_cast<ConstantInt *>(prevLhs);
                    auto *ciPrevRhs = dynamic_cast<ConstantInt *>(prevRhs);

                    if (ciPrevLhs && !ciPrevRhs)
                    {
                        totalConst += ciPrevLhs->Value;
                        next = prevRhs;
                        chainInsts.push_back(prevAdd);
                    }
                    else if (ciPrevRhs && !ciPrevLhs)
                    {
                        totalConst += ciPrevRhs->Value;
                        next = prevLhs;
                        chainInsts.push_back(prevAdd);
                    }
                    else
                    {
                        break;
                    }
                }
                // 如果链长度大于1且有常量加数
                if (chainInsts.size() > 1 && totalConst != 0)
                {
                    if (verbose)
                    {
                        debugInfo << "Found constant addition chain: \n";
                        for (auto *chainInst : chainInsts)
                            debugInfo << chainInst->toString() << "\n";
                        debugInfo << std::endl;
                    }
                    Value *addBase = next; // 最后一个非常量
                    auto *newConst = new ConstantInt(IntegerType::getInstance(), totalConst);
                    auto *newAdd = new BinaryOperator(Opcode::Add, addBase, newConst, inst->getName() + "_chainadd");
                    insts.insert(insts.begin() + i + 1, std::unique_ptr<Instruction>(newAdd));
                    inst->replaceAllUsesWith(newAdd);
                    inst->removeThisFromOperands();
                    needToDelete.push_back(insts[i].release());
                    insts.erase(insts.begin() + i);
                    changed = true;
                    // 删除链上的所有 add
                    for (auto *chainInst : chainInsts)
                    {
                        if (chainInst != inst)
                        {
                            auto chainIt = std::find_if(insts.begin(), insts.end(),
                                                        [chainInst](const std::unique_ptr<Instruction> &ptr)
                                                        { return ptr.get() == chainInst; });
                            if (chainIt != insts.end())
                            {
                                chainInst->removeThisFromOperands();
                                needToDelete.push_back(chainIt->release());
                                insts.erase(chainIt);
                            }
                        }
                    }
                    continue;
                }
            }
            // 2. 指针相等链式加法归约（如 y + x + x）
            int ptrChainLen = 1;
            Value *ptrBase = lhs;
            std::vector<Instruction *> ptrChainInsts = {inst};
            while (true)
            {
                auto *prevAdd = dynamic_cast<BinaryOperator *>(ptrBase);
                if (!prevAdd || prevAdd->getOpcode() != Opcode::Add || prevAdd->getUsers().size() > 1)
                    break;
                Value *prevLhs = prevAdd->getOperands()[0];
                Value *prevRhs = prevAdd->getOperands()[1];
                if (prevRhs == rhs)
                {
                    ptrChainLen++;
                    ptrChainInsts.push_back(prevAdd);
                    ptrBase = prevLhs;
                }
                else
                {
                    break;
                }
            }
            if (ptrChainLen > 1)
            {
                if (verbose)
                {
                    debugInfo << "Found pointer addition chain: \n";
                    for (auto *chainInst : ptrChainInsts)
                        debugInfo << chainInst->toString() << "\n";
                    debugInfo << std::endl;
                }
                // 归约为 y + x * n
                auto *mulInst = new BinaryOperator(Opcode::Mul, rhs, new ConstantInt(IntegerType::getInstance(), ptrChainLen), inst->getName() + "_mul");
                auto *addInst = new BinaryOperator(Opcode::Add, ptrBase, mulInst, inst->getName() + "_add");
                insts.insert(insts.begin() + i + 1, std::unique_ptr<Instruction>(mulInst));
                insts.insert(insts.begin() + i + 2, std::unique_ptr<Instruction>(addInst));
                inst->replaceAllUsesWith(addInst);
                inst->removeThisFromOperands();
                needToDelete.push_back(insts[i].release());
                insts.erase(insts.begin() + i);
                changed = true;
                // 删除链上的所有 add
                for (auto *chainInst : ptrChainInsts)
                {
                    if (chainInst != inst)
                    {
                        auto chainIt = std::find_if(insts.begin(), insts.end(),
                                                    [chainInst](const std::unique_ptr<Instruction> &ptr)
                                                    { return ptr.get() == chainInst; });
                        if (chainIt != insts.end())
                        {
                            chainInst->removeThisFromOperands();
                            needToDelete.push_back(chainIt->release());
                            insts.erase(chainIt);
                        }
                    }
                }
            }
        }
    }
    return changed;
}