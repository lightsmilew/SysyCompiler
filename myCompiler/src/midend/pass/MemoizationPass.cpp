#include "MemoizationPass.h"
using namespace std;
using namespace optimization;

const int MAX_PARAMS = 4;
const int ARRAY_SIZE = 65536;
static const int primes[] = {1000003, 1000033, 1000211, 1000433}; // 更大的质数，减少哈希冲突
// 哈希索引，数组大小65536
Value *MemoizationPass::getMemoIndex(const std::vector<Value *> &args, Function *func)
{
    // 目前还有一点小瑕疵 没有判断是否产生哈希冲突 没有确保idx非负-->已处理
    auto &module = *func->getParent();
    auto funcName = func->getName();
    auto memoFlagArrayName = getMemoFlagArrayName(funcName);
    auto memoValueArrayName = getMemoValueArrayName(funcName);
    // 获取或新建全局flag和value数组（避免重复创建）
    auto *flagArr = module.getGlobalVariable(memoFlagArrayName);
    if (!flagArr) {
        flagArr = module.addGlobalVariable(ArrayType::getInstance(IntegerType::getInstance(), ARRAY_SIZE),
                                           memoFlagArrayName,
                                           nullptr, false);
    }
    auto *valueArr = module.getGlobalVariable(memoValueArrayName);
    if (!valueArr) {
        valueArr = module.addGlobalVariable(ArrayType::getInstance(IntegerType::getInstance(), ARRAY_SIZE),
                                            memoValueArrayName,
                                            nullptr, false);
    }
    //1
    // auto *argsArr= module.addGlobalVariable(ArrayType::getInstance(ArrayType::getInstance(IntegerType::getInstance(), MAX_PARAMS), ARRAY_SIZE),
    //                                           getMemoArgsArrayName(funcName),
    //                                           nullptr, false);
    auto *mergeBB = func->getEntryBlock();
    // 新增block
    auto *condBB = new BasicBlock(funcName + "_memo_cond", func);
    auto *thenBB = new BasicBlock(funcName + "_memo_then", func);
    // 这里要把condBB插入到数组头
    func->BasicBlocks.insert(func->BasicBlocks.begin(), unique_ptr<BasicBlock>(condBB));
    func->addBasicBlock(unique_ptr<BasicBlock>(thenBB));
    // 更新CFG连接
    condBB->addSuccessor(thenBB);
    condBB->addSuccessor(mergeBB);
    thenBB->addPredecessor(condBB);
    mergeBB->addPredecessor(condBB);
    // idx = (a * p0 + b * p1 + c * p2 + ...) % ARRAY_SIZE
    // 只将 Instruction 插入到基本块；常量不插入。
    Value *current = new ConstantInt(IntegerType::getInstance(), 0);
    for (size_t i = 0; i < args.size(); ++i)
    {
        auto *factor = new ConstantInt(IntegerType::getInstance(), primes[i % 4]);
        auto *mul = new BinaryOperator(Opcode::Mul, args[i], factor, "mul_memo_idx" + to_string(i));
        condBB->insertBeforeTerminator(unique_ptr<Instruction>(mul));
        auto *add = new BinaryOperator(Opcode::Add, current, mul, "add_memo_idx" + to_string(i));
        condBB->insertBeforeTerminator(unique_ptr<Instruction>(add));
        current = add;
    }
    auto *MODCONSTANT = new ConstantInt(IntegerType::getInstance(), ARRAY_SIZE);
    auto *rem = new BinaryOperator(Opcode::SRem, current, MODCONSTANT, "rem_memo_idx");
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(rem));
    // 加上ARRAY_SIZE再取模一次，防止为负数
    auto *idx_add = new BinaryOperator(Opcode::Add, rem, MODCONSTANT, "idx_add");
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(idx_add));
    auto *idx_final = new BinaryOperator(Opcode::SRem, idx_add, MODCONSTANT, "idx_final");
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(idx_final));
    // if(flagArr[idx_final]) return valueArr[idx_final];
    auto *flagPtr = new GetElementPtrInst(flagArr, {idx_final}, "gep_" + memoFlagArrayName);
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(flagPtr));
    auto *flagVal = new LoadInst(flagPtr, "load_" + memoFlagArrayName);
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(flagVal));
    Instruction *cond = new ICmpInst(ICmpInst::Predicate::ICMP_EQ, flagVal, new ConstantInt(IntegerType::getInstance(), 1), "icmp_" + memoFlagArrayName);
    //2
    // for(int i=0;i<args.size();++i)
    // {
    //     auto *argGep=new GetElementPtrInst(argsArr,{idx_final,new ConstantInt(IntegerType::getInstance(),i)},"gep_"+getMemoArgsArrayName(funcName)+"_"+to_string(i));
    //     auto *argLoad=new LoadInst(argGep,"load_"+getMemoArgsArrayName(funcName)+"_"+to_string(i));
    //     auto *argCmp=new ICmpInst(ICmpInst::Predicate::ICMP_EQ,argLoad,args[i],"icmp_args_"+to_string(i));
    //     // 如果flag==1 && argsArray[i]==args[i]
    //     // 则表示已经计算过且没有产生哈希冲突
    //     cond=new BinaryOperator(Opcode::And,cond,argCmp,"cond_args_"+to_string(i));
    //     condBB->insertBeforeTerminator(unique_ptr<Instruction>(argGep));
    //     condBB->insertBeforeTerminator(unique_ptr<Instruction>(argLoad));
    //     condBB->insertBeforeTerminator(unique_ptr<Instruction>(argCmp));
    // }
    auto *br = new BranchInst(cond, thenBB, mergeBB);
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(cond));
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(br));
    // 插入thenBB的指令
    // then: return valueArr[idx_final];
    auto *valPtr = new GetElementPtrInst(valueArr, {idx_final}, "gep_" + memoValueArrayName);
    thenBB->insertBeforeTerminator(unique_ptr<Instruction>(valPtr));
    auto *val = new LoadInst(valPtr, "load_" + memoValueArrayName);
    thenBB->insertBeforeTerminator(unique_ptr<Instruction>(val));
    auto *ret = new ReturnInst(val);
    thenBB->insertBeforeTerminator(unique_ptr<Instruction>(ret));
    return idx_final;
}

