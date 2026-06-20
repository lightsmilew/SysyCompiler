#include "ArrayPass.h"
#include "ControlFlowAnalysis.h"
#include <algorithm>
#include <limits>
#include <regex>
#include <unordered_set>
using namespace std;
using namespace optimization;

namespace
{
    Value *stripCopy(Value *v)
    {
        while (auto *cpy = dynamic_cast<CopyInst *>(v))
            v = cpy->getSource();
        return v;
    }

    // arr[idx] = (idx % modN) + addConst
    struct ModuloOffsetPattern
    {
        int modN = 0;
        int addConst = 0;
    };

    bool matchIndexModuloOffset(Value *val, Value *idx, ModuloOffsetPattern &pat)
    {
        val = stripCopy(val);
        idx = stripCopy(idx);
        auto *bin = dynamic_cast<BinaryOperator *>(val);
        if (!bin)
            return false;

        BinaryOperator *srem = nullptr;
        ConstantInt *offsetConst = nullptr;
        if (bin->getOpcode() == Opcode::Sub)
        {
            srem = dynamic_cast<BinaryOperator *>(bin->getLHS());
            offsetConst = dynamic_cast<ConstantInt *>(bin->getRHS());
            if (!srem || !offsetConst)
                return false;
            pat.addConst = -offsetConst->Value;
        }
        else if (bin->getOpcode() == Opcode::Add)
        {
            srem = dynamic_cast<BinaryOperator *>(bin->getLHS());
            offsetConst = dynamic_cast<ConstantInt *>(bin->getRHS());
            if (srem && offsetConst)
                pat.addConst = offsetConst->Value;
            else
            {
                srem = dynamic_cast<BinaryOperator *>(bin->getRHS());
                offsetConst = dynamic_cast<ConstantInt *>(bin->getLHS());
                if (!srem || !offsetConst)
                    return false;
                pat.addConst = offsetConst->Value;
            }
        }
        else
            return false;

        if (!srem || srem->getOpcode() != Opcode::SRem)
            return false;
        if (stripCopy(srem->getLHS()) != idx)
            return false;
        auto *modConst = dynamic_cast<ConstantInt *>(srem->getRHS());
        if (!modConst || modConst->Value <= 0)
            return false;
        pat.modN = modConst->Value;
        return true;
    }

    int evalModuloOffset(int index, const ModuloOffsetPattern &pat)
    {
        int r = index % pat.modN;
        if (r < 0 && pat.modN > 0)
            r += pat.modN;
        return r + pat.addConst;
    }

    bool storeMatchesPattern(StoreInst *store, const ModuloOffsetPattern &pat)
    {
        auto *gep = dynamic_cast<GetElementPtrInst *>(store->getPointer());
        if (!gep || gep->getIndices().size() != 1)
            return false;
        Value *idx = gep->getIndices()[0];
        if (auto *idxConst = dynamic_cast<ConstantInt *>(stripCopy(idx)))
        {
            if (auto *valConst = dynamic_cast<ConstantInt *>(stripCopy(store->getValueToStore())))
                return valConst->Value == evalModuloOffset(idxConst->Value, pat);
            return false;
        }
        ModuloOffsetPattern got;
        return matchIndexModuloOffset(store->getValueToStore(), idx, got) && got.modN == pat.modN &&
               got.addConst == pat.addConst;
    }

    pair<BinaryOperator *, BinaryOperator *> buildModuloOffsetExpr(Value *idx, const ModuloOffsetPattern &pat,
                                                                   size_t tag)
    {
        auto *modConst = new ConstantInt(IntegerType::getInstance(), pat.modN);
        auto *addConst = new ConstantInt(IntegerType::getInstance(), pat.addConst);
        auto *rem =
            new BinaryOperator(Opcode::SRem, idx, modConst, "arr_mod_rem_" + to_string(tag));
        auto *val = new BinaryOperator(Opcode::Add, rem, addConst, "arr_mod_val_" + to_string(tag));
        return {rem, val};
    }

    BasicBlock *findContainingLoopExit(const Loop &loop,
                                       const vector<pair<StoreInst *, BasicBlock *>> &stores)
    {
        for (auto &[st, bb] : stores)
        {
            (void)st;
            if (!loop.containsBlock(bb))
                return nullptr;
        }
        return loop.exits.empty() ? nullptr : loop.exits[0];
    }

