#include "LoopPass.h"
#include <functional>
using namespace std;
using namespace optimization;
// ========== 循环不变代码移动 ==========
bool LoopInvariantCodeMotionPass::runOnFunction(Function *func)
{
    bool changed = false;
    // 记录每一轮pass后是否有外提变量，有则继续运行直到所有能外提变量全部外提
    bool localChanged;
    func->setLoops(ControlFlowAnalysis::findLoops(func)); // 确保循环信息是最新的
    auto loops = func->getLoops();
    do
    {
        int count = 0;
        localChanged = false;
        // 1. 查找所有循环

        for (auto &loop : loops)
        {
            // 2. 找到循环的前置块（preheader）
            BasicBlock *preheader = findPreheader(loop);
            if (!preheader)
                continue;

            // 3. 收集所有循环不变指令（记录指令和所在基本块）
            std::vector<std::pair<Instruction *, BasicBlock *>> invariants;
            bool foundNew;
            do
            {
                foundNew = false;
                for (auto *bb : loop.blocks)
                {
                    for (auto &instPtr : bb->getInstructions())
                    {
                        Instruction *inst = instPtr.get();
                        if (std::find_if(invariants.begin(), invariants.end(),
                                         [inst](const auto &p)
                                         { return p.first == inst; }) == invariants.end() &&
                            canMoveToPreheader(inst, loop) && isLoopInvariant(inst, loop))
                        {
                            invariants.emplace_back(inst, bb);
                            foundNew = true;
                        }
                    }
                }
            } while (foundNew); // 递增收集直到收敛

            // 4. 将循环不变指令移动到 preheader
            for (auto &[inst, fromBB] : invariants)
            {
                auto &insts = fromBB->getInstructions();
                auto it = std::find_if(insts.begin(), insts.end(),
                                       [&](const std::unique_ptr<Instruction> &ptr)
                                       { return ptr.get() == inst; });
                if (it != insts.end())
                {
                    if (verbose)
                    {
                        debugInfo << "Moved invariant instruction: " << inst->toString()
                                  << " from " << fromBB->getName() << " to preheader "
                                  << preheader->getName() << "\n";
                    }
                    std::unique_ptr<Instruction> movedInst = std::move(*it);
                    it = insts.erase(it);
                    // 将指令插入到 preheader 的末尾(终结指令之前)
                    preheader->insertBeforeTerminator(std::move(movedInst));
                    localChanged = true;
                    changed = true;
                    count++;
                }
            }
        }
    } while (localChanged);
    return changed;
}
bool LoopInvariantCodeMotionPass::canMoveToPreheader(Instruction *inst, const Loop &loop)
{
    // 外提合法判断条件：地址不是循环改变量、没有循环内的store、没有函数调用对顶层地址进行store
    if (auto loadInst = dynamic_cast<LoadInst *>(inst))
    {
        Value *addr = loadInst->getPointer();
        // 如果循环体内有对该地址的修改，则不能外提
        if (auto loadOp = dynamic_cast<Instruction *>(addr))
        {
            if (loop.contains(loadOp))
            {
                // 如果addr是循环变量，则不能外提
                return false;
            }
        }
        // 获取addr的原始指针操作数
        Value *loadOriginalPointer = loadInst->getOriginalPointer();
        // 判断循环体内是否有对该地址的store
        for (auto *loopBB : loop.blocks)
        {
            for (auto &instPtr : loopBB->getInstructions())
            {
                Instruction *store = instPtr.get();
                if (auto storeInst = dynamic_cast<StoreInst *>(store))
                {
                    Value *storeOriginalAddr = storeInst->getOriginalPointer();
                    // 如果store的地址和load的地址相同，则不能外提
                    if (isSameAddr(storeOriginalAddr, loadOriginalPointer))
                    {
                        return false; // 两条load之间有store，不能外提
                    }
                }
            }
        }
        // 判断是否有其他call对该地址的修改
        for (auto *loopBB : loop.blocks)
        {
            for (auto &instPtr : loopBB->getInstructions())
            {
                Instruction *call = instPtr.get();
                if (auto callInst = dynamic_cast<CallInst *>(call))
                {
                    // 如果是调用函数，且函数有副作用，则不能外提
                    if (callInst->HasModifiedArray(loadOriginalPointer))
                    {
                        return false;
                    }
                }
            }
        }
        // 否则可以外提
        return true;
    }
    // 增加对phi指令的特殊处理，phi用于处理合流，不能外提
    // copy指令不能外提，因为是由合流产生
    return !inst->mayHaveSideEffects() && inst->getOpcode() != Opcode::Copy && inst->getOpcode() != Opcode::Phi;
}
// 判断指令是否在循环不变
bool LoopInvariantCodeMotionPass::isLoopInvariant(Instruction *inst, const Loop &loop)
{
    // 检查指令是否在循环中，并且不依赖于循环
    for (auto *op : inst->getOperands())
    {
        if (auto *def = dynamic_cast<Instruction *>(op))
        {
            // 如果操作数是循环中的变量，则不是循环不变
            if (loop.contains(def))
            {
                return false;
            }
        }
    }
    return true;
}
// 查找循环头的前驱块
BasicBlock *LoopInvariantCodeMotionPass::findPreheader(const Loop &loop)
{
    BasicBlock *header = loop.header;
    BasicBlock *preheader = nullptr;
    int count = 0;
    for (auto *pred : header->getPredecessors())
    {
        // preheader 必须不在循环体内
        if (std::find(loop.blocks.begin(), loop.blocks.end(), pred) == loop.blocks.end())
        {
            preheader = pred;
            ++count;
        }
    }
    // 必须只有一个循环外前驱才安全
    if (count == 1)
        return preheader;
    return nullptr;
}
bool RemoveUselessWhilePass::runOnFunction(Function *func)
{
    bool changed = false;
    // 允许多次遍历，直到没有可删的无用循环
    bool localChanged;
    do
    {
        localChanged = false;
        func->setLoops(ControlFlowAnalysis::findLoops(func)); // 确保循环信息是最新的
        auto &loops = func->getLoops();
        for (const auto &loop : loops)
        {
            if (loop.blocks.size() > 2)
                continue;
            bool onlyInc = true;
            BasicBlock *whilecond = nullptr;
            BasicBlock *whilebody = nullptr;
            for (auto *bb : loop.blocks)
            {
                if (bb == loop.header)
                {
                    whilecond = bb;
                    continue;
                }
                whilebody = bb;
                if (bb->getInstructions().size() > 2)
                {
                    onlyInc = false;
                    break;
                }
                for (auto &instPtr : bb->getInstructions())
                {
                    Instruction *inst = instPtr.get();
                    if (inst->getOpcode() == Opcode::Br)
                        continue;
                    if (auto *bin = dynamic_cast<BinaryOperator *>(inst))
                    {
                        if (!(bin->getOpcode() == Opcode::Add || bin->getOpcode() == Opcode::Sub))
                        {
                            onlyInc = false;
                            break;
                        }
                        if (!loop.IsInductionVar(bin->getLHS()->getName()) && !loop.IsInductionVar(bin->getRHS()->getName()))
                        {
                            onlyInc = false;
                            break;
                        }
                    }
                    else
                        onlyInc = false;
                    if (inst->hasExternalUse(loop))
                    {
                        onlyInc = false;
                        break;
                    }
                }
                if (!onlyInc)
                    break;
            }
            if (!onlyInc)
                continue;

            BasicBlock *prehead = nullptr;
            int count = 0;
            for (auto *pred : loop.header->getPredecessors())
            {
                if (pred == whilebody)
                    continue;
                count++;
                if (count > 1)
                {
                    onlyInc = false;
                    break;
                }
            }
            BasicBlock *exitBlock = nullptr;
            count = 0;
            for (auto *succ : loop.header->getSuccessors())
            {
                if (succ == whilebody)
                    continue;
                exitBlock = succ;
                count++;
                if (count > 1)
                {
                    onlyInc = false;
                    break;
                }
            }
            for (auto *pred : loop.header->getPredecessors())
            {
                if (pred == whilebody)
                    continue;
                for (auto &instPtr : pred->getInstructions())
                {
                    Instruction *inst = instPtr.get();
                    if (auto *br = dynamic_cast<BranchInst *>(inst))
                    {
                        if (br->getTrueBlock() == loop.header)
                            br->setTrueBlock(exitBlock);
                        if (br->getFalseBlock() == loop.header)
                            br->setFalseBlock(exitBlock);
                    }
                    prehead = pred;
                }
                pred->addSuccessor(exitBlock);
                exitBlock->addPredecessor(pred);
            }
            for (auto *bb : loop.blocks)
            {
                bb->removeSelfBasicBlock();
            }
            auto &insts = exitBlock->getInstructions();
            for (auto it = insts.begin(); it != insts.end();)
            {
                if (auto *phi = dynamic_cast<PhiInst *>(it->get()))
                {
                    auto incomingBlocks = phi->getIncomingBlocks();
                    if (find(incomingBlocks.begin(),
                             incomingBlocks.end(), loop.header) != phi->getIncomingBlocks().end())
                    {
                        phi->removeThisFromOperands();
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                        continue;
                    }
                }
                ++it;
            }
            localChanged = true;
            changed = true;
            if (verbose)
            {
                debugInfo << "RemoveUselessWhilePass: Removed useless while loop at header " << loop.header->getName() << "\n";
            }
            break; // 只处理一个，后面会重新获取loops
        }
    } while (localChanged);
    return changed;
}
// 目前只支持整型规约
bool LoopSumReductionPass::runOnFunction(Function *func)
{
    bool changed = false;
    func->setLoops(ControlFlowAnalysis::findLoops(func)); // 确保循环信息是最新的
    auto &loops = func->getLoops();
    for (const auto loop : loops)
    {
        // 循环头
        BasicBlock *header = loop.header;
        // 检查是否为 while(j < n) 头部
        // 获取终结指令前一条指令
        auto size = header->getInstructions().size();
        if (header->getInstructions().size() < 2)
            continue; // 至少需要两条指令
        auto *cmp = dynamic_cast<ICmpInst *>(header->getInstructions()[size - 2].get());
        if (!cmp || cmp->getPredicate() != ICmpInst::ICMP_SLT)
            continue;
        Value *jVar = cmp->getLHS();
        Value *nVar = cmp->getRHS();
        Value *sumVar = nullptr;
        int count_phi = 0;
        bool canReduce = true;
        for (auto &instPtr : header->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
            {
                if (count_phi >= 2)
                {
                    canReduce = false; // 只处理两个phi指令->简单求和循环
                    break;
                }
                if (phi != jVar)
                {
                    sumVar = phi;
                }
                count_phi++;
            }
        }
        if (!canReduce || !sumVar)
            continue; // 不是while(j<n)循环，或者没有sum变量
        // 找到循环体
        BasicBlock *body = nullptr;
        if (loop.blocks.size() > 2)
            continue; // 只处理简单循环
        for (auto *lp_block : loop.blocks)
        {
            if (lp_block != header)
                body = lp_block;
        }
        if (!body)
            continue; // 没有找到循环体
        // 检查循环体是否有 sum = sum + ...; j = j + 1;
        BinaryOperator *sumAdd = nullptr, *jInc = nullptr;
        bool isFloat = false;
        for (auto &instPtr : body->getInstructions())
        {
            if (auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get()))
            {
                // j = j + 1
                if (bin->getOpcode() == Opcode::Add &&
                    (bin->getLHS() == jVar && dynamic_cast<ConstantInt *>(bin->getRHS()) || bin->getRHS() == jVar && dynamic_cast<ConstantInt *>(bin->getLHS())))
                {
                    jInc = bin;
                }
                // sum = sum + j 或 sum = sum + (a+j)*(b+j)
                // 浮点数暂不支持后面一种，会有精度误差
                else if ((bin->getOpcode() == Opcode::Add || bin->getOpcode() == Opcode::FAdd) && (bin->getLHS() == sumVar || bin->getRHS() == sumVar))
                {
                    isFloat = bin->getType()->isFloatTy();
                    sumAdd = bin;
                }
            }
        }
        if (!sumAdd || !jInc)
            continue;

        // 检查sumAdd右侧是否为j，或为(a+j)*(b+j)
        Value *sumExpr = nullptr;
        if (sumAdd->getRHS() == jVar || sumAdd->getLHS() == jVar)
        {
            // sum = sum + j
            sumExpr = jVar;
        }
        else if (auto *cast = dynamic_cast<CastInst *>(sumAdd->getRHS()))
        {
            // sum = sum + (float)j
            if (cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
            {
                sumExpr = cast;
            }
        }
        else if (auto *cast = dynamic_cast<CastInst *>(sumAdd->getLHS()))
        {
            // sum = sum + (float)j
            if (cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
            {
                sumExpr = cast;
            }
        }
        else if (auto *mul = dynamic_cast<BinaryOperator *>(sumAdd->getRHS()))
        {
            // sum = sum + (a+j)*(b+j)
            if (mul->getOpcode() == Opcode::Mul)
            {
                auto *add1 = dynamic_cast<BinaryOperator *>(mul->getLHS());
                auto *add2 = dynamic_cast<BinaryOperator *>(mul->getRHS());
                if (add1 && add2 &&
                    add1->getOpcode() == Opcode::Add &&
                    add2->getOpcode() == Opcode::Add &&
                    (add1->getLHS() == jVar || add1->getRHS() == jVar) &&
                    (add2->getLHS() == jVar || add2->getRHS() == jVar))
                {
                    sumExpr = mul;
                }
            }
            // else if (mul->getOpcode() == Opcode::FMul)
            // {
            //     auto *add1 = dynamic_cast<BinaryOperator *>(mul->getLHS());
            //     auto *add2 = dynamic_cast<BinaryOperator *>(mul->getRHS());
            //     if (add1 && add2 &&
            //         add1->getOpcode() == Opcode::FAdd &&
            //         add2->getOpcode() == Opcode::FAdd )
            //     {
            //         bool isValid1=false;
            //         bool isValid2=false;
            //         if(auto *cast=dynamic_cast<CastInst *>(add1->getLHS()))
            //         {
            //             if(cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
            //             {
            //                 isValid1=true;
            //             }
            //         }
            //         else if(auto *cast=dynamic_cast<CastInst *>(add1->getRHS()))
            //         {
            //             if(cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
            //             {
            //                 isValid1=true;
            //             }
            //         }
            //         if(auto *cast=dynamic_cast<CastInst *>(add2->getLHS()))
            //         {
            //             if(cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
            //             {
            //                 isValid2=true;
            //             }
            //         }
            //         else if(auto *cast=dynamic_cast<CastInst *>(add2->getRHS()))
            //         {
            //             if(cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
            //             {
            //                 isValid2=true;
            //             }
            //         }
            //         if(isValid1&&isValid2)sumExpr = mul;
            //     }
            // }
        }
        if (!sumExpr)
            continue;
        // 从header中的phi查找到j和sum初值
        auto *jPhi = dynamic_cast<PhiInst *>(jVar);
        if (!jPhi)
            continue; // j不是phi指令，无法获取初值
        Value *jInit = nullptr;
        size_t phiIncomingNum = jPhi->getNumIncomingValues();
        if (phiIncomingNum > 2)
            continue; // 只处理简单循环，phi指令的输入必须只有两个
        for (size_t i = 0; i < phiIncomingNum; ++i)
        {
            if (jPhi->getIncomingBlock(i) != body)
            {
                jInit = jPhi->getIncomingValue(i);
                break;
            }
        }
        if (auto *constInt = dynamic_cast<ConstantInt *>(jInit))
        {
            if (constInt->Value != 0)
            {
                // 如果j初值不为0，则不需要进行归约
                continue;
            }
        }
        else
        {
            continue; // j初值不是常量0，无法进行归约
        }
        // 获取sum初值
        auto *sumPhi = dynamic_cast<PhiInst *>(sumAdd->getLHS());
        if (!sumPhi)
            continue; // sum不是phi指令，无法获取初值
        Value *sumInit = nullptr;
        phiIncomingNum = sumPhi->getNumIncomingValues();
        if (phiIncomingNum > 2)
            continue; // 只处理简单循环，phi指令的输入必须只有两个
        // 查找sum的初值
        for (size_t i = 0; i < phiIncomingNum; ++i)
        {
            if (sumPhi->getIncomingBlock(i) != body)
            {
                sumInit = sumPhi->getIncomingValue(i);
                break;
            }
        }
        // sum的初值可以不为0，因为sum可以是任意初值
        // 获取前驱块用于插入
        BasicBlock *preheader = nullptr;
        int count = 0;
        for (auto *pred : header->getPredecessors())
        {
            if (pred != body)
            {
                preheader = pred;
                count++;
            }
            if (count > 1)
            {
                preheader = nullptr; // 如果有多个前驱，则不处理
                break;
            }
        }
        // 获取退出块用于连接
        BasicBlock *exitBlock = nullptr;
        count = 0;
        for (auto *succ : header->getSuccessors())
        {
            if (succ != body)
            {
                exitBlock = succ;
                count++;
            }
            if (count > 1)
            {
                exitBlock = nullptr; // 如果有多个出口，则不处理
                break;
            }
        }
        if (!preheader || !exitBlock)
            continue;
        Instruction *formula = nullptr;
        if (sumExpr == jVar)
        {
            // sum = ∑j = n(n-1)/2
            // 这里n就是循环次数，j从0开始到n-1
            // 计算n(n-1)/2
            auto *n_minus_1 = new BinaryOperator(Opcode::Sub, nVar, new ConstantInt(IntegerType::getInstance(), 1), "n-1");
            auto *n_n_minus_1 = new BinaryOperator(Opcode::Mul, nVar, n_minus_1, "n(n-1)");
            auto *half = new BinaryOperator(Opcode::SDiv, n_n_minus_1, new ConstantInt(IntegerType::getInstance(), 2), "n(n-1)/2");
            auto *sumInit_half = new BinaryOperator(Opcode::Add, sumInit, half, "sum_init_half");
            Instruction *cast = nullptr;
            if (isFloat)
            {
                cast = new CastInst(Opcode::SIToFP, sumInit_half, FloatType::getInstance(), "sum_init_half_cast");
            }
            formula = isFloat ? cast : sumInit_half;
            // 将公式添加到preheader
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_minus_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_n_minus_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(half));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(sumInit_half));
            if (cast)
            {
                preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(cast));
            }
        }
        else
        {
            // sum = ∑(a+j)*(b+j) = n*a*b + n*(n-1)/2*(a+b) + n*(n-1)*(2n-1)/6
            auto *a = dynamic_cast<BinaryOperator *>(sumExpr)->getLHS();
            auto *b = dynamic_cast<BinaryOperator *>(sumExpr)->getRHS();
            // 获得a,b，如果其中一个是j，则另一个是常量
            if (auto *binaryInst = dynamic_cast<BinaryOperator *>(a))
            {
                if (binaryInst->getOpcode() != Opcode::Add) // && binaryInst->getOpcode() != Opcode::FAdd)
                {
                    // 如果不是加法，则不处理
                    continue;
                }
                if (binaryInst->getOpcode() == Opcode::Add)
                {
                    if (binaryInst->getLHS() == jVar)
                    {
                        a = binaryInst->getRHS();
                    }
                    else if (binaryInst->getRHS() == jVar)
                    {
                        a = binaryInst->getLHS();
                    }
                }
                // else if (binaryInst->getOpcode() == Opcode::FAdd)
                // {
                //     if(auto *cast=dynamic_cast<CastInst *>(binaryInst->getLHS()))
                //     {
                //         if (cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
                //         {
                //             a = binaryInst->getRHS();
                //         }
                //     }
                //     else if(auto *cast=dynamic_cast<CastInst *>(binaryInst->getRHS()))
                //     {
                //         if (cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
                //         {
                //             a = binaryInst->getLHS();
                //         }
                //     }
                // }
            }
            else
                continue;
            if (auto *binaryInst = dynamic_cast<BinaryOperator *>(b))
            {
                if (binaryInst->getOpcode() != Opcode::Add) //&& binaryInst->getOpcode() != Opcode::FAdd)
                {
                    // 如果不是加法，则不处理
                    continue;
                }
                if (binaryInst->getOpcode() == Opcode::Add)
                {
                    if (binaryInst->getLHS() == jVar)
                    {
                        b = binaryInst->getRHS();
                    }
                    else if (binaryInst->getRHS() == jVar)
                    {
                        b = binaryInst->getLHS();
                    }
                }
                // else if (binaryInst->getOpcode() == Opcode::FAdd)
                // {
                //     if(auto *cast=dynamic_cast<CastInst *>(binaryInst->getLHS()))
                //     {
                //         if (cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
                //         {
                //             b = binaryInst->getRHS();
                //         }
                //     }
                //     else if(auto *cast=dynamic_cast<CastInst *>(binaryInst->getRHS()))
                //     {
                //         if (cast->getOpcode() == Opcode::SIToFP && cast->getOperand() == jVar)
                //         {
                //             b = binaryInst->getLHS();
                //         }
                //     }
                // }
            }
            else
                continue;
            if (isFloat)
                continue;
            // 这种情况暂不支持float，会有精度问题
            // 这种情况下全部转为float再计算
            auto addOp = isFloat ? Opcode::FAdd : Opcode::Add;
            auto mulOp = isFloat ? Opcode::FMul : Opcode::Mul;
            auto divOp = isFloat ? Opcode::FDiv : Opcode::SDiv;
            auto subOp = isFloat ? Opcode::FSub : Opcode::Sub;
            Value *One = nullptr, *Two = nullptr, *Six = nullptr;
            if (isFloat)
            {
                One = new ConstantFloat(FloatType::getInstance(), 1.0f);
                Two = new ConstantFloat(FloatType::getInstance(), 2.0f);
                Six = new ConstantFloat(FloatType::getInstance(), 6.0f);
                nVar = new CastInst(Opcode::SIToFP, nVar, FloatType::getInstance(), "n_float");
                preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(dynamic_cast<Instruction *>(nVar)));
            }
            else
            {
                One = new ConstantInt(IntegerType::getInstance(), 1);
                Two = new ConstantInt(IntegerType::getInstance(), 2);
                Six = new ConstantInt(IntegerType::getInstance(), 6);
            }
            // 计算n*a*b
            auto *a_mutiply_b = new BinaryOperator(mulOp, a, b, "ab");
            auto *n_a_mutiply_b = new BinaryOperator(mulOp, nVar, a_mutiply_b, "nab");
            // 计算(a+b)*n*(n-1)/2
            auto *n_minus_1 = new BinaryOperator(subOp, nVar, One, "n-1");
            auto *n_n_minus_1 = new BinaryOperator(mulOp, nVar, n_minus_1, "n(n-1)");
            auto *half = new BinaryOperator(divOp, n_n_minus_1, Two, "n(n-1)/2");
            auto *a_plus_b = new BinaryOperator(addOp, a, b, "a+b");
            auto *n_n_minus_1_half = new BinaryOperator(mulOp, half, a_plus_b, "n(n-1)/2*(a+b)");
            // 计算n*(n-1)*(2n-1)/6
            auto *two_n = new BinaryOperator(mulOp, Two, nVar, "2n");
            auto *two_n_minus_1 = new BinaryOperator(subOp, two_n, One, "2n-1");
            auto *n_n_minus_1_two_n_minus_1 = new BinaryOperator(mulOp, n_n_minus_1, two_n_minus_1, "n(n-1)*(2n-1)");
            auto *n_n_minus_1_two_n_minus_1_six = new BinaryOperator(divOp, n_n_minus_1_two_n_minus_1, Six, "n(n-1)*(2n-1)/6");
            // 求和
            auto *sum_1 = new BinaryOperator(addOp, n_a_mutiply_b, n_n_minus_1_half, "sum_1");
            auto *sum_2 = new BinaryOperator(addOp, sum_1, n_n_minus_1_two_n_minus_1_six, "sum_2");
            auto *sum_3 = new BinaryOperator(addOp, sum_2, sumInit, "sum_3");
            formula = sum_3;
            // 添加
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(a_mutiply_b));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_a_mutiply_b));

            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_minus_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_n_minus_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(half));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(a_plus_b));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_n_minus_1_half));

            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(two_n));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(two_n_minus_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_n_minus_1_two_n_minus_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(n_n_minus_1_two_n_minus_1_six));

            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(sum_1));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(sum_2));
            preheader->insertBeforeTerminator(std::unique_ptr<Instruction>(sum_3));
        }
        // 替换原来prehead的sumphi
        sumPhi->replaceAllUsesWith(formula);
        sumPhi->removeThisFromOperands();
        // 删除原来的sumphi指令
        needToDelete.push_back(sumPhi);
        preheader->Instructions.erase(std::remove_if(preheader->getInstructions().begin(), preheader->getInstructions().end(),
                                                     [sumPhi](const std::unique_ptr<Instruction> &inst)
                                                     { return inst.get() == sumPhi; }),
                                      preheader->getInstructions().end());
        // 替换原来的jphi
        jPhi->replaceAllUsesWith(nVar);
        jPhi->removeThisFromOperands();
        // 删除原来的jphi指令
        needToDelete.push_back(jPhi);
        preheader->Instructions.erase(std::remove_if(preheader->getInstructions().begin(), preheader->getInstructions().end(),
                                                     [jPhi](const std::unique_ptr<Instruction> &inst)
                                                     { return inst.get() == jPhi; }),
                                      preheader->getInstructions().end());
        // 修正prehead的跳转指令到exitBlock
        for (auto &instPtr : preheader->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (auto *br = dynamic_cast<BranchInst *>(inst))
            {
                if (br->getTrueBlock() == header)
                {
                    // 如果是循环头的跳转，直接跳到循环出口
                    br->setTrueBlock(exitBlock);
                }
                if (br->getFalseBlock() == header)
                {
                    // 如果是循环头的跳转，直接跳到循环出口
                    br->setFalseBlock(exitBlock);
                }
            }
        }
        for (auto &bb : loop.blocks)
        {
            bb->removeSelfBasicBlock(); // 删除基本块的CFG连接，便于删除基本块
        }
        // 建立prehead到while.exit的连接
        preheader->addSuccessor(exitBlock);
        exitBlock->addPredecessor(preheader);

        // 修正exit的phi 指令
        auto &exitInsts = exitBlock->getInstructions();
        for (auto it = exitInsts.begin(); it != exitInsts.end();)
        {
            if (auto *phi = dynamic_cast<PhiInst *>(it->get()))
            {
                // 如果有来自header输入的phi
                if (find(phi->getIncomingBlocks().begin(), phi->getIncomingBlocks().end(), header) != phi->getIncomingBlocks().end())
                {
                    phi->replaceIncomingBasicBlock(header, preheader); // 替换为preheader
                    continue;
                }
            }
            ++it;
        }
        changed = true;
        if (verbose)
        {
            debugInfo << "LoopSumReductionPass: Reduced sum loop at header " << header->getName() << " to formula.\n";
        }
        break; // 只处理一个循环
    }
    return changed;
}
bool ModLoopReductionPass ::runOnFunction(Function *func)
{
    bool changed = false;
    func->setLoops(ControlFlowAnalysis::findLoops(func)); // 确保循环信息是最新的
    auto &loops = func->getLoops();
    for (const auto &loop : loops)
    {
        BasicBlock *headBlock = loop.header;
        if (headBlock->getInstructions().size() < 2)
            continue;
        // 1. 检查循环条件 while(i < maxindex)
        // 只处理小于等于
        auto *cmp = dynamic_cast<ICmpInst *>(headBlock->getInstructions()[headBlock->getInstructions().size() - 2].get());
        if (!cmp || cmp->getPredicate() != ICmpInst::ICMP_SLT)
            continue;
        Value *iVar = cmp->getLHS();
        Value *maxindex = cmp->getRHS();

        // 2. 检查phi获取i和sum初值
        PhiInst *iPhi = nullptr, *sumPhi = nullptr;
        for (auto &instPtr : headBlock->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
            {
                if (phi == iVar)
                {
                    iPhi = phi;
                }
                else
                    sumPhi = phi;
            }
        }
        if (!iPhi || !sumPhi)
            continue;
        Value *iInit = nullptr, *sumInit = nullptr;
        for (size_t i = 0; i < iPhi->getNumIncomingValues(); ++i)
        {
            auto *iInitInst = dynamic_cast<Instruction *>(iPhi->getIncomingValue(i));
            if (iInitInst == nullptr || !loop.contains(iInitInst))
            {
                iInit = iPhi->getIncomingValue(i);
                break;
            }
        }
        for (size_t i = 0; i < sumPhi->getNumIncomingValues(); ++i)
        {
            auto *sumInitInst = dynamic_cast<Instruction *>(sumPhi->getIncomingValue(i));
            if (sumInitInst == nullptr || !loop.contains(sumInitInst))
            {
                sumInit = sumPhi->getIncomingValue(i);
                break;
            }
        }
        // 4. 在所有body块中查找 sum += x; sum %= remconst; i++
        BinaryOperator *sumAdd = nullptr, *sumMod = nullptr, *iInc = nullptr;
        Value *x = nullptr, *remconst = nullptr, *stepLength = nullptr;
        for (auto *bb : loop.blocks)
        {
            if (bb == headBlock)
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                if (auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get()))
                {
                    if (!sumAdd && bin->getOpcode() == Opcode::Add &&
                        (bin->getLHS() == sumPhi || bin->getRHS() == sumPhi))
                    {
                        sumAdd = bin;
                        x = (bin->getLHS() == sumPhi) ? bin->getRHS() : bin->getLHS();
                    }
                    if (!sumMod && bin->getOpcode() == Opcode::SRem &&
                        bin->getLHS() == sumAdd)
                    {
                        sumMod = bin;
                        remconst = bin->getRHS();
                    }
                    if (!iInc && bin->getOpcode() == Opcode::Add &&
                        (bin->getLHS() == iPhi || bin->getRHS() == iPhi))
                    {
                        iInc = bin;
                        stepLength = (bin->getLHS() == iPhi) ? bin->getRHS() : bin->getLHS();
                    }
                }
            }
        }
        if (!sumAdd || !sumMod || !iInc || !x || !remconst)
            continue;
        // 5. 只处理常量remconst和x
        auto *remconstC = dynamic_cast<ConstantInt *>(remconst);
        auto *xC = dynamic_cast<ConstantInt *>(x);
        if (!remconstC || !xC)
            continue;
        // 超过2^16的常量不处理,因为会溢出
        if (remconstC->Value > 65536)
            continue;
        // 6. 生成归约公式
        // 公式 initsum%remconst+(maxindex-i/stepLength*x%remconst)&remconst
        auto *sumInitMod = new BinaryOperator(Opcode::SRem, sumInit, remconst, "sumInit_mod");
        auto *max_minus_i = new BinaryOperator(Opcode::Sub, maxindex, iInit, "max_minus_i");
        auto *max_minus_i_div_step = new BinaryOperator(Opcode::SDiv, max_minus_i, stepLength, "max_minus_i_div");
        auto *max_minus_i_mod = new BinaryOperator(Opcode::SRem, max_minus_i_div_step, remconst, "max_minus_i_mod");
        auto *x_mod = new BinaryOperator(Opcode::SRem, x, remconst, "x_mod");
        auto *mul = new BinaryOperator(Opcode::Mul, max_minus_i_mod, x_mod, "mul_mod");
        auto *mul_mod = new BinaryOperator(Opcode::SRem, mul, remconst, "mul_mod2");
        auto *finalSum = new BinaryOperator(Opcode::Add, sumInitMod, mul_mod, "final_sum");
        auto *finalSumMod = new BinaryOperator(Opcode::SRem, finalSum, remconst, "final_sum_mod");

        auto *if_sumPhi = new PhiInst(sumInit->getType(), "if_sum_phi");
        auto *i_Phi = new PhiInst(iInit->getType(), "i_phi");
        // 7. 替换循环为if-else
        BasicBlock *preBlock = nullptr;
        for (auto *pred : headBlock->getPredecessors())
            if (find(loop.blocks.begin(), loop.blocks.end(), pred) == loop.blocks.end())
                preBlock = pred;
        if (!preBlock)
            continue;
        BasicBlock *exitBlock = nullptr;
        for (auto *succ : headBlock->getSuccessors())
            if (find(loop.blocks.begin(), loop.blocks.end(), succ) == loop.blocks.end())
                exitBlock = succ;
        if (!exitBlock)
            continue;

        auto *cond = new ICmpInst(ICmpInst::ICMP_SLT, iInit, maxindex, "modulo_cond");
        auto *thenBB = new BasicBlock("modulo_then", func);
        auto *elseBB = new BasicBlock("modulo_else", func);
        // phi添加输入
        if_sumPhi->addIncoming(finalSumMod, thenBB);
        if_sumPhi->addIncoming(sumInit, elseBB);
        // 如果来自then，则已经循环到最大
        i_Phi->addIncoming(maxindex, thenBB);
        i_Phi->addIncoming(iInit, elseBB);
        // then块跳转
        thenBB->addInstruction(std::unique_ptr<Instruction>(sumInitMod));
        thenBB->addInstruction(std::unique_ptr<Instruction>(max_minus_i));
        thenBB->addInstruction(std::unique_ptr<Instruction>(max_minus_i_div_step));
        thenBB->addInstruction(std::unique_ptr<Instruction>(max_minus_i_mod));
        thenBB->addInstruction(std::unique_ptr<Instruction>(max_minus_i));
        thenBB->addInstruction(std::unique_ptr<Instruction>(x_mod));
        thenBB->addInstruction(std::unique_ptr<Instruction>(mul));
        thenBB->addInstruction(std::unique_ptr<Instruction>(mul_mod));
        thenBB->addInstruction(std::unique_ptr<Instruction>(finalSum));
        thenBB->addInstruction(std::unique_ptr<Instruction>(finalSumMod));

        // 跳转到merge块
        thenBB->addInstruction(std::make_unique<BranchInst>(exitBlock));
        elseBB->addInstruction(std::make_unique<BranchInst>(exitBlock));

        exitBlock->addInstruction(std::unique_ptr<Instruction>(if_sumPhi));
        exitBlock->addInstruction(std::unique_ptr<Instruction>(i_Phi));
        // if.cond块添加跳转
        preBlock->addInstruction(std::unique_ptr<Instruction>(cond));
        preBlock->addInstruction(std::make_unique<BranchInst>(cond, thenBB, elseBB));

        preBlock->addSuccessor(thenBB);
        preBlock->addSuccessor(elseBB);
        thenBB->addPredecessor(preBlock);
        elseBB->addPredecessor(preBlock);
        thenBB->addSuccessor(exitBlock);
        elseBB->addSuccessor(exitBlock);
        exitBlock->addPredecessor(thenBB);
        exitBlock->addPredecessor(elseBB);

        sumPhi->replaceAllUsesWith(if_sumPhi);
        iPhi->replaceAllUsesWith(i_Phi);
        // 删除原循环体
        for (auto *bb : loop.blocks)
            bb->removeSelfBasicBlock();

        func->addBasicBlock(std::unique_ptr<BasicBlock>(thenBB));
        func->addBasicBlock(std::unique_ptr<BasicBlock>(elseBB));
        // 删除原来preBlock的跳转
        auto &preInsts = preBlock->getInstructions();
        // 先收集，统一删除
        std::vector<Instruction *> branchToDelete;
        for (auto it = preInsts.begin(); it != preInsts.end();)
        {
            if (auto *br = dynamic_cast<BranchInst *>(it->get()))
            {
                // 如果是无条件跳转，删除
                if (!br->isConditional() && br->getTrueBlock() == headBlock)
                {
                    branchToDelete.push_back(br);
                }
            }
            ++it;
        }
        for (auto *br : branchToDelete)
        {
            br->removeThisFromOperands();
            needToDelete.push_back(br);
            preInsts.erase(std::remove_if(preInsts.begin(), preInsts.end(),
                                          [br](const std::unique_ptr<Instruction> &inst)
                                          { return inst.get() == br; }),
                           preInsts.end());
        }
        // 修正exit的phi输入
        auto &exitInsts = exitBlock->getInstructions();
        for (auto it = exitInsts.begin(); it != exitInsts.end();)
        {
            if (auto *phi = dynamic_cast<PhiInst *>(it->get()))
            {
                // 这里需要先获取incomingBlocks再用于find比较，否则获得的是拷贝
                auto incomingBlocks = phi->getIncomingBlocks();
                // 如果有来自header输入的phi
                if (find(incomingBlocks.begin(), incomingBlocks.end(), headBlock) != incomingBlocks.end())
                {
                    phi->replaceIncomingBasicBlock(headBlock, preBlock); // 替换为preBlock
                    continue;
                }
            }
            ++it;
        }
        changed = true;
        if (verbose)
            debugInfo << "LoopModuloReductionPass: Reduced loop at header " << headBlock->getName() << " to modulo formula.\n";
        break; // 只处理一个
    }
    return changed;
}

