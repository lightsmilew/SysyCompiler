#include "GraphColorRegisterAllocator.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <functional>
#include <fstream>
using namespace RISCV;
// ============================================================================
// GraphColorRegisterAllocator 基础实现
// ============================================================================

// 可用的物理寄存器定义（与LinearScanRegisterAllocator保持一致）
const vector<shared_ptr<RISCVRegister>>
    GraphColorRegisterAllocator::availableGeneralRegs = {
        // 临时寄存器 (caller-saved) - 优先使用
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T0),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T1),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T2),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T3),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T4),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T5),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T6),

        // 参数寄存器 (caller-saved)
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A0),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A1),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A2),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A3),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A4),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A5),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A6),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A7),

        // 保存寄存器 (callee-saved)
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S0),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S1),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S2),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S3),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S4),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S5),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S6),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S7),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S8),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S9),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S10),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S11)};

const vector<shared_ptr<RISCVRegister>>
    GraphColorRegisterAllocator::availableFloatRegs = {
        // 临时浮点寄存器 (caller-saved)
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT0),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT1),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT2),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT3),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT4),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT5),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT6),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT7),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT8),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT9),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT10),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT11),

        // 浮点参数寄存器 (caller-saved)
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA0),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA1),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA2),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA3),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA4),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA5),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA6),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA7),

        // 保存浮点寄存器 (callee-saved)
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS0),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS1),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS2),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS3),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS4),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS5),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS6),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS7),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS8),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS9),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS10),
        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS11)};

const vector<shared_ptr<RISCVRegister>> CallerSavedGeneralRegs = {
    // 临时寄存器 (caller-saved)
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T0),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T1),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T2),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T3),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T4),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T5),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T6),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A0),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A1),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A2),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A3),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A4),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A5),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A6),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A7)};
const vector<shared_ptr<RISCVRegister>> CallerSavedFloatRegs = {
    // 临时浮点寄存器 (caller-saved)
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT0),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT1),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT2),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT3),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT4),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT5),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT6),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT7),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT8),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT9),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT10),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT11),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA0),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA1),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA2),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA3),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA4),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA5),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA6),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA7)};

// 主要接口：为函数分配寄存器
void GraphColorRegisterAllocator::allocateRegisters(
    shared_ptr<RISCVFunction> func, shared_ptr<Module> irModule)
{
  currentFunc = func;
  currentModule = irModule;

  bool needRestart = false;
  int spillIterations = 0;
#ifdef DEBUG_REG_ALLOC
  // 把初始代码写入文件用于比对
  std::string initialCodeFile = "initial_code_" + func->getName() + ".s";
  std::ofstream initialCodeStream(initialCodeFile);
  if (initialCodeStream.is_open())
  {
    initialCodeStream << func->toString();
    initialCodeStream.close();
  }
#endif
  do
  {

    for (auto &bb : currentFunc->getBasicBlocks())
    {
      bb->setLiveIn({});  // 清空LiveIn信息
      bb->setLiveOut({}); // 清空LiveOut信息
    }
    auto &livenessInfo = currentFunc->getLivenessInfo();
    livenessInfo.clear();

    computeBasicBlockUseDef(currentFunc);
    computeLiveInOut(currentFunc);
    computeLiveRanges(currentFunc, irModule);
#ifdef DEBUG_REG_ALLOC
    printLiveRanges(currentFunc);
#endif

    // 清空所有数据结构
    interferenceGraph = InterferenceGraph();
    readInterferenceGraph = InterferenceGraph();
    moveList = MoveList();
    worklistManager.clear();
    nodeStates.clear();
    allocation.clear();
    spilledRegs.clear();
    coalescingManager.clear();
    currentFunc->clearUsedCalleeSavedRegs();
    costsSet.clear();
    while (!selectStack.empty())
      selectStack.pop();

#ifdef DEBUG_REG_ALLOC
    if (needRestart)
    {
      std::cout
          << "\n=== Restarting allocation after spill handling (iteration "
          << spillIterations << ") ===" << std::endl;
    }
    else
    {
      std::cout << "=== Graph Coloring Register Allocation ===" << std::endl;
    }
    std::cout << "Function: " << func->getName() << std::endl;
#endif

    // 执行图染色算法的主要步骤
    buildInterferenceGraph();
    analyzeMoveInstructions();
    initializeWorklists();

    // 主要算法循环：简化、合并、冻结、溢出选择
    while (!worklistManager.isEmpty() || !moveList.isEmpty())
    {
      if (!worklistManager.isEmpty(WorklistManager::WorklistType::SIMPLIFY))
      {
        // 执行简化阶段
        performSimplification();
      }
      else if (!moveList.isEmpty())
      {
        // 尝试执行合并阶段
        performCoalescing();
      }
      else if (!worklistManager.isEmpty(
                   WorklistManager::WorklistType::FREEZE))
      {
        // 执行冻结阶段
        performFreezing();
      }
      else if (!worklistManager.isEmpty(
                   WorklistManager::WorklistType::SPILL))
      {
        // 执行溢出选择阶段
        selectSpillCandidates();
      }
      else
      {
        // 如果没有可处理的节点，退出循环
        break;
      }
    }

    // 执行着色阶段
    assignColors();

    // 如果有溢出的寄存器，需要处理溢出
    needRestart = false;
    if (!spilledRegs.empty())
    {

      handleSpilledRegisters();

      // 处理完溢出后，需要重新开始分配过程
      needRestart = true;
      spillIterations++;
    }
  } while (needRestart);

#ifdef DEBUG_REG_ALLOC
  printStatistics();

  // 验证最终的分配结果
  validateAllocation();
#endif

  // 应用分配结果到指令中
  applyAllocation();
}