    Value *getArrayRoot(Value *ptr)
    {
        ptr = stripCopy(ptr);
        while (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr))
            ptr = gep->getPointerOperand();
        if (auto *bc = dynamic_cast<CastInst *>(ptr))
        {
            if (bc->getOpcode() == Opcode::BitCast)
                ptr = bc->getOperand();
        }
        return ptr;
    }

    Value *canonicalArrayKey(Value *arr, vector<Value *> &keys)
    {
        for (Value *k : keys)
        {
            if (isSameAddr(k, arr))
                return k;
        }
        keys.push_back(arr);
        return arr;
    }

    // 同一基本块内 store 必须在 load 之前；跨块时 init 的 store 块不能支配 load 块。
    bool allStoresBeforeAllLoads(const vector<pair<StoreInst *, BasicBlock *>> &stores,
                                 const vector<pair<LoadInst *, BasicBlock *>> &loads,
                                 BasicBlock *completion,
                                 const unordered_map<BasicBlock *, BasicBlock *> &idom)
    {
        for (auto &[load, loadBB] : loads)
        {
            for (auto &[store, storeBB] : stores)
            {
                if (storeBB == loadBB)
                {
                    if (storeBB->getInstructionOrder(store) >= loadBB->getInstructionOrder(load))
                        return false;
                }
                else if (ControlFlowAnalysis::dominates(idom, storeBB, loadBB))
                {
                    return false;
                }
            }
        }
        if (completion)
        {
            for (auto &[load, loadBB] : loads)
            {
                (void)load;
                if (!ControlFlowAnalysis::dominates(idom, completion, loadBB))
                    return false;
            }
        }
        return true;
    }

    BasicBlock *findInitCompletionBB(Function *func, const vector<pair<StoreInst *, BasicBlock *>> &stores)
    {
        auto loops = ControlFlowAnalysis::findLoops(func);
        for (const Loop &loop : loops)
        {
            if (BasicBlock *exitBB = findContainingLoopExit(loop, stores))
                return exitBB;
        }

        auto idom = ControlFlowAnalysis::analyze(func);
        BasicBlock *deepest = nullptr;
        for (auto &[st, bb] : stores)
        {
            (void)st;
            if (!deepest || ControlFlowAnalysis::dominates(idom, bb, deepest))
                deepest = bb;
        }
        return deepest;
    }

    bool eliminateModuloClosedFormArrays(Function *func, bool verbose, Pass *pass)
    {
        unordered_map<Value *, vector<pair<StoreInst *, BasicBlock *>>> storesByArr;
        unordered_map<Value *, vector<pair<LoadInst *, BasicBlock *>>> loadsByArr;
        vector<Value *> arrayKeys;

        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            for (auto &instPtr : bb->getInstructions())
            {
                if (auto *store = dynamic_cast<StoreInst *>(instPtr.get()))
                {
                    auto *gep = dynamic_cast<GetElementPtrInst *>(store->getPointer());
                    if (!gep || gep->getIndices().size() != 1)
                        continue;
                    Value *key = canonicalArrayKey(getArrayRoot(gep->getPointerOperand()), arrayKeys);
                    storesByArr[key].emplace_back(store, bb);
                }
                else if (auto *load = dynamic_cast<LoadInst *>(instPtr.get()))
                {
                    auto *gep = dynamic_cast<GetElementPtrInst *>(load->getPointer());
                    if (!gep || gep->getIndices().size() != 1)
                        continue;
                    Value *key = canonicalArrayKey(getArrayRoot(gep->getPointerOperand()), arrayKeys);
                    loadsByArr[key].emplace_back(load, bb);
                }
            }
        }

        bool changed = false;
        auto idom = ControlFlowAnalysis::analyze(func);
        size_t tag = 0;

        for (auto &[arr, loads] : loadsByArr)
        {
            auto storeIt = storesByArr.find(arr);
            if (storeIt == storesByArr.end() || storeIt->second.empty())
                continue;

            auto &stores = storeIt->second;
            ModuloOffsetPattern pat;
            if (!matchIndexModuloOffset(stores[0].first->getValueToStore(),
                                        dynamic_cast<GetElementPtrInst *>(stores[0].first->getPointer())
                                            ->getIndices()[0],
                                        pat))
                continue;

            bool allMatch = true;
            for (auto &[st, bb] : stores)
            {
                (void)bb;
                if (!storeMatchesPattern(st, pat))
                {
                    allMatch = false;
                    break;
                }
            }
            if (!allMatch)
                continue;

            BasicBlock *completion = findInitCompletionBB(func, stores);
            if (!allStoresBeforeAllLoads(stores, loads, completion, idom))
                continue;

            for (auto &[load, loadBB] : loads)
            {
                (void)loadBB;
                auto *gep = dynamic_cast<GetElementPtrInst *>(load->getPointer());
                Value *idx = gep->getIndices()[0];
                auto [remInst, valInst] = buildModuloOffsetExpr(idx, pat, tag++);
                vector<Instruction *> toInsert = {remInst, valInst};
                load->replaceAllUsesWith(valInst);
                load->removeThisFromOperands();

                auto &insts = loadBB->getInstructions();
                for (size_t j = 0; j < insts.size(); ++j)
                {
                    if (insts[j].get() == load)
                    {
                        pass->needToDelete.push_back(insts[j].release());
                        insts.erase(insts.begin() + static_cast<long>(j));
                        for (size_t k = 0; k < toInsert.size(); ++k)
                            insts.insert(insts.begin() + static_cast<long>(j + k),
                                         unique_ptr<Instruction>(toInsert[k]));
                        break;
                    }
                }
                changed = true;
                if (verbose)
                {
                    pass->debugInfo << "Array Elimination (modulo): replaced load " << load->getName()
                                    << " with closed form in " << loadBB->getName() << "\n";
                }
            }

            for (auto &[store, storeBB] : stores)
            {
                (void)storeBB;
                store->removeThisFromOperands();
                auto &insts = storeBB->getInstructions();
                for (size_t j = 0; j < insts.size(); ++j)
                {
                    if (insts[j].get() == store)
                    {
                        pass->needToDelete.push_back(insts[j].release());
                        insts.erase(insts.begin() + static_cast<long>(j));
                        changed = true;
                        if (verbose)
                        {
                            pass->debugInfo << "Array Elimination (modulo): removed store " << store->getName()
                                            << " in " << storeBB->getName() << "\n";
                        }
                        break;
                    }
                }
            }
        }
        return changed;
    }
} // namespace

