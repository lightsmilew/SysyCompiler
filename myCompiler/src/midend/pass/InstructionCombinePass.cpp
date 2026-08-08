#include "InstructionCombinePass.h"
#include <optional>
using namespace std;
using namespace optimization;

namespace
{
    static string freshName(const string &prefix)
    {
        static int id = 0;
        return prefix + to_string(id++);
    }

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

    // 第二处地址为 addd(addr1, 4)，或同基址 addd 偏移差 4，或 gep(..., idx+1)
    static bool isConsecutiveStoreAddr(Value *addr1, Value *addr2,
                                       const vector<Value *> &indices1)
    {
        addr1 = stripCopy(addr1);
        addr2 = stripCopy(addr2);
        auto *gep2 = dynamic_cast<GetElementPtrInst *>(addr2);
        if (gep2 && !indices1.empty())
            return indicesAreConsecutive(indices1, gep2->getIndices());
        auto *addd2 = dynamic_cast<BinaryOperator *>(addr2);
        if (addd2 && addd2->getOpcode() == Opcode::Addd && stripCopy(addd2->getLHS()) == addr1)
        {
            auto *off = dynamic_cast<ConstantInt *>(addd2->getRHS());
            return off && off->Value == 4;
        }
        auto *addd1 = dynamic_cast<BinaryOperator *>(addr1);
        if (addd1 && addd2 && addd1->getOpcode() == Opcode::Addd && addd2->getOpcode() == Opcode::Addd &&
            stripCopy(addd1->getLHS()) == stripCopy(addd2->getLHS()))
        {
            auto *o1 = dynamic_cast<ConstantInt *>(addd1->getRHS());
            auto *o2 = dynamic_cast<ConstantInt *>(addd2->getRHS());
            return o1 && o2 && o2->Value == o1->Value + 4;
        }
        return false;
    }

    static GetElementPtrInst *rootGep(Value *addr)
    {
        addr = stripCopy(addr);
        while (addr)
        {
            if (auto *gep = dynamic_cast<GetElementPtrInst *>(addr))
                return gep;
            auto *addd = dynamic_cast<BinaryOperator *>(addr);
            if (!addd || addd->getOpcode() != Opcode::Addd)
                return nullptr;
            addr = stripCopy(addd->getLHS());
        }
        return nullptr;
    }

    // 证明 loadPtr 与 storeAddr 指向不同的 i32 单元（保守：无法证明则视为可能别名）
    static bool clearlyDistinctI32Addrs(Value *loadPtr, Value *storeAddr)
    {
        loadPtr = stripCopy(loadPtr);
        storeAddr = stripCopy(storeAddr);
        if (!loadPtr || !storeAddr || loadPtr == storeAddr)
            return false;

        // load = addd(store, imm≠0) 或 store = addd(load, imm≠0)
        auto distinctAddd = [](Value *a, Value *b) -> bool {
            auto *addd = dynamic_cast<BinaryOperator *>(a);
            if (!addd || addd->getOpcode() != Opcode::Addd || stripCopy(addd->getLHS()) != b)
                return false;
            auto *off = dynamic_cast<ConstantInt *>(addd->getRHS());
            return off && off->Value != 0;
        };
        if (distinctAddd(loadPtr, storeAddr) || distinctAddd(storeAddr, loadPtr))
            return true;

        // 同基址 addd，常量偏移不同
        auto *a1 = dynamic_cast<BinaryOperator *>(storeAddr);
        auto *a2 = dynamic_cast<BinaryOperator *>(loadPtr);
        if (a1 && a2 && a1->getOpcode() == Opcode::Addd && a2->getOpcode() == Opcode::Addd &&
            stripCopy(a1->getLHS()) == stripCopy(a2->getLHS()))
        {
            auto *o1 = dynamic_cast<ConstantInt *>(a1->getRHS());
            auto *o2 = dynamic_cast<ConstantInt *>(a2->getRHS());
            if (o1 && o2 && o1->Value != o2->Value)
                return true;
        }

        auto *g1 = rootGep(storeAddr);
        auto *g2 = rootGep(loadPtr);
        if (!g1 || !g2)
            return false;
        // 不同底层数组/指针
        if (stripCopy(g1->getPointerOperand()) != stripCopy(g2->getPointerOperand()))
            return true;
        // 同数组：末维为 storeIdx 与 storeIdx+k（k≠0）
        auto i1 = g1->getIndices();
        auto i2 = g2->getIndices();
        if (i1.size() != i2.size() || i1.empty())
            return false;
        for (size_t d = 0; d + 1 < i1.size(); ++d)
        {
            if (stripCopy(i1[d]) != stripCopy(i2[d]))
                return true; // 其它维不同 → 不同元素
        }
        Value *last1 = stripCopy(i1.back());
        Value *last2 = stripCopy(i2.back());
        if (last1 == last2)
            return false;
        if (auto *add = dynamic_cast<BinaryOperator *>(last2))
        {
            if (add->getOpcode() == Opcode::Add && stripCopy(add->getLHS()) == last1)
            {
                auto *c = dynamic_cast<ConstantInt *>(add->getRHS());
                if (c && c->Value != 0)
                    return true;
            }
        }
        if (auto *add = dynamic_cast<BinaryOperator *>(last1))
        {
            if (add->getOpcode() == Opcode::Add && stripCopy(add->getLHS()) == last2)
            {
                auto *c = dynamic_cast<ConstantInt *>(add->getRHS());
                if (c && c->Value != 0)
                    return true;
            }
        }
        return false;
    }

