#include "RISCVBuilder.h"
using namespace RISCV;

// 寄存器分配器
class RegisterAllocator
{
public:
    struct LiveInterval
    {
        shared_ptr<RISCVRegister> virtualReg;
        int start;
        int end;
        shared_ptr<RISCVRegister> assignedReg;
        bool isSpilled;

        LiveInterval(shared_ptr<RISCVRegister> reg, int s, int e)
            : virtualReg(reg), start(s), end(e), assignedReg(nullptr), isSpilled(false) {}
    };

private:
    shared_ptr<RISCVFunction> currentFunc;
    vector<LiveInterval> intervals;

    // 可用的物理寄存器
    static const vector<shared_ptr<RISCVRegister>> availableGeneralRegs;
    static const vector<shared_ptr<RISCVRegister>> availableFloatRegs;

public:
    RegisterAllocator() = default;

    // 主要接口
    void allocateRegisters(shared_ptr<RISCVFunction> func);

private:
    // 活跃变量分析
    void computeLiveness();
    void computeLiveIntervals();

    // 线性扫描寄存器分配
    void linearScanAllocation();
    void expireOldIntervals(const LiveInterval &current,
                            vector<LiveInterval *> &active);
    void spillAtInterval(LiveInterval &current,
                         vector<LiveInterval *> &active);

    // 插入溢出代码
    void insertSpillCode();
};
