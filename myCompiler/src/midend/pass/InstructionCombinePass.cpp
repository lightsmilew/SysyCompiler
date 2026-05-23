#include "InstructionCombinePass.h"
#include <optional>
using namespace std;
using namespace optimization;

namespace
{
    static Value *stripCopy(Value *v)
    {
        while (auto *cpy = dynamic_cast<CopyInst *>(v))
            v = cpy->getSource();
        return v;
    }

    static const ConstantInt *asConstantInt(Value *v)
    {
        return dynamic_cast<const ConstantInt *>(stripCopy(v));
    }

    static bool findPhiLatchAndPreheader(PhiInst *phi, BasicBlock *storeBb,
                                         BasicBlock *&latch, BasicBlock *&preheader)
    {
        latch = nullptr;
        preheader = nullptr;
        for (auto *pred : phi->getIncomingBlocks())
        {
            if (pred == storeBb)
                latch = storeBb;
            else
                preheader = pred;
        }
        if (!latch && phi->getIncomingBlocks().size() == 2)
        {
            latch = phi->getIncomingBlocks()[1];
            preheader = phi->getIncomingBlocks()[0];
        }
        return latch && preheader;
    }

    static int getPhiStepOnLatch(PhiInst *phi, BasicBlock *latch)
    {
        for (int i = 0; i < phi->getIncomingBlocks().size(); ++i)
        {
            if (phi->getIncomingBlocks()[i] != latch)
                continue;
            Value *back = stripCopy(phi->getIncomingValue(i));
            if (back == phi)
                return 0;
            auto *addInst = dynamic_cast<BinaryOperator *>(back);
            if (!addInst || addInst->getOpcode() != Opcode::Add)
                return -1;
            const ConstantInt *step = nullptr;
            if (addInst->getLHS() == phi)
                step = dynamic_cast<ConstantInt *>(addInst->getRHS());
            else if (addInst->getRHS() == phi)
                step = dynamic_cast<ConstantInt *>(addInst->getLHS());
            else
                return -1;
            if (!step)
                return -1;
            return step->Value;
        }
        return -1;
    }

    static Value *getPhiInitOnPreheader(PhiInst *phi, BasicBlock *preheader)
    {
        for (int i = 0; i < phi->getIncomingBlocks().size(); ++i)
        {
            if (phi->getIncomingBlocks()[i] == preheader)
                return phi->getIncomingValue(i);
        }
        return nullptr;
    }

    static bool phiInductionWithEvenInit(PhiInst *phi, BasicBlock *storeBb);

    // 循环外初值为偶数：常数偶数，或外层满足条件的归纳变量
    static bool initValueIsEven(Value *init, BasicBlock *latchBb)
    {
        init = stripCopy(init);
        if (auto *initConst = asConstantInt(init))
            return (initConst->Value & 1) == 0;
        if (auto *initPhi = dynamic_cast<PhiInst *>(init))
            return phiInductionWithEvenInit(initPhi, latchBb);
        return false;
    }

    // 归纳变量：preheader 偶数初值 + latch 常数偶数步长 => 下标恒为偶数
    static bool phiInductionWithEvenInit(PhiInst *phi, BasicBlock *storeBb)
    {
        if (phi->getIncomingBlocks().size() > 2)
            return false;
        BasicBlock *latch = nullptr;
        BasicBlock *preheader = nullptr;
        if (!findPhiLatchAndPreheader(phi, storeBb, latch, preheader))
            return false;
        Value *init = getPhiInitOnPreheader(phi, preheader);
        if (!init || !initValueIsEven(init, latch))
            return false;
        int step = getPhiStepOnLatch(phi, latch);
        return step >= 0 && (step & 1) == 0;
    }

    // 已知偶/奇；nullopt 表示无法证明（不得合并 sd）
    static optional<bool> indexParityEven(Value *idx, BasicBlock *loopBb);