    // 仅当中间访存可证明不碰首 store 地址时才允许合并
    static bool hasInterveningConflictWithAddr(const vector<unique_ptr<Instruction>> &insts,
                                               size_t from, size_t to, Value *storeAddr)
    {
        for (size_t k = from + 1; k < to; ++k)
        {
            Instruction *midInst = insts[k].get();
            if (!midInst)
                continue;
            if (midInst->getOpcode() == Opcode::Call)
                return true;
            // 任何访存（标量/向量/strided load 或 store）若不能证明与首 store 地址不同，则禁止合并
            if (!midInst->isMemoryLoad() && !midInst->isMemoryStore())
                continue;
            Value *midPtr = midInst->getPointerOperand();
            if (!clearlyDistinctI32Addrs(midPtr, storeAddr))
                return true;
        }
        return false;
    }

    static bool storeAddrsSameBase(Value *addr1, GetElementPtrInst *gep1, Value *addr2)
    {
        addr1 = stripCopy(addr1);
        addr2 = stripCopy(addr2);
        if (auto *gep2 = dynamic_cast<GetElementPtrInst *>(addr2))
            return gep1->getPointerOperand() == gep2->getPointerOperand();
        auto *addd = dynamic_cast<BinaryOperator *>(addr2);
        if (addd && addd->getOpcode() == Opcode::Addd && stripCopy(addd->getLHS()) == addr1)
            return true;
        auto *root2 = rootGep(addr2);
        return root2 && root2->getPointerOperand() == gep1->getPointerOperand();
    }

    // i32 双字 store(sd) 要求首地址 8 字节对齐：末维下标为偶数，或 addd 偏移为 8 的倍数
    static bool lastIndexAlignedForI64Store(GetElementPtrInst *gep, BasicBlock *bb)
    {
        auto indices = gep->getIndices();
        if (indices.empty())
            return false;
        return isEvenIndexValue(indices.back(), bb);
    }

    static bool addrAlignedForI64Store(Value *addr, BasicBlock *bb)
    {
        addr = stripCopy(addr);
        if (auto *gep = dynamic_cast<GetElementPtrInst *>(addr))
            return lastIndexAlignedForI64Store(gep, bb);
        auto *addd = dynamic_cast<BinaryOperator *>(addr);
        if (!addd || addd->getOpcode() != Opcode::Addd)
            return false;
        auto *off = dynamic_cast<ConstantInt *>(addd->getRHS());
        if (!off || (off->Value & 7) != 0)
            return false;
        return addrAlignedForI64Store(addd->getLHS(), bb);
    }

