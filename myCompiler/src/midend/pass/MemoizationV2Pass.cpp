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

static bool isTailRecursiveReturn(BasicBlock *bb, CallInst *call)
{
    auto *ret = dynamic_cast<ReturnInst *>(bb->getTerminator());
    if (!ret || ret->getReturnValue() != call)
        return false;
    auto &insts = bb->getInstructions();
    if (insts.size() < 2)
        return false;
    return dynamic_cast<CallInst *>(insts[insts.size() - 2].get()) == call;
}

static bool isAllTailRecursive(Function *func)
{
    bool sawRecursive = false;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        for (auto &instPtr : bb->getInstructions())
        {
            auto *call = dynamic_cast<CallInst *>(instPtr.get());
            if (!call || call->getCalledFunction() != func)
                continue;
            sawRecursive = true;
            if (!isTailRecursiveReturn(bb, call))
                return false;
        }
    }
    return sawRecursive;
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

static void insertBlockAfter(Function *func, BasicBlock *anchor, BasicBlock *newBlock)
{
    auto &bbs = func->getBasicBlocks();
    auto it = std::find_if(bbs.begin(), bbs.end(),
                           [anchor](const unique_ptr<BasicBlock> &bb) { return bb.get() == anchor; });
    if (it == bbs.end())
        return;
    bbs.insert(it + 1, unique_ptr<BasicBlock>(newBlock));
}

struct DenseShape
{
    int numSlots = 0;
    // Bound constants for each used arg (0 = unused).
    int bound0 = 0;
    int bound1 = 0;
    int bound2 = 0;
};

static DenseShape shapeForArity(int numArgs)
{
    DenseShape s;
    if (numArgs == 1)
    {
        s.numSlots = MemoizationV2Pass::DENSE1_SIZE;
        s.bound0 = MemoizationV2Pass::DENSE1_SIZE;
    }
    else if (numArgs == 2)
    {
        s.numSlots = MemoizationV2Pass::DENSE2_ROWS * MemoizationV2Pass::DENSE2_COLS;
        s.bound0 = MemoizationV2Pass::DENSE2_ROWS;
        s.bound1 = MemoizationV2Pass::DENSE2_COLS;
    }
    else
    {
        s.numSlots = MemoizationV2Pass::DENSE3_D0 * MemoizationV2Pass::DENSE3_D1 *
                     MemoizationV2Pass::DENSE3_D2;
        s.bound0 = MemoizationV2Pass::DENSE3_D0;
        s.bound1 = MemoizationV2Pass::DENSE3_D1;
        s.bound2 = MemoizationV2Pass::DENSE3_D2;
    }
    return s;
}

// Emit oob = OR_i (ai < 0 || ai >= bound_i) for used args; branch oob→oobBB else→inBB.
static void emitBoundsCheck(BasicBlock *bb, const vector<Value *> &ai, const DenseShape &shape,
                            BasicBlock *oobBB, BasicBlock *inBB)
{
    auto *zero = new ConstantInt(IntegerType::getInstance(), 0);
    Value *oob = nullptr;

    auto addBound = [&](Value *a, int bound, const string &tag)
    {
        if (bound <= 0)
            return;
        auto *boundC = new ConstantInt(IntegerType::getInstance(), bound);
        auto *neg = new ICmpInst(ICmpInst::ICMP_SLT, a, zero, freshName("memo_" + tag + "_neg"));
        bb->insertBeforeTerminator(unique_ptr<Instruction>(neg));
        auto *hi = new ICmpInst(ICmpInst::ICMP_SGE, a, boundC, freshName("memo_" + tag + "_hi"));
        bb->insertBeforeTerminator(unique_ptr<Instruction>(hi));
        auto *part = new BinaryOperator(Opcode::Or, neg, hi, freshName("memo_" + tag + "_oob"));
        bb->insertBeforeTerminator(unique_ptr<Instruction>(part));
        if (!oob)
            oob = part;
        else
        {
            auto *merged = new BinaryOperator(Opcode::Or, oob, part, freshName("memo_oob_acc"));
            bb->insertBeforeTerminator(unique_ptr<Instruction>(merged));
            oob = merged;
        }
    };

    addBound(ai[0], shape.bound0, "a0");
    if (ai.size() >= 2)
        addBound(ai[1], shape.bound1, "a1");
    if (ai.size() >= 3)
        addBound(ai[2], shape.bound2, "a2");

    bb->insertBeforeTerminator(unique_ptr<Instruction>(new BranchInst(oob, oobBB, inBB)));
    bb->addSuccessor(oobBB);
    bb->addSuccessor(inBB);
    oobBB->addPredecessor(bb);
    inBB->addPredecessor(bb);
}

