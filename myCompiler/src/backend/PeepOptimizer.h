#include "RISCVBuilder.h"
using namespace RISCV;
// 窥孔优化器
class PeepholeOptimizer
{
private:
    shared_ptr<RISCVFunction> currentFunc;

public:
    PeepholeOptimizer() = default;

    void optimize(shared_ptr<RISCVFunction> func);

private:
    // 优化模式
    bool optimizeRedundantMoves(shared_ptr<RISCVBasicBlock> bb);
    bool optimizeConstantFolding(shared_ptr<RISCVBasicBlock> bb);
    bool optimizeDeadCode(shared_ptr<RISCVBasicBlock> bb);
};