// 辅助函数实现
bool GraphColorRegisterAllocator::isPrecolored(
    shared_ptr<RISCVRegister> reg) const
{
  return reg->isPhysical();
}

int GraphColorRegisterAllocator::getK(RegisterType type) const
{
  return type == RegisterType::INT ? K_GENERAL : K_FLOAT;
}

vector<shared_ptr<RISCVRegister>>
GraphColorRegisterAllocator::getAvailableColors(RegisterType type) const
{
  return type == RegisterType::INT ? availableGeneralRegs : availableFloatRegs;
}

NodeState
GraphColorRegisterAllocator::getNodeState(shared_ptr<RISCVRegister> reg) const
{
  auto it = nodeStates.find(reg);
  return it != nodeStates.end() ? it->second : NodeState::INITIAL;
}

void GraphColorRegisterAllocator::setNodeState(shared_ptr<RISCVRegister> reg,
                                               NodeState state)
{
  nodeStates[reg] = state;
}

void GraphColorRegisterAllocator::printStatistics()
{
  std::cout << "=== Graph Coloring Allocation Statistics ===" << std::endl;
  std::cout << "Function: " << currentFunc->getName() << std::endl;
  std::cout << "Interference graph nodes: "
            << interferenceGraph.getNodes().size() << std::endl;
  std::cout << "Move instructions: " << moveList.getAllMoves().size()
            << std::endl;
  std::cout << "Successfully allocated: " << allocation.size() << std::endl;
  std::cout << "Spilled registers: " << spilledRegs.size() << std::endl;
  std::cout << "===========================================" << std::endl;
}

// 初始化工作列表
void GraphColorRegisterAllocator::initializeWorklists()
{
#ifdef DEBUG_REG_ALLOC
  std::cout << "Initializing worklists..." << std::endl;
#endif

  // 对所有节点进行分类
  for (auto reg : interferenceGraph.getNodes())
  {
    classifyNode(reg);
  }

#ifdef DEBUG_REG_ALLOC
  worklistManager.printWorklistSizes();
#endif
}

// 辅助函数：检查指令是否为move指令
bool GraphColorRegisterAllocator::isMoveInstruction(
    shared_ptr<RISCVInstruction> instr) const
{
  auto opcode = instr->getOpcode();
  return opcode == RISCVOpcode::MV || opcode == RISCVOpcode::FMV_S;
}

// 节点分类函数实现
void GraphColorRegisterAllocator::classifyNode(shared_ptr<RISCVRegister> reg)
{
  if (isPrecolored(reg))
  {
    setNodeState(reg, NodeState::PRECOLORED);
    return;
  }

  int degree = interferenceGraph.getDegree(reg);
  int K = getK(reg->getType());
  bool moveRelated = moveList.isMoveRelated(reg);

  if (degree < K)
  {
    if (!costsSet.empty())
    {
      for (auto it = costsSet.begin(); it != costsSet.end(); ++it)
      {
        if (it->second == reg)
        {
          costsSet.erase(it);
          break;
        }
      }
    }

    if (moveRelated)
    {
      setNodeState(reg, NodeState::FREEZE_READY);
      worklistManager.addToWorklist(reg, WorklistManager::WorklistType::FREEZE);
    }
    else
    {
      setNodeState(reg, NodeState::SIMPLIFY_READY);
      worklistManager.addToWorklist(reg, WorklistManager::WorklistType::SIMPLIFY);
    }
  }
  else
  {
    setNodeState(reg, NodeState::SPILL_READY);
    worklistManager.addToWorklist(reg, WorklistManager::WorklistType::SPILL);
  }
}

// 动态重新分类单个节点
void GraphColorRegisterAllocator::reclassifyNode(
    shared_ptr<RISCVRegister> reg)
{
  // 跳过预着色寄存器和已经被移除的节点
  if (isPrecolored(reg) || getNodeState(reg) == NodeState::COLORED)
  {
    return;
  }

  // 从当前工作列表中移除
  worklistManager.removeFromWorklist(reg);

  // 重新评估并分类
  classifyNode(reg);

#ifdef DEBUG_REG_ALLOC
  std::cout << "Reclassified node " << reg->toString()
            << " (degree=" << interferenceGraph.getDegree(reg)
            << ", move-related=" << moveList.isMoveRelated(reg) << ")"
            << std::endl;
#endif
}

// 批量重新分类受影响的节点
void GraphColorRegisterAllocator::reclassifyAffectedNodes(
    const vector<shared_ptr<RISCVRegister>> &affectedNodes)
{
#ifdef DEBUG_REG_ALLOC
  std::cout << "Reclassifying " << affectedNodes.size() << " affected nodes..."
            << std::endl;
#endif

  for (auto reg : affectedNodes)
  {
    reclassifyNode(reg);
  }

#ifdef DEBUG_REG_ALLOC
  // 打印更新后的工作列表状态
  worklistManager.printWorklistSizes();
#endif
}

// ============================================================================
// 着色阶段算法实现
// ============================================================================

