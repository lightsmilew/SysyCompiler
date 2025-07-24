#include "GraphColorRegisterAllocator.h"
#include <algorithm>
#include <iostream>
#include <cmath>

using namespace RISCV;
// Move指令分析
void GraphColorRegisterAllocator::analyzeMoveInstructions()
{
#ifdef DEBUG_REG_ALLOC
    std::cout << "Analyzing move instructions..." << std::endl;
#endif
    int totalMoves = 0;
    int constrainedMoves = 0;

    // 遍历所有基本块和指令，查找move指令
    for (auto &bb : currentFunc->getBasicBlocks())
    {
        for (auto &instr : bb->getInstructions())
        {
            // 检查是否为move类型的指令
            if (isMoveInstruction(instr))
            {
                auto useRegs = instr->getUseRegisters();
                auto defRegs = instr->getDefRegisters();

                // Move指令通常有一个源寄存器和一个目标寄存器
                if (useRegs.size() == 1 && defRegs.size() == 1)
                {
                    auto srcReg = useRegs[0];
                    auto dstReg = defRegs[0];

                    // 为每对有 move 关系的寄存器建立连接
                    moveList.addMove(srcReg, dstReg, instr);
                    totalMoves++;

                    // 检测受限的 move 关系（源和目标寄存器冲突）
                    if (interferenceGraph.interferes(srcReg, dstReg))
                    {
                        // 如果源和目标寄存器在冲突图中冲突，则标记为受限
                        moveList.constrainMoves(srcReg, dstReg);
                        constrainedMoves++;
                    }
                }
            }
        }
    }

#ifdef DEBUG_REG_ALLOC
    std::cout << "Move instruction analysis completed:" << std::endl;
    std::cout << "- Total move instructions found: " << totalMoves << std::endl;
    for (const auto &move : moveList.getAllMoves())
    {
        std::cout << "  Move: " << move.src->toString() << " -> "
                  << move.dst->toString() << " (State: "
                  << static_cast<int>(move.state) << ")" << std::endl;
    }
    std::cout << "- Constrained moves: " << constrainedMoves << std::endl;
    std::cout << "- Available for coalescing: " << (totalMoves - constrainedMoves)
              << std::endl;
#endif
}