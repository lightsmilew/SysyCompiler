#include "OptimizationPasses.h"
#include <iostream>
#include <stack>

using namespace std;
using namespace optimization;

// ========== PassManager 实现 ==========
void PassManager::addPass(std::unique_ptr<Pass> pass) {
    passes.push_back(std::move(pass));
}

bool PassManager::runOnModule(Module *module) {
    bool changed = false;
    // function从下标13开始 前13项为库函数
    for(size_t i = 13; i < module->Functions.size(); ++i) 
    {
        auto &func = module->Functions[i];
        if (verbose)
            std::cout << "[Optimization] Running passes on function " << func->getName() << std::endl;
        for (auto &pass : passes) 
        {
            if (verbose)
                std::cout << "[Optimization] Running " << pass->getName()
                          << " on function " << func->getName() << std::endl;
            changed |= pass->runOnFunction(func.get());
        }
    }
    return changed;
}

// ========== 死代码消除 ==========
bool DeadCodeEliminationPass::runOnFunction(Function *func) 
{
    std::unordered_set<Instruction *> liveInsts;
    markLiveInstructions(func, liveInsts);

    bool changed = false;
    for (auto &bb : func->getBasicBlocks()) 
    {
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();) 
        {
            Instruction *inst = it->get();
            if (liveInsts.count(inst) == 0) 
            {
                it = insts.erase(it);
                changed = true;
            }
            else 
            {
                ++it;
            }
        }
    }
    return changed;
}

// 标记所有关键指令和其依赖为活跃
void DeadCodeEliminationPass::markLiveInstructions(Function *func, std::unordered_set<Instruction *> &liveInsts) 
{
    std::stack<Instruction *> worklist;
    // 1. 标记终结指令、store、call等有副作用的指令为活跃
    for (auto &bb : func->getBasicBlocks()) 
    {
        for (auto &instPtr : bb->getInstructions()) 
        {
            Instruction *inst = instPtr.get();
            //如果是关键指令
            if (isInstructionCritical(inst)) 
            {
                liveInsts.insert(inst);
                worklist.push(inst);
            }
        }
    }
    // 2. 反向传播依赖
    while (!worklist.empty()) 
    {
        Instruction *inst = worklist.top();
        worklist.pop();
        for (auto *op : inst->getOperands()) 
        {
            if (auto *def = dynamic_cast<Instruction *>(op)) 
            {
                if (liveInsts.insert(def).second) 
                {
                    worklist.push(def);
                }
            }
        }
    }
}

// 判断指令是否为关键指令
bool DeadCodeEliminationPass::isInstructionCritical(Instruction *inst) 
{
    return inst->isTerminator() || inst->mayHaveSideEffects();
}

// ========== 常量折叠 ==========
bool ConstantFoldingPass::runOnFunction(Function *func) 
{
    bool changed = false;
    for (auto &bb : func->getBasicBlocks()) 
    {
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end(); ++it) 
        {
            Instruction *inst = it->get();
            // 二元运算常量折叠
            if (auto *binOp = dynamic_cast<BinaryOperator *>(inst)) 
            {
                if (isConstant(binOp->getOperandByIndex(0)) && isConstant(binOp->getOperandByIndex(1))) {
                    Value *folded = foldBinaryOperation(binOp);
                    if (folded) 
                    {
                        inst->replaceAllUsesWith(folded);
                        // 将折叠后的常量替换原指令
                        it = insts.erase(it);
                        changed = true;
                        if (it == insts.end()) break;
                    }
                }
            }
            // 比较指令常量折叠
            else if (auto *cmp = dynamic_cast<ICmpInst *>(inst)) 
            {
                if (isConstant(cmp->getOperandByIndex(0)) && isConstant(cmp->getOperandByIndex(1))) {
                    Value *folded = foldComparison(cmp);
                    if (folded) 
                    {
                        inst->replaceAllUsesWith(folded);
                        // 将折叠后的常量替换原指令
                        it = insts.erase(it);
                        changed = true;
                        if (it == insts.end()) break;
                    }
                }
            }
        }
    }
    return changed;
}