void GraphColorRegisterAllocator::assignColors()
{
#ifdef DEBUG_REG_ALLOC
  std::cout << "Performing coloring phase..." << std::endl;
#endif

  // 清空分配结果
  allocation.clear();
  spilledRegs.clear();

  // 为预着色寄存器设置分配（它们已经有固定的物理寄存器）
  for (auto reg : readInterferenceGraph.getNodes())
  {
    if (isPrecolored(reg))
    {
      allocation[reg] = reg; // 预着色寄存器映射到自身
    }
  }

  // 从栈中弹出节点并为其分配颜色
  int coloredCount = 0;
  int spilledCount = 0;
  const vector<int> callSiteIndices = collectCallSiteIndices();

  while (!selectStack.empty())
  {
    // 1. 从栈中弹出节点
    auto reg = selectStack.top();
    selectStack.pop();

#ifdef DEBUG_REG_ALLOC
    std::cout << "Coloring node: " << reg->toString() << std::endl;
#endif

    // 2. 获取可用的物理寄存器列表（颜色）
    auto availableColors = getAvailableColors(reg->getType());
    int K = getK(reg->getType());

    // 3. 标记已被邻居使用的颜色
    vector<bool> colorUsed(availableColors.size(), false);

    for (auto neighbor : readInterferenceGraph.getNeighbors(reg))
    {
      auto it = allocation.find(neighbor);
      if (it != allocation.end())
      {
        auto neighborColor = it->second;

        // 找到邻居的颜色在可用颜色列表中的索引
        for (size_t i = 0; i < availableColors.size(); i++)
        {
          if (availableColors[i]->getPhysicalReg() ==
              neighborColor->getPhysicalReg())
          {
            colorUsed[i] = true;
            break;
          }
        }
      }
    }

    // 4. 按 biased coloring 策略选择最优可用颜色
    shared_ptr<RISCVRegister> selectedColor =
        selectBiasedColor(reg, availableColors, colorUsed, callSiteIndices);

    // 5. 如果找到可用颜色，则分配；否则标记为溢出
    if (selectedColor)
    {
      allocation[reg] = selectedColor;
      currentFunc->addUsedCalleeSavedReg(selectedColor);

      coloredCount++;
#ifdef DEBUG_REG_ALLOC
      std::cout << "  Assigned color: " << selectedColor->toString()
                << " (liveAcrossCall="
                << isLiveAcrossCall(reg, callSiteIndices)
                << ", loopDepth=" << getMaxLoopDepthForReg(reg) << ")"
                << std::endl;
#endif
    }
    else
    {
      spilledRegs.insert(reg);
      spilledCount++;
#ifdef DEBUG_REG_ALLOC
      std::cout << "  No available color, marked as spilled" << std::endl;
#endif
    }
  }

#ifdef DEBUG_REG_ALLOC
  std::cout << "Coloring phase completed." << std::endl;
  std::cout << "Successfully colored: " << coloredCount << " registers"
            << std::endl;
  std::cout << "Spilled: " << spilledCount << " registers" << std::endl;
#endif

  // 验证着色结果
  validateAllocation();
}

// 验证分配结果
void GraphColorRegisterAllocator::validateAllocation()
{
#ifdef DEBUG_REG_ALLOC
  std::cout << "Validating register allocation..." << std::endl;
#endif

  int errors = 0;

  // 检查1：没有两个冲突的寄存器被分配相同的物理寄存器
  for (auto reg1 : readInterferenceGraph.getNodes())
  {
    if (spilledRegs.find(reg1) != spilledRegs.end())
    {
      continue; // 跳过溢出的寄存器
    }

    auto it1 = allocation.find(reg1);
    if (it1 == allocation.end())
    {
#ifdef DEBUG_REG_ALLOC
      std::cout << "Error: Register " << reg1->toString()
                << " has no allocation" << std::endl;
#endif
      errors++;
      continue;
    }

    auto color1 = it1->second;

    for (auto reg2 : readInterferenceGraph.getNeighbors(reg1))
    {
      if (spilledRegs.find(reg2) != spilledRegs.end())
      {
        continue; // 跳过溢出的寄存器
      }

      auto it2 = allocation.find(reg2);
      if (it2 == allocation.end())
      {
#ifdef DEBUG_REG_ALLOC
        std::cout << "Error: Register " << reg2->toString()
                  << " has no allocation" << std::endl;
#endif
        errors++;
        continue;
      }

      auto color2 = it2->second;

      if (color1->getPhysicalReg() == color2->getPhysicalReg())
      {
#ifdef DEBUG_REG_ALLOC
        std::cout << "Error: Conflicting registers " << reg1->toString()
                  << " and " << reg2->toString() << " assigned same color "
                  << color1->toString() << std::endl;
#endif
        errors++;
      }
    }
  }

  // 检查2：预着色寄存器保持原始分配
  for (auto reg : readInterferenceGraph.getNodes())
  {
    if (isPrecolored(reg))
    {
      auto it = allocation.find(reg);
      if (it == allocation.end() ||
          it->second->getPhysicalReg() != reg->getPhysicalReg())
      {
#ifdef DEBUG_REG_ALLOC
        std::cout << "Error: Precolored register " << reg->toString()
                  << " lost its original color" << std::endl;
#endif
        errors++;
      }
    }
  }

  // 检查3：寄存器类型约束得到满足
  for (auto reg : readInterferenceGraph.getNodes())
  {
    if (spilledRegs.find(reg) != spilledRegs.end())
    {
      continue; // 跳过溢出的寄存器
    }

    auto it = allocation.find(reg);
    if (it != allocation.end())
    {
      auto color = it->second;
      if (reg->getType() != color->getType())
      {
#ifdef DEBUG_REG_ALLOC
        std::cout << "Error: Register " << reg->toString() << " of type "
                  << (reg->getType() == RegisterType::INT ? "INT" : "FLOAT")
                  << " assigned color " << color->toString() << " of type "
                  << (color->getType() == RegisterType::INT ? "INT" : "FLOAT")
                  << std::endl;
#endif
        errors++;
      }
    }
  }

  if (errors == 0)
  {
#ifdef DEBUG_REG_ALLOC
    std::cout << "Allocation validation passed successfully!" << std::endl;
#endif
  }
  else
  {
#ifdef DEBUG_REG_ALLOC
    std::cout << "Allocation validation failed with " << errors << " errors"
              << std::endl;
#endif
  }
}