    static optional<bool> indexParityEvenImpl(Value *idx, BasicBlock *loopBb)
    {
        idx = stripCopy(idx);
        if (auto *c = asConstantInt(idx))
            return (c->Value & 1) == 0;
        if (auto *phi = dynamic_cast<PhiInst *>(idx))
        {
            if (phiInductionWithEvenInit(phi, loopBb))
                return true;
            return nullopt;
        }
        if (auto *addInst = dynamic_cast<BinaryOperator *>(idx))
        {
            if (addInst->getOpcode() != Opcode::Add)
                return nullopt;
            const ConstantInt *off = nullptr;
            Value *base = nullptr;
            if (auto *rhs = asConstantInt(addInst->getRHS()))
            {
                off = rhs;
                base = addInst->getLHS();
            }
            else if (auto *lhs = asConstantInt(addInst->getLHS()))
            {
                off = lhs;
                base = addInst->getRHS();
            }
            else
                return nullopt;
            auto baseParity = indexParityEven(base, loopBb);
            if (!baseParity.has_value())
                return nullopt;
            if ((off->Value & 1) == 0)
                return *baseParity;
            return !*baseParity;
        }
        return nullopt;
    }

    static optional<bool> indexParityEven(Value *idx, BasicBlock *loopBb)
    {
        return indexParityEvenImpl(idx, loopBb);
    }

    static bool isEvenIndexValue(Value *idx, BasicBlock *loopBb)
    {
        auto parity = indexParityEven(idx, loopBb);
        return parity.has_value() && *parity;
    }

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
                    auto initParity = indexParityEven(initPhi, bb);
                    if (initParity.has_value() && !*initParity)
                        return false;
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

    // 第二处地址为 addd(gep1, 4) 时也视为与 gep(..., idx+1) 等价
    static bool isConsecutiveStoreAddr(Value *addr1, Value *addr2,
                                       const vector<Value *> &indices1)
    {
        auto *gep2 = dynamic_cast<GetElementPtrInst *>(addr2);
        if (gep2)
            return indicesAreConsecutive(indices1, gep2->getIndices());
        auto *addd = dynamic_cast<BinaryOperator *>(addr2);
        if (!addd || addd->getOpcode() != Opcode::Addd || addd->getLHS() != addr1)
            return false;
        auto *off = dynamic_cast<ConstantInt *>(addd->getRHS());
        return off && off->Value == 4;
    }

    static bool hasInterveningLoad(const vector<unique_ptr<Instruction>> &insts, size_t from, size_t to)
    {
        // 两条 store 之间只要有 load 就不合并：避免延后写入而中间 load 仍读到旧值。
        for (size_t k = from + 1; k < to; ++k)
        {
            Instruction *midInst = insts[k].get();
            if (midInst && midInst->getOpcode() == Opcode::Load)
                return true;
        }
        return false;
    }

    static bool storeAddrsSameBase(Value *addr1, GetElementPtrInst *gep1, Value *addr2)
    {
        if (auto *gep2 = dynamic_cast<GetElementPtrInst *>(addr2))
            return gep1->getPointerOperand() == gep2->getPointerOperand();
        auto *addd = dynamic_cast<BinaryOperator *>(addr2);
        return addd && addd->getOpcode() == Opcode::Addd && addd->getLHS() == addr1;
    }

    // i32 双字 store(sd) 要求首地址 8 字节对齐：末维下标为偶数（含归纳变量偶数初值）
    static bool lastIndexAlignedForI64Store(GetElementPtrInst *gep, BasicBlock *bb)
    {
        auto indices = gep->getIndices();
        if (indices.empty())
            return false;
        return isEvenIndexValue(indices.back(), bb);
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
            if (val1->getType()->isFloatTy())
                continue;
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
                if (val2->getType()->isFloatTy())
                    continue;
                Value *addr2 = inst2->getOperands()[1];
                if (!isConsecutiveStoreAddr(addr1, addr2, indices1))
                    continue;
                if (!storeAddrsSameBase(addr1, gep1, addr2))
                    continue;
                if (!lastIndexAlignedForI64Store(gep1, bb.get()))
                    continue;
                if (hasInterveningLoad(insts, i, j))
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

                inst1->removeThisFromOperands();
                inst2->removeThisFromOperands();
                if (verbose)
                {
                    debugInfo << "Combined instructions: " << inst1->toString() << " and "
                              << inst2->toString() << " with " << combined->toString() << "\n";
                }
                // 在第二个 store 处插入，保证其间对 val2 的 load 等已执行
                needToDelete.push_back(insts[j].release());
                insts[j] = std::unique_ptr<Instruction>(combined);
                needToDelete.push_back(insts[i].release());
                insts.erase(insts.begin() + i);
                changed = true;
                break;
            }
        }
    }
    return changed;
}
