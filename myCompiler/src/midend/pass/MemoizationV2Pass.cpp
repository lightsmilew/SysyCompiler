#include "MemoizationV2Pass.h"
#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace std;
using namespace optimization;

namespace
{
static constexpr int kCacheSize = 8192;

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

static int64_t extractConstant(Value *val)
{
    if (auto *c = dynamic_cast<ConstantInt *>(val))
        return c->Value;
    return -1;
}

// Infer a conservative upper bound for an integer argument from icmp in the function.
static int inferArgUpperBound(Function *func, unsigned argIndex)
{
    if (argIndex >= func->getArguments().size())
        return -1;

    Argument *target = func->getArguments()[argIndex].get();
    int upper = -1;

    auto considerBound = [&](int64_t bound, bool inclusive)
    {
        if (bound < 0)
            return;
        int candidate = inclusive ? static_cast<int>(bound) : static_cast<int>(bound - 1);
        if (candidate > upper)
            upper = candidate;
    };

    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get());
            if (!icmp)
                continue;

            Value *lhs = icmp->getLHS();
            Value *rhs = icmp->getRHS();
            int64_t rhsConst = extractConstant(rhs);
            int64_t lhsConst = extractConstant(lhs);

            switch (icmp->getPredicate())
            {
            case ICmpInst::ICMP_SLT:
                if (lhs == target)
                    considerBound(rhsConst, false);
                break;
            case ICmpInst::ICMP_SLE:
                if (lhs == target)
                    considerBound(rhsConst, true);
                break;
            case ICmpInst::ICMP_SGT:
                if (rhs == target)
                    considerBound(lhsConst, false);
                break;
            case ICmpInst::ICMP_SGE:
                if (rhs == target)
                    considerBound(lhsConst, true);
                break;
            default:
                break;
            }
        }
    }

    return upper;
}

static GlobalVariable *getRootGlobalVariable(Value *ptr)
{
    while (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr))
        ptr = gep->getOriginalPointerOperand();
    return dynamic_cast<GlobalVariable *>(ptr);
}

static bool isArgMinusOne(Value *index, Argument *param)
{
    auto *bin = dynamic_cast<BinaryOperator *>(index);
    if (!bin || bin->getOpcode() != Opcode::Sub)
        return false;
    return bin->getLHS() == param && extractConstant(bin->getRHS()) == 1;
}

// arg0 used as (arg0-1) to index at least two distinct global arrays (weight/value pattern).
static bool hasKnapsackLikeArrayPattern(Function *func, Argument *arg0)
{
    set<string> arrayNames;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            auto *gep = dynamic_cast<GetElementPtrInst *>(instPtr.get());
            if (!gep)
                continue;

            auto *gv = getRootGlobalVariable(gep->getPointerOperand());
            if (!gv || !gv->isArray())
                continue;

            for (Value *idx : gep->getIndices())
            {
                if (isArgMinusOne(idx, arg0))
                {
                    arrayNames.insert(gv->getName());
                    break;
                }
            }
        }
    }
    return arrayNames.size() >= 2;
}

// If arg0 indexes global array(s) as (arg0-1), upper bound is array length (i in [1, len]).
static int inferArgUpperBoundFromArrayIndex(Function *func, unsigned argIndex)
{
    if (argIndex >= func->getArguments().size())
        return -1;

    Argument *target = func->getArguments()[argIndex].get();
    int upper = -1;

    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            auto *gep = dynamic_cast<GetElementPtrInst *>(instPtr.get());
            if (!gep)
                continue;

            auto *gv = getRootGlobalVariable(gep->getPointerOperand());
            if (!gv || !gv->isArray())
                continue;

            for (Value *idx : gep->getIndices())
            {
                if (!isArgMinusOne(idx, target))
                    continue;
                const int len = static_cast<int>(gv->getTotallength());
                if (len > upper)
                    upper = len;
            }
        }
    }

    return upper;
}