    static BinaryOperator *asBinary(Value *v, Opcode op)
    {
        auto *bin = dynamic_cast<BinaryOperator *>(stripCopy(v));
        if (!bin || bin->getOpcode() != op)
            return nullptr;
        return bin;
    }

    // (a*b)%2==0  <=>  ((a&1)&(b&1))==0；从 srem(mul,2) 或 SRFixed 的 mod2n 链提取 a,b
    static BinaryOperator *extractMulFromProductMod2(Value *modVal, Value *&a, Value *&b)
    {
        a = nullptr;
        b = nullptr;
        modVal = stripCopy(modVal);

        if (auto *srem = asBinary(modVal, Opcode::SRem))
        {
            if (!asConstantInt(srem->getRHS()) || asConstantInt(srem->getRHS())->Value != 2)
                return nullptr;
            auto *mul = asBinary(srem->getLHS(), Opcode::Mul);
            if (!mul)
                return nullptr;
            a = mul->getLHS();
            b = mul->getRHS();
            return mul;
        }

        auto *sub = asBinary(modVal, Opcode::Sub);
        if (!sub)
            return nullptr;
        auto *andmask = asBinary(sub->getLHS(), Opcode::And);
        auto *bias = asBinary(sub->getRHS(), Opcode::And);
        if (!andmask || !bias)
            return nullptr;
        if (!asConstantInt(andmask->getRHS()) || asConstantInt(andmask->getRHS())->Value != 1)
            return nullptr;
        if (!asConstantInt(bias->getRHS()) || asConstantInt(bias->getRHS())->Value != 1)
            return nullptr;

        auto *addbias = asBinary(andmask->getLHS(), Opcode::Add);
        if (!addbias || addbias->getRHS() != bias)
            return nullptr;

        auto *mul = asBinary(addbias->getLHS(), Opcode::Mul);
        if (!mul)
            return nullptr;

        auto *signmask = asBinary(bias->getLHS(), Opcode::Sra);
        if (!signmask || signmask->getLHS() != mul)
            return nullptr;
        if (!asConstantInt(signmask->getRHS()) || asConstantInt(signmask->getRHS())->Value != 31)
            return nullptr;

        a = mul->getLHS();
        b = mul->getRHS();
        return mul;
    }

    static void eraseInstFromBlock(vector<unique_ptr<Instruction>> &insts, Instruction *inst,
                                   vector<Value *> &needToDelete)
    {
        if (!inst)
            return;
        for (size_t k = 0; k < insts.size(); ++k)
        {
            if (insts[k].get() != inst)
                continue;
            inst->removeThisFromOperands();
            needToDelete.push_back(insts[k].release());
            insts.erase(insts.begin() + static_cast<long>(k));
            return;
        }
    }

    static void eraseIfUnused(vector<unique_ptr<Instruction>> &insts, Instruction *inst,
                              vector<Value *> &needToDelete)
    {
        if (!inst || !inst->getUsers().empty())
            return;
        eraseInstFromBlock(insts, inst, needToDelete);
    }