// 如果store和load循环范围不一致也不能简单删除
bool ArrayEliminationPass::runOnFunction(Function *func)
{
    bool changed = eliminateModuloClosedFormArrays(func, verbose, this);
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        for (size_t i = 0; i < insts.size(); ++i)
        {
            auto *storeInst = dynamic_cast<StoreInst *>(insts[i].get());
            if (!storeInst)
                continue;
            auto *gep_store = dynamic_cast<GetElementPtrInst *>(storeInst->getPointer());
            if (!gep_store || gep_store->getIndices().size() != 1)
                continue;
            Value *arr = gep_store->getPointerOperand();
            Value *idx_store = gep_store->getIndices()[0];
            bool isSimple = false;
            bool needTypeCast = false;
            Value *A = nullptr;
            Opcode binOpcode;
            auto *bin = dynamic_cast<BinaryOperator *>(storeInst->getValueToStore());
            if (bin)
            {
                if (bin->getOpcode() == Opcode::Add || bin->getOpcode() == Opcode::FAdd)
                {
                    binOpcode = bin->getOpcode();
                    auto *lhs_sitofp = dynamic_cast<CastInst *>(bin->getLHS());
                    auto *rhs_sitofp = dynamic_cast<CastInst *>(bin->getRHS());
                    if (lhs_sitofp && lhs_sitofp->getOpcode() == Opcode::SIToFP && lhs_sitofp->getOperand() == idx_store)
                    {
                        // a[j] = A + (float)j
                        A = bin->getRHS();
                        needTypeCast = true;
                        isSimple = true;
                    }
                    else if (rhs_sitofp && rhs_sitofp->getOpcode() == Opcode::SIToFP && rhs_sitofp->getOperand() == idx_store)
                    {
                        // a[j] = (float)j + A
                        A = bin->getLHS();
                        needTypeCast = true;
                        isSimple = true;
                    }
                }
            }
            else if (storeInst->getValueToStore() == idx_store)
            {
                A = nullptr;
                isSimple = true;
            }
            if (!isSimple)
                continue;

            // 2. 检查所有load是否合法
            bool canReplace = true;
            std::vector<std::tuple<BasicBlock *, Instruction *, size_t>> loadsToReplace;
            BasicBlock *storeBB = bb;
            for (auto &bb2Ptr : func->getBasicBlocks())
            {
                BasicBlock *bb2 = bb2Ptr.get();
                auto &insts2 = bb2->getInstructions();
                for (size_t j = 0; j < insts2.size(); ++j)
                {
                    auto *load = dynamic_cast<LoadInst *>(insts2[j].get());
                    if (!load)
                        continue;
                    auto *gep2 = dynamic_cast<GetElementPtrInst *>(load->getPointer());
                    if (!gep2 || gep2->getPointerOperand() != arr)
                        continue;
                    if (gep2->getIndices().size() != 1)
                    {
                        canReplace = false;
                        break;
                    }
                    if (bb2 == storeBB)
                    {
                        // 只允许store之后的load
                        if (j <= i)
                            continue;
                        // 判断1:检查store和load之间有无其它store
                        for (size_t k = i + 1; k < j; ++k)
                        {
                            auto *otherStore = dynamic_cast<StoreInst *>(insts2[k].get());
                            if (otherStore)
                            {
                                auto *otherGep = dynamic_cast<GetElementPtrInst *>(otherStore->getPointer());
                                if (otherGep && isSameAddr(otherGep->getPointerOperand(), arr))
                                {
                                    canReplace = false;
                                    break;
                                }
                            }
                        }
                        if (!canReplace)
                        {
                            loadsToReplace.clear();
                            continue;
                        }
                    }
                    if (!canReplace)
                        break;
                    // 这里合并了判断2和判断3
                    // 判断2:检查路径上有无其它store
                    // 判断3:load到出口是否还有store，如果有则不能替换
                    // 判断:store到出口还要store，则不能替换
                    for (auto &exitBB : func->getExitBlocks())
                    {
                        if (ControlFlowAnalysis::hasStoreOnPath(storeBB, exitBB, arr))
                        {
                            // 如果有store到出口块，不能进行替换
                            if (verbose)
                            {
                                debugInfo << "Array Elimination: Cannot replace array access in " << bb->getName()
                                          << " due to store on path to exit block.\n";
                            }
                            canReplace = false;
                            break;
                        }
                    }
                    if (!canReplace)
                    {
                        // 判断到有非法load，则要把loadsToReplace清空
                        loadsToReplace.clear();
                        break;
                    }

                    loadsToReplace.emplace_back(bb2, load, j);
                }
                if (!canReplace)
                    break;
            }
            if (!canReplace)
                continue;

            // 3. 如果没有找到有效的load，此时说明不能进行替换store
            if (loadsToReplace.empty())
            {
                if (verbose)
                {
                    debugInfo << "Array Elimination: No valid loads found for array " << arr->getName() << " in " << bb->getName() << "\n";
                }
                continue;
            }
            // 3. 替换所有load为表达式
            for (auto &[bb2, load, pos] : loadsToReplace)
            {
                auto loadinst = dynamic_cast<LoadInst *>(load);
                if (!loadinst)
                    continue;
                auto *gep_load = dynamic_cast<GetElementPtrInst *>(loadinst->getPointer());
                Value *idx_load = gep_load->getIndices()[0];
                Value *newIdx_load = idx_load;
                Value *newExpr_load = nullptr;
                vector<Instruction *> needToAdd;
                // a[i]=i结构
                if (A == nullptr)
                {
                    if (needTypeCast)
                    {
                        newIdx_load = new CastInst(Opcode::SIToFP, idx_load, FloatType::getInstance(), "scalar_repl_cast_" + to_string(ArrayEliminationCount));
                        needToAdd.push_back(dynamic_cast<Instruction *>(newIdx_load));
                    }
                    newExpr_load = newIdx_load;
                }
                // a[i]=A+i结构
                else
                {
                    if (needTypeCast)
                    {
                        // 如果需要类型转换，使用CastInst
                        newIdx_load = new CastInst(Opcode::SIToFP, idx_load, FloatType::getInstance(), "scalar_repl_cast" + to_string(ArrayEliminationCount));
                        needToAdd.push_back(dynamic_cast<Instruction *>(newIdx_load));
                    }
                    newExpr_load = new BinaryOperator(binOpcode, A, newIdx_load, "scalar_repl" + to_string(ArrayEliminationCount));
                }
                auto newExprInst = dynamic_cast<Instruction *>(newExpr_load);
                if (!newExprInst)
                    continue;
                // 只有当A+j结构时才需要额外插入一条指令，否则直接使用循环变量即可
                if (A != nullptr)
                {
                    needToAdd.push_back(newExprInst);
                }
                load->replaceAllUsesWith(newExpr_load);
                auto &insts2 = bb2->getInstructions();
                load->removeThisFromOperands();
                needToDelete.push_back(insts2[pos].release());
                // 在原位置删除load指令
                insts2.erase(insts2.begin() + pos);
                // 在原位置插入新的表达式
                for (size_t k = 0; k < needToAdd.size(); ++k)
                {
                    insts2.insert(insts2.begin() + pos + k, std::unique_ptr<Instruction>(needToAdd[k]));
                    if (verbose)
                    {
                        debugInfo << "Array Elimination: Inserted scalar expression " << needToAdd[k]->getName()
                                  << " in " << bb2->getName() << "\n";
                    }
                }
                changed = true;
                if (verbose)
                {
                    debugInfo << "Array Elimination: Replaced array load " << loadinst->getName()
                              << " with scalar expression in " << bb2->getName() << "\n";
                }
            }
            // 4. 删除原store
            storeInst->removeThisFromOperands();
            needToDelete.push_back(insts[i].release());
            insts.erase(insts.begin() + i);
            --i;
            ArrayEliminationCount++;
            changed = true;
            if (verbose)
            {
                debugInfo << "Array Elimination: Replaced array store " << storeInst->getName()
                          << " with scalar expression in " << bb->getName() << "\n";
            }
        }
    }
    return changed;
}

namespace
{
    bool hasLoadOrCallOnArrayRoot(Module *module, Value *root)
    {
        if (!module || !root)
            return false;
        for (auto &funcPtr : module->Functions)
        {
            Function *func = funcPtr.get();
            if (func->isLibraryFunction())
                continue;
            for (auto &bbPtr : func->getBasicBlocks())
            {
                for (auto &instPtr : bbPtr->getInstructions())
                {
                    Instruction *inst = instPtr.get();
                    if (auto *load = dynamic_cast<LoadInst *>(inst))
                    {
                        if (isSameAddr(load->getOriginalPointer(), root))
                            return true;
                    }
                    if (auto *call = dynamic_cast<CallInst *>(inst))
                    {
                        if (call->HasUsedArray(root))
                            return true;
                    }
                }
            }
        }
        return false;
    }