bool MemoizationPass::isMemoizable(Function *func)
{
    auto *funcTy = dynamic_cast<FunctionType *>(func->getType());
    if (!funcTy || funcTy->ReturnType->isVoidTy())
        return false;
    if (func->getArguments().size() > MAX_PARAMS)
        return false;
    // 参数必须全为int/float类型
    for (auto &argPtr : func->getArguments())
    {
        Argument *arg = argPtr.get();
        if (!arg->getType()->isIntegerTy() && !arg->getType()->isFloatTy())
            return false;
    }
    auto recursiveCalls = getRecursiveCallInstr(func);
    if (recursiveCalls.size() < 2)
        return false;

    auto &formalArgs = func->getArguments();
    bool hasStableContextArg = false;
    for (size_t argIdx = 0; argIdx < formalArgs.size(); ++argIdx)
    {
        Argument *formalArg = formalArgs[argIdx].get();
        bool preservedAcrossAllCalls = true;
        for (auto *call : recursiveCalls)
        {
            auto callArgs = call->getArguments();
            if (argIdx >= callArgs.size() || callArgs[argIdx] != formalArg)
            {
                preservedAcrossAllCalls = false;
                break;
            }
        }
        if (preservedAcrossAllCalls)
        {
            hasStableContextArg = true;
            break;
        }
    }

    return hasStableContextArg;
}

// Helper: 将val在blk中取模并保证非负，返回产生结果的 Instruction*
static Value *makeNonNegative(Value *val, Value *modVal, Function *function, BasicBlock *blk)
{
    auto *add = new BinaryOperator(Opcode::Add, val, modVal, "memo_idx_add_mod");
    blk->insertBeforeTerminator(unique_ptr<Instruction>(add));
    auto *rem = new BinaryOperator(Opcode::SRem, add, modVal, "memo_idx_final_mod");
    blk->insertBeforeTerminator(unique_ptr<Instruction>(rem));
    return rem;
}

// Helper: 计算哈希，插入到hashBlk，返回产生的Value*
static Value *hashCal(BasicBlock *hashBlk, Function *function,
                      const std::vector<Value *> &args, Value *modVal, Value *baseVal)
{
    if (args.empty())
    {
        return new ConstantInt(IntegerType::getInstance(), 0);
    }

    // 初始 hash = args[0] % modVal, 保证非负
    auto *rem0 = new BinaryOperator(Opcode::SRem, args[0], modVal, "hash_rem0");
    hashBlk->insertBeforeTerminator(unique_ptr<Instruction>(rem0));
    Value *hashVal = makeNonNegative(rem0, modVal, function, hashBlk);

    for (size_t i = 1; i < args.size(); ++i) {
        auto *mul = new BinaryOperator(Opcode::Mul, hashVal, baseVal, "hash_mul");
        hashBlk->insertBeforeTerminator(unique_ptr<Instruction>(mul));
        auto *add = new BinaryOperator(Opcode::Add, mul, args[i], "hash_add");
        hashBlk->insertBeforeTerminator(unique_ptr<Instruction>(add));
        auto *rem = new BinaryOperator(Opcode::SRem, add, modVal, "hash_rem");
        hashBlk->insertBeforeTerminator(unique_ptr<Instruction>(rem));
        hashVal = makeNonNegative(rem, modVal, function, hashBlk);
    }
    return hashVal;
}