bool LoopUnrollingPass::runOnFunction(Function *func)
{
    bool changed = false;
    func->setLoops(ControlFlowAnalysis::findLoops(func));
    auto &loops = func->getLoops();
    for (const auto &loop : loops)
    {
        BasicBlock *header = loop.header;
        // 只处理简单循环
        if (header->getInstructions().size() < 2 || loop.blocks.size() > 2)
            continue;

        // 1. 收集所有phi指令
        std::vector<PhiInst *> headerPhis;
        for (auto &instPtr : header->getInstructions())
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                headerPhis.push_back(phi);
        if (headerPhis.empty())
            continue;

        // 2. 找到循环条件cmp
        auto &headerInsts = header->getInstructions();

        auto *cmp = dynamic_cast<ICmpInst *>(headerInsts[headerInsts.size() - 2].get());
        if (!cmp)
            continue;
        // 只处理 < 或 <=，否则跳过
        if (cmp->getPredicate() != ICmpInst::ICMP_SLT && cmp->getPredicate() != ICmpInst::ICMP_SLE)
            continue;
        // 3. 找到induction variable phi
        PhiInst *indPhi = nullptr;
        Value *indVar = nullptr;
        Value *boundVal = nullptr;
        int cmpSide = -1;
        for (int side = 0; side < 2; ++side)
        {
            Value *v = (side == 0) ? cmp->getLHS() : cmp->getRHS();
            for (auto *phi : headerPhis)
            {
                if (v == phi)
                {
                    indVar = v;
                    indPhi = phi;
                    cmpSide = side;
                    boundVal = (side == 0) ? cmp->getRHS() : cmp->getLHS();
                    break; // 找到就退出
                }
            }
            if (indPhi)
                break;
        }
        if (!indVar || !indPhi || !boundVal)
            continue;

        // 4. 获取所有phi的初值和增量
        std::unordered_map<PhiInst *, Value *> phiInit, phiInc;
        for (auto *phi : headerPhis)
        {
            for (size_t i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                BasicBlock *from = phi->getIncomingBlock(i);
                if (std::find(loop.blocks.begin(), loop.blocks.end(), from) == loop.blocks.end())
                    phiInit[phi] = phi->getIncomingValue(i);
                else
                    phiInc[phi] = phi->getIncomingValue(i);
            }
        }
        if (!phiInit[indPhi])
            continue;

        // 5. 获取indVar增量
        int intcValue = -1;
        // 如果不是i=i+1自增则跳过
        if (auto *bin = dynamic_cast<BinaryOperator *>(phiInc[indPhi]))
        {
            if (bin->getOpcode() != Opcode::Add || (bin->getLHS() != indVar && bin->getRHS() != indVar))
                continue;
            if (auto *constVal = dynamic_cast<ConstantInt *>(bin->getRHS()))
                intcValue = constVal->Value;
            else if (auto *constVal = dynamic_cast<ConstantInt *>(bin->getLHS()))
                intcValue = constVal->Value;
            else
                continue;
        }
        // 不是常数，跳过
        if (intcValue < 0)
            continue;

        // 6. tripCount
        int tripCount = -1;
        auto *initConst = dynamic_cast<ConstantInt *>(phiInit[indPhi]);
        auto *boundConst = dynamic_cast<ConstantInt *>(boundVal);
        if (initConst && boundConst)
        {
            int init = initConst->Value;
            int bound = boundConst->Value;
            if (cmp->getPredicate() == ICmpInst::ICMP_SLT)
                tripCount = (cmpSide == 0) ? (bound - init) : (init - bound);
            else if (cmp->getPredicate() == ICmpInst::ICMP_SLE)
                tripCount = (cmpSide == 0) ? (bound - init + 1) : (init - bound + 1);
            tripCount = tripCount / intcValue;
        }

        // 7. 找到循环体
        BasicBlock *body = nullptr;
        for (auto *bb : loop.blocks)
            if (bb != header)
                body = bb;
        if (!body)
            continue;
        // // 判断内存访问比例是否超过阈值0.66
        // // 0.66对应循环体只有a[i]=b[i]这种情况
        // int totalInst = 0, memInst = 0;
        // for (auto &instPtr : body->getInstructions())
        // {
        //     if (dynamic_cast<LoadInst *>(instPtr.get()) || dynamic_cast<StoreInst *>(instPtr.get()))
        //     {
        //         memInst++;
        //         totalInst++;
        //     }
        //     else if (dynamic_cast<BinaryOperator *>(instPtr.get()))
        //         totalInst++;
        // }
        // if (totalInst > 0 && memInst * 1.0 / totalInst > 0.66)
        //     continue;
        // 新增：只处理 body 的终结指令唯一跳转回 header 的情况（防止 break）
        auto &bodyInsts = body->getInstructions();
        if (bodyInsts.empty() || !bodyInsts.back()->isTerminator())
            continue;
        auto *br = dynamic_cast<BranchInst *>(bodyInsts.back().get());
        if (!br || br->getTrueBlock() != header || br->isConditional())
            continue;
        // 8. 找到preheader和exitblock
        BasicBlock *preheader = nullptr;
        BasicBlock *exitBlock = nullptr;
        for (auto *pred : header->getPredecessors())
            if (std::find(loop.blocks.begin(), loop.blocks.end(), pred) == loop.blocks.end())
                preheader = pred;
        for (auto *succ : header->getSuccessors())
            if (std::find(loop.blocks.begin(), loop.blocks.end(), succ) == loop.blocks.end())
                exitBlock = succ;
        if (!preheader || !exitBlock)
            continue;

        // 9. 完全展开
        if (tripCount > 0 && tripCount <= 16)
        {
            auto &preInsts = preheader->getInstructions();
            auto insertPos = preInsts.size();
            if (!preInsts.empty() && preInsts.back()->isTerminator())
                insertPos--;
            std::unordered_map<Value *, Value *> valueMap;
            for (auto *phi : headerPhis)
                valueMap[phi] = phiInit[phi];

            for (int i = 0; i < tripCount; ++i)
            {
                std::vector<Instruction *> clonedInsts;
                for (auto &instPtr : body->getInstructions())
                {
                    if (instPtr->isTerminator())
                        continue;
                    // 不跳过任何指令
                    Instruction *cloned = instPtr->clone();
                    cloned->setName(cloned->getName() + "_unroll" + std::to_string(i));
                    // 替换操作数
                    for (size_t k = 0; k < cloned->getOperands().size(); ++k)
                    {
                        Value *oldOp = cloned->getOperands()[k];
                        for (auto *phi : headerPhis)
                        {
                            if (oldOp == phi)
                                cloned->setOperandByIndex(k, valueMap[phi]);
                        }
                        if (valueMap.count(oldOp))
                            cloned->setOperandByIndex(k, valueMap[oldOp]);
                    }
                    valueMap[instPtr.get()] = cloned;
                    clonedInsts.push_back(cloned);
                }
                // 插入到preheader
                for (auto *cloned : clonedInsts)
                    preInsts.insert(preInsts.begin() + insertPos++, std::unique_ptr<Instruction>(cloned));
                // 递推所有phi的值
                for (auto *phi : headerPhis)
                {
                    auto *inc = phiInc[phi];
                    if (inc)
                        valueMap[phi] = valueMap[inc];
                }
            }
            // 修正preheader跳转到loop exit
            for (auto &instPtr : preInsts)
            {
                if (auto *br = dynamic_cast<BranchInst *>(instPtr.get()))
                {
                    if (br->getTrueBlock() == header)
                        br->setTrueBlock(exitBlock);
                    if (br->getFalseBlock() == header)
                        br->setFalseBlock(exitBlock);
                }
            }
            // 修正退出块phi的输入和基本块来源
            for (auto &instPtr : exitBlock->getInstructions())
            {
                if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                {
                    for (size_t i = 0; i < phi->getIncomingBlocks().size(); ++i)
                    {
                        if (phi->getIncomingBlock(i) == header)
                        {
                            phi->setIncomingBlock(i, preheader);
                            if (valueMap.count(phi))
                                phi->setIncomingValue(i, valueMap[phi]);
                        }
                    }
                }
            }
            // 替换所有phi的引用为最后一次递推的值
            for (auto *phi : headerPhis)
                phi->replaceAllUsesWith(valueMap[phi]);
            // 删除原循环体
            for (auto *bb : loop.blocks)
                bb->removeSelfBasicBlock();
            // 增加preheader到exit的连接
            preheader->addSuccessor(exitBlock);
            exitBlock->addPredecessor(preheader);
            changed = true;
            if (verbose)
                debugInfo << "LoopUnrollingPass: Fully unrolled loop at " << header->getName() << " tripCount=" << tripCount << "\n";
        }
        // 10. 部分展开（四路展开，类似处理所有phi）
        else
        {
            int unrollFactor = 4;
            // 1. 新建展开循环块
            auto *unrollHeader = new BasicBlock(header->getName() + "_unroll_header", func);
            auto *unrollBody = new BasicBlock(body->getName() + "_unroll_body", func);
            auto *unrollExit = new BasicBlock(header->getName() + "_unroll_exit", func);

            // 2. 构造unrollHeader的phi（每个header phi都要新建）
            std::unordered_map<PhiInst *, PhiInst *> phiMap;
            for (auto *phi : headerPhis)
            {
                auto *newPhi = new PhiInst(phi->getType(), phi->getName() + "_unroll_phi");
                unrollHeader->addInstruction(std::unique_ptr<Instruction>(newPhi));
                newPhi->addIncoming(phiInit[phi], preheader);
                phiMap[phi] = newPhi;
            }
            auto *unrollPhi = phiMap[indPhi];

            // 2.5 复制原header的非phi、非cmp、非br指令到unrollHeader
            std::unordered_map<Value *, Value *> headerValueMap;
            for (auto &instPtr : header->getInstructions())
            {
                Instruction *inst = instPtr.get();
                // 跳过phi、cmp、br
                if (dynamic_cast<PhiInst *>(inst) || dynamic_cast<ICmpInst *>(inst) || dynamic_cast<BranchInst *>(inst))
                    continue;
                Instruction *cloned = inst->clone();
                // 替换操作数
                for (size_t k = 0; k < cloned->getOperands().size(); ++k)
                {
                    Value *oldOp = cloned->getOperands()[k];
                    // phi变量用新phi
                    for (auto *phi : headerPhis)
                    {
                        if (oldOp == phi)
                            cloned->setOperandByIndex(k, phiMap[phi]);
                    }
                    // 其它header内SSA变量
                    if (headerValueMap.count(oldOp))
                        cloned->setOperandByIndex(k, headerValueMap[oldOp]);
                }
                headerValueMap[inst] = cloned;
                unrollHeader->addInstruction(std::unique_ptr<Instruction>(cloned));
            }
            // 3. 构造unrollHeader的条件判断
            Value *condLHS = unrollPhi;
            Value *condRHS = boundVal;
            // 替换操作数为 headerValueMap 或 phiMap 中的克隆
            auto replaceSSA = [&](Value *v) -> Value *
            {
                if (headerValueMap.count(v))
                    return headerValueMap[v];
                for (auto *phi : headerPhis)
                    if (v == phi)
                        return phiMap[phi];
                return v;
            };
            condLHS = replaceSSA(condLHS);
            condRHS = replaceSSA(condRHS);

            auto *unrollStep = new BinaryOperator(
                Opcode::Add,
                condLHS,
                new ConstantInt(IntegerType::getInstance(), unrollFactor * intcValue),
                unrollPhi->getName() + "_unroll_step");
            unrollHeader->addInstruction(std::unique_ptr<Instruction>(unrollStep));
            auto *unrollCond = new ICmpInst(ICmpInst::ICMP_SLT, unrollStep, condRHS, "unroll_cmp");
            unrollHeader->addInstruction(std::unique_ptr<Instruction>(unrollCond));

            // 4. 构造unrollHeader的分支
            auto *unrollBr = new BranchInst(unrollCond, unrollBody, unrollExit);
            unrollHeader->addInstruction(std::unique_ptr<Instruction>(unrollBr));

            // 5. 构造unrollBody：展开unrollFactor次，递推所有phi
            // 记录每个phi在本次展开前的“当前值”
            std::unordered_map<Value *, Value *> valueMap;
            for (auto *phi : headerPhis)
                valueMap[phi] = phiMap[phi];

            for (int u = 0; u < unrollFactor; ++u)
            {
                // 克隆循环体，替换所有phi为valueMap[phi]
                std::vector<Instruction *> clonedInsts;
                for (auto &instPtr : body->getInstructions())
                {
                    if (instPtr->isTerminator())
                        continue;
                    Instruction *cloned = instPtr->clone();
                    cloned->setName(cloned->getName() + "_unroll" + std::to_string(u));
                    // 替换操作数
                    for (size_t k = 0; k < cloned->getOperands().size(); ++k)
                    {
                        Value *oldOp = cloned->getOperands()[k];
                        for (auto *phi : headerPhis)
                        {
                            if (oldOp == phi)
                                cloned->setOperandByIndex(k, valueMap[phi]);
                        }
                        if (valueMap.count(oldOp))
                            cloned->setOperandByIndex(k, valueMap[oldOp]);
                    }
                    unrollBody->addInstruction(std::unique_ptr<Instruction>(cloned));
                    // 递推更新
                    valueMap[instPtr.get()] = cloned;
                }
                // 更新phi的值
                for (auto *phi : headerPhis)
                {
                    auto *inc = phiInc[phi];
                    if (inc)
                    {
                        valueMap[phi] = valueMap[inc];
                    }
                }
            }
            // 6. unrollBody末尾插入所有phi的自增和跳转
            for (auto *phi : headerPhis)
            {
                if (phi == indPhi)
                {
                    auto *unrollInc = new BinaryOperator(
                        Opcode::Add,
                        phiMap[phi],
                        new ConstantInt(IntegerType::getInstance(), unrollFactor * intcValue),
                        phiMap[phi]->getName() + "_inc");
                    unrollBody->addInstruction(std::unique_ptr<Instruction>(unrollInc));
                    phiMap[phi]->addIncoming(unrollInc, unrollBody);
                }
                else
                {
                    // 其它phi的自增使用最后一次更新值
                    auto *inc = phiInc[phi];
                    if (inc)
                    {
                        phiMap[phi]->addIncoming(valueMap[phi], unrollBody);
                    }
                }
            }
            // 修正prehead的跳转到新循环头
            for (auto &instPtr : preheader->getInstructions())
            {
                if (auto *br = dynamic_cast<BranchInst *>(instPtr.get()))
                {
                    if (br->getTrueBlock() == header)
                        br->setTrueBlock(unrollHeader);
                    if (br->getFalseBlock() == header)
                        br->setFalseBlock(unrollExit);
                }
            }
            unrollBody->addInstruction(std::make_unique<BranchInst>(unrollHeader));
            // exit添加跳转到原来的循环
            unrollExit->addInstruction(std::make_unique<BranchInst>(header));
            // 7. CFG连接
            // 断开连接
            preheader->removeSuccessor(header);
            header->removePredecessor(preheader);

            preheader->addSuccessor(unrollHeader);
            unrollHeader->addPredecessor(preheader);
            // 内部连接
            unrollHeader->addSuccessor(unrollBody);
            unrollBody->addPredecessor(unrollHeader);

            unrollBody->addSuccessor(unrollHeader);
            unrollHeader->addPredecessor(unrollBody);

            unrollHeader->addSuccessor(unrollExit);
            unrollExit->addPredecessor(unrollHeader);
            // 外部连接
            unrollExit->addSuccessor(header);
            header->addPredecessor(unrollExit);

            // 8. 插入新基本块到函数
            func->addBasicBlock(std::unique_ptr<BasicBlock>(unrollHeader));
            func->addBasicBlock(std::unique_ptr<BasicBlock>(unrollBody));
            func->addBasicBlock(std::unique_ptr<BasicBlock>(unrollExit));
            // 修改原来循环phi输入为第一个循环结束后的值
            for (auto &instPtr : header->getInstructions())
            {
                if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                {
                    for (size_t i = 0; i < phi->getNumIncomingValues(); ++i)
                    {
                        if (phi->getIncomingBlock(i) == preheader)
                        {
                            phi->setIncomingBlock(i, unrollExit);
                            // 如果是原来的循环变量，则把初值替换成第一循环的phi
                            if (phiMap.count(phi))
                            {
                                phi->setIncomingValue(i, phiMap[phi]);
                            }
                        }
                    }
                }
            }

            changed = true;
            if (verbose)
                debugInfo << "LoopUnrollingPass: 4-way unrolled loop at " << header->getName() << " (inserted unroll loop before original)\n";
        }
    }
    return changed;
}
