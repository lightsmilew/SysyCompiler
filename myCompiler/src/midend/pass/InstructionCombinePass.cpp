#include "InstructionCombinePass.h"
using namespace std;
using namespace optimization;

namespace
{
    static bool gepPassesArrayChecks(GetElementPtrInst *gep, BasicBlock *bb)
    {
        auto indices = gep->getIndices();
        auto dimsizes = gep->getArrayStride();
        if (dimsizes)
        {
            for (size_t d = 0; d < (*dimsizes).size(); ++d)
            {
                if ((*dimsizes)[d] % 2 != 0)
                {
                    return false;
                }
            }
        }
        if (!indices.empty())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(indices.back()))
            {
                if (phi->getIncomingBlocks().size() > 2)
                    return false;
                Value *initPhi = nullptr;
                for (int i = 0; i < phi->getIncomingBlocks().size(); i++)
                {
                    if (phi->getIncomingBlocks()[i] == bb)
                        continue;
                    initPhi = phi->getIncomingValue(i);
                    break;
                }
                if (initPhi)
                {
                    if (auto initConstant = dynamic_cast<ConstantInt *>(initPhi))
                    {
                        if (initConstant->Value != 0)
                            return false;
                    }
                }
            }
        }
        return true;
    }

    static bool indicesAreConsecutive(const vector<Value *> &indices1,
                                      const vector<Value *> &indices2)
    {
        if (indices1.size() != indices2.size())
            return false;
        for (size_t d = 0; d + 1 < indices1.size(); ++d)
        {
            if (indices1[d] != indices2[d])
                return false;
        }
        auto *binaryInst = dynamic_cast<BinaryOperator *>(indices2.back());
        if (!binaryInst || binaryInst->getOpcode() != Opcode::Add || binaryInst->getLHS() != indices1.back())
            return false;
        auto constIdx = dynamic_cast<ConstantInt *>(binaryInst->getRHS());
        return constIdx && constIdx->Value == 1;
    }

    static bool hasInterveningLoad(const vector<unique_ptr<Instruction>> &insts,
                                   size_t from, size_t to, Value *addr1, Value *addr2)
    {
        for (size_t k = from + 1; k < to; ++k)
        {
            Instruction *midInst = insts[k].get();
            if (!midInst || midInst->getOpcode() != Opcode::Load)
                continue;
            Value *loadAddr = midInst->getOperands()[0];
            if (loadAddr == addr1 || loadAddr == addr2)
                return true;
        }
        return false;
    }
}

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
            Value *addr1 = inst1->getOperands()[1];
            auto *gep1 = dynamic_cast<GetElementPtrInst *>(addr1);
            if (!gep1)
                continue;
            if (!gepPassesArrayChecks(gep1, bb.get()))
                continue;
            auto indices1 = gep1->getIndices();

            for (size_t j = i + 1; j < insts.size(); ++j)
            {
                Instruction *inst2 = insts[j].get();
                if (!inst2 || inst2->getOpcode() != Opcode::Store)
                    continue;

                Value *val2 = inst2->getOperands()[0];
                Value *addr2 = inst2->getOperands()[1];
                auto *gep2 = dynamic_cast<GetElementPtrInst *>(addr2);
                if (!gep2)
                    continue;
                auto indices2 = gep2->getIndices();
                if (!indicesAreConsecutive(indices1, indices2))
                    continue;
                if (hasInterveningLoad(insts, i, j, addr1, addr2))
                    continue;

                auto *const2 = dynamic_cast<ConstantInt *>(val2);
                Instruction *combined = nullptr;

                if (const1 && const2 && const2->Value == const1->Value)
                {
                    auto *combineConstant = new ConstantLong(
                        LongType::getInstance(),
                        (static_cast<uint64_t>(static_cast<uint32_t>(const1->Value)) << 32) |
                            static_cast<uint32_t>(const2->Value));
                    combined = new StoreInst(Opcode::Stored, combineConstant, addr1);
                }
                else
                {
                    // hi = 高地址(val2)，lo = 低地址(val1)
                    combined = new StorePairInst(addr1, val2, val1);
                }

                insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(combined));
                inst1->removeThisFromOperands();
                inst2->removeThisFromOperands();
                if (verbose)
                {
                    debugInfo << "Combined instructions: " << inst1->toString() << " and "
                              << inst2->toString() << " with " << combined->toString() << "\n";
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
