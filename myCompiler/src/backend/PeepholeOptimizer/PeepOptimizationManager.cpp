#include "PeepOptimizationManager.h"
#include <iostream>
void PeepOptimizationManager::addPass(shared_ptr<PeepPass> pass)
{
    if (pass)
    {
        passes.push_back(pass);
    }
}

void PeepOptimizationManager::optimizeFunction(shared_ptr<RISCVFunction> func)
{
    if (!func)
    {
        return;
    }

    for (auto &bb : func->getBasicBlocks())
    {
        optimizeBasicBlock(bb);
    }
}

void PeepOptimizationManager::optimizeBasicBlock(shared_ptr<RISCVBasicBlock> bb)
{
    if (!bb)
    {
        return;
    }

    auto &instructions = bb->getInstructions();
    auto it = instructions.begin();

    while (it != instructions.end())
    {
        auto currentInstr = *it;
        bool shouldDelete = false;

        // 对当前指令应用所有优化pass
        for (auto &pass : passes)
        {
            auto state = pass->optimize(currentInstr, bb);

            if (state == PeepOptiState::DELETE)
            {
                shouldDelete = true;
                break;
            }
        }

        if (shouldDelete)
        {
            it = instructions.erase(it);
        }
        else
        {
            // 移动到下一条指令
            ++it;
        }
    }
}

void PeepOptimizationManager::optimizeModule(shared_ptr<RISCVModule> module)
{
    if (!module)
    {
        return;
    }

    for (auto &func : module->getFunctions())
    {
        optimizeFunction(func);
    }
}