// ============================================================================
// 溢出处理阶段算法实现
// ============================================================================
void GraphColorRegisterAllocator::handleSpilledRegisters()
{
#ifdef DEBUG_REG_ALLOC
  std::cout << "Handling spilled registers..." << std::endl;
#endif

  if (spilledRegs.empty())
  {
#ifdef DEBUG_REG_ALLOC
    std::cout << "No registers to spill, skipping." << std::endl;
#endif
    return;
  }

#ifdef DEBUG_REG_ALLOC
  std::cout << "Total spilled registers: " << spilledRegs.size() << std::endl;
#endif

  // 为每个溢出的寄存器在栈上分配空间
  for (auto spilledReg : spilledRegs)
  {
    // 为溢出寄存器在栈上分配空间
    string spillName = "spill_" + spilledReg->toString();
    int spillSize = 8; // 无法获得大小，先用最大的8

    currentFunc->getStackFrame().allocateValueSpace(spillName, spillSize);
#ifdef DEBUG_REG_ALLOC
    std::cout << "Allocated stack space for " << spilledReg->toString()
              << " at offset "
              << currentFunc->getStackFrame().getValueOffset(spillName)
              << " with size " << spillSize << std::endl;
#endif
  }

  // 遍历所有基本块和指令，为溢出寄存器插入load/store代码
  for (auto &bb : currentFunc->getBasicBlocks())
  {
    vector<shared_ptr<RISCVInstruction>> newInstructions;

    for (auto instr : bb->getInstructions())
    {
      vector<shared_ptr<RISCVInstruction>> beforeInstr;
      vector<shared_ptr<RISCVInstruction>> afterInstr;

      // 检查指令是否使用了溢出寄存器
      for (auto useReg : instr->getUseRegisters())
      {
        if (spilledRegs.find(useReg) != spilledRegs.end())
        {
          // 为使用的溢出寄存器创建临时寄存器
          auto tempReg = make_shared<RISCVRegister>(useReg->getType());
          auto spReg =
              make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);

          // 获取溢出寄存器在栈上的偏移量
          string spillName = "spill_" + useReg->toString();
          int offset = currentFunc->getStackFrame().getValueOffset(spillName);

          // 创建load指令，从栈上加载值到临时寄存器
          RISCVOpcode loadOp;
          if (useReg->getType() == RegisterType::INT)
          {
            loadOp = RISCVOpcode::LD; // 加载整数
          }
          else
          {
            loadOp = RISCVOpcode::FLD; // 加载浮点数
          }

          if (offset <= 2047 && offset >= -2048)
          {
            auto loadInstr =
                RISCVInstruction::createIType(loadOp, tempReg, spReg, offset);
            beforeInstr.push_back(loadInstr);
          }
          else
          {
            auto addrReg = make_shared<RISCVRegister>(RegisterType::INT);
            auto liInstr = RISCVInstruction::createPseudoLI(addrReg, offset);
            beforeInstr.push_back(liInstr);
            auto addInst = RISCVInstruction::createRType(
                RISCVOpcode::ADD, addrReg, spReg, addrReg);
            beforeInstr.push_back(addInst);
            auto loadInstr =
                RISCVInstruction::createIType(loadOp, tempReg, addrReg, 0);
            beforeInstr.push_back(loadInstr);
          }
          // 替换指令中的寄存器引用
          instr->replaceUseRegister(useReg, tempReg);
        }
      }

      // 检查指令是否定义了溢出寄存器
      for (auto defReg : instr->getDefRegisters())
      {
        if (spilledRegs.find(defReg) != spilledRegs.end())
        {
          // 为定义的溢出寄存器创建临时寄存器
          auto tempReg = make_shared<RISCVRegister>(defReg->getType());
          auto spReg =
              make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);

          // 获取溢出寄存器在栈上的偏移量
          string spillName = "spill_" + defReg->toString();
          int offset = currentFunc->getStackFrame().getValueOffset(spillName);

          // 替换指令中的寄存器引用
          instr->replaceDefRegister(defReg, tempReg);

          // 创建store指令，将临时寄存器的值存储到栈上
          RISCVOpcode storeOp;
          if (defReg->getType() == RegisterType::INT)
          {
            storeOp = RISCVOpcode::SD; // 存储整数
          }
          else
          {
            storeOp = RISCVOpcode::FSD; // 存储浮点数
          }

          if (offset <= 2047 && offset >= -2048)
          {
            auto storeInstr =
                RISCVInstruction::createSType(storeOp, spReg, tempReg, offset);
            afterInstr.push_back(storeInstr);
          }
          else
          {
            auto addrReg = make_shared<RISCVRegister>(RegisterType::INT);
            auto liInstr = RISCVInstruction::createPseudoLI(addrReg, offset);
            afterInstr.push_back(liInstr);
            auto addInst = RISCVInstruction::createRType(
                RISCVOpcode::ADD, addrReg, spReg, addrReg);
            afterInstr.push_back(addInst);
            auto storeInstr =
                RISCVInstruction::createSType(storeOp, addrReg, tempReg, 0);
            afterInstr.push_back(storeInstr);
          }
        }
      }

      // 构建新的指令序列
      for (auto &beforeI : beforeInstr)
      {
        newInstructions.push_back(beforeI);
      }

      newInstructions.push_back(instr);

      for (auto &afterI : afterInstr)
      {
        newInstructions.push_back(afterI);
      }
    }

    // 用新的指令序列替换原来的指令序列
    bb->setInstructions(newInstructions);
  }
  // 清空溢出寄存器集合，因为它们已经被处理
  spilledRegs.clear();
}
// ============================================================================
// 应用分配结果
// ============================================================================
void GraphColorRegisterAllocator::applyAllocation()
{
#ifdef DEBUG_REG_ALLOC
  std::cout << "Applying register allocation to instructions..." << std::endl;
#endif

  int replacedCount = 0;
  int totalVirtualRegs = 0;

  // 遍历所有基本块和指令，替换合并的寄存器
  for (auto &bb : currentFunc->getBasicBlocks())
  {
    auto &instrs = bb->getInstructions();

    for (auto it = instrs.begin(); it != instrs.end();)
    {
      auto instr = *it;

      for (auto &useReg : instr->getUseRegisters())
      {
        if (auto keepReg = findFinalReplacement(useReg))
        {
          // 如果有keep寄存器，替换为keep寄存器
          instr->replaceUseRegister(useReg, keepReg);
        }
      }

      for (auto &defReg : instr->getDefRegisters())
      {
        if (auto keepReg = findFinalReplacement(defReg))
        {
          // 如果有keep寄存器，替换为keep寄存器
          instr->replaceDefRegister(defReg, keepReg);
        }
      }

      // 检查指令是否为move指令
      if (isMoveInstruction(instr))
      {

        auto dest = instr->getDefRegisters().front();
        auto src = instr->getUseRegisters().front();
        // 如果是move指令，检查是否需要合并
        if (dest == src)
        {
          it = instrs.erase(it); // 删除move指令
          continue;              // 跳过当前迭代，直接处理下一个指令
        }
      }

      ++it;
    }
  }
  // 遍历所有基本块和指令
  for (auto &bb : currentFunc->getBasicBlocks())
  {
    for (auto &instr : bb->getInstructions())
    {
      // 处理使用的寄存器
      auto useRegs = instr->getUseRegisters();
      for (auto &useReg : useRegs)
      {
        if (useReg->isVirtual())
        {
          totalVirtualRegs++;
          auto it = allocation.find(useReg);
          if (it != allocation.end())
          {
            // 替换虚拟寄存器为分配的物理寄存器
            instr->replaceUseRegister(useReg, it->second);
            replacedCount++;
          }
          else
          {
#ifdef DEBUG_REG_ALLOC
            std::cout << "Warning: Virtual register " << useReg->toString()
                      << " used in " << instr->toString()
                      << " has no allocation" << std::endl;
#endif
          }
        }
      }

      // 处理定义的寄存器
      auto defRegs = instr->getDefRegisters();
      for (auto &defReg : defRegs)
      {
        if (defReg->isVirtual())
        {
          totalVirtualRegs++;
          auto it = allocation.find(defReg);
          if (it != allocation.end())
          {
            // 替换虚拟寄存器为分配的物理寄存器
            instr->replaceDefRegister(defReg, it->second);
            replacedCount++;
          }
          else
          {
#ifdef DEBUG_REG_ALLOC
            std::cout << "Warning: Virtual register " << defReg->toString()
                      << " defined in " << instr->toString()
                      << " has no allocation" << std::endl;
#endif
          }
        }
      }
    }
  }

#ifdef DEBUG_REG_ALLOC
  std::cout << "Register allocation applied: replaced " << replacedCount
            << " out of " << totalVirtualRegs << " virtual register references"
            << std::endl;
#endif

  eliminateRedundantCallerArgSaveMoves();
}

