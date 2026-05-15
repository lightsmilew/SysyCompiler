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
    // 新建全局flag和value数组
    auto *flagArr = module.addGlobalVariable(ArrayType::getInstance(IntegerType::getInstance(), ARRAY_SIZE),
                                             memoFlagArrayName,
                                             nullptr, false);
    auto *valueArr = module.addGlobalVariable(ArrayType::getInstance(IntegerType::getInstance(), ARRAY_SIZE),
                                              memoValueArrayName,
                                              nullptr, false);
    //1
    auto *argsArr= module.addGlobalVariable(ArrayType::getInstance(ArrayType::getInstance(IntegerType::getInstance(), MAX_PARAMS), ARRAY_SIZE),
                                              getMemoArgsArrayName(funcName),
                                              nullptr, false);
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
    // idx = (a * 1000003 + b * 1000033 + c * 1000211 + ...) % 65536
    Value *idx = new ConstantInt(IntegerType::getInstance(), 0);
    // 插入获取index的指令
    for (size_t i = 0; i < args.size(); ++i)
    {
        auto *factor = new ConstantInt(IntegerType::getInstance(), primes[i % 4]);
        auto *mul = new BinaryOperator(Opcode::Mul, args[i], factor, "mul_memo_idx" + to_string(i));
        idx = new BinaryOperator(Opcode::Add, idx, mul, "add_memo_idx" + to_string(i));
        condBB->insertBeforeTerminator(unique_ptr<Instruction>(mul));
        condBB->insertBeforeTerminator(unique_ptr<Instruction>(dynamic_cast<Instruction *>(idx)));
    }
    auto *MODCONSTANT = new ConstantInt(IntegerType::getInstance(), ARRAY_SIZE);
    idx = new BinaryOperator(Opcode::SRem, idx, MODCONSTANT, "rem_memo_idx");
    // 加上65536再取模一次，防止为负数
    auto *idx_add=new BinaryOperator(Opcode::Add,idx,MODCONSTANT,"idx_add");
    auto *idx_final=new BinaryOperator(Opcode::SRem,idx_add,MODCONSTANT,"idx_final");
    // if(flagArr[idx_final]) return valueArr[idx_final];
    auto *flagPtr = new GetElementPtrInst(flagArr, {idx_final}, "gep_" + memoFlagArrayName);
    auto *flagVal = new LoadInst(flagPtr, "load_" + memoFlagArrayName);
    Instruction *cond = new ICmpInst(ICmpInst::Predicate::ICMP_EQ, flagVal, new ConstantInt(IntegerType::getInstance(), 1), "icmp_" + memoFlagArrayName);
    //2
    for(int i=0;i<args.size();++i)
    {
        auto *argGep=new GetElementPtrInst(argsArr,{idx_final,new ConstantInt(IntegerType::getInstance(),i)},"gep_"+getMemoArgsArrayName(funcName)+"_"+to_string(i));
        auto *argLoad=new LoadInst(argGep,"load_"+getMemoArgsArrayName(funcName)+"_"+to_string(i));
        auto *argCmp=new ICmpInst(ICmpInst::Predicate::ICMP_EQ,argLoad,args[i],"icmp_args_"+to_string(i));
        // 如果flag==1 && argsArray[i]==args[i]
        // 则表示已经计算过且没有产生哈希冲突
        cond=new BinaryOperator(Opcode::And,cond,argCmp,"cond_args_"+to_string(i));
        condBB->insertBeforeTerminator(unique_ptr<Instruction>(argGep));
        condBB->insertBeforeTerminator(unique_ptr<Instruction>(argLoad));
        condBB->insertBeforeTerminator(unique_ptr<Instruction>(argCmp));
    }
    auto *br = new BranchInst(cond, thenBB, mergeBB);
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(dynamic_cast<Instruction *>(idx)));
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(idx_add));
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(idx_final));
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(flagPtr));
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(flagVal));
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(cond));
    condBB->insertBeforeTerminator(unique_ptr<Instruction>(br));
    // 插入thenBB的指令
    // then: return valueArr[idx_final];
    auto *valPtr = new GetElementPtrInst(valueArr, {idx_final}, "gep_" + memoValueArrayName);
    auto *val = new LoadInst(valPtr, "load_" + memoValueArrayName);
    auto *ret = new ReturnInst(val);
    thenBB->insertBeforeTerminator(unique_ptr<Instruction>(valPtr));
    thenBB->insertBeforeTerminator(unique_ptr<Instruction>(val));
    thenBB->insertBeforeTerminator(unique_ptr<Instruction>(ret));
    return idx_final;
}

