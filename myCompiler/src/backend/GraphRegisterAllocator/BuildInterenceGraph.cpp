#include "GraphColorRegisterAllocator.h"
#include <algorithm>
#include <iostream>
#include <cmath>

using namespace RISCV;

// 构建冲突图
void GraphColorRegisterAllocator::buildInterferenceGraph()
{
#ifdef DEBUG_REG_ALLOC
    std::cout << "Building interference graph..." << std::endl;
#endif

    const auto &livenessInfo = currentFunc->getLivenessInfo();

    // 1. 从活跃性信息中提取寄存器生存期数据
    // 遍历所有寄存器的活跃区间
    for (const auto &regRangesPair : livenessInfo.liveRanges)
    {
        auto reg = regRangesPair.first;

        // 添加寄存器节点到冲突图
        interferenceGraph.addNode(reg);

        // 处理预着色寄存器（物理寄存器）
        if (isPrecolored(reg))
        {
            setNodeState(reg, NodeState::PRECOLORED);
        }
    }

    // 2. 识别同时活跃的寄存器对，在冲突图中添加对应的边
    // 按寄存器类型分别构建冲突图
    buildInterferencesByType(RegisterType::INT);
    buildInterferencesByType(RegisterType::FLOAT);

    // 3. 添加预着色寄存器之间的冲突边
    // 预着色寄存器（物理寄存器）之间通常不冲突，除非它们是同一个寄存器
    // 但我们需要确保虚拟寄存器不会与已占用的物理寄存器冲突
    addPrecoloredInterferences();

// 4. 实现冲突图的调试输出功能
#ifdef DEBUG_REG_ALLOC
    std::cout << "Interference graph construction completed." << std::endl;
    std::cout << "Total nodes: " << interferenceGraph.getNodes().size()
              << std::endl;
#endif

    // 统计不同类型寄存器的数量
    int generalCount = 0, floatCount = 0, precoloredCount = 0, virtualCount = 0;
    int totalEdges = 0;

    for (auto reg : interferenceGraph.getNodes())
    {
        if (reg->getType() == RegisterType::INT)
            generalCount++;
        else if (reg->getType() == RegisterType::FLOAT)
            floatCount++;

        if (isPrecolored(reg))
            precoloredCount++;
        else
            virtualCount++;

        totalEdges += interferenceGraph.getDegree(reg);
    }

    // 每条边被计算了两次（无向图），所以除以2
    totalEdges /= 2;

#ifdef DEBUG_REG_ALLOC
    std::cout << "General registers: " << generalCount << std::endl;
    std::cout << "Float registers: " << floatCount << std::endl;
    std::cout << "Precolored registers: " << precoloredCount << std::endl;
    std::cout << "Virtual registers: " << virtualCount << std::endl;
    std::cout << "Total interference edges: " << totalEdges << std::endl;
#endif

    // 可选：打印详细的冲突图信息（调试时使用）
    if (interferenceGraph.getNodes().size() <= 20) // 只在节点数较少时打印详细信息
    {
        interferenceGraph.printGraph();
    }
}

// 按寄存器类型构建冲突关系
void GraphColorRegisterAllocator::buildInterferencesByType(RegisterType type)
{
    const auto &livenessInfo = currentFunc->getLivenessInfo();

    // 收集指定类型的所有寄存器
    vector<shared_ptr<RISCVRegister>> registersOfType;
    for (const auto &regRangesPair : livenessInfo.liveRanges)
    {
        auto reg = regRangesPair.first;
        if (reg->getType() == type)
        {
            registersOfType.push_back(reg);
        }
    }

    // 对于每对寄存器，检查它们是否同时活跃
    for (size_t i = 0; i < registersOfType.size(); i++)
    {
        for (size_t j = i + 1; j < registersOfType.size(); j++)
        {
            auto reg1 = registersOfType[i];
            auto reg2 = registersOfType[j];

            // 使用LivenessInfo的interferes方法检查冲突
            if (livenessInfo.interferes(reg1, reg2))
            {
                interferenceGraph.addEdge(reg1, reg2);
            }
        }
    }
}

// 添加预着色寄存器之间的冲突边
void GraphColorRegisterAllocator::addPrecoloredInterferences()
{
    // 收集实际在函数中使用的物理寄存器（预着色寄存器）
    vector<shared_ptr<RISCVRegister>> usedPhysicalRegs;

    for (auto reg : interferenceGraph.getNodes())
    {
        if (isPrecolored(reg))
        {
            usedPhysicalRegs.push_back(reg);
        }
    }

    // 只对实际使用的物理寄存器进行处理
    // 同类型的物理寄存器之间互相冲突（因为它们不能同时被分配给不同的虚拟寄存器）
    for (size_t i = 0; i < usedPhysicalRegs.size(); i++)
    {
        for (size_t j = i + 1; j < usedPhysicalRegs.size(); j++)
        {
            auto reg1 = usedPhysicalRegs[i];
            auto reg2 = usedPhysicalRegs[j];

            // 只有同类型的物理寄存器才会冲突
            if (reg1->getType() == reg2->getType())
            {
                interferenceGraph.addEdge(reg1, reg2);
            }
        }
    }

#ifdef DEBUG_REG_ALLOC
    std::cout << "Added precolored interferences for " << usedPhysicalRegs.size()
              << " actually used physical registers" << std::endl;
#endif
}