    static bool tryFoldMulMod2ParityEq(BasicBlock *bb,
                                       vector<unique_ptr<Instruction>> &insts,
                                       size_t idx,
                                       bool verbose,
                                       stringstream &debugInfo,
                                       vector<Value *> &needToDelete)
    {
        auto *icmp = dynamic_cast<ICmpInst *>(insts[idx].get());
        if (!icmp || icmp->getPredicate() != ICmpInst::ICMP_EQ)
            return false;

        Value *lhs = stripCopy(icmp->getLHS());
        Value *rhs = stripCopy(icmp->getRHS());
        if (!asConstantInt(rhs) || asConstantInt(rhs)->Value != 0)
            return false;

        Value *a = nullptr;
        Value *b = nullptr;
        auto *mul = extractMulFromProductMod2(lhs, a, b);
        if (!mul || !a || !b)
            return false;

        auto *i32 = IntegerType::getInstance();
        auto *one = new ConstantInt(i32, 1);
        auto *zero = new ConstantInt(i32, 0);
        auto *aOdd = new BinaryOperator(Opcode::And, a, one, freshName("parity_a"));
        auto *bOdd = new BinaryOperator(Opcode::And, b, one, freshName("parity_b"));
        auto *bothOdd = new BinaryOperator(Opcode::And, aOdd, bOdd, freshName("parity_and"));
        auto *newIcmp = new ICmpInst(ICmpInst::ICMP_EQ, bothOdd, zero, icmp->getName());

        icmp->replaceAllUsesWith(newIcmp);
        insts[idx] = unique_ptr<Instruction>(aOdd);
        insts.insert(insts.begin() + static_cast<long>(idx) + 1, unique_ptr<Instruction>(bOdd));
        insts.insert(insts.begin() + static_cast<long>(idx) + 2, unique_ptr<Instruction>(bothOdd));
        insts.insert(insts.begin() + static_cast<long>(idx) + 3, unique_ptr<Instruction>(newIcmp));
        eraseInstFromBlock(insts, icmp, needToDelete);

        if (auto *sub = asBinary(lhs, Opcode::Sub))
        {
            eraseIfUnused(insts, sub, needToDelete);
            if (auto *andmask = asBinary(sub->getLHS(), Opcode::And))
            {
                eraseIfUnused(insts, andmask, needToDelete);
                if (auto *addbias = asBinary(andmask->getLHS(), Opcode::Add))
                {
                    eraseIfUnused(insts, addbias, needToDelete);
                    if (auto *bias = asBinary(addbias->getRHS(), Opcode::And))
                    {
                        eraseIfUnused(insts, bias, needToDelete);
                        if (auto *signmask = asBinary(bias->getLHS(), Opcode::Sra))
                            eraseIfUnused(insts, signmask, needToDelete);
                    }
                }
            }
            eraseIfUnused(insts, mul, needToDelete);
        }
        else if (auto *srem = asBinary(lhs, Opcode::SRem))
        {
            eraseIfUnused(insts, srem, needToDelete);
            eraseIfUnused(insts, mul, needToDelete);
        }

        if (verbose)
        {
            debugInfo << "InstructionCombine: (a*b)%2==0 -> ((a&1)&(b&1))==0 in "
                      << bb->getName() << "\n";
        }
        return true;
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
            if (tryFoldMulMod2ParityEq(bb.get(), insts, i, verbose, debugInfo, needToDelete))
            {
                changed = true;
                continue;
            }
        }
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
            auto *gep1 = rootGep(addr1);
            if (!gep1)
                continue;
            if (!gepPassesArrayChecks(gep1, bb.get()))
                continue;
            auto indices1 = dynamic_cast<GetElementPtrInst *>(stripCopy(addr1))
                                ? gep1->getIndices()
                                : vector<Value *>{};

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
                if (!addrAlignedForI64Store(addr1, bb.get()))
                    continue;
                if (hasInterveningConflictWithAddr(insts, i, j, addr1))
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
                    auto *pack = new PackI64Inst(val2, val1, freshName("pack"));
                    auto *stored = new StoreInst(Opcode::Stored, pack, addr1);
                    inst1->removeThisFromOperands();
                    inst2->removeThisFromOperands();
                    if (verbose)
                    {
                        debugInfo << "Combined instructions: " << inst1->toString() << " and "
                                  << inst2->toString() << " with " << pack->toString() << " and "
                                  << stored->toString() << "\n";
                    }
                    needToDelete.push_back(insts[j].release());
                    insts[j] = std::unique_ptr<Instruction>(pack);
                    insts.insert(insts.begin() + static_cast<long>(j) + 1,
                                 std::unique_ptr<Instruction>(stored));
                    needToDelete.push_back(insts[i].release());
                    insts.erase(insts.begin() + i);
                    changed = true;
                    break;
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
