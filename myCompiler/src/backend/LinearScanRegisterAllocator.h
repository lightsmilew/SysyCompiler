// #pragma once
// #include "RISCVBuilder.h"
// #include "RISCVDataStructure.h"
// #include <algorithm>
// #include <unordered_map>
// #include <unordered_set>
// #include <vector>

// using std::unordered_map;
// using std::unordered_set;
// using std::vector;

// namespace RISCV {
// // 活跃区间结构
// struct LiveInterval {
//   shared_ptr<RISCVRegister> reg; // 对应的寄存器
//   int start;                     // 活跃区间开始位置
//   int end;                       // 活跃区间结束位置

//   LiveInterval(shared_ptr<RISCVRegister> r, int s, int e)
//       : reg(r), start(s), end(e) {}

//   // 检查两个区间是否重叠
//   bool overlaps(const LiveInterval &other) const {
//     return !(end <= other.start || other.end <= start);
//   }

//   // 按开始位置排序的比较函数
//   bool operator<(const LiveInterval &other) const {
//     return start < other.start;
//   }
// };

// // 活跃区间指针的比较函数（按结束位置排序，用于active列表）
// struct LiveIntervalEndComparator {
//   bool operator()(const LiveInterval *a, const LiveInterval *b) const {
//     return a->end > b->end; // 结束位置晚的排在前面
//   }
// };

// // 线性扫描寄存器分配器
// class LinearScanRegisterAllocator {
// public:
//   // 主要接口：为函数分配寄存器
//   void allocateRegisters(shared_ptr<RISCVFunction> func);

// private:
//   // 当前处理的函数
//   shared_ptr<RISCVFunction> currentFunc;

//   // 可用的物理寄存器
//   static const vector<shared_ptr<RISCVRegister>> availableGeneralRegs;
//   static const vector<shared_ptr<RISCVRegister>> availableFloatRegs;

//   // 寄存器分类常量
//   static const int R_GENERAL = 24; // 可用通用寄存器数量
//   static const int R_FLOAT = 32;   // 可用浮点寄存器数量

//   // 活跃区间相关
//   vector<LiveInterval> intervals;                  // 所有活跃区间
//   vector<LiveInterval *> active;                   // 当前活跃的区间列表
//   vector<shared_ptr<RISCVRegister>> availableRegs; // 当前可用寄存器
//   vector<LiveInterval *> spilledIntervals;         // 被溢出的区间

//   // 分配结果
//   unordered_map<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>>
//       allocation;
//   unordered_set<shared_ptr<RISCVRegister>> spilledRegs;

//   // 算法主要阶段
//   void buildIntervalsFromLivenessInfo();
//   void performLinearScan();
//   void handleSpilledRegisters();

//   // 辅助函数
//   void sortIntervalsByStart();
//   void expireOldIntervals(int currentPos);
//   void spillAtInterval(LiveInterval *interval);
//   shared_ptr<RISCVRegister> selectSpillCandidate();
//   void insertSpillCode(shared_ptr<RISCVRegister> spilledReg);

//   // 寄存器管理
//   vector<shared_ptr<RISCVRegister>> getAvailableRegisters(RegisterType type);
//   int getRegisterCount(RegisterType type);
//   bool isPrecolored(shared_ptr<RISCVRegister> reg);

//   // 调试和统计
//   void printStatistics();
//   void validateAllocation();
// };
// } // namespace RISCV