    bool hasLoadOrCallOnArrayRootInFunction(Function *func, Value *root)
    {
        if (!func || !root)
            return false;
        for (auto &bbPtr : func->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                Instruction *inst = instPtr.get();
                if (auto *load = dynamic_cast<LoadInst *>(inst))
                {
                    if (isSameAddr(load->getOriginalPointer(), root))
                        return true;
                }
                if (auto *call = dynamic_cast<CallInst *>(inst))
                {
                    if (call->HasUsedArray(root))
                        return true;
                }
            }
        }
        return false;
    }

    void collectWriteOnlyRelatedInsts(Value *root, std::unordered_set<Instruction *> &related)
    {
        if (auto *rootInst = dynamic_cast<Instruction *>(root))
            related.insert(rootInst);

        std::vector<Value *> worklist;
        std::unordered_set<Value *> visited;
        worklist.push_back(root);
        visited.insert(root);

        while (!worklist.empty())
        {
            Value *node = worklist.back();
            worklist.pop_back();
            for (User *user : node->getUsers())
            {
                if (auto *store = dynamic_cast<StoreInst *>(user))
                {
                    related.insert(store);
                }
                else if (auto *gep = dynamic_cast<GetElementPtrInst *>(user))
                {
                    related.insert(gep);
                    if (!visited.count(gep))
                    {
                        visited.insert(gep);
                        worklist.push_back(gep);
                    }
                }
                else if (auto *bitcast = dynamic_cast<CastInst *>(user))
                {
                    if (bitcast->getOpcode() != Opcode::BitCast)
                        continue;
                    related.insert(bitcast);
                    if (!visited.count(bitcast))
                    {
                        visited.insert(bitcast);
                        worklist.push_back(bitcast);
                    }
                }
                else if (auto *add = dynamic_cast<BinaryOperator *>(user))
                {
                    if (add->getOpcode() != Opcode::Addd)
                        continue;
                    related.insert(add);
                    if (!visited.count(add))
                    {
                        visited.insert(add);
                        worklist.push_back(add);
                    }
                }
            }
        }
    }
}

bool RemoveOnlyWriteArrayPass::removeWriteOnlyRootInFunction(Value *root, Function *func)
{
    if (!root || !func || hasLoadOrCallOnArrayRootInFunction(func, root))
        return false;

    std::unordered_set<Instruction *> relatedInsts;
    collectWriteOnlyRelatedInsts(root, relatedInsts);
    if (relatedInsts.empty())
        return false;

    bool changed = false;
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();
            if (!relatedInsts.count(inst))
            {
                ++it;
                continue;
            }
            if (verbose)
            {
                debugInfo << "RemoveOnlyWriteArrayPass: Removing write-only instruction " << inst->getName()
                          << " in function " << func->getName() << "\n";
            }
            inst->removeThisFromOperands();
            needToDelete.push_back(it->release());
            it = insts.erase(it);
            changed = true;
        }
    }
    return changed;
}

bool RemoveOnlyWriteArrayPass::removeWriteOnlyGlobals(Module *module)
{
    bool changed = false;
    for (auto &gvPtr : module->GlobalVariables)
    {
        GlobalVariable *gv = gvPtr.get();
        if (gv->isEliminated || !gv->isArray() || gv->IsConstant)
            continue;

        if (hasLoadOrCallOnArrayRoot(module, gv))
            continue;

        std::unordered_set<Instruction *> relatedInsts;
        collectWriteOnlyRelatedInsts(gv, relatedInsts);

        for (auto &funcPtr : module->Functions)
        {
            Function *func = funcPtr.get();
            if (func->isLibraryFunction())
                continue;
            for (auto &bbPtr : func->getBasicBlocks())
            {
                auto &insts = bbPtr->getInstructions();
                for (auto it = insts.begin(); it != insts.end();)
                {
                    Instruction *inst = it->get();
                    if (!relatedInsts.count(inst))
                    {
                        ++it;
                        continue;
                    }
                    if (verbose)
                    {
                        debugInfo << "RemoveOnlyWriteArrayPass: Removing write-only global @" << gv->getName()
                                  << " instruction " << inst->getName() << " in function " << func->getName() << "\n";
                    }
                    inst->removeThisFromOperands();
                    needToDelete.push_back(it->release());
                    it = insts.erase(it);
                    changed = true;
                }
            }
        }

        gv->isEliminated = true;
        changed = true;
        if (verbose)
        {
            debugInfo << "RemoveOnlyWriteArrayPass: Eliminated write-only global array @" << gv->getName() << "\n";
        }
    }
    return changed;
}

bool RemoveOnlyWriteArrayPass::runOnFunction(Function *func)
{
    bool changed = false;
    std::vector<AllocaInst *> arrayAllocas;

    for (auto &bb : func->getBasicBlocks())
    {
        for (auto &inst : bb->getInstructions())
        {
            if (auto *alloca = dynamic_cast<AllocaInst *>(inst.get()))
            {
                arrayAllocas.push_back(alloca);
                if (verbose)
                {
                    debugInfo << "RemoveOnlyWriteArrayPass: Found array alloca " << alloca->getName()
                              << " in function " << func->getName() << "\n";
                }
            }
        }
    }

    for (auto *alloca : arrayAllocas)
    {
        changed |= removeWriteOnlyRootInFunction(alloca, func);
    }

    Module *module = func->getParent();
    if (module && !writeOnlyGlobalsProcessed)
    {
        writeOnlyGlobalsProcessed = true;
        changed |= removeWriteOnlyGlobals(module);
    }

    return changed;
}

namespace
{
    bool sameArrayValue(Value *a, Value *b)
    {
        if (!a || !b)
        {
            return false;
        }
        if (stripCopy(a) == stripCopy(b))
        {
            return true;
        }
        if (!a->getName().empty() && a->getName() == b->getName())
        {
            return true;
        }
        return false;
    }