static int inferArgUpperBoundFromCallSites(Module *module, Function *func, unsigned argIndex)
{
    if (!module || argIndex >= func->getArguments().size())
        return -1;

    int upper = -1;
    for (auto &funcPtr : module->Functions)
    {
        Function *caller = funcPtr.get();
        if (!caller)
            continue;

        for (auto &bbPtr : caller->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *call = dynamic_cast<CallInst *>(instPtr.get());
                if (!call || call->getCalledFunction() != func)
                    continue;

                const vector<Value *> callArgs = call->getArguments();
                if (argIndex >= callArgs.size())
                    continue;

                const int64_t c = extractConstant(callArgs[argIndex]);
                if (c >= 0 && static_cast<int>(c) > upper)
                    upper = static_cast<int>(c);
            }
        }
    }

    return upper;
}

// Recursive actual must be: param, param-const, or param-nonNegativeLoad (w - weight[i-1]).
static bool isSafeRecursiveActual(Value *actual, Argument *param)
{
    if (actual == param)
        return true;

    auto *bin = dynamic_cast<BinaryOperator *>(actual);
    if (!bin)
        return false;

    if (bin->getOpcode() == Opcode::Sub && bin->getLHS() == param)
        return true;

    if (bin->getOpcode() == Opcode::Add && bin->getLHS() == param)
    {
        const int64_t c = extractConstant(bin->getRHS());
        return c <= 0;
    }

    return false;
}

static bool rejectsRecursiveActual(Value *actual, Argument *param)
{
    if (actual == param)
        return false;

    auto *bin = dynamic_cast<BinaryOperator *>(actual);
    if (!bin)
        return true;

    if (bin->getOpcode() == Opcode::Mul)
    {
        if (bin->getLHS() == param || bin->getRHS() == param)
            return true;
    }

    if (bin->getOpcode() == Opcode::Add && bin->getLHS() == param)
    {
        const int64_t c = extractConstant(bin->getRHS());
        if (c > 0)
            return true;
    }

    if (bin->getOpcode() == Opcode::SDiv || bin->getOpcode() == Opcode::SRem)
    {
        if (bin->getLHS() == param)
            return true;
    }

    return false;
}

// Both args only shrink (or stay) along recursive edges; reject fun(n*3+1, dep+1) style.
static bool recursiveArgsShrinkOnly(Function *func)
{
    if (func->getArguments().size() != 2)
        return false;

    Argument *arg0 = func->getArguments()[0].get();
    Argument *arg1 = func->getArguments()[1].get();
    bool sawRecursive = false;

    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            auto *call = dynamic_cast<CallInst *>(instPtr.get());
            if (!call || call->getCalledFunction() != func)
                continue;

            sawRecursive = true;
            const vector<Value *> callArgs = call->getArguments();
            if (callArgs.size() < 2)
                return false;

            if (rejectsRecursiveActual(callArgs[0], arg0) || rejectsRecursiveActual(callArgs[1], arg1))
                return false;

            if (!isSafeRecursiveActual(callArgs[0], arg0) || !isSafeRecursiveActual(callArgs[1], arg1))
                return false;
        }
    }

    return sawRecursive;
}