// 二元运算常量折叠
Value *ConstantFoldingPass::foldBinaryOperation(BinaryOperator *binOp) 
{
    bool isFloat = binOp->getType()->isFloatTy();
    if (isFloat) 
    {
        // 处理浮点数的常量折叠
        auto *lhs = dynamic_cast<ConstantFloat *>(binOp->getOperandByIndex(0));
        auto *rhs = dynamic_cast<ConstantFloat *>(binOp->getOperandByIndex(1));
        if (!lhs || !rhs) return nullptr;
        float l = lhs->Value, r = rhs->Value;
        float res = 0.0f;
        switch (binOp->getOpcode()) 
        {
            case Opcode::FAdd: res = l + r; break;
            case Opcode::FSub: res = l - r; break;
            case Opcode::FMul: res = l * r; break;
            case Opcode::FDiv:
                if (r == 0.0f) return nullptr; // 避免除以零
                res = l / r;
                break;
            // 处理其他浮点操作
            default: return nullptr; // 不支持的操作
        }
        return new ConstantFloat(FloatType::getInstance(), res);
    }
    auto *lhs = dynamic_cast<ConstantInt *>(binOp->getOperandByIndex(0));
    auto *rhs = dynamic_cast<ConstantInt *>(binOp->getOperandByIndex(1));
    if (!lhs || !rhs) return nullptr;
    int l = lhs->Value, r = rhs->Value;
    int res = 0;
    switch (binOp->getOpcode()) 
    {
        case Opcode::Add: res = l + r; break;
        case Opcode::Sub: res = l - r; break;
        case Opcode::Mul: res = l * r; break;
        case Opcode::SDiv: 
            if (r == 0) return nullptr; // 避免除以零
            res = l / r; 
            break;    
        default: return nullptr; // 不支持的操作
    }
    return new ConstantInt(IntegerType::getInstance(), res);
}

// 比较指令常量折叠
Value *ConstantFoldingPass::foldComparison(ICmpInst *cmpInst) 
{
    bool isFloat = cmpInst->getType()->isFloatTy();
    if (isFloat) 
    {
        // 处理浮点数的比较
        auto *lhs = dynamic_cast<ConstantFloat *>(cmpInst->getOperandByIndex(0));
        auto *rhs = dynamic_cast<ConstantFloat *>(cmpInst->getOperandByIndex(1));
        if (!lhs || !rhs) return nullptr;
        float l = lhs->Value, r = rhs->Value;
        bool res = false;
        switch (cmpInst->getPredicate()) 
        {
            case ICmpInst::Predicate::ICMP_EQ: res = (l == r); break;
            case ICmpInst::Predicate::ICMP_NE: res = (l != r); break;
            case ICmpInst::Predicate::ICMP_SLT: res = (l < r); break;
            case ICmpInst::Predicate::ICMP_SLE: res = (l <= r); break;
            case ICmpInst::Predicate::ICMP_SGT:
                res = (l > r); break;
            case ICmpInst::Predicate::ICMP_SGE: res = (l >= r); break;
            default: return nullptr;
        }
        return new ConstantInt(IntegerType::getInstance(), res ? 1 : 0);
    }
    auto *lhs = dynamic_cast<ConstantInt *>(cmpInst->getOperandByIndex(0));
    auto *rhs = dynamic_cast<ConstantInt *>(cmpInst->getOperandByIndex(1));
    if (!lhs || !rhs) return nullptr;
    int l = lhs->Value, r = rhs->Value;
    bool res = false;
    switch (cmpInst->getPredicate()) 
    {
        case ICmpInst::Predicate::ICMP_EQ: res = (l == r); break;
        case ICmpInst::Predicate::ICMP_NE: res = (l != r); break;
        case ICmpInst::Predicate::ICMP_SLT: res = (l < r); break;
        case ICmpInst::Predicate::ICMP_SLE: res = (l <= r); break;
        case ICmpInst::Predicate::ICMP_SGT: res = (l > r); break;
        case ICmpInst::Predicate::ICMP_SGE: res = (l >= r); break;
        default: return nullptr;
    }
    return new ConstantBool(BooleanType::getInstance(), res);
}

bool ConstantFoldingPass::isConstant(Value *val) 
{
    //这里可以优化，增加查找常量符号表，const变量也算是常量
    return dynamic_cast<ConstantInt *>(val) != nullptr||
           dynamic_cast<ConstantFloat *>(val) != nullptr||
           dynamic_cast<ConstantBool *>(val) != nullptr;
}

