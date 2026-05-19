#include "PhiEliminationPass.h"
#include <unordered_map>
#include <unordered_set>

using namespace std;
using namespace optimization;

namespace
{
struct PendingCopy
{
    Value *destValue;
    Value *sourceValue;
    std::string destName;
};

void emitParallelCopies(BasicBlock *pred, std::vector<PendingCopy> copies, size_t &tempIndex)
{
    std::vector<PendingCopy> remaining = std::move(copies);
    while (!remaining.empty())
    {
        std::unordered_set<Value *> sourceSet;
        sourceSet.reserve(remaining.size());
        for (const auto &copy : remaining)
        {
            sourceSet.insert(copy.sourceValue);
        }

        size_t readyIndex = remaining.size();
        for (size_t i = 0; i < remaining.size(); ++i)
        {
            if (sourceSet.find(remaining[i].destValue) == sourceSet.end())
            {
                readyIndex = i;
                break;
            }
        }

        if (readyIndex < remaining.size())
        {
            pred->insertBeforeTerminator(std::make_unique<CopyInst>(remaining[readyIndex].sourceValue,
                                                                    remaining[readyIndex].destName));
            remaining.erase(remaining.begin() + static_cast<vector<PendingCopy>::difference_type>(readyIndex));
            continue;
        }

        auto chosen = remaining.front();
        auto tempName = pred->getName() + ".phi_tmp." + std::to_string(tempIndex++);
        auto tempCopy = std::make_unique<CopyInst>(chosen.destValue, tempName);
        auto *tempValue = tempCopy.get();
        pred->insertBeforeTerminator(std::move(tempCopy));
        pred->insertBeforeTerminator(std::make_unique<CopyInst>(chosen.sourceValue, chosen.destName));
        remaining.erase(remaining.begin());
        for (auto &copy : remaining)
        {
            if (copy.sourceValue == chosen.destValue)
            {
                copy.sourceValue = tempValue;
            }
        }
    }
}
} // namespace

// phi消除
bool PhiEliminationPass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        vector<BasicBlock *> predOrder;
        unordered_map<BasicBlock *, vector<PendingCopy>> pendingCopiesByPred;
        unordered_set<Instruction *> multiInputPhis;
        vector<string> multiInputPhiNames;

        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            auto *phi = dynamic_cast<PhiInst *>(inst);
            if (!phi)
            {
                ++it;
                continue;
            }

            if (phi->getNumIncomingValues() == 0 ||
                phi->getNumOperands() != phi->getNumIncomingValues())
            {
                if (verbose)
                {
                    debugInfo << "Phi Elimination: Skipping malformed phi " << phi->getName()
                              << " in " << bb->getName() << "\n";
                }
                ++it;
                continue;
            }

            if (phi->getNumIncomingValues() == 1)
            {
                Value *incomingValue = phi->getIncomingValue(0);
                phi->removeThisFromOperands();
                phi->replaceAllUsesWith(incomingValue);
                needToDelete.push_back(it->release());
                it = insts.erase(it);
                changed = true;
                if (verbose)
                {
                    debugInfo << "Phi Elimination: Replaced single-input phi " << phi->getName()
                              << " with its incoming value in " << bb->getName() << "\n";
                }
                continue;
            }

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
                if (verbose)
                {
                    debugInfo << "Phi Elimination: Replaced all-input phi " << phi->getName() << "\n";
                }
                continue;
            }

            multiInputPhis.insert(phi);
            multiInputPhiNames.push_back(phi->getName());
            for (size_t i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                BasicBlock *pred = phi->getIncomingBlock(i);
                Value *val = phi->getIncomingValue(i);
                auto &copies = pendingCopiesByPred[pred];
                if (copies.empty())
                {
                    predOrder.push_back(pred);
                }
                copies.push_back(PendingCopy{phi, val, phi->getName()});
            }
            ++it;
        }

        size_t tempIndex = 0;
        for (auto *pred : predOrder)
        {
            emitParallelCopies(pred, pendingCopiesByPred[pred], tempIndex);
        }

        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            if (multiInputPhis.find(inst) != multiInputPhis.end())
            {
                auto *phi = dynamic_cast<PhiInst *>(inst);
                if (phi)
                {
                    phi->removeThisFromOperands();
                }
                needToDelete.push_back(it->release());
                it = insts.erase(it);
                changed = true;
                continue;
            }
            ++it;
        }

        if (verbose)
        {
            for (const auto &phiName : multiInputPhiNames)
            {
                debugInfo << "Phi Elimination: Replaced multi-input phi " << phiName
                          << " with copies in predecessor blocks" << "\n";
            }
        }
    }

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