void GraphColorRegisterAllocator::eliminateRedundantCallerArgSaveMoves()
{
  if (!currentModule)
    return;

  auto irFunc = currentModule->getFunction(currentFunc->getName());
  // 超过 8 个寄存器形参时会用到栈传参，save/restore 逻辑更复杂，暂不优化
  if (irFunc && irFunc->getArguments().size() > 8)
    return;

  static const vector<RISCVRegister::PhysicalReg> kCallArgDestRegs = {
      RISCVRegister::PhysicalReg::A0, RISCVRegister::PhysicalReg::A1,
      RISCVRegister::PhysicalReg::A2, RISCVRegister::PhysicalReg::A3,
      RISCVRegister::PhysicalReg::A4, RISCVRegister::PhysicalReg::A5,
      RISCVRegister::PhysicalReg::A6, RISCVRegister::PhysicalReg::A7,
      RISCVRegister::PhysicalReg::FA0, RISCVRegister::PhysicalReg::FA1,
      RISCVRegister::PhysicalReg::FA2, RISCVRegister::PhysicalReg::FA3,
      RISCVRegister::PhysicalReg::FA4, RISCVRegister::PhysicalReg::FA5,
      RISCVRegister::PhysicalReg::FA6, RISCVRegister::PhysicalReg::FA7};

  auto isCallArgDestReg = [&](const shared_ptr<RISCVRegister> &reg) -> bool
  {
    if (!reg || !reg->isPhysical())
      return false;
    for (auto pr : kCallArgDestRegs)
    {
      if (reg->getPhysicalReg() == pr)
        return true;
    }
    return false;
  };

  struct SaveInfo
  {
    shared_ptr<RISCVRegister> temp;
    shared_ptr<RISCVRegister> src;
    shared_ptr<RISCVInstruction> saveInstr;
    bool shouldDelete;
  };
  vector<SaveInfo> redundantSaves;

  for (const auto &entry : currentFunc->getMoveInstructionsBeforeCallMap())
  {
    for (const auto &moveInstr : entry.second)
    {
      if (!moveInstr || !isMoveInstruction(moveInstr))
        continue;

      const auto &useRegs = moveInstr->getUseRegisters();
      const auto &defRegs = moveInstr->getDefRegisters();
      if (useRegs.size() != 1 || defRegs.size() != 1)
        continue;

      const auto &src = useRegs[0];
      const auto &temp = defRegs[0];
      if (!src->isPhysical() || !isCalleeSavedColor(src))
        continue;

      // mv a0, s0 等同时承担 call 实参传递，需保留；mv s5/t0, s0 等可消除
      const bool shouldDelete = !isCallArgDestReg(temp);
      redundantSaves.push_back({temp, src, moveInstr, shouldDelete});
    }
  }

  if (redundantSaves.empty())
    return;

  auto rewriteCallArgMovesBeforeCall = [&](const SaveInfo &info)
  {
    for (auto &bb : currentFunc->getBasicBlocks())
    {
      auto &instrs = bb->getInstructions();
      auto saveIt = std::find(instrs.begin(), instrs.end(), info.saveInstr);
      if (saveIt == instrs.end())
        continue;

      for (auto it = saveIt + 1; it != instrs.end(); ++it)
      {
        if ((*it)->getOpcode() == RISCVOpcode::CALL)
          break;

        for (const auto &defReg : (*it)->getDefRegisters())
        {
          if (*defReg == *info.temp)
            return;
        }

        if (!isMoveInstruction(*it))
          continue;

        const auto &useRegs = (*it)->getUseRegisters();
        const auto &defRegs = (*it)->getDefRegisters();
        if (useRegs.size() != 1 || defRegs.size() != 1)
          continue;

        if (*useRegs[0] == *info.temp && isCallArgDestReg(defRegs[0]))
        {
          (*it)->replaceUseRegister(useRegs[0], info.src);
        }
      }
    }
  };

  for (const auto &info : redundantSaves)
  {
    rewriteCallArgMovesBeforeCall(info);
  }

  for (const auto &info : redundantSaves)
  {
    if (!info.shouldDelete)
      continue;

    for (auto &bb : currentFunc->getBasicBlocks())
    {
      auto &instrs = bb->getInstructions();
      auto it = std::find(instrs.begin(), instrs.end(), info.saveInstr);
      if (it != instrs.end())
      {
        instrs.erase(it);
        break;
      }
    }
  }

#ifdef DEBUG_REG_ALLOC
  size_t deletedCount = 0;
  for (const auto &info : redundantSaves)
  {
    if (info.shouldDelete)
      ++deletedCount;
  }
  if (deletedCount > 0)
  {
    std::cout << "Eliminated " << deletedCount
              << " redundant caller-arg save moves (callee-saved source)"
              << std::endl;
  }
#endif
}

