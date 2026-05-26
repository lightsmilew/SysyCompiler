#include "AllocaCoalescePass.h"
#include <algorithm>
#include <map>
#include <vector>

using namespace std;
using namespace optimization;

namespace
{
    struct AllocaSite
    {
        AllocaInst *inst;
        BasicBlock *block;
        size_t blockIndex;
        size_t instIndex;
    };

    BasicBlock *findBlockContaining(Function *func, Instruction *inst)
    {
        for (auto &bbPtr : func->getBasicBlocks())
        {
            auto &insts = bbPtr->getInstructions();
            for (size_t i = 0; i < insts.size(); ++i)
            {
                if (insts[i].get() == inst)
                    return bbPtr.get();
            }
        }
        return nullptr;
    }

    size_t blockIndexInFunction(Function *func, BasicBlock *bb)
    {
        auto &bbs = func->getBasicBlocks();
        for (size_t i = 0; i < bbs.size(); ++i)
        {
            if (bbs[i].get() == bb)
                return i;
        }
        return bbs.size();
    }

    size_t getAllocaTotalElements(AllocaInst *alloca)
    {
        auto *arrayTy = dynamic_cast<ArrayType *>(alloca->AllocatedType);
        if (!arrayTy)
            return 0;
        return arrayTy->getArrayLength();
    }

    BasicBlock *findDominatorMeet(const unordered_map<BasicBlock *, BasicBlock *> &idom,
                                  const vector<BasicBlock *> &blocks)
    {
        if (blocks.empty())
            return nullptr;
        BasicBlock *meet = blocks[0];
        for (size_t i = 1; i < blocks.size() && meet; ++i)
        {
            BasicBlock *b = blocks[i];
            while (meet && !ControlFlowAnalysis::dominates(idom, meet, b))
            {
                auto it = idom.find(meet);
                meet = (it != idom.end()) ? it->second : nullptr;
            }
        }
        return meet;
    }

    bool allocaUsesDominatedBy(AllocaInst *alloca, BasicBlock *target,
                               const unordered_map<BasicBlock *, BasicBlock *> &idom,
                               Function *func)
    {
        for (auto *user : alloca->getUsers())
        {
            auto *useInst = dynamic_cast<Instruction *>(user);
            if (!useInst)
                continue;
            BasicBlock *useBB = findBlockContaining(func, useInst);
            if (!useBB)
                continue;
            if (!ControlFlowAnalysis::dominates(idom, target, useBB))
                return false;
        }
        return true;
    }

    size_t leadingAllocaEnd(BasicBlock *bb)
    {
        size_t i = 0;
        auto &insts = bb->getInstructions();
        while (i < insts.size() && dynamic_cast<AllocaInst *>(insts[i].get()))
            ++i;
        return i;
    }

    void moveAllocaToBlock(AllocaSite &site, BasicBlock *dest, size_t insertIndex)
    {
        auto &srcInsts = site.block->getInstructions();
        unique_ptr<Instruction> moved = std::move(srcInsts[site.instIndex]);
        srcInsts.erase(srcInsts.begin() + static_cast<long>(site.instIndex));

        auto &destInsts = dest->getInstructions();
        destInsts.insert(destInsts.begin() + static_cast<long>(insertIndex), std::move(moved));
    }
}

bool AllocaCoalescePass::runOnFunction(Function *func)
{
    if (func->isLibraryFunction() || func->getBasicBlocks().empty())
        return false;

    auto idom = ControlFlowAnalysis::analyze(func);
    vector<AllocaSite> sites;

    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        size_t bbIdx = blockIndexInFunction(func, bb);
        auto &insts = bb->getInstructions();
        for (size_t i = 0; i < insts.size(); ++i)
        {
            auto *alloca = dynamic_cast<AllocaInst *>(insts[i].get());
            if (!alloca || !alloca->getIsInitialized())
                continue;
            size_t elems = getAllocaTotalElements(alloca);
            if (elems == 0)
                continue;
            sites.push_back({alloca, bb, bbIdx, i});
        }
    }

    if (sites.size() < 2)
        return false;

    using GroupKey = pair<size_t, int>;
    map<GroupKey, vector<AllocaSite *>> groups;
    for (auto &site : sites)
    {
        GroupKey key{getAllocaTotalElements(site.inst), site.inst->getAllocatedSize()};
        groups[key].push_back(&site);
    }

    bool changed = false;
    for (auto &[key, group] : groups)
    {
        if (group.size() < 2)
            continue;

        vector<BasicBlock *> blocks;
        blocks.reserve(group.size());
        for (auto *site : group)
            blocks.push_back(site->block);

        BasicBlock *target = findDominatorMeet(idom, blocks);
        if (!target)
            continue;

        bool safe = true;
        for (auto *site : group)
        {
            if (!allocaUsesDominatedBy(site->inst, target, idom, func))
            {
                safe = false;
                break;
            }
        }
        if (!safe)
            continue;

        vector<AllocaSite *> toMove;
        toMove.reserve(group.size());
        for (auto *site : group)
        {
            if (site->block != target)
                toMove.push_back(site);
        }
        if (toMove.empty())
            continue;

        // 同一基本块内从后往前删，避免下标失效
        sort(toMove.begin(), toMove.end(), [](const AllocaSite *a, const AllocaSite *b) {
            if (a->blockIndex != b->blockIndex)
                return a->blockIndex < b->blockIndex;
            return a->instIndex > b->instIndex;
        });

        size_t insertPos = leadingAllocaEnd(target);
        for (auto *site : toMove)
        {
            BasicBlock *srcBB = site->block;
            auto &srcInsts = srcBB->getInstructions();
            size_t idx = srcInsts.size();
            for (size_t i = 0; i < srcInsts.size(); ++i)
            {
                if (srcInsts[i].get() == site->inst)
                {
                    idx = i;
                    break;
                }
            }
            if (idx >= srcInsts.size())
                continue;

            AllocaSite moving{site->inst, srcBB, site->blockIndex, idx};
            moveAllocaToBlock(moving, target, insertPos);
            ++insertPos;
            changed = true;

            if (verbose)
            {
                debugInfo << "AllocaCoalescePass: moved " << site->inst->getName()
                          << " (" << key.first << " elems) to " << target->getName() << "\n";
            }
        }
    }

    return changed;
}