static Value *buildCacheSlotIndex(BasicBlock *bb, const vector<Value *> &args, bool directIndex2Arg, int stride)
{
    Value *hashVal = asI32(args[0], bb, "memo_arg0_i32");

    if (directIndex2Arg && args.size() == 2)
    {
        auto *strideConst = new ConstantInt(IntegerType::getInstance(), stride);
        auto *arg1 = asI32(args[1], bb, "memo_arg1_i32");
        auto *mul =
            new BinaryOperator(Opcode::Mul, hashVal, strideConst, freshName("memo_hash_stride_mul"));
        bb->insertBeforeTerminator(unique_ptr<Instruction>(mul));
        auto *add = new BinaryOperator(Opcode::Add, mul, arg1, freshName("memo_hash_stride_add"));
        bb->insertBeforeTerminator(unique_ptr<Instruction>(add));
        hashVal = add;
    }
    else
    {
        if (args.size() >= 2)
        {
            auto *mult33 = new ConstantInt(IntegerType::getInstance(), 33);
            auto *arg1 = asI32(args[1], bb, "memo_arg1_i32");
            auto *mul = new BinaryOperator(Opcode::Mul, arg1, mult33, freshName("memo_hash_mul33"));
            bb->insertBeforeTerminator(unique_ptr<Instruction>(mul));
            auto *add = new BinaryOperator(Opcode::Add, hashVal, mul, freshName("memo_hash_add1"));
            bb->insertBeforeTerminator(unique_ptr<Instruction>(add));
            hashVal = add;
        }
        if (args.size() >= 3)
        {
            auto *mult65 = new ConstantInt(IntegerType::getInstance(), 65);
            auto *arg2 = asI32(args[2], bb, "memo_arg2_i32");
            auto *mul = new BinaryOperator(Opcode::Mul, arg2, mult65, freshName("memo_hash_mul65"));
            bb->insertBeforeTerminator(unique_ptr<Instruction>(mul));
            auto *add = new BinaryOperator(Opcode::Add, hashVal, mul, freshName("memo_hash_add2"));
            bb->insertBeforeTerminator(unique_ptr<Instruction>(add));
            hashVal = add;
        }
    }

    auto *cacheMask = new ConstantInt(IntegerType::getInstance(), kCacheSize - 1);
    auto *cacheSlot = new BinaryOperator(Opcode::And, hashVal, cacheMask, freshName("memo_cache_slot"));
    bb->insertBeforeTerminator(unique_ptr<Instruction>(cacheSlot));
    return cacheSlot;
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

MemoizationV2Pass::DirectIndex2ArgPlan MemoizationV2Pass::analyzeDirectIndex2Arg(Function *func) const
{
    DirectIndex2ArgPlan plan;

    const auto &args = func->getArguments();
    if (args.size() != 2)
        return plan;

    for (const auto &argPtr : args)
    {
        if (!argPtr->getType()->isIntegerTy())
            return plan;
    }

    if (!recursiveArgsShrinkOnly(func))
        return plan;

    Argument *arg0 = args[0].get();

    int max0 = inferArgUpperBound(func, 0);
    const int fromArray = inferArgUpperBoundFromArrayIndex(func, 0);
    if (fromArray > max0)
        max0 = fromArray;

    int max1 = inferArgUpperBound(func, 1);
    const int fromCalls = inferArgUpperBoundFromCallSites(func->getParent(), func, 1);
    if (fromCalls > max1)
        max1 = fromCalls;

    // Knapsack-like: two global arrays indexed by (arg0-1); derive stride from arg0 bound.
    if (max0 >= 0 && max1 < 0 && hasKnapsackLikeArrayPattern(func, arg0))
    {
        const int stride = (CACHE_SIZE - 1) / (max0 + 1);
        if (stride >= 2)
        {
            max1 = stride - 1;
        }
    }

    if (max0 < 0 || max1 < 0)
        return plan;

    const int stride = max1 + 1;
    const int64_t maxSlot = static_cast<int64_t>(max0) * stride + max1;
    if (maxSlot >= CACHE_SIZE)
        return plan;

    plan.useDirectIndex = true;
    plan.stride = stride;
    return plan;
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
    auto *entrySizeConst = new ConstantInt(IntegerType::getInstance(), entrySize);

    vector<Value *> args;
    args.reserve(argPtrs.size());
    for (const auto &argPtr : argPtrs)
        args.push_back(argPtr.get());

    const DirectIndex2ArgPlan directPlan = analyzeDirectIndex2Arg(func);
    Value *cacheSlot =
        buildCacheSlotIndex(cacheLookupBlock, args, directPlan.useDirectIndex, directPlan.stride);

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
                  << ", bytes=" << (totalSize * 4);
        if (directPlan.useDirectIndex)
            debugInfo << ", hash=direct_arg0*" << directPlan.stride << "+arg1";
        else
            debugInfo << ", hash=poly";
        debugInfo << ")\n";
    }
}

bool MemoizationV2Pass::runOnFunction(Function *func)
{
    if (!analyzeFunctionForMemoization(func))
        return false;

    addMemoizationToFunction(func);
    return true;
}
