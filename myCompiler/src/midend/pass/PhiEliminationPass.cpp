#include "PhiEliminationPass.h"
using namespace std;
using namespace optimization;
// phi消除
bool PhiEliminationPass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            auto *phi = dynamic_cast<PhiInst *>(inst);
            if (!phi)
            {
                ++it;
                continue;
            }
            // 1. 只有一个输入，直接替换
            if (phi->getNumIncomingValues() == 1)
            {
                Value *incomingValue = phi->getIncomingValue(0);
                phi->removeThisFromOperands();
                phi->replaceAllUsesWith(incomingValue);
                needToDelete.push_back(it->release());
                it = insts.erase(it);
                changed = true;
                if(verbose)
                {
                    debugInfo << "Phi Elimination: Replaced single-input phi " << phi->getName()
                              << " with its incoming value in " << bb->getName() << "\n";
                }
                continue;
            }
            // 如果所有输入都相同，直接替换，不需要产生copy指令
            bool allSame = true;
            Value *firstVal = phi->getIncomingValue(0);
            for (size_t i = 1; i < phi->getNumIncomingValues(); ++i)
            {
                if (phi->getIncomingValue(i) != firstVal)
                {
                    allSame = false;
                    break;
                }
            }
            if (allSame)
            {
                phi->removeThisFromOperands();
                phi->replaceAllUsesWith(firstVal);
                needToDelete.push_back(it->release());
                it = insts.erase(it);
                changed = true;
                if(verbose)
                {
                    debugInfo<<"Phi Elimination: Replaced all-input phi " << phi->getName()<<"\n";
                }
                continue;
            }
            vector<Value *> operands;
            // 2. 多输入phi，插入move/copy到前驱块末尾
            for (size_t i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                BasicBlock *pred = phi->getIncomingBlock(i);
                Value *val = phi->getIncomingValue(i);
                // 在终结指令前插入
                auto rawPtr = new CopyInst(val, phi->getName());
                std::unique_ptr<Instruction> copyInst(rawPtr);
                pred->insertBeforeTerminator(std::move(copyInst));
            }
            // 从基本块中删除原来指令，phi对应value仍然保留
            // 从所有phi的操作数中删除自己
            phi->removeThisFromOperands();
            needToDelete.push_back(it->release());
            it = insts.erase(it);
            changed = true;
            if(verbose)
            {
                debugInfo << "Phi Elimination: Replaced multi-input phi " << phi->getName()
                          << " with copies in predecessor blocks"<<"\n";
            }
        }
    }
    // 遍历所有基本块的所有指令，如果替换后的copy指令源操作数名字与目的操作数名字相同则删除
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            if (auto *copy = dynamic_cast<CopyInst *>(inst))
            {
                if (copy->getSource()->getName() == copy->getDest()->getName())
                {
                    // 删除无效的copy指令
                    copy->removeThisFromOperands();
                    needToDelete.push_back(it->release());
                    it = insts.erase(it);
                    changed = true;
                    continue;
                }
            }
            ++it;
        }
    }
    return changed;
}