bool MemoizationPass::isMemoizable(Function *func)
{
    if (func->getArguments().size() > MAX_PARAMS)
        return false;
    // 参数必须全为int类型
    for (auto &argPtr : func->getArguments())
    {
        Argument *arg = argPtr.get();
        if (!arg->getType()->isIntegerTy())
            return false;
    }
    // 简单判断：递归（调用自身）
    for (auto &bb : func->getBasicBlocks())
    {
        for (auto &inst : bb->getInstructions())
        {
            if (auto *call = dynamic_cast<CallInst *>(inst.get()))
            {
                if (call->getCalledFunction() == func && !call->ifHasSideEffects())
                    return true;
            }
        }
    }
    return false;
}

bool MemoizationPass::runOnFunction(Function *func)
{

    if (!isMemoizable(func))
        return false;

    // 在函数入口插入记忆化判断
    auto *entry = func->getEntryBlock();
    auto &module = *func->getParent();
    auto &funcName = func->getName();
    std::vector<Value *> args;
    for (auto &arg : func->getArguments())
        args.push_back(arg.get());
    Value *idx = getMemoIndex(args, func);
    Value *valueArr = module.getGlobalVariable(getMemoValueArrayName(funcName));
    Value *flagArr = module.getGlobalVariable(getMemoFlagArrayName(funcName));
    //3
    Value *argsArr=module.getGlobalVariable(getMemoArgsArrayName(funcName));
    // 在所有return前插入写回
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        if (insts.empty())
            continue;
        auto *inst = insts.back().get();
        if (auto *ret = dynamic_cast<ReturnInst *>(inst))
        {
            // 在return前插入指令
            auto *gepVal = new GetElementPtrInst(valueArr, {idx}, "gep_" + getMemoValueArrayName(funcName));
            auto *storeVal = new StoreInst(ret->getReturnValue(), gepVal);
            bb->insertBeforeTerminator(unique_ptr<Instruction>(gepVal));
            bb->insertBeforeTerminator(unique_ptr<Instruction>(storeVal));
            auto *gepFlag = new GetElementPtrInst(flagArr, {idx}, "gep_" + getMemoFlagArrayName(funcName));
            auto *storeFlag = new StoreInst(new ConstantInt(IntegerType::getInstance(), 1), gepFlag);
            bb->insertBeforeTerminator(unique_ptr<Instruction>(gepFlag));
            bb->insertBeforeTerminator(unique_ptr<Instruction>(storeFlag));
            // // 把当前参数存入args数组
            //4
            int args_num=args.size();
            for(int i=0;i<args_num;++i)
            {
                auto *gepArg=new GetElementPtrInst(argsArr,{idx,new ConstantInt(IntegerType::getInstance(),i)},"gep_"+getMemoArgsArrayName(funcName)+"_"+to_string(i));
                auto *storeArg=new StoreInst(args[i],gepArg);
                bb->insertBeforeTerminator(unique_ptr<Instruction>(gepArg));
                bb->insertBeforeTerminator(unique_ptr<Instruction>(storeArg));
            }
        }
    }
    if (verbose)
        debugInfo << "memorization func:" << funcName << std::endl;
    //std::cout << func->toString() << std::endl;
    return true;
}