shared_ptr<RISCVRegister> GraphColorRegisterAllocator::findFinalReplacement(const shared_ptr<RISCVRegister> &reg)
{
  // 递归/循环查找最终代表（如 a->b->c，返回c）
  shared_ptr<RISCVRegister> cur = reg;
  std::unordered_set<shared_ptr<RISCVRegister>> visited;
  while (true)
  {
    // coalescingManager.getKeepRegister 返回 nullptr 或最终代表
    auto next = coalescingManager.getKeepRegister(cur);
    if (!next || next == cur)
    {
      break;
    }
    // 防止环
    if (visited.count(next))
    {
      break;
    }
    visited.insert(cur);
    cur = next;
  }
  return cur;
}
namespace RISCV
{
  vector<shared_ptr<RISCVBasicBlock>> getPostOrder(shared_ptr<RISCVFunction> currentFunc)
  {
    vector<shared_ptr<RISCVBasicBlock>> postOrder;
    unordered_set<shared_ptr<RISCVBasicBlock>> visited;

    std::function<void(shared_ptr<RISCVBasicBlock>)> dfs = [&](shared_ptr<RISCVBasicBlock> bb)
    {
      if (visited.find(bb) != visited.end())
        return;
      visited.insert(bb);

      for (auto succ : bb->getSuccessors())
      {
        dfs(succ);
      }
      postOrder.push_back(bb);
    };

    // 从入口基本块开始
    if (!currentFunc->getBasicBlocks().empty())
    {
      dfs(currentFunc->getBasicBlocks()[0]);
    }

    return postOrder;
  }

