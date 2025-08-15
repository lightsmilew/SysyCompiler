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
            if (!gep1 || gep1->getIndices().size() > 1)
                continue;
            auto ops1 = gep1->getIndices()[0];

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
                if (!gep2 || gep2->getIndices().size() > 1)
                    continue;
                auto ops2 = gep2->getIndices()[0];
                // 判断地址是否连续
                if (auto *binaryInst = dynamic_cast<BinaryOperator *>(ops2))
                {
                    if (binaryInst->getOpcode() != Opcode::Add || binaryInst->getLHS() != ops1)
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
                    auto *combineConstant = new ConstantLong(LongType::getInstance(), (static_cast<uint64_t>(const1->Value) << 32) | static_cast<uint32_t>(const2->Value));
                    auto *store2 = new StoreInst(Opcode::Stored, combineConstant, addr1);
                    insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(store2));
                    inst1->removeThisFromOperands();
                    inst2->removeThisFromOperands();
                    if (verbose)
                    {
                        debugInfo << "Combined instructions: " << inst1->toString() << " and " << inst2->toString() << " with " << store2->toString() << "\n";
                    }
                    // 删除指令,因为插入了一条stored，所以要加1
                    needToDelete.push_back(insts[i + 1].release());
                    needToDelete.push_back(insts[j + 1].release());
                    insts.erase(insts.begin() + j + 1);
                    insts.erase(insts.begin() + i + 1);
                    changed = true;
                    break;
                }
            }
        }
    }
    return changed;
}