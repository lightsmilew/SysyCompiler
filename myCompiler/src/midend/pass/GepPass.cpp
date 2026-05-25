#include "GepPass.h"
#include <algorithm>
#include <unordered_map>
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

    static int getElemSizeBytes(Type *ty)
    {
        if (!ty)
            return 4;
        if (ty->isIntegerTy() || ty->isFloatTy())
            return 4;
        if (ty->isLongTy())
            return 8;
        return 4;
    }

    // 仅处理展开后的一维 GEP：只有一个有效索引
    Value *getActiveIndex(GetElementPtrInst *gep)
    {
        Value *active = nullptr;
        int nonZeroCount = 0;
        for (Value *idx : gep->getIndices())
        {
            if (auto *c = dynamic_cast<ConstantInt *>(idx))
            {
                if (c->Value != 0)
                {
                    ++nonZeroCount;
                    active = idx;
                }
            }
            else
            {
                ++nonZeroCount;
                active = idx;
            }
        }
        return nonZeroCount == 1 ? active : nullptr;
    }

    // 多维 GEP：恰有一维下标变化，其余维为常数（如 gep [1400 x i32], A, %row, 0）
    struct MultiDimGepInfo
    {
        GetElementPtrInst *gep = nullptr;
        int varyPos = -1;
        vector<int> constSig; // 常数下标；变化维为 -1
        Value *varyIndex = nullptr;
    };

    static int64_t strideBytesForVaryingIndex(GetElementPtrInst *gep, int varyPos)
    {
        Type *ty = gep->getPointerOperand()->getType();
        if (auto *ptrTy = dynamic_cast<PointerType *>(ty))
            ty = ptrTy->ElementType;
        const auto &indices = gep->getIndices();
        for (int i = 0; i < varyPos && ty; ++i)
        {
            if (auto *arr = dynamic_cast<ArrayType *>(ty))
                ty = arr->ElementType;
        }
        if (auto *arr = dynamic_cast<ArrayType *>(ty))
            return static_cast<int64_t>(arr->NumElements) *
                   getElemSizeBytes(arr->getGroundElementType());
        return getElemSizeBytes(ty);
    }

    static bool parseMultiDimGep(GetElementPtrInst *gep, MultiDimGepInfo &info)
    {
        if (!gep || gep->getOpcode() != Opcode::GetElementPtr)
            return false;
        if (dynamic_cast<GetElementPtrInst *>(stripCopy(gep->getPointerOperand())))
            return false;

        int varyCount = 0;
        info = {};
        info.gep = gep;
        for (Value *idx : gep->getIndices())
        {
            if (auto *c = dynamic_cast<ConstantInt *>(idx))
                info.constSig.push_back(c->Value);
            else
            {
                ++varyCount;
                info.varyPos = static_cast<int>(info.constSig.size());
                info.constSig.push_back(-1);
                info.varyIndex = idx;
            }
        }
        if (varyCount != 1 || info.varyPos < 0 || !info.varyIndex)
            return false;
        return strideBytesForVaryingIndex(gep, info.varyPos) > 0;
    }

    struct MultiDimGroupKey
    {
        Value *base = nullptr;
        int varyPos = -1;
        vector<int> constSig;

        bool operator==(const MultiDimGroupKey &o) const
        {
            return base == o.base && varyPos == o.varyPos && constSig == o.constSig;
        }
    };

    struct MultiDimGroupKeyHash
    {
        size_t operator()(const MultiDimGroupKey &k) const
        {
            size_t h = std::hash<Value *>()(k.base) ^ (std::hash<int>()(k.varyPos) << 1);
            for (int v : k.constSig)
                h = h * 31u + static_cast<unsigned>(v);
            return h;
        }
    };

    bool isFoldable1DGep(GetElementPtrInst *gep)
    {
        if (!gep || gep->getOpcode() != Opcode::GetElementPtr)
            return false;
        Value *ptr = gep->getPointerOperand();
        if (!ptr || !ptr->getType()->isPointerTy())
            return false;
        auto *ptrTy = dynamic_cast<PointerType *>(ptr->getType());
        if (!ptrTy)
            return false;
        // 仅处理元素为标量（非嵌套数组）的指针
        if (dynamic_cast<ArrayType *>(ptrTy->ElementType))
            return false;
        return getActiveIndex(gep) != nullptr;
    }

    // offset = baseOff + delta（常量）
    bool tryDeltaFromBaseOff(Value *offset, Value *baseOff, int &delta)
    {
        if (offset == baseOff)
        {
            delta = 0;
            return true;
        }
        if (auto *cOff = dynamic_cast<ConstantInt *>(offset))
        {
            if (auto *cBase = dynamic_cast<ConstantInt *>(baseOff))
            {
                delta = cOff->Value - cBase->Value;
                return true;
            }
            return false;
        }
        if (auto *add = dynamic_cast<BinaryOperator *>(stripCopy(offset)))
        {
            if (add->getOpcode() != Opcode::Add)
                return false;
            if (auto *c = dynamic_cast<ConstantInt *>(add->getRHS()))
            {
                if (stripCopy(add->getLHS()) == baseOff)
                {
                    delta = c->Value;
                    return true;
                }
            }
            if (auto *c = dynamic_cast<ConstantInt *>(add->getLHS()))
            {
                if (stripCopy(add->getRHS()) == baseOff)
                {
                    delta = c->Value;
                    return true;
                }
            }
        }
        return false;
    }

    // offset = prevOff + 常量，已知 prevOff 相对 baseOff 的 delta 为 prevDelta
    bool tryDeltaFromPrevOff(Value *offset, Value *prevOff, int prevDelta, int &delta)
    {
        offset = stripCopy(offset);
        prevOff = stripCopy(prevOff);
        if (offset == prevOff)
        {
            delta = prevDelta;
            return true;
        }
        if (auto *add = dynamic_cast<BinaryOperator *>(offset))
        {
            if (add->getOpcode() != Opcode::Add)
                return false;
            if (auto *c = dynamic_cast<ConstantInt *>(add->getRHS()))
            {
                if (stripCopy(add->getLHS()) == prevOff)
                {
                    delta = prevDelta + c->Value;
                    return true;
                }
            }
            if (auto *c = dynamic_cast<ConstantInt *>(add->getLHS()))
            {
                if (stripCopy(add->getRHS()) == prevOff)
                {
                    delta = prevDelta + c->Value;
                    return true;
                }
            }
        }
        return false;
    }

    struct GepChainEntry
    {
        GetElementPtrInst *gep;
        Value *offset;
        size_t order;
    };

    bool computeIndexDeltas(const vector<GepChainEntry> &entries, vector<int> &deltas)
    {
        if (entries.empty())
            return false;
        deltas.assign(entries.size(), 0);
        Value *baseOff = stripCopy(entries[0].offset);
        Value *prevOff = baseOff;
        int prevDelta = 0;

        if (!tryDeltaFromBaseOff(stripCopy(entries[0].offset), baseOff, deltas[0]))
            return false;

        for (size_t i = 1; i < entries.size(); ++i)
        {
            int d = 0;
            Value *cur = stripCopy(entries[i].offset);
            if (!tryDeltaFromBaseOff(cur, baseOff, d) &&
                !tryDeltaFromPrevOff(cur, prevOff, prevDelta, d))
                return false;
            deltas[i] = d;
            prevOff = cur;
            prevDelta = d;
        }
        return true;
    }

    // 同一 (基址, 常数下标签名) 下再按「变化维递推链」拆簇，避免 t3 / t12 等混组
    static vector<vector<GepChainEntry>> clusterByIndexChain(vector<GepChainEntry> entries)
    {
        std::sort(entries.begin(), entries.end(),
                  [](const GepChainEntry &a, const GepChainEntry &b) { return a.order < b.order; });
        vector<vector<GepChainEntry>> chains;
        for (const GepChainEntry &e : entries)
        {
            bool placed = false;
            for (auto &chain : chains)
            {
                vector<GepChainEntry> trial = chain;
                trial.push_back(e);
                vector<int> deltas;
                if (computeIndexDeltas(trial, deltas))
                {
                    chain.push_back(e);
                    placed = true;
                    break;
                }
            }
            if (!placed)
                chains.push_back({e});
        }
        return chains;
    }

    bool foldGepGroup(BasicBlock *bb, vector<GepChainEntry> &group, int64_t indexStrideBytes,
                      bool verbose, std::stringstream &debugInfo, vector<Value *> &needToDelete,
                      bool &changed)
    {
        if (group.size() < 2)
            return false;

        std::sort(group.begin(), group.end(),
                  [](const GepChainEntry &a, const GepChainEntry &b) { return a.order < b.order; });

        vector<int> deltas;
        if (!computeIndexDeltas(group, deltas))
            return false;

        bool hasNonZeroDelta = false;
        for (size_t i = 1; i < deltas.size(); ++i)
        {
            if (deltas[i] != 0)
                hasNonZeroDelta = true;
        }
        if (!hasNonZeroDelta)
            return false;

        GetElementPtrInst *anchor = group[0].gep;
        Value *basePtr = anchor->getPointerOperand();
        auto &insts = bb->getInstructions();

        for (size_t i = 1; i < group.size(); ++i)
        {
            if (deltas[i] == 0)
            {
                group[i].gep->replaceAllUsesWith(anchor);
                group[i].gep->removeThisFromOperands();
                needToDelete.push_back(group[i].gep);
                for (auto it = insts.begin(); it != insts.end(); ++it)
                {
                    if (it->get() == group[i].gep)
                    {
                        insts.erase(it);
                        break;
                    }
                }
                changed = true;
                continue;
            }

            auto *byteOff = new ConstantLong(LongType::getInstance(),
                                             static_cast<int64_t>(deltas[i]) * indexStrideBytes);
            auto *newAddr = new BinaryOperator(Opcode::Addd, anchor, byteOff,
                                               group[i].gep->getName() + "_foldadd");

            for (auto it = insts.begin(); it != insts.end(); ++it)
            {
                if (it->get() == group[i].gep)
                {
                    it = insts.insert(it, std::unique_ptr<Instruction>(newAddr));
                    ++it;
                    group[i].gep->replaceAllUsesWith(newAddr);
                    group[i].gep->removeThisFromOperands();
                    needToDelete.push_back(group[i].gep);
                    it = insts.erase(it);
                    changed = true;
                    break;
                }
            }
        }

        if (verbose && changed)
        {
            debugInfo << "GEPChainFold: folded " << group.size() << " GEPs (stride "
                      << indexStrideBytes << "B) on " << basePtr->toRef() << " in "
                      << bb->getName() << "\n";
        }
        return changed;
    }
} // namespace
bool GEPExpansionPass ::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            if (auto *gep = dynamic_cast<GetElementPtrInst *>(inst))
            {
                // 取消维度限制，可以增加循环不变量外提优化
                auto indices = gep->getIndices();
                vector<unique_ptr<Instruction>> newgepInsts;
                auto pointer = gep->getPointerOperand();
                std::string basename = gep->getName();
                int size = static_cast<int>(indices.size()) - std::max(0, gep->num_addedzero);
                for (int i = 0; i < size; i++)
                {
                    auto newgep = std::make_unique<GetElementPtrInst>(pointer, vector<Value *>{indices[i]}, basename + "_gep" + std::to_string(i));
                    newgepInsts.push_back(std::move(newgep));
                    // 更新指针操作数
                    pointer = newgepInsts.back().get();
                }
                // 插入新GEP指令到当前基本块
                it = insts.insert(it, std::make_move_iterator(newgepInsts.begin()), std::make_move_iterator(newgepInsts.end()));
                // 跳过新插入的GEP
                std::advance(it, size);
                Instruction *lastNewGEP = prev(it, 1)->get(); // 获取最后一个新插入的GEP指令
                // 替换原GEP的所有使用
                gep->replaceAllUsesWith(lastNewGEP);
                // 删除原来的GEP指令
                gep->removeThisFromOperands();
                needToDelete.push_back(it->release());
                it = insts.erase(it);
                changed = true;
                if (verbose)
                {
                    debugInfo << "GEP Expansion: Replaced GEP " << gep->getName() << " with "
                              << indices.size() << " new GEP instructions in " << bb->getName() << "\n";
                }
            }
            else
            {
                ++it; // 如果不是GEP，继续下一个指令
            }
        }
    }
    return changed;
}
bool GEPToBitCastPass::runOnFunction(Function *func)
{
    bool changed = false;
    // gep展开经过公共子表达式消除后可以强度削弱->查看是否有多余的GEP指令（比如indices全为0的情况）
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            if (auto *gep = dynamic_cast<GetElementPtrInst *>(inst))
            {
                // 检查是否所有索引都是0
                bool allZero = true;
                for (auto *index : gep->getIndices())
                {
                    if (auto *constInt = dynamic_cast<ConstantInt *>(index))
                    {
                        if (constInt->Value != 0)
                        {
                            allZero = false;
                            break;
                        }
                    }
                    else
                    {
                        allZero = false;
                    }
                }
                if (allZero)
                {
                    // 类型转换：插入BitCast指令
                    auto *ptrOperand = gep->getPointerOperand();
                    auto *targetType = gep->getType(); // GEP的结果类型
                    auto *castInst = new CastInst(Opcode::BitCast, ptrOperand, targetType, gep->getName() + "_bitcast");
                    // 在GEP指令前面插入BitCast指令
                    it = insts.insert(it, std::unique_ptr<Instruction>(castInst));
                    gep->removeThisFromOperands();
                    gep->replaceAllUsesWith(castInst);
                    ++it;
                    // 删除原来的GEP指令（此时it指向gep，castInst在gep前面）
                    needToDelete.push_back(it->release());
                    it = insts.erase(it);
                    changed = true;
                    if (verbose)
                    {
                        debugInfo << "GEP to BitCast: Replaced GEP " << gep->getName() << " with BitCast in "
                                  << bb->getName() << "\n";
                    }
                }
                else
                {
                    ++it;
                }
            }
            else
            {
                ++it; // 如果不是GEP，继续下一个指令
            }
        }
    }
    // 替换完再扫描一遍bitcast，如果类型相同直接删除,用操作数替换
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            if (auto *bitCast = dynamic_cast<CastInst *>(inst))
            {
                if (bitCast->getOpcode() == Opcode::BitCast)
                {
                    // 检查源类型和目标类型是否相同
                    Type *srcType = bitCast->getOperand()->getType();
                    Type *destType = bitCast->getType();
                    if (destType->isTypeEqual(destType, srcType))
                    {
                        // 删除无效的BitCast指令
                        bitCast->removeThisFromOperands();
                        bitCast->replaceAllUsesWith(bitCast->getOperand());
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "Removed redundant BitCast: " << bitCast->getName() << " in " << bb->getName() << "\n";
                        }
                    }
                    else
                    {
                        ++it; // 如果不是冗余的BitCast，继续下一个指令
                    }
                }
                else
                {
                    ++it; // 如果不是BitCast，继续下一个指令
                }
            }
            else
            {
                ++it; // 如果不是BitCast，继续下一个指令
            }
        }
    }
    return changed;
}

