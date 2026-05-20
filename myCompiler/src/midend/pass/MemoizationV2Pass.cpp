#include "MemoizationV2Pass.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace std;
using namespace optimization;

namespace
{
static int nameCounter = 0;

static string freshName(const string &prefix)
{
    return prefix + "_" + to_string(nameCounter++);
}

static Value *asI32(Value *val, BasicBlock *bb, const string &tag)
{
    if (val->getType()->isIntegerTy())
        return val;
    if (val->getType()->isFloatTy())
    {
        auto *cast = new CastInst(Opcode::FPToSI, val, IntegerType::getInstance(), tag);
        bb->insertBeforeTerminator(unique_ptr<Instruction>(cast));
        return cast;
    }
    return val;
}

static bool isMainFunction(Function *func)
{
    return func->getName() == "main";
}

static int countRecursiveCalls(Function *func)
{
    int count = 0;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            if (auto *call = dynamic_cast<CallInst *>(instPtr.get()))
            {
                if (call->getCalledFunction() == func)
                    ++count;
            }
        }
    }
    return count;
}

static bool storeIsLocalAllocaOnly(StoreInst *store)
{
    Value *ptr = store->getOriginalPointer();
    while (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr))
        ptr = gep->getOriginalPointerOperand();
    return dynamic_cast<AllocaInst *>(ptr) != nullptr;
}

static bool hasDisallowedSideEffects(Function *func)
{
    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            if (auto *store = dynamic_cast<StoreInst *>(instPtr.get()))
            {
                if (!storeIsLocalAllocaOnly(store))
                    return true;
            }
            else if (auto *call = dynamic_cast<CallInst *>(instPtr.get()))
            {
                Function *callee = call->getCalledFunction();
                if (!callee || callee == func)
                    continue;
                if (callee->isLibraryFunction())
                    continue;
                if (call->ifHasSideEffects())
                    return true;
            }
        }
    }
    return false;
}

static void redirectPredecessors(BasicBlock *from, BasicBlock *to)
{
    vector<BasicBlock *> preds = from->getPredecessors();
    for (BasicBlock *pred : preds)
    {
        auto *br = dynamic_cast<BranchInst *>(pred->getTerminator());
        if (!br)
            continue;

        bool changed = false;
        auto retarget = [&](BasicBlock *&edge)
        {
            if (edge != from)
                return;
            edge = to;
            changed = true;
        };

        if (!br->isConditional())
            retarget(br->TrueBlock);
        else
        {
            retarget(br->TrueBlock);
            retarget(br->FalseBlock);
        }

        if (!changed)
            continue;

        pred->removeSuccessor(from);
        pred->addSuccessor(to);
        from->removePredecessor(pred);
        to->addPredecessor(pred);
    }
}

static void insertBeforeReturn(BasicBlock *block, vector<unique_ptr<Instruction>> insts)
{
    auto &list = block->getInstructions();
    unsigned insertAt = 0;
    for (unsigned i = 0; i < list.size(); ++i)
    {
        if (dynamic_cast<ReturnInst *>(list[i].get()))
        {
            insertAt = i;
            break;
        }
    }
    for (auto &inst : insts)
        block->insert(std::move(inst), insertAt++);
}

} // namespace

bool MemoizationV2Pass::analyzeFunctionForMemoization(Function *func)
{
    if (func->isLibraryFunction() || isMainFunction(func))
        return false;
    if (func->getBasicBlocks().empty())
        return false;

    auto *funcTy = func->getFunctionType();
    if (!funcTy)
        return false;
    if (!funcTy->ReturnType->isIntegerTy() && !funcTy->ReturnType->isFloatTy())
        return false;

    const auto &args = func->getArguments();
    if (args.empty() || args.size() > 3)
        return false;

    for (const auto &argPtr : args)
    {
        Type *ty = argPtr->getType();
        if (!ty->isIntegerTy() && !ty->isFloatTy())
            return false;
    }

    if (countRecursiveCalls(func) < MIN_RECURSIVE_CALLS)
        return false;
    if (hasDisallowedSideEffects(func))
        return false;

    return true;
}