// Flat cell index (before * entrySize).
static Value *buildDenseSlot(BasicBlock *bb, const vector<Value *> &ai, int numArgs)
{
    if (numArgs == 1)
        return ai[0];

    if (numArgs == 2)
    {
        auto *cols = new ConstantInt(IntegerType::getInstance(), MemoizationV2Pass::DENSE2_COLS);
        auto *mul = new BinaryOperator(Opcode::Mul, ai[0], cols, freshName("memo_dense_mul"));
        bb->insertBeforeTerminator(unique_ptr<Instruction>(mul));
        auto *add = new BinaryOperator(Opcode::Add, mul, ai[1], freshName("memo_dense_slot"));
        bb->insertBeforeTerminator(unique_ptr<Instruction>(add));
        return add;
    }

    // a0*(D1*D2) + a1*D2 + a2
    auto *d1 = new ConstantInt(IntegerType::getInstance(), MemoizationV2Pass::DENSE3_D1);
    auto *d2 = new ConstantInt(IntegerType::getInstance(), MemoizationV2Pass::DENSE3_D2);
    auto *stride0 = new ConstantInt(IntegerType::getInstance(),
                                    MemoizationV2Pass::DENSE3_D1 * MemoizationV2Pass::DENSE3_D2);
    auto *m0 = new BinaryOperator(Opcode::Mul, ai[0], stride0, freshName("memo_d3_m0"));
    bb->insertBeforeTerminator(unique_ptr<Instruction>(m0));
    auto *m1 = new BinaryOperator(Opcode::Mul, ai[1], d2, freshName("memo_d3_m1"));
    bb->insertBeforeTerminator(unique_ptr<Instruction>(m1));
    auto *s01 = new BinaryOperator(Opcode::Add, m0, m1, freshName("memo_d3_s01"));
    bb->insertBeforeTerminator(unique_ptr<Instruction>(s01));
    auto *slot = new BinaryOperator(Opcode::Add, s01, ai[2], freshName("memo_dense_slot"));
    bb->insertBeforeTerminator(unique_ptr<Instruction>(slot));
    (void)d1;
    return slot;
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
    const DenseShape shape = shapeForArity(numArgs);

    constexpr int entrySize = 2; // [result, valid]
    constexpr int resultField = 0;
    constexpr int validField = 1;
    const int totalSize = shape.numSlots * entrySize;

    auto *cacheArrayTy =
        ArrayType::getInstance(IntegerType::getInstance(), static_cast<unsigned>(totalSize));
    const string cacheName = "__memo_cache_dense_" + funcName;
    GlobalVariable *cacheVar = module.getGlobalVariable(cacheName);
    if (!cacheVar)
        cacheVar = module.addGlobalVariable(cacheArrayTy, cacheName, nullptr, false);

    BasicBlock *entryBlock = func->getEntryBlock();
    auto &entryInsts = entryBlock->getInstructions();

    auto splitIt = entryInsts.begin();
    while (splitIt != entryInsts.end() && dynamic_cast<AllocaInst *>(splitIt->get()))
        ++splitIt;

    auto *memoIndexSlotTy = ArrayType::getInstance(IntegerType::getInstance(), 1);
    auto *memoIndexSlot = new AllocaInst(memoIndexSlotTy, freshName("memo_idx_slot"));
    splitIt = entryInsts.insert(splitIt, unique_ptr<Instruction>(memoIndexSlot));
    ++splitIt;

    BasicBlock *cacheLookupBlock = new BasicBlock(funcName + "_cache_lookup", func);
    BasicBlock *memoOobBlock = new BasicBlock(funcName + "_memo_oob", func);
    BasicBlock *memoInBlock = new BasicBlock(funcName + "_memo_dense_in", func);
    BasicBlock *cacheHitBlock = new BasicBlock(funcName + "_cache_hit", func);
    BasicBlock *cacheMissBlock = new BasicBlock(funcName + "_cache_miss", func);
    BasicBlock *computeBlock = new BasicBlock(funcName + "_compute", func);

    insertBlockAfter(func, entryBlock, cacheLookupBlock);
    insertBlockAfter(func, cacheLookupBlock, memoOobBlock);
    insertBlockAfter(func, memoOobBlock, memoInBlock);
    insertBlockAfter(func, memoInBlock, cacheMissBlock);
    insertBlockAfter(func, cacheMissBlock, cacheHitBlock);
    insertBlockAfter(func, cacheHitBlock, computeBlock);

    vector<BasicBlock *> entryOldSuccs = entryBlock->getSuccessors();

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
    auto *negOne = new ConstantInt(IntegerType::getInstance(), -1);
    auto *entrySizeConst = new ConstantInt(IntegerType::getInstance(), entrySize);

    vector<Value *> args;
    args.reserve(argPtrs.size());
    for (const auto &argPtr : argPtrs)
        args.push_back(argPtr.get());

    vector<Value *> ai;
    for (int i = 0; i < numArgs; ++i)
        ai.push_back(asI32(args[static_cast<size_t>(i)], cacheLookupBlock,
                           "memo_dense_a" + to_string(i)));

    emitBoundsCheck(cacheLookupBlock, ai, shape, memoOobBlock, memoInBlock);

    // OOB: index=-1, skip store on return.
    memoOobBlock->insertBeforeTerminator(
        unique_ptr<Instruction>(new StoreInst(negOne, memoIndexSlot)));
    memoOobBlock->insertBeforeTerminator(unique_ptr<Instruction>(new BranchInst(computeBlock)));
    memoOobBlock->addSuccessor(computeBlock);
    computeBlock->addPredecessor(memoOobBlock);

    // In-range lookup.
    Value *slot = buildDenseSlot(memoInBlock, ai, numArgs);
    auto *cacheIndex =
        new BinaryOperator(Opcode::Mul, slot, entrySizeConst, freshName("memo_cache_index"));
    memoInBlock->insertBeforeTerminator(unique_ptr<Instruction>(cacheIndex));
    memoInBlock->insertBeforeTerminator(
        unique_ptr<Instruction>(new StoreInst(cacheIndex, memoIndexSlot)));

    auto *cacheEntry =
        new GetElementPtrInst(cacheVar, {zero, cacheIndex}, freshName("memo_cache_entry"));
    memoInBlock->insertBeforeTerminator(unique_ptr<Instruction>(cacheEntry));

    auto *hasValueOffset = new ConstantInt(IntegerType::getInstance(), validField);
    auto *hasValuePtr =
        new GetElementPtrInst(cacheEntry, {hasValueOffset}, freshName("memo_has_value_ptr"));
    memoInBlock->insertBeforeTerminator(unique_ptr<Instruction>(hasValuePtr));
    auto *hasValue = new LoadInst(hasValuePtr, freshName("memo_has_value"));
    memoInBlock->insertBeforeTerminator(unique_ptr<Instruction>(hasValue));
    auto *isValid = new ICmpInst(ICmpInst::ICMP_NE, hasValue, zero, freshName("memo_is_valid"));
    memoInBlock->insertBeforeTerminator(unique_ptr<Instruction>(isValid));

    memoInBlock->insertBeforeTerminator(
        unique_ptr<Instruction>(new BranchInst(isValid, cacheHitBlock, cacheMissBlock)));
    memoInBlock->addSuccessor(cacheHitBlock);
    memoInBlock->addSuccessor(cacheMissBlock);
    cacheHitBlock->addPredecessor(memoInBlock);
    cacheMissBlock->addPredecessor(memoInBlock);

    // Hit.
    {
        auto *hitCacheIndex = new LoadInst(memoIndexSlot, freshName("memo_hit_cache_index"));
        cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(hitCacheIndex));
        auto *hitCacheEntry =
            new GetElementPtrInst(cacheVar, {zero, hitCacheIndex}, freshName("memo_hit_cache_entry"));
        cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(hitCacheEntry));
        auto *resultOffset = new ConstantInt(IntegerType::getInstance(), resultField);
        auto *resultPtr =
            new GetElementPtrInst(hitCacheEntry, {resultOffset}, freshName("memo_result_ptr"));
        cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(resultPtr));
        auto *cachedResult = new LoadInst(resultPtr, freshName("memo_cached_result"));
        cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(cachedResult));
        Value *retVal = cachedResult;
        if (func->getFunctionType()->ReturnType->isFloatTy())
        {
            auto *toFp = new CastInst(Opcode::SIToFP, cachedResult, FloatType::getInstance(),
                                      freshName("memo_cached_fp"));
            cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(toFp));
            retVal = toFp;
        }
        cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(new ReturnInst(retVal)));
    }

    cacheMissBlock->insertBeforeTerminator(unique_ptr<Instruction>(new BranchInst(computeBlock)));
    cacheMissBlock->addSuccessor(computeBlock);
    computeBlock->addPredecessor(cacheMissBlock);

    vector<BasicBlock *> returnBlocks;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *block = bbPtr.get();
        if (block == entryBlock || block == cacheLookupBlock || block == memoOobBlock ||
            block == memoInBlock || block == cacheHitBlock || block == cacheMissBlock)
            continue;
        auto *ret = dynamic_cast<ReturnInst *>(block->getTerminator());
        if (ret && ret->getReturnValue())
            returnBlocks.push_back(block);
    }

    for (BasicBlock *block : returnBlocks)
    {
        auto *ret = dynamic_cast<ReturnInst *>(block->getTerminator());
        Value *retVal = ret->getReturnValue();

        BasicBlock *doStoreBB = new BasicBlock(freshName(block->getName() + "_memo_store"), func);
        BasicBlock *afterBB = new BasicBlock(freshName(block->getName() + "_memo_after"), func);
        insertBlockAfter(func, block, doStoreBB);
        insertBlockAfter(func, doStoreBB, afterBB);

        auto &insts = block->getInstructions();
        unique_ptr<Instruction> retOwned = std::move(insts.back());
        insts.pop_back();
        static_cast<ReturnInst *>(retOwned.get())->removeThisFromOperands();

        auto *localIndex = new LoadInst(memoIndexSlot, freshName("memo_store_cache_index"));
        block->addInstruction(unique_ptr<Instruction>(localIndex));
        auto *idxOk = new ICmpInst(ICmpInst::ICMP_SGE, localIndex, zero, freshName("memo_idx_ok"));
        block->addInstruction(unique_ptr<Instruction>(idxOk));
        block->addInstruction(unique_ptr<Instruction>(new BranchInst(idxOk, doStoreBB, afterBB)));
        block->addSuccessor(doStoreBB);
        block->addSuccessor(afterBB);
        doStoreBB->addPredecessor(block);
        afterBB->addPredecessor(block);

        auto *localEntry =
            new GetElementPtrInst(cacheVar, {zero, localIndex}, freshName("memo_store_cache_entry"));
        doStoreBB->addInstruction(unique_ptr<Instruction>(localEntry));
        auto *resOffset = new ConstantInt(IntegerType::getInstance(), resultField);
        auto *resPtr =
            new GetElementPtrInst(localEntry, {resOffset}, freshName("memo_store_res_ptr"));
        doStoreBB->addInstruction(unique_ptr<Instruction>(resPtr));
        Value *storedRet = retVal;
        if (retVal->getType()->isFloatTy())
        {
            auto *toI32 = new CastInst(Opcode::FPToSI, retVal, IntegerType::getInstance(),
                                       freshName("memo_store_ret_i32"));
            doStoreBB->addInstruction(unique_ptr<Instruction>(toI32));
            storedRet = toI32;
        }
        doStoreBB->addInstruction(unique_ptr<Instruction>(new StoreInst(storedRet, resPtr)));
        auto *validOffset = new ConstantInt(IntegerType::getInstance(), validField);
        auto *validPtr =
            new GetElementPtrInst(localEntry, {validOffset}, freshName("memo_store_valid_ptr"));
        doStoreBB->addInstruction(unique_ptr<Instruction>(validPtr));
        doStoreBB->addInstruction(unique_ptr<Instruction>(new StoreInst(one, validPtr)));
        doStoreBB->addInstruction(unique_ptr<Instruction>(new BranchInst(afterBB)));
        doStoreBB->addSuccessor(afterBB);
        afterBB->addPredecessor(doStoreBB);

        afterBB->addInstruction(unique_ptr<Instruction>(new ReturnInst(retVal)));
    }

    if (verbose)
    {
        debugInfo << "MemoizationV2Pass: memoized " << funcName << " (dense arity=" << numArgs
                  << ", slots=" << shape.numSlots << ", bytes=" << (totalSize * 4) << ")\n";
    }
}

bool MemoizationV2Pass::runOnFunction(Function *func)
{
    if (func->isLibraryFunction() || isMainFunction(func))
        return false;

    if (isAllTailRecursive(func))
    {
        if (verbose)
        {
            debugInfo << "MemoizationV2Pass: skip tail-recursive function " << func->getName()
                      << "\n";
        }
        return false;
    }

    if (!analyzeFunctionForMemoization(func))
        return false;

    addMemoizationToFunction(func);
    return true;
}