bool GEPChainFoldPass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();

        std::unordered_map<Value *, vector<GepChainEntry>> groups1d;
        std::unordered_map<MultiDimGroupKey, vector<GepChainEntry>, MultiDimGroupKeyHash> groupsMd;
        std::unordered_map<MultiDimGroupKey, int64_t, MultiDimGroupKeyHash> groupStride;

        size_t order = 0;
        for (auto &instPtr : insts)
        {
            auto *gep = dynamic_cast<GetElementPtrInst *>(instPtr.get());
            if (!gep)
            {
                ++order;
                continue;
            }

            MultiDimGepInfo mdInfo;
            if (parseMultiDimGep(gep, mdInfo))
            {
                MultiDimGroupKey key{gep->getPointerOperand(), mdInfo.varyPos, mdInfo.constSig};
                groupsMd[key].push_back({gep, mdInfo.varyIndex, order});
                groupStride[key] = strideBytesForVaryingIndex(gep, mdInfo.varyPos);
                ++order;
                continue;
            }

            if (!isFoldable1DGep(gep))
            {
                ++order;
                continue;
            }
            Value *base = gep->getPointerOperand();
            groups1d[base].push_back({gep, getActiveIndex(gep), order});
            ++order;
        }

        for (auto &kv : groups1d)
            foldGepGroup(bb, kv.second, 4, verbose, debugInfo, needToDelete, changed);

        for (auto &kv : groupsMd)
        {
            int64_t stride = groupStride[kv.first];
            for (vector<GepChainEntry> &chain : clusterByIndexChain(std::move(kv.second)))
                foldGepGroup(bb, chain, stride, verbose, debugInfo, needToDelete, changed);
        }
    }
    return changed;
}
