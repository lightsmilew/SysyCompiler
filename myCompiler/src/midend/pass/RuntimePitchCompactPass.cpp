#include "RuntimePitchCompactPass.h"
#include <unordered_map>
#include <unordered_set>

using namespace std;
using namespace optimization;

namespace
{
    Value *stripCopyCast(Value *v)
    {
        while (v)
        {
            if (auto *cpy = dynamic_cast<CopyInst *>(v))
            {
                v = cpy->getSource();
                continue;
            }
            if (auto *cast = dynamic_cast<CastInst *>(v))
            {
                v = cast->getOperand();
                continue;
            }
            break;
        }
        return v;
    }

    ConstantInt *ci(int v) { return new ConstantInt(IntegerType::getInstance(), v); }

    unique_ptr<Instruction> own(Instruction *inst) { return unique_ptr<Instruction>(inst); }

    /// 第一个 getint() 的返回值（sl1/2/3：N）。
    Value *findFirstGetint(Function *func)
    {
        for (auto &bbPtr : func->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *call = dynamic_cast<CallInst *>(instPtr.get());
                if (!call || !call->getCalledFunction())
                    continue;
                if (call->getCalledFunction()->getName() == "getint")
                    return call;
            }
        }
        return nullptr;
    }

    bool isCubicI32Global(GlobalVariable *gv, size_t &dimOut)
    {
        if (!gv || gv->isEliminated || !gv->isArray())
            return false;
        if (!gv->getGroundElementType() || !gv->getGroundElementType()->isIntegerTy())
            return false;
        auto dims = gv->getDims();
        if (dims.size() != 3)
            return false;
        if (dims[0] < 2 || dims[0] != dims[1] || dims[1] != dims[2])
            return false;
        dimOut = dims[0];
        return true;
    }

    bool gepTargetsGlobal(GetElementPtrInst *gep, GlobalVariable *gv)
    {
        if (!gep || !gv)
            return false;
        Value *root = stripCopyCast(getOriginalPointerFromAddress(gep->getPointerOperand()));
        return root == gv || isSameAddr(root, gv);
    }

    /// 逻辑下标个数（去掉 constructOperands 补的 0）。
    int logicalIndexCount(GetElementPtrInst *gep)
    {
        auto idxs = gep->getIndices();
        int n = static_cast<int>(idxs.size()) - gep->num_addedzero;
        return n > 0 ? n : static_cast<int>(idxs.size());
    }

    /// 在 defining instruction 之后插入；若 N 不是指令则插到 entry 开头。
    void insertAfterValue(Function *func, Value *after, vector<unique_ptr<Instruction>> &toInsert)
    {
        if (toInsert.empty())
            return;
        auto *afterInst = dynamic_cast<Instruction *>(after);
        for (auto &bbPtr : func->getBasicBlocks())
        {
            auto &insts = bbPtr->getInstructions();
            for (auto it = insts.begin(); it != insts.end(); ++it)
            {
                if (afterInst && it->get() == afterInst)
                {
                    ++it;
                    for (auto &up : toInsert)
                        it = insts.insert(it, std::move(up)) + 1;
                    toInsert.clear();
                    return;
                }
            }
        }
        // fallback：entry 开头
        BasicBlock *entry = func->getEntryBlock();
        if (!entry)
            return;
        auto &entryInsts = entry->getInstructions();
        auto it = entryInsts.begin();
        for (auto &up : toInsert)
            it = entryInsts.insert(it, std::move(up)) + 1;
        toInsert.clear();
    }

    struct FlatMaterial
    {
        Value *baseI32 = nullptr; // bitcast 后的 i32*
        Value *nn = nullptr;      // N*N
    };

    FlatMaterial ensureMaterial(Function *func, GlobalVariable *gv, Value *N,
                                unordered_map<GlobalVariable *, FlatMaterial> &cache,
                                Pass *pass)
    {
        auto it = cache.find(gv);
        if (it != cache.end())
            return it->second;

        auto *i32ty = IntegerType::getInstance();
        auto *i32pty = PointerType::getInstance(i32ty);

        auto *baseCast = new CastInst(Opcode::BitCast, gv, i32pty, gv->getName() + "_rpc_base");
        auto *nnMul = new BinaryOperator(Opcode::Mul, N, N, gv->getName() + "_rpc_nn");

        vector<unique_ptr<Instruction>> hoist;
        hoist.push_back(own(baseCast));
        hoist.push_back(own(nnMul));
        insertAfterValue(func, N, hoist);

        FlatMaterial mat{baseCast, nnMul};
        cache.emplace(gv, mat);
        if (pass->verbose)
        {
            pass->debugInfo << "RuntimePitchCompact: materialize base/NN for @" << gv->getName()
                            << " with extent " << N->toRef() << "\n";
        }
        return mat;
    }
}