    bool valueDependsOnImpl(Value *val, Value *target, unordered_set<Value *> &visited)
    {
        if (!val || !target)
        {
            return false;
        }
        if (sameArrayValue(val, target))
        {
            return true;
        }
        if (!visited.insert(val).second)
        {
            return false;
        }
        if (auto *inst = dynamic_cast<Instruction *>(val))
        {
            for (auto *op : inst->getOperands())
            {
                if (valueDependsOnImpl(op, target, visited))
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool valueDependsOn(Value *val, Value *target)
    {
        unordered_set<Value *> visited;
        return valueDependsOnImpl(val, target, visited);
    }

    bool findInstructionInFunction(Function *func, Instruction *inst, BasicBlock *&outBb,
                                 unsigned &outIndex)
    {
        if (!func || !inst)
        {
            return false;
        }
        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            auto &insts = bb->getInstructions();
            for (unsigned i = 0; i < insts.size(); ++i)
            {
                if (insts[i].get() == inst)
                {
                    outBb = bb;
                    outIndex = i;
                    return true;
                }
            }
        }
        return false;
    }

    Value *getLoopIvValue(const Loop &loop)
    {
        BasicBlock *header = loop.header;
        if (!header || header->getInstructions().size() < 2)
        {
            return nullptr;
        }
        auto *cmp = dynamic_cast<ICmpInst *>(
            header->getInstructions()[header->getInstructions().size() - 2].get());
        if (!cmp || cmp->getPredicate() != ICmpInst::ICMP_SLT)
        {
            return nullptr;
        }
        return cmp->getLHS();
    }

    bool isLoopInvariantValue(Value *val, const Loop &loop, Value *loopIv)
    {
        (void)loop;
        if (!val)
        {
            return false;
        }
        return !loopIv || !valueDependsOn(val, loopIv);
    }

    bool extractAddOffset(Value *expr, Value *iv, Value *&offset)
    {
        offset = nullptr;
        expr = stripCopy(expr);
        iv = stripCopy(iv);
        auto *add = dynamic_cast<BinaryOperator *>(expr);
        if (!add || add->getOpcode() != Opcode::Add)
        {
            return false;
        }
        Value *lhs = stripCopy(add->getLHS());
        Value *rhs = stripCopy(add->getRHS());
        if (sameArrayValue(lhs, iv))
        {
            offset = add->getRHS();
            return true;
        }
        if (sameArrayValue(rhs, iv))
        {
            offset = add->getLHS();
            return true;
        }
        return false;
    }

    size_t findIvIndexPos(const vector<Value *> &indices, Value *iv)
    {
        for (size_t i = 0; i < indices.size(); ++i)
        {
            if (sameArrayValue(stripCopy(indices[i]), stripCopy(iv)))
            {
                return i;
            }
        }
        return static_cast<size_t>(-1);
    }

    bool isZeroIndex(Value *idx)
    {
        auto *c = dynamic_cast<ConstantInt *>(stripCopy(idx));
        return c && c->Value == 0;
    }

    bool sameValueForAccess(Value *lhs, Value *rhs)
    {
        if (lhs == rhs)
        {
            return true;
        }

        auto *lhsConstInt = dynamic_cast<ConstantInt *>(lhs);
        auto *rhsConstInt = dynamic_cast<ConstantInt *>(rhs);
        if (lhsConstInt && rhsConstInt)
        {
            return lhsConstInt->Value == rhsConstInt->Value;
        }

        if (!lhs || !rhs)
        {
            return false;
        }

        return isSameAddr(lhs, rhs);
    }

    bool collectAccessPattern(Value *value, Value *&baseValue, vector<Value *> &indices)
    {
        if (!value)
        {
            return false;
        }

        if (auto *castInst = dynamic_cast<CastInst *>(value))
        {
            return collectAccessPattern(castInst->getOperand(), baseValue, indices);
        }

        if (auto *gepInst = dynamic_cast<GetElementPtrInst *>(value))
        {
            if (!collectAccessPattern(gepInst->getPointerOperand(), baseValue, indices))
            {
                return false;
            }

            auto gepIndices = gepInst->getIndices();
            indices.insert(indices.end(), gepIndices.begin(), gepIndices.end());
            return true;
        }

        if (!baseValue)
        {
            baseValue = value;
            return true;
        }

        return sameValueForAccess(baseValue, value);
    }

    bool sameAccessPattern(const vector<Value *> &lhs, const vector<Value *> &rhs)
    {
        if (lhs.size() != rhs.size())
        {
            return false;
        }

        for (size_t i = 0; i < lhs.size(); ++i)
        {
            if (!sameValueForAccess(lhs[i], rhs[i]))
            {
                return false;
            }
        }

        return true;
    }

    int getLoopConstTripUpperBound(const Loop &loop)
    {
        BasicBlock *header = loop.header;
        if (!header || header->getInstructions().size() < 2)
        {
            return -1;
        }

        auto *cmp = dynamic_cast<ICmpInst *>(
            header->getInstructions()[header->getInstructions().size() - 2].get());
        if (!cmp || cmp->getPredicate() != ICmpInst::ICMP_SLT)
        {
            return -1;
        }

        auto *boundConst = dynamic_cast<ConstantInt *>(stripCopy(cmp->getRHS()));
        if (!boundConst)
        {
            return -1;
        }
        return boundConst->Value;
    }

    BasicBlock *findLoopLatchBlock(const Loop &loop)
    {
        for (auto *bb : loop.blocks)
        {
            if (bb == loop.header)
            {
                continue;
            }
            for (auto *succ : bb->getSuccessors())
            {
                if (succ == loop.header)
                {
                    return bb;
                }
            }
        }
        return nullptr;
    }

    set<BasicBlock *> collectPerIterationBlocks(const Loop &outer)
    {
        set<BasicBlock *> perIter;
        if (!outer.header)
        {
            return perIter;
        }

        BasicBlock *body = nullptr;
        auto &headerInsts = outer.header->getInstructions();
        auto *br = dynamic_cast<BranchInst *>(headerInsts.back().get());
        if (br && br->isConditional())
        {
            body = br->getTrueBlock();
            if (!outer.containsBlock(body))
            {
                body = br->getFalseBlock();
            }
        }
        if (!body || !outer.containsBlock(body))
        {
            return perIter;
        }

        vector<BasicBlock *> worklist = {body};
        perIter.insert(body);
        while (!worklist.empty())
        {
            BasicBlock *bb = worklist.back();
            worklist.pop_back();
            for (auto *succ : bb->getSuccessors())
            {
                if (succ == outer.header || !outer.containsBlock(succ) || perIter.count(succ))
                {
                    continue;
                }
                perIter.insert(succ);
                worklist.push_back(succ);
            }
        }
        return perIter;
    }

    vector<BasicBlock *> orderPerIterationBlocks(const Loop &outer)
    {
        vector<BasicBlock *> order;
        if (!outer.header)
        {
            return order;
        }

        BasicBlock *body = nullptr;
        auto &headerInsts = outer.header->getInstructions();
        auto *br = dynamic_cast<BranchInst *>(headerInsts.back().get());
        if (br && br->isConditional())
        {
            body = br->getTrueBlock();
            if (!outer.containsBlock(body))
            {
                body = br->getFalseBlock();
            }
        }
        if (!body || !outer.containsBlock(body))
        {
            return order;
        }

        set<BasicBlock *> visited;
        vector<BasicBlock *> worklist = {body};
        visited.insert(body);
        while (!worklist.empty())
        {
            BasicBlock *bb = worklist.front();
            worklist.erase(worklist.begin());
            order.push_back(bb);
            for (auto *succ : bb->getSuccessors())
            {
                if (succ == outer.header || !outer.containsBlock(succ) || visited.count(succ))
                {
                    continue;
                }
                visited.insert(succ);
                worklist.push_back(succ);
            }
        }
        return order;
    }

    string buildMemoryAccessKey(Value *ptr);
    string getArrayBaseKey(Value *ptr);
    bool valueDependsOnLoadAtKey(Value *val, const string &key);

    bool provePerIterFirstStoreFresh(const Loop &loop)
    {
        if (!loop.header || !loop.header->Parent)
        {
            return false;
        }

        auto ordered = orderPerIterationBlocks(loop);
        if (ordered.empty())
        {
            return false;
        }

        unordered_map<string, bool> seenCellKey;
        unordered_set<string> seenArrayBase;
        for (BasicBlock *bb : ordered)
        {
            if (!bb)
            {
                continue;
            }

            for (auto &instPtr : bb->getInstructions())
            {
                auto *store = dynamic_cast<StoreInst *>(instPtr.get());
                if (!store)
                {
                    continue;
                }

                const string cellKey = buildMemoryAccessKey(store->getPointer());
                const string baseKey = getArrayBaseKey(store->getPointer());
                if (cellKey.empty() || baseKey.empty())
                {
                    return false;
                }
                if (seenCellKey.count(cellKey))
                {
                    continue;
                }
                seenCellKey[cellKey] = true;
                if (seenArrayBase.count(baseKey))
                {
                    continue;
                }
                seenArrayBase.insert(baseKey);
                if (valueDependsOnLoadAtKey(store->getValueToStore(), cellKey))
                {
                    return false;
                }
            }
        }
        return true;
    }

    string getArrayBaseKey(Value *ptr)
    {
        if (!ptr)
        {
            return "";
        }

        Value *current = ptr;
        while (auto *gep = dynamic_cast<GetElementPtrInst *>(current))
        {
            current = gep->getPointerOperand();
        }
        return current ? current->toRef() : "";
    }

    string buildMemoryAccessKey(Value *ptr)
    {
        if (!ptr)
        {
            return "";
        }

        vector<string> indexParts;
        Value *current = ptr;
        while (auto *gep = dynamic_cast<GetElementPtrInst *>(current))
        {
            const auto indices = gep->getIndices();
            const int usefulCount =
                static_cast<int>(indices.size()) - std::max(0, gep->num_addedzero);
            if (usefulCount <= 0)
            {
                return "";
            }

            vector<string> level;
            level.reserve(static_cast<size_t>(usefulCount));
            for (int i = 0; i < usefulCount; ++i)
            {
                if (!indices[static_cast<size_t>(i)])
                {
                    return "";
                }
                level.push_back(indices[static_cast<size_t>(i)]->toRef());
            }
            indexParts.insert(indexParts.begin(), level.begin(), level.end());
            current = gep->getPointerOperand();
        }

        string key = current->toRef();
        for (const auto &part : indexParts)
        {
            key += "#";
            key += part;
        }
        return key;
    }

    bool valueDependsOnLoadAtKeyImpl(Value *val,
                                     const string &key,
                                     unordered_set<Value *> &visited)
    {
        val = stripCopy(val);
        if (!val)
        {
            return false;
        }
        if (!visited.insert(val).second)
        {
            return false;
        }

        if (auto *load = dynamic_cast<LoadInst *>(val))
        {
            if (buildMemoryAccessKey(load->getPointer()) == key)
            {
                return true;
            }
        }

        if (auto *inst = dynamic_cast<Instruction *>(val))
        {
            for (auto *op : inst->getOperands())
            {
                if (valueDependsOnLoadAtKeyImpl(op, key, visited))
                {
                    return true;
                }
            }
        }
        return false;
    }

    bool valueDependsOnLoadAtKey(Value *val, const string &key)
    {
        unordered_set<Value *> visited;
        return valueDependsOnLoadAtKeyImpl(val, key, visited);
    }

    bool storeTargetsBase(Value *ptr, Value *base)
    {
        Value *origin = nullptr;
        vector<Value *> indices;
        if (!collectAccessPattern(ptr, origin, indices) || !origin)
        {
            return false;
        }
        return isSameAddr(origin, base);
    }

    const Loop *findCopyPropagationRegion(const Loop &copyLoop,
                                          Value *dstArray,
                                          const vector<Loop> &allLoops)
    {
        const Loop *best = nullptr;
        size_t bestSize = numeric_limits<size_t>::max();
        set<BasicBlock *> copyBlocks(copyLoop.blocks.begin(), copyLoop.blocks.end());

        for (const auto &candidate : allLoops)
        {
            if (!candidate.containsBlock(copyLoop.header))
            {
                continue;
            }

            bool hasDstStoreOutsideCopy = false;
            for (auto *bb : candidate.blocks)
            {
                for (auto &instPtr : bb->getInstructions())
                {
                    auto *store = dynamic_cast<StoreInst *>(instPtr.get());
                    if (!store || !storeTargetsBase(store->getPointer(), dstArray))
                    {
                        continue;
                    }
                    if (copyBlocks.count(bb))
                    {
                        continue;
                    }
                    hasDstStoreOutsideCopy = true;
                    break;
                }
                if (hasDstStoreOutsideCopy)
                {
                    break;
                }
            }

            if (!hasDstStoreOutsideCopy)
            {
                continue;
            }

            if (candidate.blocks.size() < bestSize)
            {
                bestSize = candidate.blocks.size();
                best = &candidate;
            }
        }
        return best;
    }
}

bool ArrayCopyPropagationPass::analyzeCopyLoop(const Loop &loop,
                                                 CopyLoopPattern &pattern) const
{
    pattern = {};

    int loadCount = 0;
    int storeCount = 0;
    LoadInst *copyLoad = nullptr;
    StoreInst *copyStore = nullptr;
    vector<Value *> loadIndices;
    vector<Value *> storeIndices;

    for (auto *bb : loop.blocks)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (dynamic_cast<CallInst *>(inst))
            {
                return false;
            }

            if (auto *load = dynamic_cast<LoadInst *>(inst))
            {
                if (++loadCount > 1)
                {
                    return false;
                }
                copyLoad = load;
                Value *origin = nullptr;
                vector<Value *> indices;
                if (!collectAccessPattern(load->getPointer(), origin, indices) || !origin)
                {
                    return false;
                }
                if (!pattern.srcArray)
                {
                    pattern.srcArray = origin;
                    loadIndices = std::move(indices);
                }
                else if (!isSameAddr(pattern.srcArray, origin) ||
                         !sameAccessPattern(loadIndices, indices))
                {
                    return false;
                }
                continue;
            }

            if (auto *store = dynamic_cast<StoreInst *>(inst))
            {
                if (++storeCount > 1)
                {
                    return false;
                }
                copyStore = store;
                Value *origin = nullptr;
                vector<Value *> indices;
                if (!collectAccessPattern(store->getPointer(), origin, indices) || !origin)
                {
                    return false;
                }
                if (!pattern.dstArray)
                {
                    pattern.dstArray = origin;
                    storeIndices = std::move(indices);
                }
                else if (!isSameAddr(pattern.dstArray, origin) ||
                         !sameAccessPattern(storeIndices, indices))
                {
                    return false;
                }
                continue;
            }

            if (dynamic_cast<BranchInst *>(inst) ||
                dynamic_cast<ICmpInst *>(inst) ||
                dynamic_cast<PhiInst *>(inst) ||
                dynamic_cast<BinaryOperator *>(inst) ||
                dynamic_cast<CastInst *>(inst) ||
                dynamic_cast<GetElementPtrInst *>(inst) ||
                dynamic_cast<CopyInst *>(inst))
            {
                continue;
            }

            return false;
        }
    }