  void computeBasicBlockUseDef(shared_ptr<RISCVFunction> currentFunc)
  {
    for (auto &bb : currentFunc->getBasicBlocks())
    {
      unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> useSet;
      unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> defSet;

      for (const auto &instr : bb->getInstructions())
      {
        // 先处理 use：只加入未被本块内 def 过的寄存器
        for (const auto &reg : instr->getUseRegisters())
        {
          if (defSet.find(reg) == defSet.end())
            useSet.insert(reg);
        }

        // 再处理 def：只记录第一次定义的寄存器
        for (const auto &reg : instr->getDefRegisters())
        {
          if (defSet.find(reg) == defSet.end())
            defSet.insert(reg);
        }

#ifdef DEBUG_REG_ALLOC
        std::cout << "BB: " << bb->getLabel() << ", Instr: " << instr->toString() << std::endl;
        std::cout << "  Use: ";
        for (const auto &useReg : instr->getUseRegisters())
        {
          std::cout << useReg->toString() << " ";
        }
        std::cout << "\n  Def: ";
        for (const auto &defReg : instr->getDefRegisters())
        {
          std::cout << defReg->toString() << " ";
        }
        std::cout << std::endl;
#endif
      }
      bb->setUse(useSet);
      bb->setDef(defSet);
    }
  }

  void computeLiveInOut(shared_ptr<RISCVFunction> currentFunc)
  {
    vector<shared_ptr<RISCVBasicBlock>> postOrder = getPostOrder(currentFunc);

    bool changed;
    do
    {
      changed = false;
      for (auto bb : postOrder)
      {
        unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> newLiveIn;
        unordered_set<shared_ptr<RISCVRegister>, RISCVRegister::RegisterHash, RISCVRegister::RegisterEqual> newLiveOut;

        // 计算 LiveOut
        for (const auto &succ : bb->getSuccessors())
        {
          const auto &succLiveIn = succ->getLiveIn();
          for (const auto &reg : succLiveIn)
          {
            newLiveOut.insert(reg);
          }
        }

        // 计算 LiveIn
        const auto &use = bb->getUse();
        const auto &def = bb->getDef();
        for (const auto &reg : use)
        {
          newLiveIn.insert(reg);
        }
        for (const auto &reg : newLiveOut)
        {
          if (def.find(reg) == def.end())
            newLiveIn.insert(reg);
        }

        // 检查是否有变化
        if (newLiveIn != bb->getLiveIn() || newLiveOut != bb->getLiveOut())
        {
          bb->setLiveIn(newLiveIn);
          bb->setLiveOut(newLiveOut);
          changed = true;
        }
      }
    } while (changed);

#ifdef DEBUG_REG_ALLOC
    std::cout << "Live In/Out computed for function: " << currentFunc->getName() << std::endl;
    for (const auto &bb : currentFunc->getBasicBlocks())
    {
      std::cout << "BB: " << bb->getLabel() << ", Live In: ";
      for (const auto &reg : bb->getLiveIn())
      {
        std::cout << reg->toString() << " ";
      }
      std::cout << ", Live Out: ";
      for (const auto &reg : bb->getLiveOut())
      {
        std::cout << reg->toString() << " ";
      }
      std::cout << std::endl;
    }
#endif
  }

