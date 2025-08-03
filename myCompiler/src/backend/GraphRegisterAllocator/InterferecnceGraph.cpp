#include "GraphColorRegisterAllocator.h"
#include <algorithm>
#include <iostream>
#include <cmath>

using namespace RISCV;
// 空集合，用于返回不存在节点的邻居
const unordered_set<shared_ptr<RISCVRegister>, RegisterHash, RegisterEqual> InterferenceGraph::emptySet = {};
// ============================================================================
// InterferenceGraph 实现
// ============================================================================
void InterferenceGraph::addNode(shared_ptr<RISCVRegister> reg)
{
    if (nodes.find(reg) == nodes.end())
    {
        nodes.insert(reg);
        adjList[reg] = unordered_set<shared_ptr<RISCVRegister>, RegisterHash, RegisterEqual>();
        degree[reg] = 0;
    }
}

void InterferenceGraph::addEdge(shared_ptr<RISCVRegister> reg1,
                                shared_ptr<RISCVRegister> reg2)
{
    // 确保两个节点都存在
    addNode(reg1);
    addNode(reg2);

    // 避免自环和重复边
    if (reg1 == reg2)
        return;
    if (adjList[reg1].find(reg2) != adjList[reg1].end())
        return;

    // 添加无向边
    adjList[reg1].insert(reg2);
    adjList[reg2].insert(reg1);
    degree[reg1]++;
    degree[reg2]++;
}

void InterferenceGraph::removeNode(shared_ptr<RISCVRegister> reg)
{
    if (nodes.find(reg) == nodes.end())
        return;

    // 移除所有相关的边
    auto neighbors = adjList[reg]; // 拷贝一份
    for (auto neighbor : neighbors)
    {
        adjList[neighbor].erase(reg);
        degree[neighbor]--;
    }

    // 移除节点
    nodes.erase(reg);
    adjList.erase(reg);
    degree.erase(reg);
}

bool InterferenceGraph::interferes(shared_ptr<RISCVRegister> reg1,
                                   shared_ptr<RISCVRegister> reg2) const
{
    auto it = adjList.find(reg1);
    if (it == adjList.end())
        return false;
    return it->second.find(reg2) != it->second.end();
}

int InterferenceGraph::getDegree(shared_ptr<RISCVRegister> reg) const
{
    auto it = degree.find(reg);
    return it != degree.end() ? it->second : 0;
}

const unordered_set<shared_ptr<RISCVRegister>, RegisterHash, RegisterEqual> &
InterferenceGraph::getNeighbors(shared_ptr<RISCVRegister> reg) const
{
    auto it = adjList.find(reg);
    return it != adjList.end() ? it->second : emptySet;
}

void InterferenceGraph::coalesceNodes(shared_ptr<RISCVRegister> reg1,
                                      shared_ptr<RISCVRegister> reg2)
{
    // 将reg2的所有邻居连接到reg1
    for (auto neighbor : adjList[reg2])
    {
        if (neighbor != reg1)
        {
            addEdge(reg1, neighbor);
        }
    }

    // 移除reg2节点
    removeNode(reg2);
}

void InterferenceGraph::printGraph() const
{
#ifdef DEBUG_REG_ALLOC
    std::cout << "=== Interference Graph ===" << std::endl;
    std::cout << "Nodes: " << nodes.size() << std::endl;

    for (auto reg : nodes)
    {
        std::cout << reg->toString() << " (degree=" << getDegree(reg) << "): ";
        for (auto neighbor : getNeighbors(reg))
        {
            std::cout << neighbor->toString() << " ";
        }
        std::cout << std::endl;
    }
    std::cout << "=========================" << std::endl;
#endif
}