    if (loadCount != 1 || storeCount != 1 || !pattern.srcArray || !pattern.dstArray ||
        !copyLoad || !copyStore || isSameAddr(pattern.srcArray, pattern.dstArray))
    {
        return false;
    }

    if (!sameArrayValue(stripCopy(copyStore->getValueToStore()), stripCopy(copyLoad)))
    {
        return false;
    }

    if (sameAccessPattern(loadIndices, storeIndices))
    {
        pattern.valid = true;
        return true;
    }

    Value *loopIv = getLoopIvValue(loop);
    if (!loopIv)
    {
        return false;
    }

    const size_t storeIvPos = findIvIndexPos(storeIndices, loopIv);
    if (storeIvPos == static_cast<size_t>(-1))
    {
        return false;
    }

    if (loadIndices.size() == 1 && storeIndices.size() >= 1)
    {
        Value *offset = nullptr;
        if (!extractAddOffset(loadIndices[0], loopIv, offset) ||
            !isLoopInvariantValue(offset, loop, loopIv))
        {
            return false;
        }
        for (size_t i = 0; i < storeIvPos; ++i)
        {
            if (!isZeroIndex(storeIndices[i]))
            {
                return false;
            }
        }
        for (size_t i = storeIvPos + 1; i < storeIndices.size(); ++i)
        {
            if (!sameValueForAccess(storeIndices[i], loadIndices[i]))
            {
                return false;
            }
        }
        pattern.indexOffset = offset;
        pattern.storeIvIndexPos = storeIvPos;
        pattern.srcFlatIndex = true;
        pattern.valid = true;
        return true;
    }

