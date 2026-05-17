#include "ArrayPass.h"
#include <regex>
using namespace std;
using namespace optimization;
// 如果store和load循环范围不一致也不能简单删除
bool ArrayEliminationPass::runOnFunction(Function *func)
{
    bool changed = false;
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
bool RemoveOnlyWriteArrayPass::runOnFunction(Function *func)
{
    bool changed = false;
    std::vector<AllocaInst *> arrayAllocas;

    // 1. 收集所有数组 alloca
    for (auto &bb : func->getBasicBlocks())
    {
        for (auto &inst : bb->getInstructions())
        {
            if (auto *alloca = dynamic_cast<AllocaInst *>(inst.get()))
            {
                arrayAllocas.push_back(alloca);
                if (verbose)
                {
                    debugInfo << "RemoveWriteOnlyArrayPass: Found array alloca " << alloca->getName() << " in function " << func->getName() << "\n";
                }
            }
        }
    }

    for (auto *alloca : arrayAllocas)
    {
        bool hasLoadOrCall = false;
        std::unordered_set<Instruction *> relatedInsts;
        std::vector<User *> worklist;
        worklist.push_back(alloca);

        // 1. 全局查找所有 load 和 call 指令
        for (auto &bb : func->getBasicBlocks())
        {
            for (auto &inst : bb->getInstructions())
            {
                // 检查load
                if (auto *load = dynamic_cast<LoadInst *>(inst.get()))
                {
                    Value *origPtr = load->getOriginalPointer();
                    if (origPtr == alloca)
                    {
                        hasLoadOrCall = true;
                        break;
                    }
                }
                // 检查call
                if (auto *call = dynamic_cast<CallInst *>(inst.get()))
                {
                    if (call->HasUsedArray(alloca))
                    {
                        hasLoadOrCall = true;
                        break;
                    }
                }
            }
            if (hasLoadOrCall)
                break;
        }

        // 2. 没有load/call才删除
        if (!hasLoadOrCall)
        {
            relatedInsts.insert(alloca);
            std::vector<User *> worklist;
            worklist.push_back(alloca);

            while (!worklist.empty())
            {
                User *user = worklist.back();
                worklist.pop_back();
                for (auto *u : user->getUsers())
                {
                    if (auto *store = dynamic_cast<StoreInst *>(u))
                    {
                        relatedInsts.insert(store);
                    }
                    else if (auto *gep = dynamic_cast<GetElementPtrInst *>(u))
                    {
                        relatedInsts.insert(gep);
                        worklist.push_back(gep);
                    }
                    else if (auto *bitcast = dynamic_cast<CastInst *>(u))
                    {
                        if (bitcast->getOpcode() != Opcode::BitCast)
                            continue;
                        relatedInsts.insert(bitcast);
                        worklist.push_back(bitcast);
                    }
                }
            }
            // 删除相关指令
            for (auto &bb : func->getBasicBlocks())
            {
                auto &insts = bb->getInstructions();
                for (auto it = insts.begin(); it != insts.end();)
                {
                    auto *inst = it->get();
                    if (relatedInsts.count(inst))
                    {
                        if (verbose)
                        {
                            debugInfo << "RemoveWriteOnlyArrayPass: Removing write-only array instruction " << inst->getName() << " in function " << func->getName() << "\n";
                        }
                        inst->removeThisFromOperands();
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
            changed = true;
        }
    }
    return changed;
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
    auto *gep = dynamic_cast<GetElementPtrInst *>(ptr);
    if (!gep)
        return "";

    const auto indices = gep->getIndices();
    if (indices.size() != 1 || !indices[0])
        return "";

    Value *base = gep->getOriginalPointerOperand();
    if (!base)
        return "";

    return stripInlineSuffix(base->getName()) + "#" + stripInlineSuffix(indices[0]->getName());
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
                string key = buildArrayIndexKey(loadInst->getPointer());
                auto found = key.empty() ? latestStoredValue.end() : latestStoredValue.find(key);
                if (found != latestStoredValue.end() && found->second)
                {
                    loadInst->replaceAllUsesWith(found->second);
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
                // 保守起见，任何store先清空已记录映射，再记录当前store。
                latestStoredValue.clear();
                string key = buildArrayIndexKey(storeInst->getPointer());
                if (!key.empty())
                {
                    latestStoredValue[key] = storeInst->getValueToStore();
                }
            }

            ++it;
        }
    }

    return changed;
}
