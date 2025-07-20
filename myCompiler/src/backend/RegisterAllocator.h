#pragma once
#include "../RISCVDataStructure.h"
#include <unordered_set>
#include <unordered_map>
#include <queue>
#include <stack>
using std::stack;

namespace RISCV
{
    // 图染色寄存器分配器
    class RegisterAllocator
    {
    public:
        // 主要接口：为函数分配寄存器
        void allocateRegisters(shared_ptr<RISCVFunction> func);

    private:
        // 当前处理的函数
        shared_ptr<RISCVFunction> currentFunc;

        // 可用的物理寄存器
        static const vector<shared_ptr<RISCVRegister>> availableGeneralRegs;
        static const vector<shared_ptr<RISCVRegister>> availableFloatRegs;

        // 寄存器分类常量
        static const int K_GENERAL = 24; // 可用通用寄存器数量
        static const int K_FLOAT = 32;   // 可用浮点寄存器数量

        // 哈希函数用于寄存器对
        struct RegisterPairHash
        {
            size_t operator()(const pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>> &p) const
            {
                auto h1 = hash<shared_ptr<RISCVRegister>>{}(p.first);
                auto h2 = hash<shared_ptr<RISCVRegister>>{}(p.second);
                return h1 ^ (h2 << 1);
            }
        };

        // 冲突图相关数据结构
        unordered_map<shared_ptr<RISCVRegister>, unordered_set<shared_ptr<RISCVRegister>>> adjList;
        unordered_set<pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>>, RegisterPairHash> adjSet;
        unordered_map<shared_ptr<RISCVRegister>, int> degree;

        // 移动指令相关数据结构
        unordered_set<pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>>, RegisterPairHash> moveList;
        unordered_map<shared_ptr<RISCVRegister>, unordered_set<shared_ptr<RISCVRegister>>> moveRelated;

        // 工作列表
        unordered_set<shared_ptr<RISCVRegister>> simplifyWorklist;
        unordered_set<shared_ptr<RISCVRegister>> freezeWorklist;
        unordered_set<shared_ptr<RISCVRegister>> spillWorklist;
        unordered_set<shared_ptr<RISCVRegister>> spilledNodes;
        unordered_set<shared_ptr<RISCVRegister>> coalescedNodes;
        unordered_set<shared_ptr<RISCVRegister>> coloredNodes;
        stack<shared_ptr<RISCVRegister>> selectStack;

        // 移动指令分类
        unordered_set<pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>>, RegisterPairHash> coalescedMoves;
        unordered_set<pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>>, RegisterPairHash> constrainedMoves;
        unordered_set<pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>>, RegisterPairHash> frozenMoves;
        unordered_set<pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>>, RegisterPairHash> worklistMoves;
        unordered_set<pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>>, RegisterPairHash> activeMoves;

        // 合并相关
        unordered_map<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>> alias;

        // 着色结果
        unordered_map<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>> color;

        // 预着色寄存器
        unordered_set<shared_ptr<RISCVRegister>> precolored;

        // 所有寄存器节点
        unordered_set<shared_ptr<RISCVRegister>> initial;

        // 算法主要阶段
        void build();
        void makeWorklist();
        void simplify();
        void coalesce();
        void freeze();
        void selectSpill();
        void assignColors();
        void rewriteProgram();

        // 辅助函数
        void addEdge(shared_ptr<RISCVRegister> u, shared_ptr<RISCVRegister> v);
        bool adjacent(shared_ptr<RISCVRegister> n, shared_ptr<RISCVRegister> m);
        unordered_set<shared_ptr<RISCVRegister>> getAdjacent(shared_ptr<RISCVRegister> n);
        unordered_set<pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>>, RegisterPairHash>
        getNodeMoves(shared_ptr<RISCVRegister> n);
        bool isMoveRelated(shared_ptr<RISCVRegister> n);
        void decrementDegree(shared_ptr<RISCVRegister> m);
        void enableMoves(const unordered_set<shared_ptr<RISCVRegister>> &nodes);
        void addWorkList(shared_ptr<RISCVRegister> u);
        bool ok(shared_ptr<RISCVRegister> t, shared_ptr<RISCVRegister> r);
        bool conservative(shared_ptr<RISCVRegister> u, shared_ptr<RISCVRegister> v);
        shared_ptr<RISCVRegister> getAlias(shared_ptr<RISCVRegister> n);
        void combine(shared_ptr<RISCVRegister> u, shared_ptr<RISCVRegister> v);
        void freezeMoves(shared_ptr<RISCVRegister> u);

        // 工具函数
        int getK(shared_ptr<RISCVRegister> reg);
        bool isPrecolored(shared_ptr<RISCVRegister> reg);
        void initializePrecoloredRegisters();
        void collectMoveInstructions();
        void collectAllRegisters();

        // 调试和统计
        void printStatistics();
        void validateAllocation();
    };
}