    if (loadIndices.size() == storeIndices.size())
    {
        for (size_t k = 0; k < loadIndices.size(); ++k)
        {
            Value *offset = nullptr;
            if (!extractAddOffset(loadIndices[k], loopIv, offset))
            {
                continue;
            }
            if (!sameArrayValue(stripCopy(storeIndices[k]), stripCopy(loopIv)) ||
                !isLoopInvariantValue(offset, loop, loopIv))
            {
                continue;
            }
            bool othersMatch = true;
            for (size_t j = 0; j < loadIndices.size(); ++j)
            {
                if (j == k)
                {
                    continue;
                }
                if (!sameValueForAccess(loadIndices[j], storeIndices[j]))
                {
                    othersMatch = false;
                    break;
                }
            }
            if (!othersMatch)
            {
                continue;
            }
            pattern.indexOffset = offset;
            pattern.storeIvIndexPos = k;
            pattern.srcFlatIndex = false;
            pattern.valid = true;
            return true;
        }
    }

    return false;
}

bool ArrayCopyPropagationPass::isCopyPropagationSafe(const Loop &copyLoop,
                                                       Value *dstArray,
                                                       const vector<Loop> &allLoops) const
{
    if (!dstArray)
    {
        return false;
    }

    const Loop *region = findCopyPropagationRegion(copyLoop, dstArray, allLoops);
    if (!region)
    {
        return true;
    }

    return provePerIterFirstStoreFresh(*region);
}

void ArrayCopyPropagationPass::applyCopyPropagation(Function *func,
                                                      const CopyLoopPattern &pattern) const
{
    if (!func || !pattern.valid || !pattern.srcArray || !pattern.dstArray)
    {
        return;
    }

    if (!pattern.indexOffset)
    {
        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            for (auto &instPtr : bb->getInstructions())
            {
                Instruction *inst = instPtr.get();
                for (size_t i = 0; i < inst->getOperands().size(); ++i)
                {
                    Value *op = inst->getOperandByIndex(i);
                    if (op == pattern.dstArray || sameArrayValue(op, pattern.dstArray))
                    {
                        inst->setOperandByIndex(i, pattern.srcArray);
                    }
                }
            }
        }
        return;
    }

    vector<GetElementPtrInst *> dstGeps;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        for (auto &instPtr : bb->getInstructions())
        {
            auto *gep = dynamic_cast<GetElementPtrInst *>(instPtr.get());
            if (!gep)
            {
                continue;
            }
            Value *base = nullptr;
            vector<Value *> indices;
            if (!collectAccessPattern(gep, base, indices) || !base)
            {
                continue;
            }
            if (!isSameAddr(base, pattern.dstArray))
            {
                continue;
            }
            dstGeps.push_back(gep);
        }
    }

    for (auto *gep : dstGeps)
    {
        Value *base = nullptr;
        vector<Value *> indices;
        if (!collectAccessPattern(gep, base, indices))
        {
            continue;
        }

        Value *ivVal = nullptr;
        if (indices.size() > pattern.storeIvIndexPos)
        {
            ivVal = indices[pattern.storeIvIndexPos];
        }
        else if (indices.size() == 1 && pattern.storeIvIndexPos >= 1)
        {
            ivVal = indices[0];
        }
        if (!ivVal)
        {
            continue;
        }

        vector<Value *> newIndices;
        auto *addOp = new BinaryOperator(Opcode::Add, pattern.indexOffset, ivVal, "copy_prop_idx");
        if (pattern.srcFlatIndex)
        {
            newIndices.push_back(addOp);
        }
        else
        {
            newIndices = indices;
            newIndices[pattern.storeIvIndexPos] = addOp;
        }

        BasicBlock *bb = nullptr;
        unsigned gepIndex = 0;
        if (!findInstructionInFunction(func, gep, bb, gepIndex))
        {
            delete addOp;
            continue;
        }

        auto *newGep =
            new GetElementPtrInst(pattern.srcArray, newIndices, gep->getName() + "_cp");
        bb->insert(std::unique_ptr<Instruction>(addOp), gepIndex);
        bb->insert(std::unique_ptr<Instruction>(newGep), gepIndex + 1);
        gep->replaceAllUsesWith(newGep);
    }
}

