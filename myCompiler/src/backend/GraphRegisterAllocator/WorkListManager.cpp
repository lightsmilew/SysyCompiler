#include "GraphColorRegisterAllocator.h"
#include <algorithm>
#include <iostream>
#include <cmath>

using namespace RISCV;
// ============================================================================
// WorklistManager 实现
// ============================================================================

void WorklistManager::addToWorklist(shared_ptr<RISCVRegister> reg,
                                    WorklistType type)
{
    // 如果寄存器已经在某个工作列表中，先移除
    removeFromWorklist(reg);

    worklists[type].push_back(reg);
    regToWorklist[reg] = type;
}

void WorklistManager::removeFromWorklist(shared_ptr<RISCVRegister> reg)
{
    auto it = regToWorklist.find(reg);

    if (it != regToWorklist.end())
    {
        auto type = it->second;
        regToWorklist.erase(it);
        auto &worklist = worklists[type];
        worklist.erase(
            remove(worklist.begin(), worklist.end(), reg), worklist.end());
    }
}

shared_ptr<RISCVRegister> WorklistManager::getNext(WorklistType type)
{
    auto &worklist = worklists[type];

    if (!worklist.empty())
    {
        auto reg = worklist.front();
        return reg;
    }

    return nullptr;
}

bool WorklistManager::isEmpty(WorklistType type) const
{
    auto it = worklists.find(type);
    if (it == worklists.end())
        return true;

    return it->second.empty();
}

bool WorklistManager::isEmpty() const
{
    return isEmpty(WorklistType::SIMPLIFY) && isEmpty(WorklistType::FREEZE) &&
           isEmpty(WorklistType::SPILL);
}

WorklistManager::WorklistType
WorklistManager::getWorklistType(shared_ptr<RISCVRegister> reg) const
{
    auto it = regToWorklist.find(reg);
    if (it != regToWorklist.end())
    {
        return it->second;
    }
    return WorklistType::SIMPLIFY;
}

bool WorklistManager::isInWorklist(shared_ptr<RISCVRegister> reg) const
{
    return regToWorklist.find(reg) != regToWorklist.end();
}

size_t WorklistManager::getSize(WorklistType type) const
{
    auto it = worklists.find(type);
    return it != worklists.end() ? it->second.size() : 0;
}

void WorklistManager::printWorklistSizes() const
{
#ifdef DEBUG_REG_ALLOC
    std::cout << "=== Worklist Sizes ===" << std::endl;
    std::cout << "SIMPLIFY: " << getSize(WorklistType::SIMPLIFY) << std::endl;
    std::cout << "FREEZE: " << getSize(WorklistType::FREEZE) << std::endl;
    std::cout << "SPILL: " << getSize(WorklistType::SPILL) << std::endl;
    std::cout << "======================" << std::endl;
#endif
}

void WorklistManager::clear()
{
    worklists.clear();
    regToWorklist.clear();
}

vector<shared_ptr<RISCVRegister>>
WorklistManager::getAllNodes(WorklistType type) const
{
    vector<shared_ptr<RISCVRegister>> result;

    // 遍历regToWorklist映射，找到属于指定类型的所有节点
    for (const auto &pair : regToWorklist)
    {
        if (pair.second == type)
        {
            result.push_back(pair.first);
        }
    }

    return result;
}