static std::vector<CallInst *> getRecursiveCallInstr(Function *function)
{
    std::vector<CallInst *> list;
    for (auto &bbPtr : function->getBasicBlocks()) {
        for (auto &instPtr : bbPtr->getInstructions()) {
            if (auto *call = dynamic_cast<CallInst *>(instPtr.get())) {
                if (call->getCalledFunction() == function)
                    list.push_back(call);
            }
        }
    }
    return list;
}

// split block around call: returns {prevBlk, callBlk, aftBlk}
struct SplitBlocks
{
    BasicBlock *prevBlk;
    BasicBlock *callBlk;
    BasicBlock *aftBlk;
};

static SplitBlocks splitBlk(CallInst *call)
{
    Function *parentFunc = nullptr;
    BasicBlock *callBlock = nullptr;
    Module *module = call->getCalledFunction()->getParent();
    for (auto &funcPtr : module->Functions)
    {
        Function *func = funcPtr.get();
        for (auto &bbPtr : func->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                if (instPtr.get() == call)
                {
                    callBlock = bbPtr.get();
                    parentFunc = func;
                    break;
                }
            }
            if (callBlock)
                break;
        }
        if (callBlock)
            break;
    }
    if (!callBlock || !parentFunc)
        return {nullptr, nullptr, nullptr};

    BasicBlock *prevBlk = callBlock;

    // create new blocks
    static size_t splitId = 0;
    size_t localId = splitId++;
    BasicBlock *callBlk = new BasicBlock(parentFunc->getName() + "_memo_call_" + to_string(localId), parentFunc);
    BasicBlock *aftBlk = new BasicBlock(parentFunc->getName() + "_memo_aft_" + to_string(localId), parentFunc);

    // insert callBlk and aftBlk into function basicblocks after prevBlk
    auto &bbs = parentFunc->getBasicBlocks();
    int idx = -1;
    for (int i = 0; i < (int)bbs.size(); ++i) {
        if (bbs[i].get() == prevBlk) { idx = i; break; }
    }
    if (idx < 0) return {nullptr,nullptr,nullptr};
    // insert callBlk after prevBlk
    bbs.insert(bbs.begin() + idx + 1, unique_ptr<BasicBlock>(callBlk));
    bbs.insert(bbs.begin() + idx + 2, unique_ptr<BasicBlock>(aftBlk));

    // move call instruction into callBlk and move following instructions into aftBlk
    auto &insts = prevBlk->getInstructions();
    auto callIt = std::find_if(insts.begin(), insts.end(),
                               [call](const std::unique_ptr<Instruction> &inst)
                               { return inst.get() == call; });
    if (callIt == insts.end())
        return {nullptr, nullptr, nullptr};

    unique_ptr<Instruction> callUptr = std::move(*callIt);
    callBlk->addInstruction(std::move(callUptr));

    callIt = insts.erase(callIt);
    while (callIt != insts.end())
    {
        unique_ptr<Instruction> moved = std::move(*callIt);
        callIt = insts.erase(callIt);
        aftBlk->addInstruction(std::move(moved));
    }

    // fix CFG: successors of prevBlk now should be successors of aftBlk
    vector<BasicBlock *> oldSucs = prevBlk->getSuccessors();
    for (auto *s : oldSucs)
    {
        s->removePredecessor(prevBlk);
        prevBlk->removeSuccessor(s);
        s->addPredecessor(aftBlk);
        aftBlk->addSuccessor(s);
        // update phi nodes in successor
        for (auto &instrPtr : s->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instrPtr.get()))
            {
                phi->replaceIncomingBasicBlock(prevBlk, aftBlk);
            }
            else
            {
                break;
            }
        }
    }

    // add jump from callBlk to aftBlk
    auto *brFromCall = new BranchInst(aftBlk);
    callBlk->insertBeforeTerminator(unique_ptr<Instruction>(brFromCall));

    return {prevBlk, callBlk, aftBlk};
}