void ArrayCopyPropagationPass::redirectAndRemoveLoop(Function *func, const Loop &loop)
{
    if (!func || !loop.header)
    {
        return;
    }

    set<BasicBlock *> loopBlocks(loop.blocks.begin(), loop.blocks.end());

    BasicBlock *preheader = nullptr;
    int externalPreds = 0;
    for (auto *pred : loop.header->getPredecessors())
    {
        if (!loopBlocks.count(pred))
        {
            preheader = pred;
            ++externalPreds;
        }
    }

    BasicBlock *exitBlock = nullptr;
    int externalSuccs = 0;
    for (auto *succ : loop.header->getSuccessors())
    {
        if (!loopBlocks.count(succ))
        {
            exitBlock = succ;
            ++externalSuccs;
        }
    }

    if (externalPreds != 1 || externalSuccs != 1 || !preheader || !exitBlock)
    {
        return;
    }

    for (auto &instPtr : preheader->getInstructions())
    {
        if (auto *br = dynamic_cast<BranchInst *>(instPtr.get()))
        {
            if (br->getTrueBlock() == loop.header)
            {
                preheader->removeSuccessor(loop.header);
                loop.header->removePredecessor(preheader);
                preheader->addSuccessor(exitBlock);
                exitBlock->addPredecessor(preheader);
                br->setTrueBlock(exitBlock);
            }
            if (br->getFalseBlock() == loop.header)
            {
                preheader->removeSuccessor(loop.header);
                loop.header->removePredecessor(preheader);
                preheader->addSuccessor(exitBlock);
                exitBlock->addPredecessor(preheader);
                br->setFalseBlock(exitBlock);
            }
        }
    }

    for (auto *bb : loop.blocks)
    {
        for (auto *succ : bb->getSuccessors())
        {
            if (!loopBlocks.count(succ))
            {
                removePhiIncomingFromPredecessor(succ, bb);
            }
        }
    }

    for (auto *bb : loop.blocks)
    {
        bb->removeSelfBasicBlock();
    }

    auto &bbs = func->getBasicBlocks();
    for (auto it = bbs.begin(); it != bbs.end();)
    {
        if (loopBlocks.count(it->get()))
        {
            needToDelete.push_back(it->release());
            it = bbs.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

bool ArrayCopyPropagationPass::runOnFunction(Function *func)
{
    if (!func || func->isLibraryFunction())
    {
        return false;
    }

    func->setLoops(ControlFlowAnalysis::findLoops(func));
    const auto loops = func->getLoops();

    for (const auto &loop : loops)
    {
        CopyLoopPattern pattern;
        if (!analyzeCopyLoop(loop, pattern))
        {
            continue;
        }
        if (!isCopyPropagationSafe(loop, pattern.dstArray, loops))
        {
            continue;
        }

        applyCopyPropagation(func, pattern);
        redirectAndRemoveLoop(func, loop);

        if (verbose)
        {
            debugInfo << "ArrayCopyPropagation: removed pure copy loop at "
                      << loop.header->getName() << " (" << pattern.dstArray->getName() << " -> "
                      << pattern.srcArray->getName();
            if (pattern.indexOffset)
            {
                debugInfo << " + offset " << pattern.indexOffset->toRef();
            }
            debugInfo << ")\n";
        }
        func->setLoops(ControlFlowAnalysis::findLoops(func));
        return true;
    }

    return false;
}

namespace
{
    string stripInlineSuffix(const string &name)
    {
        static const regex inlRegex("(_inl\\d+)");
        return regex_replace(name, inlRegex, "");
    }
}

string ArrayStoreLoadForwardPass::buildArrayIndexKey(Value *ptr) const
{
    if (!ptr)
        return "";

    // 自顶向下沿 GEP 链收集各级有效下标（GEP 展开后每级仅一个下标，须拼接完整路径）
    vector<string> indexParts;
    Value *current = ptr;
    while (auto *gep = dynamic_cast<GetElementPtrInst *>(current))
    {
        const auto indices = gep->getIndices();
        const int usefulCount =
            static_cast<int>(indices.size()) - std::max(0, gep->num_addedzero);
        if (usefulCount <= 0)
            return "";

        vector<string> level;
        level.reserve(static_cast<size_t>(usefulCount));
        for (int i = 0; i < usefulCount; ++i)
        {
            if (!indices[static_cast<size_t>(i)])
                return "";
            level.push_back(indices[static_cast<size_t>(i)]->toRef());
        }
        indexParts.insert(indexParts.begin(), level.begin(), level.end());
        current = gep->getPointerOperand();
    }

    string key = current->toRef();
    for (const auto &part : indexParts)
    {
        key += "#";
        key += part;
    }
    return key;
}

string ArrayStoreLoadForwardPass::getForwardingKey(Value *ptr) const
{
    if (!ptr)
        return "";
    string key = buildArrayIndexKey(ptr);
    if (!key.empty())
        return key;
    return ptr->toRef();
}

bool ArrayStoreLoadForwardPass::runOnFunction(Function *func)
{
    bool changed = false;

    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        unordered_map<string, Value *> latestStoredValue;

        for (auto it = insts.begin(); it != insts.end();)
        {
            Instruction *inst = it->get();

            if (auto *loadInst = dynamic_cast<LoadInst *>(inst))
            {
                const string key = getForwardingKey(loadInst->getPointer());
                Value *forwardVal = nullptr;
                if (!key.empty())
                {
                    auto found = latestStoredValue.find(key);
                    if (found != latestStoredValue.end())
                        forwardVal = found->second;
                }

                if (forwardVal)
                {
                    loadInst->replaceAllUsesWith(forwardVal);
                    if (verbose)
                    {
                        debugInfo << "ArrayStoreLoadForward: replaced load " << loadInst->getName()
                                  << " in " << bb->getName() << " of func " << func->getName() << "\n";
                    }
                    loadInst->removeThisFromOperands();
                    needToDelete.push_back(it->release());
                    it = insts.erase(it);
                    changed = true;
                    continue;
                }
            }

            // 保守失效策略：调用可能读写任意内存，先清空再继续。
            if (dynamic_cast<CallInst *>(inst))
            {
                latestStoredValue.clear();
                ++it;
                continue;
            }

            if (auto *storeInst = dynamic_cast<StoreInst *>(inst))
            {
                const string key = getForwardingKey(storeInst->getPointer());
                if (!key.empty())
                    latestStoredValue[key] = storeInst->getValueToStore();
            }

            ++it;
        }
    }

    return changed;
}
