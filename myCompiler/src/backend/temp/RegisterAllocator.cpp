#include "RegisterAllocator.h"
using namespace RISCV;

void RegisterAllocator::allocateRegisters(shared_ptr<RISCVFunction> func)
{
    // 1. 活跃变量分析
    LiveVariableAnalysisPass lva;
    lva.runOnFunction(func.get());

    // 2. 构建冲突图
    InterferenceGraph graph;
    buildInterferenceGraph(func.get(), graph);

    // 3. 图着色分配
    std::unordered_map<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>> assign;
    colorGraph(graph, availableGeneralRegs, assign);

    // 4. 替换虚拟寄存器为物理寄存器
    for (auto &bb : func->getBasicBlocks())
    {
        for (auto &inst : bb->getInstructions())
        {
            for (auto &op : inst->getOperands())
            {
                if (op->getType() == RISCVOperand::Type::REGISTER)
                {
                    auto vreg = op->getReg();
                    if (assign.count(vreg) && assign[vreg])
                        op->setReg(assign[vreg]);
                    else
                        ; // 插入spill代码
                }
            }
        }
    }
    // 5. 插入spill代码（略）
}