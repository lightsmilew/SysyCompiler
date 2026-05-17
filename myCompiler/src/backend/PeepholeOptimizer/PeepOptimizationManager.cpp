#include "PeepOptimizationManager.h"
#include <iostream>

namespace
{
    constexpr size_t kMaxAddressFusionBlockInstrCount = 3000;
    const string kAddressFusionPassName = "FoldAdjacentMoveAndAddress";
}

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

    size_t totalInstructionCount = 0;
    for (auto &bb : func->getBasicBlocks())
    {
        if (bb)
        {
            totalInstructionCount += bb->getInstructions().size();
        }
    }

    const bool skipAddressFusion = totalInstructionCount > kMaxAddressFusionBlockInstrCount;

    for (auto &bb : func->getBasicBlocks())
    {
        optimizeBasicBlock(bb, skipAddressFusion);
    }
}

void PeepOptimizationManager::optimizeBasicBlock(shared_ptr<RISCVBasicBlock> bb, bool skipAddressFusion)
{
    if (!bb)
    {
        return;
    }

    auto &instructions = bb->getInstructions();
    for (auto &pass : passes)
    {
        if (skipAddressFusion && pass && pass->getName() == kAddressFusionPassName)
        {
            continue;
        }

        auto it = instructions.begin();
        while (it != instructions.end())
        {
            auto currentInstr = *it;
            auto state = pass->optimize(currentInstr, bb);

            if (state == PeepOptiState::DELETE)
            {
                it = instructions.erase(it);
            }
            else
            {
                ++it;
            }
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
