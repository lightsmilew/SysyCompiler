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

    worklists[type].push(reg);
    regToWorklist[reg] = type;
}

void WorklistManager::removeFromWorklist(shared_ptr<RISCVRegister> reg)
{
    auto it = regToWorklist.find(reg);
    if (it != regToWorklist.end())
    {
        regToWorklist.erase(it);
        // 注意：这里不从queue中移除，因为queue不支持随机删除
        // 在getNext时会检查寄存器是否仍在映射中
    }
}

shared_ptr<RISCVRegister> WorklistManager::getNext(WorklistType type)
{
    auto &worklist = worklists[type];

    while (!worklist.empty())
    {
        auto reg = worklist.front();
        worklist.pop();

        // 检查寄存器是否仍在此工作列表中
        auto it = regToWorklist.find(reg);
        if (it != regToWorklist.end() && it->second == type)
        {
            regToWorklist.erase(it);
            return reg;
        }
    }

    return nullptr;
}

bool WorklistManager::isEmpty(WorklistType type) const
{
    auto it = worklists.find(type);
    if (it == worklists.end())
        return true;

    // 需要检查队列中是否有有效的寄存器
    // 这是一个简化的实现，实际中可能需要更复杂的逻辑
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
    // 默认返回SIMPLIFY，实际使用中应该处理这种情况
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
    std::cout << "=== Worklist Sizes ===" << std::endl;
    std::cout << "SIMPLIFY: " << getSize(WorklistType::SIMPLIFY) << std::endl;
    std::cout << "FREEZE: " << getSize(WorklistType::FREEZE) << std::endl;
    std::cout << "SPILL: " << getSize(WorklistType::SPILL) << std::endl;
    std::cout << "======================" << std::endl;
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