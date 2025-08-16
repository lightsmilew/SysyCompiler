#include "InstructionCombinePass.h"
using namespace std;
using namespace optimization;
bool InstructionCombinePass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        for (size_t i = 0; i < insts.size(); ++i)
        {
            Instruction *inst1 = insts[i].get();
            if (!inst1 || inst1->getOpcode() != Opcode::Store)
                continue;
            Value *val1 = inst1->getOperands()[0];
            auto *const1 = dynamic_cast<ConstantInt *>(val1);
            if (!const1)
                continue;
            Value *addr1 = inst1->getOperands()[1];
            auto *gep1 = dynamic_cast<GetElementPtrInst *>(addr1);
            if (!gep1)
                continue;
            auto indices1 = gep1->getIndices();
            auto dimsizes1 = gep1->getArrayStride();
            // 一维数组不检查
            if (dimsizes1)
            {
                // 多维数组：如果有一维长度为奇数则跳过
                bool oddDim = false;
                for (size_t d = 0; d < (*dimsizes1).size(); ++d)
                {
                    int dimSize = (*dimsizes1)[d];
                    if (dimSize % 2 != 0)
                    {
                        oddDim = true;
                        break;
                    }
                }
                if (oddDim)
                    continue;
            }
            // 检查最后一个下标是否为phi指令，且初值为0
            if (!indices1.empty())
            {
                auto *phi = dynamic_cast<PhiInst *>(indices1.back());
                if (phi)
                {
                    if (phi->getIncomingBlocks().size() > 2)
                        continue;
                    // 检查来自非本基本块的phi输入，则是初值
                    Value *initPhi = nullptr;
                    for (int i = 0; i < phi->getIncomingBlocks().size(); i++)
                    {
                        if (phi->getIncomingBlocks()[i] != bb.get())
                            continue;
                        else
                        {
                            initPhi = phi->getIncomingValue(i);
                            break;
                        }
                    }
                    if (initPhi)
                    {
                        auto initConstant = dynamic_cast<ConstantInt *>(initPhi);
                        if (initConstant && initConstant->Value != 0)
                        {
                            continue;
                        }
                    }
                }
            }

            for (size_t j = i + 1; j < insts.size(); ++j)
            {
                Instruction *inst2 = insts[j].get();
                if (!inst2 || inst2->getOpcode() != Opcode::Store)
                    continue;
                Value *val2 = inst2->getOperands()[0];
                auto *const2 = dynamic_cast<ConstantInt *>(val2);
                if (!const2 || const2->Value != const1->Value)
                    continue;
                Value *addr2 = inst2->getOperands()[1];
                auto *gep2 = dynamic_cast<GetElementPtrInst *>(addr2);
                if (!gep2)
                    continue;
                auto indices2 = gep2->getIndices();
                // 多维判断：所有维度除最后一维都相同，最后一维连续
                if (indices1.size() != indices2.size())
                    continue;
                bool allDimEq = true;
                for (size_t d = 0; d + 1 < indices1.size(); ++d)
                {
                    if (indices1[d] != indices2[d])
                    {
                        allDimEq = false;
                        break;
                    }
                }
                if (!allDimEq)
                    continue;
                // 最后一维是否连续
                auto *binaryInst = dynamic_cast<BinaryOperator *>(indices2.back());
                if (!binaryInst || binaryInst->getOpcode() != Opcode::Add || binaryInst->getLHS() != indices1.back())
                    continue;
                auto constIdx = dynamic_cast<ConstantInt *>(binaryInst->getRHS());
                if (!constIdx || constIdx->Value != 1)
                    continue;
                // 检查i和j之间是否有对这两个地址的load
                bool hasLoad = false;
                for (size_t k = i + 1; k < j; ++k)
                {
                    Instruction *midInst = insts[k].get();
                    if (!midInst || midInst->getOpcode() != Opcode::Load)
                        continue;
                    Value *loadAddr = midInst->getOperands()[0];
                    if (loadAddr == addr1 || loadAddr == addr2)
                    {
                        hasLoad = true;
                        break;
                    }
                }
                if (hasLoad)
                    continue;

                // 构造一个constantLong
                auto *combineConstant = new ConstantLong(LongType::getInstance(),
                                                         (static_cast<uint64_t>(const1->Value) << 32) | static_cast<uint32_t>(const2->Value));
                auto *store2 = new StoreInst(Opcode::Stored, combineConstant, addr1);
                insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(store2));
                inst1->removeThisFromOperands();
                inst2->removeThisFromOperands();
                if (verbose)
                {
                    debugInfo << "Combined instructions: " << inst1->toString() << " and " << inst2->toString() << " with " << store2->toString() << "\n";
                }
                needToDelete.push_back(insts[i + 1].release());
                needToDelete.push_back(insts[j + 1].release());
                insts.erase(insts.begin() + j + 1);
                insts.erase(insts.begin() + i + 1);
                changed = true;
                break;
            }
        }
    }
    return changed;
}