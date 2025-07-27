#pragma once
#include "../RISCVBuilder.h"
#include "../RISCVDataStructure.h"
#include <algorithm>
#include <functional>
#include <memory>
#include <queue>
#include <stack>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

// 调试输出控制宏
// 定义 DEBUG_REG_ALLOC 以启用寄存器分配器的详细输出
//#define DEBUG_REG_ALLOC

using std::deque;
using std::shared_ptr;
using std::stack;
using std::unordered_map;
using std::unordered_set;
using std::vector;

namespace RISCV
{
  // 前向声明
  class RISCVRegister;

  // 自定义hash函数对象 - 基于寄存器属性而不是指针地址
  struct RegisterHash
  {
    size_t operator()(const shared_ptr<RISCVRegister> &reg) const
    {
      if (!reg)
        return 0;

      // 基于寄存器的实际属性计算hash，而不是指针地址
      size_t h1 = std::hash<int>{}(static_cast<int>(reg->getType()));
      size_t h2 = reg->isVirtual()
                      ? std::hash<int>{}(reg->getVirtualId())
                      : std::hash<int>{}(static_cast<int>(reg->getPhysicalReg()));

      return h1 ^ (h2 << 1);
    }
  };

  // 自定义相等比较函数对象
  struct RegisterEqual
  {
    bool operator()(const shared_ptr<RISCVRegister> &lhs,
                    const shared_ptr<RISCVRegister> &rhs) const
    {
      if (!lhs || !rhs)
        return lhs == rhs;

      // 基于寄存器属性比较，而不是指针地址
      if (lhs->getType() != rhs->getType())
        return false;

      if (lhs->isVirtual() != rhs->isVirtual())
        return false;

      if (lhs->isVirtual())
        return lhs->getVirtualId() == rhs->getVirtualId();
      else
        return lhs->getPhysicalReg() == rhs->getPhysicalReg();
    }
  };

  // 节点状态枚举
  enum class NodeState
  {
    INITIAL,        // 初始状态
    PRECOLORED,     // 预着色（物理寄存器）
    SIMPLIFY_READY, // 可简化
    FREEZE_READY,   // 可冻结
    SPILL_READY,    // 需要溢出
    COALESCED,      // 已合并
    COLORED,        // 已着色
    SPILLED         // 已溢出
  };

  // Move指令状态枚举
  enum class MoveState
  {
    COALESCED,      // 已合并
    CONSTRAINED,    // 受限（源和目标冲突）
    FROZEN,         // 已冻结
    WORKLIST_MOVES, // 工作列表中的move
    ACTIVE_MOVES    // 活跃的move
  };

  // 冲突图类
  class InterferenceGraph
  {
  public:
    // 节点管理
    void addNode(shared_ptr<RISCVRegister> reg);
    void addEdge(shared_ptr<RISCVRegister> reg1, shared_ptr<RISCVRegister> reg2);
    void removeNode(shared_ptr<RISCVRegister> reg);

    // 查询接口
    bool interferes(shared_ptr<RISCVRegister> reg1,
                    shared_ptr<RISCVRegister> reg2) const;
    int getDegree(shared_ptr<RISCVRegister> reg) const;
    const unordered_set<shared_ptr<RISCVRegister>, RegisterHash, RegisterEqual> &
    getNeighbors(shared_ptr<RISCVRegister> reg) const;
    const unordered_set<shared_ptr<RISCVRegister>, RegisterHash, RegisterEqual> &
    getNodes() const
    {
      return nodes;
    }

    // 合并操作
    void coalesceNodes(shared_ptr<RISCVRegister> reg1,
                       shared_ptr<RISCVRegister> reg2);

    // 调试输出
    void printGraph() const;

  private:
    // 邻接表表示 - 使用自定义hash和equal函数
    unordered_map<
        shared_ptr<RISCVRegister>,
        unordered_set<shared_ptr<RISCVRegister>, RegisterHash, RegisterEqual>,
        RegisterHash, RegisterEqual>
        adjList;
    unordered_map<shared_ptr<RISCVRegister>, int, RegisterHash, RegisterEqual>
        degree;
    unordered_set<shared_ptr<RISCVRegister>, RegisterHash, RegisterEqual> nodes;

    // 空集合，用于返回不存在节点的邻居
    static const unordered_set<shared_ptr<RISCVRegister>, RegisterHash,
                               RegisterEqual>
        emptySet;
  };