// ========== 公共子表达式消除 ==========
bool CommonSubexpressionEliminationPass::runOnFunction(Function *func) 
{
    exprMap.clear();
    bool changed = false;
    for (auto &bb : func->getBasicBlocks()) 
    {
        for (auto &instPtr : bb->getInstructions()) 
        {
            Instruction *inst = instPtr.get();
            if (!canBeCommonSubexpression(inst)) continue;
            auto key = getExpressionKey(inst);
            auto it = exprMap.find(key);
            if (it != exprMap.end()) 
            {
                inst->replaceAllUsesWith(it->second);
                changed = true;
            } 
            else 
            {
                exprMap[key] = inst;
            }
        }
    }
    return changed;
}
// 为每个指令生成唯一的表达式键
std::pair<string, vector<Value *>> CommonSubexpressionEliminationPass::getExpressionKey(Instruction *inst) 
{
    vector<Value *> ops = inst->getOperands();
    return {inst->getOpcodeName(), ops};
}
// 判断指令是否可以作为公共子表达式
bool CommonSubexpressionEliminationPass::canBeCommonSubexpression(Instruction *inst) 
{
    // 只处理无副作用的二元运算,不包括Store Call Ret Br
    return inst->isBinaryOp() && !inst->mayHaveSideEffects();
}
// 哈希函数，用于表达式键的哈希表
std::size_t CommonSubexpressionEliminationPass::ExpressionHash::operator()(const std::pair<string, vector<Value *>> &expr) const 
{
    std::size_t h = std::hash<string>()(expr.first);
    for (auto *v : expr.second) h ^= std::hash<void *>()(v);
    return h;
}

// ========== 复制传播 ==========
bool CopyPropagationPass::runOnFunction(Function *func) 
{
    copyMap.clear();
    collectCopies(func);
    bool changed = false;
    for (auto &bb : func->getBasicBlocks()) 
    {
        for (auto &instPtr : bb->getInstructions()) 
        {
            Instruction *inst = instPtr.get();
            for (size_t i = 0; i < inst->getNumOperands(); ++i)
             {
                Value *op = inst->getOperandByIndex(i);
                Value *newOp = followCopyChain(op);
                if (newOp != op) 
                {
                    inst->setOperand(i, newOp);
                    changed = true;
                }
            }
        }
    }
    return changed;
}
// 收集所有的mov/copy指令
void CopyPropagationPass::collectCopies(Function *func) 
{
    for (auto &bb : func->getBasicBlocks()) 
    {
        for (auto &instPtr : bb->getInstructions()) 
        {
            Instruction *inst = instPtr.get();
            // 假设mov/copy指令为: %a = %b
            if (inst->isCopy())
            {
                // 记录复制关系 从第2个操作数复制到第1个操作数
                copyMap[inst->getOperandByIndex(0)] = inst->getOperandByIndex(1);
            }
        }
    }
}
// 跟踪复制链，直到找到最终的值
Value *CopyPropagationPass::followCopyChain(Value *val) 
{
    while (copyMap.count(val)) val = copyMap[val];
    return val;
}