void MemoizationV2Pass::addMemoizationToFunction(Function *func)
{
    Module &module = *func->getParent();
    const string funcName = func->getName();
    const auto &argPtrs = func->getArguments();
    const int numArgs = static_cast<int>(argPtrs.size());
    const int entrySize = numArgs + 2; // args..., result, has_value
    const int totalSize = CACHE_SIZE * entrySize;

    auto *cacheArrayTy = ArrayType::getInstance(IntegerType::getInstance(), static_cast<unsigned>(totalSize));
    const string cacheName = "__memo_cache_v2_" + funcName;
    GlobalVariable *cacheVar = module.getGlobalVariable(cacheName);
    if (!cacheVar)
    {
        cacheVar = module.addGlobalVariable(cacheArrayTy, cacheName, nullptr, false);
    }

    BasicBlock *entryBlock = func->getEntryBlock();
  auto &entryInsts = entryBlock->getInstructions();

    // Skip leading allocas in entry block.
    auto splitIt = entryInsts.begin();
    while (splitIt != entryInsts.end() && dynamic_cast<AllocaInst *>(splitIt->get()))
        ++splitIt;

    // Slot holding flat cache index (i32) for hit path and return stores.
    auto *memoIndexSlotTy = ArrayType::getInstance(IntegerType::getInstance(), 1);
    auto *memoIndexSlot = new AllocaInst(memoIndexSlotTy, freshName("memo_idx_slot"));
    splitIt = entryInsts.insert(splitIt, unique_ptr<Instruction>(memoIndexSlot));
    ++splitIt;

    auto insertBlockAfter = [&](BasicBlock *anchor, BasicBlock *newBlock)
    {
        auto &bbs = func->getBasicBlocks();
        auto it = std::find_if(bbs.begin(), bbs.end(),
                               [anchor](const unique_ptr<BasicBlock> &bb) { return bb.get() == anchor; });
        if (it == bbs.end())
            return;
        bbs.insert(it + 1, unique_ptr<BasicBlock>(newBlock));
    };

    BasicBlock *cacheLookupBlock = new BasicBlock(funcName + "_cache_lookup", func);
    BasicBlock *computeBlock = new BasicBlock(funcName + "_compute", func);
    BasicBlock *cacheHitBlock = new BasicBlock(funcName + "_cache_hit", func);
    BasicBlock *cacheMissBlock = new BasicBlock(funcName + "_cache_miss", func);

    insertBlockAfter(entryBlock, cacheLookupBlock);
    insertBlockAfter(cacheLookupBlock, cacheMissBlock);
    insertBlockAfter(cacheMissBlock, cacheHitBlock);
    insertBlockAfter(cacheHitBlock, computeBlock);

    vector<BasicBlock *> entryOldSuccs = entryBlock->getSuccessors();

    // Move original entry logic (after allocas) into compute block.
    while (splitIt != entryInsts.end())
    {
        unique_ptr<Instruction> inst = std::move(*splitIt);
        splitIt = entryInsts.erase(splitIt);
        computeBlock->addInstruction(std::move(inst));
    }

    for (BasicBlock *succ : entryOldSuccs)
    {
        succ->removePredecessor(entryBlock);
        succ->addPredecessor(computeBlock);
        for (auto &instPtr : succ->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                phi->replaceIncomingBasicBlock(entryBlock, computeBlock);
            else
                break;
        }
        entryBlock->removeSuccessor(succ);
        computeBlock->addSuccessor(succ);
    }

    entryBlock->addInstruction(unique_ptr<Instruction>(new BranchInst(cacheLookupBlock)));
    entryBlock->addSuccessor(cacheLookupBlock);
    cacheLookupBlock->addPredecessor(entryBlock);

    redirectPredecessors(entryBlock, cacheLookupBlock);

    auto *zero = new ConstantInt(IntegerType::getInstance(), 0);
    auto *one = new ConstantInt(IntegerType::getInstance(), 1);
    auto *cacheMask = new ConstantInt(IntegerType::getInstance(), CACHE_SIZE - 1);
    auto *entrySizeConst = new ConstantInt(IntegerType::getInstance(), entrySize);

    vector<Value *> args;
    args.reserve(argPtrs.size());
    for (const auto &argPtr : argPtrs)
        args.push_back(argPtr.get());

    // hash = arg0 + arg1*33 + arg2*65
    Value *hashVal = asI32(args[0], cacheLookupBlock, "memo_arg0_i32");
    if (args.size() >= 2)
    {
        auto *mult33 = new ConstantInt(IntegerType::getInstance(), 33);
        auto *arg1 = asI32(args[1], cacheLookupBlock, "memo_arg1_i32");
        auto *mul = new BinaryOperator(Opcode::Mul, arg1, mult33, freshName("memo_hash_mul33"));
        cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(mul));
        auto *add = new BinaryOperator(Opcode::Add, hashVal, mul, freshName("memo_hash_add1"));
        cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(add));
        hashVal = add;
    }
    if (args.size() >= 3)
    {
        auto *mult65 = new ConstantInt(IntegerType::getInstance(), 65);
        auto *arg2 = asI32(args[2], cacheLookupBlock, "memo_arg2_i32");
        auto *mul = new BinaryOperator(Opcode::Mul, arg2, mult65, freshName("memo_hash_mul65"));
        cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(mul));
        auto *add = new BinaryOperator(Opcode::Add, hashVal, mul, freshName("memo_hash_add2"));
        cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(add));
        hashVal = add;
    }

    auto *cacheSlot = new BinaryOperator(Opcode::And, hashVal, cacheMask, freshName("memo_cache_slot"));
    cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(cacheSlot));

    auto *cacheIndex = new BinaryOperator(Opcode::Mul, cacheSlot, entrySizeConst, freshName("memo_cache_index"));
    cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(cacheIndex));

    cacheLookupBlock->insertBeforeTerminator(
        unique_ptr<Instruction>(new StoreInst(cacheIndex, memoIndexSlot)));

    auto *cacheEntry = new GetElementPtrInst(cacheVar, {zero, cacheIndex}, freshName("memo_cache_entry"));
    cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(cacheEntry));

    auto *hasValueOffset = new ConstantInt(IntegerType::getInstance(), entrySize - 1);
    auto *hasValuePtr = new GetElementPtrInst(cacheEntry, {hasValueOffset}, freshName("memo_has_value_ptr"));
    cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(hasValuePtr));

    auto *hasValue = new LoadInst(hasValuePtr, freshName("memo_has_value"));
    cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(hasValue));

    auto *isValid = new ICmpInst(ICmpInst::ICMP_NE, hasValue, zero, freshName("memo_is_valid"));
    cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(isValid));

    Value *argsMatch = isValid;
    for (int i = 0; i < numArgs; ++i)
    {
        auto *argOffset = new ConstantInt(IntegerType::getInstance(), i);
        auto *storedArgPtr = new GetElementPtrInst(cacheEntry, {argOffset}, freshName("memo_stored_arg_ptr"));
        cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(storedArgPtr));

        auto *storedArg = new LoadInst(storedArgPtr, freshName("memo_stored_arg"));
        cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(storedArg));

        Value *cmpRhs = asI32(args[static_cast<size_t>(i)], cacheLookupBlock,
                              "memo_cmp_arg" + to_string(i));
        auto *argEq = new ICmpInst(ICmpInst::ICMP_EQ, storedArg, cmpRhs, freshName("memo_arg_eq"));
        cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(argEq));

        auto *match = new BinaryOperator(Opcode::And, argsMatch, argEq, freshName("memo_args_match"));
        cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(match));
        argsMatch = match;
    }

    cacheLookupBlock->insertBeforeTerminator(
        unique_ptr<Instruction>(new BranchInst(argsMatch, cacheHitBlock, cacheMissBlock)));
    cacheLookupBlock->addSuccessor(cacheHitBlock);
    cacheLookupBlock->addSuccessor(cacheMissBlock);
    cacheHitBlock->addPredecessor(cacheLookupBlock);
    cacheMissBlock->addPredecessor(cacheLookupBlock);

    // Cache hit: load cached result and return.
    auto *hitCacheIndex = new LoadInst(memoIndexSlot, freshName("memo_hit_cache_index"));
    cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(hitCacheIndex));
    auto *hitCacheEntry =
        new GetElementPtrInst(cacheVar, {zero, hitCacheIndex}, freshName("memo_hit_cache_entry"));
    cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(hitCacheEntry));

    auto *resultOffset = new ConstantInt(IntegerType::getInstance(), numArgs);
    auto *resultPtr = new GetElementPtrInst(hitCacheEntry, {resultOffset}, freshName("memo_result_ptr"));
    cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(resultPtr));

    auto *cachedResult = new LoadInst(resultPtr, freshName("memo_cached_result"));
    cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(cachedResult));

    Value *retVal = cachedResult;
    if (func->getFunctionType()->ReturnType->isFloatTy())
    {
        auto *toFp = new CastInst(Opcode::SIToFP, cachedResult, FloatType::getInstance(), freshName("memo_cached_fp"));
        cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(toFp));
        retVal = toFp;
    }

    cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(new ReturnInst(retVal)));

    // Cache miss: run original function body.
    cacheMissBlock->insertBeforeTerminator(unique_ptr<Instruction>(new BranchInst(computeBlock)));
    cacheMissBlock->addSuccessor(computeBlock);
    computeBlock->addPredecessor(cacheMissBlock);

    // On each return in compute path, fill cache entry.
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *block = bbPtr.get();
        if (block == entryBlock || block == cacheLookupBlock || block == cacheHitBlock ||
            block == cacheMissBlock)
            continue;

        Instruction *term = block->getTerminator();
        auto *ret = dynamic_cast<ReturnInst *>(term);
        if (!ret || !ret->getReturnValue())
            continue;

        Value *returnValue = ret->getReturnValue();
        vector<unique_ptr<Instruction>> prelude;

        auto *localIndex = new LoadInst(memoIndexSlot, freshName("memo_store_cache_index"));
        prelude.push_back(unique_ptr<Instruction>(localIndex));

        auto *localEntry =
            new GetElementPtrInst(cacheVar, {zero, localIndex}, freshName("memo_store_cache_entry"));
        prelude.push_back(unique_ptr<Instruction>(localEntry));

        for (int i = 0; i < numArgs; ++i)
        {
            auto *argOffset = new ConstantInt(IntegerType::getInstance(), i);
            auto *argPtr = new GetElementPtrInst(localEntry, {argOffset}, freshName("memo_store_arg_ptr"));
            prelude.push_back(unique_ptr<Instruction>(argPtr));

            Value *argI32 = args[static_cast<size_t>(i)];
            if (!argI32->getType()->isIntegerTy())
            {
                auto *cast = new CastInst(Opcode::FPToSI, argI32, IntegerType::getInstance(),
                                          freshName("memo_store_arg_i32"));
                prelude.push_back(unique_ptr<Instruction>(cast));
                argI32 = cast;
            }
            prelude.push_back(unique_ptr<Instruction>(new StoreInst(argI32, argPtr)));
        }

        auto *resOffset = new ConstantInt(IntegerType::getInstance(), numArgs);
        auto *resPtr = new GetElementPtrInst(localEntry, {resOffset}, freshName("memo_store_res_ptr"));
        prelude.push_back(unique_ptr<Instruction>(resPtr));

        Value *storedRet = returnValue;
        if (returnValue->getType()->isFloatTy())
        {
            auto *toI32 = new CastInst(Opcode::FPToSI, returnValue, IntegerType::getInstance(),
                                       freshName("memo_store_ret_i32"));
            // insertBeforeReturn will own these; asI32 path not used here.
            prelude.push_back(unique_ptr<Instruction>(toI32));
            storedRet = toI32;
        }
        prelude.push_back(unique_ptr<Instruction>(new StoreInst(storedRet, resPtr)));

        auto *validOffset = new ConstantInt(IntegerType::getInstance(), entrySize - 1);
        auto *validPtr = new GetElementPtrInst(localEntry, {validOffset}, freshName("memo_store_valid_ptr"));
        prelude.push_back(unique_ptr<Instruction>(validPtr));
        prelude.push_back(unique_ptr<Instruction>(new StoreInst(one, validPtr)));

        insertBeforeReturn(block, std::move(prelude));
    }

    if (verbose)
    {
        debugInfo << "MemoizationV2Pass: memoized " << funcName << " (slots=" << CACHE_SIZE
                  << ", bytes=" << (totalSize * 4) << ")\n";
    }
}

bool MemoizationV2Pass::runOnFunction(Function *func)
{
    if (!analyzeFunctionForMemoization(func))
        return false;

    addMemoizationToFunction(func);
    return true;
}