  // Move指令管理类
  class MoveList
  {
  public:
    struct MoveInstruction
    {
      shared_ptr<RISCVRegister> src;
      shared_ptr<RISCVRegister> dst;
      shared_ptr<RISCVInstruction> instruction;
      MoveState state;

      MoveInstruction(shared_ptr<RISCVRegister> s, shared_ptr<RISCVRegister> d,
                      shared_ptr<RISCVInstruction> instr)
          : src(s), dst(d), instruction(instr), state(MoveState::WORKLIST_MOVES)
      {
      }
    };
    // Move指令管理
    // 检查是否还有可以处理的move指令（状态为WORKLIST_MOVES）
    bool hasWorklistMoves() const;

    // 检查MoveList是否完全为空（用于你的循环条件）
    bool isEmpty() const;

    // 获取下一个可处理的move（用于合并阶段）
    std::pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>> getNextWorklistMove();

    // 将指定的move标记为已处理（从WORKLIST_MOVES移除）
    void markMoveAsProcessed(shared_ptr<RISCVRegister> src, shared_ptr<RISCVRegister> dst, MoveState newState);

    // 获取工作列表中move的数量（用于调试）
    size_t getWorklistMoveCount() const;
    void printWorklistMoves() const;
    void addMove(shared_ptr<RISCVRegister> src, shared_ptr<RISCVRegister> dst,
                 shared_ptr<RISCVInstruction> instr);

    // 查询接口
    bool isMoveRelated(shared_ptr<RISCVRegister> reg) const;
    bool canCoalesce(shared_ptr<RISCVRegister> reg1,
                     shared_ptr<RISCVRegister> reg2) const;

    // 状态管理
    void freezeMoves(shared_ptr<RISCVRegister> reg);
    void coalesceMoves(shared_ptr<RISCVRegister> reg1,
                       shared_ptr<RISCVRegister> reg2);
    void constrainMoves(shared_ptr<RISCVRegister> reg1,
                        shared_ptr<RISCVRegister> reg2);

    // 获取相关的move指令
    vector<int> getRelatedMoves(shared_ptr<RISCVRegister> reg) const;
    const vector<MoveInstruction> &getAllMoves() const { return moves; }
    MoveState getMoveState(shared_ptr<RISCVInstruction> instr);

    // 调试输出
    void printMoves() const;

  private:
    vector<MoveInstruction> moves;
    unordered_map<shared_ptr<RISCVRegister>, vector<int>, RegisterHash,
                  RegisterEqual>
        regToMoves;
  };

  // 工作列表管理类
  class WorklistManager
  {
  public:
    // 工作列表类型
    enum class WorklistType
    {
      SIMPLIFY, // 可简化节点（度数 < K）
      FREEZE,   // 可冻结节点（move-related但度数 < K）
      SPILL     // 需要溢出的节点（度数 >= K）
    };

    // 工作列表管理
    void addToWorklist(shared_ptr<RISCVRegister> reg, WorklistType type);
    void removeFromWorklist(shared_ptr<RISCVRegister> reg);
    shared_ptr<RISCVRegister> getNext(WorklistType type);
    bool isEmpty(WorklistType type) const;
    bool isEmpty() const;

    // 查询接口
    WorklistType getWorklistType(shared_ptr<RISCVRegister> reg) const;
    bool isInWorklist(shared_ptr<RISCVRegister> reg) const;

    // 统计信息
    size_t getSize(WorklistType type) const;
    void printWorklistSizes() const;

    // 清空所有工作列表
    void clear();

    // 获取特定工作列表中的所有节点（用于调试和算法验证）
    vector<shared_ptr<RISCVRegister>> getAllNodes(WorklistType type) const;

  private:
    unordered_map<WorklistType, deque<shared_ptr<RISCVRegister>>> worklists;
    unordered_map<shared_ptr<RISCVRegister>, WorklistType, RegisterHash,
                  RegisterEqual>
        regToWorklist;
  };

  struct CoalescingPair
  {
    shared_ptr<RISCVRegister> keep, merge;
    CoalescingPair(shared_ptr<RISCVRegister> k, shared_ptr<RISCVRegister> m)
        : keep(k), merge(m) {}
  };

  struct CoalescingManager
  {
    vector<CoalescingPair> pairs; // 存储合并对

    // 添加合并对
    void addPair(shared_ptr<RISCVRegister> keep,
                 shared_ptr<RISCVRegister> merge)
    {
      pairs.emplace_back(keep, merge);
    }