// ========== 基本块合并 ==========
bool BasicBlockMergePass::runOnFunction(Function *func) 
{
    bool changed = false;
    auto &bbs = func->getBasicBlocks();
    for (size_t i = 0; i + 1 < bbs.size(); ++i) 
    {
        BasicBlock *bb1 = bbs[i].get();
        BasicBlock *bb2 = bbs[i + 1].get();
        if (canMergeBlocks(bb1, bb2)) 
        {
            mergeBlocks(bb1, bb2);
            changed = true;
        }
    }
    return changed;
}
bool BasicBlockMergePass::canMergeBlocks(BasicBlock *bb1, BasicBlock *bb2) 
{
    // bb1 只有一个后继且是bb2，bb2只有一个前驱且是bb1
    return bb1->getSuccessors().size() == 1 && bb1->getSuccessors()[0] == bb2 &&
           bb2->getPredecessors().size() == 1 && bb2->getPredecessors()[0] == bb1;
}
void BasicBlockMergePass::mergeBlocks(BasicBlock *bb1, BasicBlock *bb2) 
{
    // 合并指令
    for (auto &inst : bb2->getInstructions()) 
    {
        bb1->addInstruction(std::move(inst));
    }
    bb2->getInstructions().clear();
    // 更新前驱和后继关系
    for (auto *succ : bb2->getSuccessors()) 
    {
        // 将bb2的后继转移到bb1
        bb1->addSuccessor(succ);
        // 更新后继的前驱为bb1
        succ->removePredecessor(bb2);
        succ->addPredecessor(bb1);
    }
    // 删除bb2
    delete bb2;
}
// ========== 循环不变代码移动 ==========
bool LoopInvariantCodeMotionPass::runOnFunction(Function *func) 
{
    bool changed = false;
    // 1. 查找所有循环
    auto loops = findLoops(func);
    for (auto &loop : loops) 
    {
        // 2. 找到循环的前置块（preheader）
        BasicBlock *preheader = findPreheader(loop);
        if (!preheader) continue;

        // 3. 收集所有循环不变指令
        vector<Instruction*> invariants;
        bool foundNew;
        do 
        {
            foundNew = false;
            for (auto *bb : loop.blocks) 
            {
                for (auto &instPtr : bb->getInstructions())
                {
                    Instruction *inst = instPtr.get();
                    // 只处理未被移动过的、无副作用、非终结指令
                    if (std::find(invariants.begin(), invariants.end(), inst) == invariants.end() &&
                        !inst->mayHaveSideEffects() && !inst->isTerminator() &&
                        isLoopInvariant(inst, loop)) 
                    {
                        invariants.push_back(inst);
                        foundNew = true;
                    }
                }
            }
        } while (foundNew); // 递增收集直到收敛

        // 4. 将循环不变指令移动到 preheader
        for (auto *inst : invariants) 
        {
            // 从原基本块移除
            auto &insts = inst->getParent()->getInstructions();
            auto it = std::find_if(insts.begin(), insts.end(),
                                   [&](const std::unique_ptr<Instruction>& ptr) { return ptr.get() == inst; });
            if (it != insts.end()) 
            {
                // 转移所有权到 preheader
                std::unique_ptr<Instruction> movedInst = std::move(*it);
                needToDelete.push_back(it->release()); // 记录需要删除的指令
                insts.erase(it);
                preheader->addInstruction(std::move(movedInst));
                changed = true;
            }
        }
    }
    return changed;
}

// 辅助：DFS遍历，记录访问顺序和父节点
void dfs(BasicBlock* bb, std::unordered_map<BasicBlock*, int>& dfn, vector<BasicBlock*>& order, int& idx) 
{
    dfn[bb] = idx++;
    order.push_back(bb);
    for (auto* succ : bb->getSuccessors()) 
    {
        if (!dfn.count(succ)) 
        {
            dfs(succ, dfn, order, idx);
        }
    }
}

// 查找所有自然循环（基于回边）
vector<LoopInvariantCodeMotionPass::Loop> LoopInvariantCodeMotionPass::findLoops(Function *func) 
{
    vector<Loop> loops;
    auto& bbs = func->getBasicBlocks();
    if (bbs.empty()) return loops;

    // 1. DFS遍历，记录访问顺序
    std::unordered_map<BasicBlock*, int> dfn;
    vector<BasicBlock*> order;
    int idx = 0;
    dfs(bbs[0].get(), dfn, order, idx);

    // 2. 查找回边（from->to, 且 dfn[to] <= dfn[from]）
    for (auto& bbPtr : bbs) 
    {
        BasicBlock* bb = bbPtr.get();
        for (auto* succ : bb->getSuccessors()) 
        {
            if (dfn.count(succ) && dfn[succ] <= dfn[bb]) 
            {
                // 发现回边，定义循环头为succ
                Loop loop;
                loop.header = succ;
                // 3. 收集循环体（所有能从bb逆向走到succ的块）
                std::unordered_set<BasicBlock*> loopBlocks;
                std::stack<BasicBlock*> stk;
                loopBlocks.insert(bb);
                stk.push(bb);
                while (!stk.empty()) 
                {
                    BasicBlock* cur = stk.top(); stk.pop();
                    for (auto* pred : cur->getPredecessors()) 
                    {
                        if (!loopBlocks.count(pred) && pred != succ) 
                        {
                            loopBlocks.insert(pred);
                            stk.push(pred);
                        }
                    }
                }
                loop.blocks.assign(loopBlocks.begin(), loopBlocks.end());
                loop.blocks.push_back(succ); // 加入循环头
                loops.push_back(loop);
            }
        }
    }
    return loops;
}