bool MemoizationPass::runOnFunction(Function *func)
{
    if (!isMemoizable(func))
        return false;

    auto &module = *func->getParent();
    auto funcName = func->getName();
    auto *funcTy = dynamic_cast<FunctionType *>(func->getType());

    // create globals if not exist
    auto *globalValueArr = module.getGlobalVariable(getMemoValueArrayName(funcName));
    if (!globalValueArr)
    {
        globalValueArr = module.addGlobalVariable(ArrayType::getInstance(funcTy->ReturnType, ARRAY_SIZE),
                                                  getMemoValueArrayName(funcName), nullptr, false);
    }
    auto *globalFlagArr = module.getGlobalVariable(getMemoFlagArrayName(funcName));
    if (!globalFlagArr)
    {
        globalFlagArr = module.addGlobalVariable(ArrayType::getInstance(IntegerType::getInstance(), ARRAY_SIZE),
                                                 getMemoFlagArrayName(funcName), nullptr, false);
    }
    auto *globalParamsArr = module.getGlobalVariable(getMemoArgsArrayName(funcName));
    if (!globalParamsArr)
    {
        globalParamsArr = module.addGlobalVariable(ArrayType::getInstance(
                                                        ArrayType::getInstance(IntegerType::getInstance(), MAX_PARAMS),
                                                        ARRAY_SIZE),
                                                    getMemoArgsArrayName(funcName), nullptr, false);
    }

    // collect recursive calls
    auto recursiveCalls = getRecursiveCallInstr(func);
    size_t memoId = 0;
    for (auto *call : recursiveCalls)
    {
        // split block around call
        auto blks = splitBlk(call);
        BasicBlock *prev = blks.prevBlk;
        BasicBlock *callBlk = blks.callBlk;
        BasicBlock *aftBlk = blks.aftBlk;
        if (!prev || !callBlk || !aftBlk)
            continue;

        // create hash, check, load blocks
        auto insertBlockAfter = [&](BasicBlock *anchor, BasicBlock *newBlock) -> bool
        {
            auto &bbs = func->getBasicBlocks();
            auto it = std::find_if(bbs.begin(), bbs.end(),
                                   [anchor](const std::unique_ptr<BasicBlock> &bb)
                                   { return bb.get() == anchor; });
            if (it == bbs.end())
                return false;
            bbs.insert(it + 1, unique_ptr<BasicBlock>(newBlock));
            return true;
        };

        BasicBlock *hashBlk = new BasicBlock(funcName + "_memo_hash_" + to_string(memoId), func);
        BasicBlock *checkBlk = new BasicBlock(funcName + "_memo_check_" + to_string(memoId), func);
        BasicBlock *loadBlk = new BasicBlock(funcName + "_memo_load_" + to_string(memoId), func);
        if (!insertBlockAfter(prev, hashBlk) || !insertBlockAfter(hashBlk, checkBlk) || !insertBlockAfter(callBlk, loadBlk))
            continue;

        // prepare args for hash
        vector<Value *> castArgs;
        for (auto *arg : call->getArguments())
        {
            if (arg->getType()->isFloatTy())
            {
                auto *cast = new CastInst(Opcode::FPToSI, arg, IntegerType::getInstance(), "memo_fp2si_" + to_string(memoId));
                hashBlk->insertBeforeTerminator(unique_ptr<Instruction>(cast));
                castArgs.push_back(cast);
            }
            else
            {
                castArgs.push_back(arg);
            }
        }

        // compute hash in hashBlk
        Value *modVal = new ConstantInt(IntegerType::getInstance(), ARRAY_SIZE);
        Value *baseVal = new ConstantInt(IntegerType::getInstance(), primes[0]);
        Value *hashVal = hashCal(hashBlk, func, castArgs, modVal, baseVal);

        // gep to arrays
        vector<Value *> idxList{hashVal};
        GetElementPtrInst *dataPtr = new GetElementPtrInst(globalValueArr, idxList, "gep_data");
        GetElementPtrInst *usedPtr = new GetElementPtrInst(globalFlagArr, idxList, "gep_used");
        hashBlk->insertBeforeTerminator(unique_ptr<Instruction>(dataPtr));
        hashBlk->insertBeforeTerminator(unique_ptr<Instruction>(usedPtr));

        // load used flag and branch
        LoadInst *usedVal = new LoadInst(usedPtr, "load_used");
        hashBlk->insertBeforeTerminator(unique_ptr<Instruction>(usedVal));
        ICmpInst *usedCond = new ICmpInst(ICmpInst::ICMP_EQ, usedVal, new ConstantInt(IntegerType::getInstance(), 0), "cmp_used");
        hashBlk->insertBeforeTerminator(unique_ptr<Instruction>(usedCond));
        BranchInst *br = new BranchInst(usedCond, callBlk, checkBlk);
        hashBlk->insertBeforeTerminator(unique_ptr<Instruction>(br));
        prev->insertBeforeTerminator(unique_ptr<Instruction>(new BranchInst(hashBlk)));
        prev->addSuccessor(hashBlk);
        hashBlk->addPredecessor(prev);
        hashBlk->addSuccessor(callBlk);
        hashBlk->addSuccessor(checkBlk);
        callBlk->addPredecessor(hashBlk);
        checkBlk->addPredecessor(hashBlk);

        vector<GetElementPtrInst *> paramGeps;
        vector<ICmpInst *> cmpInstrs;
        for (size_t i = 0; i < castArgs.size(); ++i)
        {
            vector<Value *> paramsIndexList{hashVal, new ConstantInt(IntegerType::getInstance(), static_cast<int>(i))};
            auto *paramGep = new GetElementPtrInst(globalParamsArr, paramsIndexList, "gep_param_" + to_string(memoId) + "_" + to_string(i));
            auto *loadParam = new LoadInst(paramGep, "load_param_" + to_string(memoId) + "_" + to_string(i));
            auto *cmpParam = new ICmpInst(ICmpInst::ICMP_EQ, loadParam, castArgs[i], "cmp_param_" + to_string(memoId) + "_" + to_string(i));
            hashBlk->insertBeforeTerminator(unique_ptr<Instruction>(paramGep));
            checkBlk->insertBeforeTerminator(unique_ptr<Instruction>(loadParam));
            checkBlk->insertBeforeTerminator(unique_ptr<Instruction>(cmpParam));
            paramGeps.push_back(paramGep);
            cmpInstrs.push_back(cmpParam);
        }

        if (cmpInstrs.empty())
        {
            checkBlk->insertBeforeTerminator(unique_ptr<Instruction>(new BranchInst(loadBlk)));
        }
        else if (cmpInstrs.size() == 1)
        {
            checkBlk->insertBeforeTerminator(unique_ptr<Instruction>(new BranchInst(cmpInstrs[0], loadBlk, callBlk)));
        }
        else
        {
            Value *result = cmpInstrs[0];
            for (size_t i = 1; i < cmpInstrs.size(); ++i)
            {
                auto *sum = new BinaryOperator(Opcode::Add, result, cmpInstrs[i], "cmp_sum_" + to_string(memoId) + "_" + to_string(i));
                checkBlk->insertBeforeTerminator(unique_ptr<Instruction>(sum));
                result = sum;
            }
            auto *checker = new ICmpInst(ICmpInst::ICMP_EQ, result, new ConstantInt(IntegerType::getInstance(), static_cast<int>(cmpInstrs.size())), "cmp_all_" + to_string(memoId));
            checkBlk->insertBeforeTerminator(unique_ptr<Instruction>(checker));
            checkBlk->insertBeforeTerminator(unique_ptr<Instruction>(new BranchInst(checker, loadBlk, callBlk)));
        }
        checkBlk->addSuccessor(loadBlk);
        checkBlk->addSuccessor(callBlk);
        loadBlk->addPredecessor(checkBlk);
        callBlk->addPredecessor(checkBlk);

        // in loadBlk: load data and jump to aftBlk
        LoadInst *dataVal = new LoadInst(dataPtr, "load_data");
        loadBlk->insertBeforeTerminator(unique_ptr<Instruction>(dataVal));
        loadBlk->insertBeforeTerminator(unique_ptr<Instruction>(new BranchInst(aftBlk)));
        loadBlk->addSuccessor(aftBlk);
        aftBlk->addPredecessor(loadBlk);

        // create phi in aftBlk's first instruction to replace call uses
        PhiInst *phi = new PhiInst(call->getType(), funcName + "_memo_phi_" + to_string(memoId));
        phi->addIncoming(dataVal, loadBlk);
        call->replaceAllUsesWith(phi);
        aftBlk->insert(unique_ptr<Instruction>(phi), 0);
        phi->addIncoming(call, callBlk);

        // after call (in callBlk) store result and set used flag
        callBlk->insertBeforeTerminator(unique_ptr<Instruction>(new StoreInst(call, dataPtr)));
        callBlk->insertBeforeTerminator(unique_ptr<Instruction>(new StoreInst(new ConstantInt(IntegerType::getInstance(), 1), usedPtr)));
        for (size_t i = 0; i < castArgs.size(); ++i)
        {
            callBlk->insertBeforeTerminator(unique_ptr<Instruction>(new StoreInst(castArgs[i], paramGeps[i])));
        }

        callBlk->addSuccessor(aftBlk);
        aftBlk->addPredecessor(callBlk);

        ++memoId;
    }

    if (verbose)
        debugInfo << "memorization func:" << funcName << std::endl;

    return true;
}