    void clear()
    {
      pairs.clear();
    }

    shared_ptr<RISCVRegister> getKeepRegister(shared_ptr<RISCVRegister> reg) const
    {
      for (const auto &pair : pairs)
      {
        if (pair.merge == reg)
          return pair.keep;
      }
      return nullptr; // 如果没有找到，返回nullptr
    }
  };

  // 图染色寄存器分配器主类
  class GraphColorRegisterAllocator
  {
  public:
    // 主要接口
    void allocateRegisters(shared_ptr<RISCVFunction> func, shared_ptr<Module> irModule);

  private:
    // 核心数据结构
    shared_ptr<RISCVFunction> currentFunc;
    InterferenceGraph interferenceGraph;
    InterferenceGraph readInterferenceGraph;
    MoveList moveList;
    WorklistManager worklistManager;
    CoalescingManager coalescingManager;

    // 节点状态管理
    unordered_map<shared_ptr<RISCVRegister>, NodeState, RegisterHash,
                  RegisterEqual>
        nodeStates;
    // 分配结果
    unordered_map<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>,
                  RegisterHash, RegisterEqual>
        allocation;
    unordered_set<shared_ptr<RISCVRegister>, RegisterHash, RegisterEqual>
        spilledRegs;

    // 算法栈
    stack<shared_ptr<RISCVRegister>> selectStack;

    // 可用寄存器
    static const vector<shared_ptr<RISCVRegister>> availableGeneralRegs;
    static const vector<shared_ptr<RISCVRegister>> availableFloatRegs;
    static const int K_GENERAL = 25; // 可用通用寄存器数量
    static const int K_FLOAT = 32;   // 可用浮点寄存器数量

    // 算法阶段
    void buildInterferenceGraph();
    void analyzeMoveInstructions();
    void initializeWorklists();
    void performSimplification();
    void performCoalescing();
    void performFreezing();
    void selectSpillCandidates();
    void assignColors();
    void handleSpilledRegisters();
    void applyAllocation();

    // 溢出代价计算
    double calculateSpillCost(shared_ptr<RISCVRegister> reg);

    // 合并阶段辅助函数
    bool canSafelyCoalesce(shared_ptr<RISCVRegister> reg1,
                           shared_ptr<RISCVRegister> reg2);
    bool briggsConservativeHeuristic(shared_ptr<RISCVRegister> reg1,
                                     shared_ptr<RISCVRegister> reg2);
    void executeCoalescing(shared_ptr<RISCVRegister> reg1,
                           shared_ptr<RISCVRegister> reg2);
    vector<shared_ptr<RISCVRegister>>
    getUnionOfNeighbors(shared_ptr<RISCVRegister> reg1,
                        shared_ptr<RISCVRegister> reg2);
    std::pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>>
    selectCoalescingCandidate();

    // 辅助函数
    void classifyNode(shared_ptr<RISCVRegister> reg);
    void reclassifyNode(shared_ptr<RISCVRegister> reg);
    void reclassifyAffectedNodes(
        const vector<shared_ptr<RISCVRegister>> &affectedNodes);
    void buildInterferencesByType(RegisterType type);
    bool isMoveInstruction(shared_ptr<RISCVInstruction> instr) const;
    bool isPrecolored(shared_ptr<RISCVRegister> reg) const;
    int getK(RegisterType type) const;
    vector<shared_ptr<RISCVRegister>> getAvailableColors(RegisterType type) const;
    NodeState getNodeState(shared_ptr<RISCVRegister> reg) const;
    void setNodeState(shared_ptr<RISCVRegister> reg, NodeState state);
    shared_ptr<RISCVRegister> findFinalReplacement(const shared_ptr<RISCVRegister> &reg);
    // 调试和统计
    void printStatistics();
    void validateAllocation();
  };
  vector<shared_ptr<RISCVBasicBlock>> getPostOrder(shared_ptr<RISCVFunction> currentFunc);
  void computeBasicBlockUseDef(shared_ptr<RISCVFunction> currentFunc);
  void computeLiveInOut(shared_ptr<RISCVFunction> currentFunc);
  void computeLiveRanges(shared_ptr<RISCVFunction> currentFunc, shared_ptr<Module> module);
  void printLiveRanges(shared_ptr<RISCVFunction> currentFunc);
} // namespace RISCV