// 判断指令是否在循环不变
bool LoopInvariantCodeMotionPass::isLoopInvariant(Instruction *inst, const Loop &loop) 
{
    // 检查指令是否在循环中，并且不依赖于循环
    for (auto *op : inst->getOperands()) 
    {
        if (auto *def = dynamic_cast<Instruction *>(op)) 
        {
            // 如果操作数是循环中的变量，则不是循环不变
            if (loop.contains(def)) 
            {
                return false;
            }
        }
    }
    return true;
}
// 查找循环头的前驱块
BasicBlock *LoopInvariantCodeMotionPass::findPreheader(const Loop &loop) 
{
    // 查找循环头的前驱块，通常是循环的入口块
    for (auto *bb : loop.blocks) 
    {
        if (bb->getPredecessors().size() == 1) 
        {
            return bb->getPredecessors()[0];
        }
    }
    return nullptr; // 如果没有找到前驱块，返回nullptr
}

// phi消除
bool PhiEliminationPass::runOnFunction(Function *func) 
{
    bool changed = false;
    for (auto &bb : func->getBasicBlocks()) 
    {
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();) 
        {
            Instruction *inst = it->get();
            if (!inst) 
            {
                std::cerr << "inst is nullptr!" << std::endl;
                continue;
            }            
            auto *phi = dynamic_cast<PhiInst *>(inst);
            if (!phi) 
            {
                ++it;
                continue;
            }
            // 1. 只有一个输入，直接替换
            if (phi->getNumIncomingValues() == 1) 
            {
                Value *incomingValue = phi->getIncomingValue(0);
                phi->replaceAllUsesWith(incomingValue);
                needToDelete.push_back(it->release()); // 释放所有权，但不析构
                it = insts.erase(it);
                changed = true;
                continue;
            }
            // 2. 多输入phi，插入move/copy到前驱块末尾
            for (size_t i = 0; i < phi->getNumIncomingValues(); ++i) 
            {
                BasicBlock *pred = phi->getIncomingBlock(i);
                Value *val = phi->getIncomingValue(i);
                // 找到终结指令（如br/ret），插入到它前面
                auto &predInsts = pred->getInstructions();
                auto termIt = std::find_if(
                    predInsts.begin(), predInsts.end(),
                    [](const std::unique_ptr<Instruction>& inst) 
                    { 
                        return inst->isTerminator(); 
                    });
                // 在前驱块末尾插入: %phi = val
                // 这里假设有 createCopy/Move 指令工厂
                auto copy = std::make_unique<CopyInst>(phi, val, phi->getName()); // CopyInst: %phi = val
                pred->insert(std::move(copy), termIt - predInsts.begin());
            }
            //从基本块中删除原来指令，phi对应value仍然保留
            needToDelete.push_back(it->release()); // 释放所有权，但不析构
            it = insts.erase(it);
            changed = true;
        }
    }
    return changed;
}
// ========== 优化管道工厂 ==========
std::unique_ptr<PassManager> optimization::createOptimizationPipeline(OptimizationLevel level, bool verbose) 
{
    auto pm = std::make_unique<PassManager>(verbose);

    if (level == OptimizationLevel::O0) 
    {
        // 消除phi
        pm->addPass(std::make_unique<PhiEliminationPass>());
    } 
    else if (level == OptimizationLevel::O1) 
    {
        // 消除phi
        pm->addPass(std::make_unique<PhiEliminationPass>());        
        pm->addPass(std::make_unique<ConstantFoldingPass>());
        pm->addPass(std::make_unique<CopyPropagationPass>());
        pm->addPass(std::make_unique<DeadCodeEliminationPass>());
    } 
    else if (level == OptimizationLevel::O2) 
    {
        // 消除phi
        pm->addPass(std::make_unique<PhiEliminationPass>());        
        pm->addPass(std::make_unique<ConstantFoldingPass>());
        pm->addPass(std::make_unique<CopyPropagationPass>());
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>());
        pm->addPass(std::make_unique<DeadCodeEliminationPass>());
        pm->addPass(std::make_unique<BasicBlockMergePass>());
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>());
    }
    return pm;
}