  void computeLiveRanges(shared_ptr<RISCVFunction> currentFunc, shared_ptr<Module> module)
  {
    auto &livenessInfo = currentFunc->getLivenessInfo();

    // 1. 获取后序的基本块列表
    vector<shared_ptr<RISCVBasicBlock>> postOrder = getPostOrder(currentFunc);

    // 编号每条指令
    unordered_map<shared_ptr<RISCVInstruction>, int> instrIndex;
    int instrNum = 0;
    for (auto it = postOrder.rbegin(); it != postOrder.rend(); ++it)
    {
      auto bb = *it;
      if (bb->getInstructions().empty())
        continue;

      // 记录每条指令的索引
      for (const auto &instr : bb->getInstructions())
      {
        instrIndex[instr] = instrNum++;
      }
    }
    livenessInfo.totalInstructions = instrNum;

    // 区间合并工具
    auto extendOrAddRange = [&](shared_ptr<RISCVRegister> reg, int start, int end)
    {
      if (start > end)
        return;
      auto &ranges = livenessInfo.liveRanges[reg];
      vector<int> toMerge;
      int newStart = start, newEnd = end;
      for (int i = 0; i < ranges.size(); i++)
      {
        const auto &range = ranges[i];
        if (newStart <= range.end + 1 && newEnd >= range.start - 1)
        {
          toMerge.push_back(i);
          newStart = std::min(newStart, range.start);
          newEnd = std::max(newEnd, range.end);
        }
      }
      if (toMerge.empty())
      {
        ranges.push_back(LiveRange(newStart, newEnd));
      }
      else
      {
        LiveRange mergedRange(newStart, newEnd);
        for (int i = toMerge.size() - 1; i >= 0; i--)
          ranges.erase(ranges.begin() + toMerge[i]);
        ranges.push_back(mergedRange);
      }
    };

    auto truncateRangeAt = [&](shared_ptr<RISCVRegister> reg, int defPos)
    {
      auto &ranges = livenessInfo.liveRanges[reg];
      vector<LiveRange> newRanges;

      for (const auto &range : ranges)
      {
        if (range.end < defPos)
        {
          // 完全在定义点之前，保留
          newRanges.push_back(range);
        }
        else if (range.start > defPos)
        {
          // 完全在定义点之后，保留
          newRanges.push_back(range);
        }
        else
        {
          if (range.end > defPos)
          {
            newRanges.push_back(LiveRange(defPos + 1, range.end));
          }
        }
      }

      ranges = newRanges;
    };

    // 主算法：逆序遍历基本块
    for (auto bb : postOrder)
    {
      if (bb->getInstructions().empty())
        continue;
      int bbStart = instrIndex[bb->getInstructions().front()];
      int bbEnd = instrIndex[bb->getInstructions().back()];

      // Step 1: 对LiveOut的变量，延长区间到整个块
      for (const auto &reg : bb->getLiveOut())
      {
        extendOrAddRange(reg, bbStart, bbEnd);
      }

      // Step 2: 逆序遍历指令
      for (auto it = bb->getInstructions().rbegin(); it != bb->getInstructions().rend(); ++it)
      {
        auto instr = *it;
        int pos = instrIndex[instr];

        // 先处理def，截断区间：移除该reg的所有区间（即后续use不会再延长到更前面）
        for (const auto &defReg : instr->getDefRegisters())
        {
          truncateRangeAt(defReg, pos);
          livenessInfo.defPoints[defReg].push_back(pos);
        }
        // 这里显式把参数寄存器加入 use 集合，确保活跃分析正确
        auto useRegs = instr->getUseRegisters();
        if (instr->getOpcode() == RISCVOpcode::CALL)
        {
          auto func = module->getFunction(instr->getOperands()[0]->toString());
          int intArgCount = 0;
          int floatArgCount = 0;
          for (const auto &arg : func->getArguments())
          {
            if (arg->getType()->isIntegerTy() || arg->getType()->isPointerTy())
            {
              if (intArgCount < 8)
              {
                useRegs.push_back(
                    make_shared<RISCVRegister>(static_cast<RISCVRegister::PhysicalReg>(
                        static_cast<int>(RISCVRegister::PhysicalReg::A0) + intArgCount)));
                intArgCount++;
              }
            }
            else if (arg->getType()->isFloatTy())
            {
              if (floatArgCount < 8)
              {
                useRegs.push_back(make_shared<RISCVRegister>(static_cast<RISCVRegister::PhysicalReg>(
                    static_cast<int>(RISCVRegister::PhysicalReg::FA0) + floatArgCount)));
                floatArgCount++;
              }
            }
          }
        }

        // 再处理use，延长区间到块头-当前指令
        for (const auto &useReg : useRegs)
        {
          extendOrAddRange(useReg, bbStart, pos);
          livenessInfo.usePoints[useReg].push_back(pos);
        }
      }
    }

    for (auto bb : postOrder)
    {
      for (auto instr : bb->getInstructions())
      {
        if (!instr)
          continue;
        if (instr->getOpcode() == RISCVOpcode::CALL)
        {
          // 所有的callersaveregs都在call指令这里活跃，以实现将所有跨调用活跃变量自动分配到calleesavereg或溢出到栈上
          for (auto &reg : CallerSavedGeneralRegs)
          {
            livenessInfo.addLiveRange(reg, instrIndex[instr], instrIndex[instr]);
          }
          for (auto &reg : CallerSavedFloatRegs)
          {
            livenessInfo.addLiveRange(reg, instrIndex[instr], instrIndex[instr]);
          }

          // 删除所有不需要的call指令之后的参数复原move指令。
          auto moveInstructionsAfterCall = currentFunc->getMoveInstructionsAfterCall(instr->getOperands()[0]->toString());
          for (auto &moveInstr : moveInstructionsAfterCall)
          {
            // 只处理move指令
            if (!moveInstr)
              continue;
            if (!(moveInstr->getOpcode() == RISCVOpcode::MV || moveInstr->getOpcode() == RISCVOpcode::FMV_S))
              continue;
            // 获取目标寄存器
            auto defRegs = moveInstr->getDefRegisters();
            if (defRegs.empty())
              continue;
            auto destReg = defRegs[0];
            // 获取move指令在全局指令序列中的编号
            auto it = instrIndex.find(moveInstr);
            if (it == instrIndex.end())
              continue;
            int movePos = it->second + 1;
            // 检查该位置是否在目标寄存器的liverange内
            const auto &ranges = livenessInfo.liveRanges[destReg];
            bool inRange = false;
            for (const auto &range : ranges)
            {
              if (range.contains(movePos))
              {
                inRange = true;
                break;
              }
            }
            // 如果不在liverange内，删除该move指令
            if (!inRange)
            {
              // 在所属基本块中删除该指令
              for (auto &bb2 : currentFunc->getBasicBlocks())
              {
                auto &instrs2 = bb2->getInstructions();
                auto it2 = std::find(instrs2.begin(), instrs2.end(), moveInstr);
                if (it2 != instrs2.end())
                {
                  instrs2.erase(it2);
                  break;
                }
              }
              // 从moveInstructionsAfterCall中移除该指令，避免悬挂指针和重复遍历
              moveInstr = nullptr;
            }
          }
        }
      }
    }
  }

  void printLiveRanges(shared_ptr<RISCVFunction> currentFunc)
  {
    auto &livenessInfo = currentFunc->getLivenessInfo();
    std::cout << "Live Ranges for function: " << currentFunc->getName() << std::endl;
    for (const auto &pair : livenessInfo.liveRanges)
    {
      const auto &reg = pair.first;
      const auto &ranges = pair.second;
      std::cout << reg->toString() << ": ";
      for (const auto &range : ranges)
      {
        std::cout << "[" << range.start << ", " << range.end << "] ";
      }
      std::cout << std::endl;
    }
  }
}