bool RuntimePitchCompactPass::runOnFunction(Function *func)
{
    if (!func || func->isLibraryFunction() || func->isDeletedFunction())
        return false;
    Module *mod = func->getParent();
    if (!mod)
        return false;

    Value *N = findFirstGetint(func);
    if (!N)
        return false;

    vector<GlobalVariable *> candidates;
    for (auto &gvPtr : mod->GlobalVariables)
    {
        size_t dim = 0;
        if (isCubicI32Global(gvPtr.get(), dim))
            candidates.push_back(gvPtr.get());
    }
    if (candidates.empty())
        return false;

    debugInfo << "RuntimePitchCompact: func=" << func->getName()
              << " N=" << N->toRef() << " candidates=" << candidates.size() << "\n";

    // 收集每个全局上、仍为「直接多维 GEP」的访问
    unordered_map<GlobalVariable *, vector<pair<BasicBlock *, GetElementPtrInst *>>> gepMap;
    unordered_set<GlobalVariable *> rejected;

    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        for (auto &instPtr : bb->getInstructions())
        {
            auto *gep = dynamic_cast<GetElementPtrInst *>(instPtr.get());
            if (!gep)
                continue;
            for (GlobalVariable *gv : candidates)
            {
                if (!gepTargetsGlobal(gep, gv))
                    continue;
                Value *directPtr = stripCopyCast(gep->getPointerOperand());
                int nIdx = logicalIndexCount(gep);
                if (verbose)
                {
                    debugInfo << "  gep %" << gep->getName() << " in " << bb->getName()
                              << " @" << gv->getName() << " logicalIdx=" << nIdx
                              << " directIsGlobal="
                              << ((directPtr == gv || isSameAddr(directPtr, gv)) ? "yes" : "no")
                              << "\n";
                }
                // 只改写指针操作数就是该全局（或 cast）的多维 GEP；
                // 展开后的单维链留给 GEPExpansion，避免重复改写。
                if (directPtr != gv && !isSameAddr(directPtr, gv))
                {
                    // 指针是中间 GEP → 说明已展开，本 pass 跳过该全局
                    rejected.insert(gv);
                    continue;
                }
                // 3 维元素访问，或 2 维取行（putarray 的 x[i][j]，尾部补 k=0）
                if (nIdx != 2 && nIdx != 3)
                {
                    rejected.insert(gv);
                    continue;
                }
                gepMap[gv].push_back({bb, gep});
                break;
            }
        }
    }

    for (GlobalVariable *gv : candidates)
    {
        debugInfo << "  @" << gv->getName() << " geps=" << gepMap[gv].size()
                  << " rejected=" << (rejected.count(gv) ? "yes" : "no") << "\n";
    }

    bool changed = false;
    unordered_map<GlobalVariable *, FlatMaterial> materials;
    int nameCounter = 0;

    for (GlobalVariable *gv : candidates)
    {
        if (rejected.count(gv))
            continue;
        auto git = gepMap.find(gv);
        if (git == gepMap.end() || git->second.empty())
            continue;

        FlatMaterial mat = ensureMaterial(func, gv, N, materials, this);

        // 按基本块改写，避免迭代器失效跨块
        unordered_map<BasicBlock *, vector<GetElementPtrInst *>> byBB;
        for (auto &[bb, gep] : git->second)
            byBB[bb].push_back(gep);

        for (auto &[bb, geps] : byBB)
        {
            unordered_set<GetElementPtrInst *> target(geps.begin(), geps.end());
            auto &insts = bb->getInstructions();
            for (auto it = insts.begin(); it != insts.end();)
            {
                auto *gep = dynamic_cast<GetElementPtrInst *>(it->get());
                if (!gep || !target.count(gep))
                {
                    ++it;
                    continue;
                }

                auto idxs = gep->getIndices();
                // 2 维 putarray 基址：尾部已由 constructOperands 补 k=0；不足则显式用 0
                Value *iIdx = ci(0);
                Value *jIdx = ci(0);
                Value *kIdx = ci(0);
                if (!idxs.empty())
                    iIdx = idxs[0];
                if (idxs.size() >= 2)
                    jIdx = idxs[1];
                if (idxs.size() >= 3)
                    kIdx = idxs[2];

                string s = to_string(nameCounter++);
                auto *mulI = new BinaryOperator(Opcode::Mul, iIdx, mat.nn, "rpc_i" + s);
                auto *mulJ = new BinaryOperator(Opcode::Mul, jIdx, N, "rpc_j" + s);
                auto *sumIJ = new BinaryOperator(Opcode::Add, mulI, mulJ, "rpc_ij" + s);
                auto *flat = new BinaryOperator(Opcode::Add, sumIJ, kIdx, "rpc_idx" + s);
                auto *newGep = new GetElementPtrInst(mat.baseI32, {flat}, gep->getName() + "_rpc");

                it = insts.insert(it, own(mulI));
                ++it;
                it = insts.insert(it, own(mulJ));
                ++it;
                it = insts.insert(it, own(sumIJ));
                ++it;
                it = insts.insert(it, own(flat));
                ++it;
                it = insts.insert(it, own(newGep));
                ++it;
                // it 现在指向旧 gep
                gep->replaceAllUsesWith(newGep);
                gep->removeThisFromOperands();
                needToDelete.push_back(it->release());
                it = insts.erase(it);
                changed = true;

                if (verbose)
                {
                    debugInfo << "RuntimePitchCompact: flattened @" << gv->getName()
                              << " gep -> " << newGep->getName() << " in " << bb->getName() << "\n";
                }
            }
        }
    }

    return changed;
}
