#include "OptimizationPasses.h"
#include <iostream>
#include <stack>
#include <set>
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
    return  inst->mayHaveSideEffects();
}
// ========== 公共子表达式消除 ==========
bool CommonSubexpressionEliminationPass::runOnFunction(Function *func) 
{
    bool changed = false;
    bool localChanged;
    // 递归消除
    do {
        exprMap.clear();
        localChanged = false;
        for (auto &bb : func->getBasicBlocks()) 
        {
            auto &insts = bb->getInstructions();
            for (auto it = insts.begin(); it != insts.end(); ) 
            {
                Instruction *inst = it->get();
                if (!canBeCommonSubexpression(inst)) { ++it; continue; }
                auto key = getExpressionKey(inst);
                auto found = exprMap.find(key);
                if (found != exprMap.end()) 
                {
                    inst->replaceAllUsesWith(found->second);
                    needToDelete.push_back(it->release());
                    it = insts.erase(it);
                    localChanged = true;
                    changed = true;
                } 
                else 
                {
                    exprMap[key] = inst;
                    ++it;
                }
            }
        }
    } while (localChanged);
    return changed;
}
// 为每个指令生成唯一的表达式键
std::pair<std::string, std::vector<std::string>> CommonSubexpressionEliminationPass::getExpressionKey(Instruction *inst) 
{
    std::vector<std::string> ops;
    for (auto *v : inst->getOperands()) {
        if (auto *ci = dynamic_cast<ConstantInt*>(v)) {
            ops.push_back("int:" + std::to_string(ci->Value));
        } else if (auto *cf = dynamic_cast<ConstantFloat*>(v)) {
            ops.push_back("float:" + std::to_string(cf->Value));
        } else {
            ops.push_back("var:" + v->getName());
        }
    }
    return {inst->getOpcodeName(), ops};
}
// 判断指令是否可以作为公共子表达式
bool CommonSubexpressionEliminationPass::canBeCommonSubexpression(Instruction *inst) 
{
    // 只处理无副作用的二元运算,不包括Store Call Ret Br
    return inst->isBinaryOp() && !inst->mayHaveSideEffects();
}
// 哈希函数，用于表达式键的哈希表
std::size_t CommonSubexpressionEliminationPass::ExpressionHash::operator()(const std::pair<std::string, std::vector<std::string>> &expr) const
{
    std::size_t h = std::hash<std::string>()(expr.first);
    for (const auto &s : expr.second)
        h ^= std::hash<std::string>()(s) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
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
    //记录每一轮pass后是否有外提变量，有则继续运行直到所有能外提变量全部外提
    bool localChanged;
    do {
        int count = 0;
        localChanged = false;
        // 1. 查找所有循环
        auto loops = findLoops(func);
        for (auto &loop : loops) 
        {
            // 2. 找到循环的前置块（preheader）
            BasicBlock *preheader = findPreheader(loop);
            if (!preheader) continue;

            // 3. 收集所有循环不变指令（记录指令和所在基本块）
            std::vector<std::pair<Instruction*, BasicBlock*>> invariants;
            bool foundNew;
            do 
            {
                foundNew = false;
                for (auto *bb : loop.blocks) 
                {
                    for (auto &instPtr : bb->getInstructions())
                    {
                        Instruction *inst = instPtr.get();
                        if (std::find_if(invariants.begin(), invariants.end(),
                                         [inst](const auto& p){ return p.first == inst; }) == invariants.end() &&
                            !inst->mayHaveSideEffects() && !inst->isTerminator() &&
                            isLoopInvariant(inst, loop)) 
                        {
                            invariants.emplace_back(inst, bb);
                            foundNew = true;
                        }
                    }
                }
            } while (foundNew); // 递增收集直到收敛

            // 4. 将循环不变指令移动到 preheader
            for (auto &[inst, fromBB] : invariants) 
            {
                auto &insts = fromBB->getInstructions();
                auto it = std::find_if(insts.begin(), insts.end(),
                                       [&](const std::unique_ptr<Instruction>& ptr) { return ptr.get() == inst; });
                if (it != insts.end()) 
                {
                    std::unique_ptr<Instruction> movedInst = std::move(*it);
                    it = insts.erase(it);
                    // 将指令插入到 preheader 的末尾(终结指令之前)
                    preheader->insertBeforeTerminator(std::move(movedInst));
                    localChanged = true;
                    changed = true;
                    count++;
                }
            }
        }
        cout<<"Removed Instructions: "<<count<<endl;
    } while (localChanged);
    return changed;
}

// 辅助：DFS遍历，记录访问顺序和父节点
void dfs(BasicBlock* bb, std::unordered_map<BasicBlock*, int>& dfn, vector<BasicBlock*>& order, int& idx,
         std::unordered_map<BasicBlock*, int>& inStack, std::vector<std::pair<BasicBlock*, BasicBlock*>>& backedges)
{
    dfn[bb] = idx++;
    inStack[bb] = 1;
    order.push_back(bb);
    for (auto* succ : bb->getSuccessors()) 
    {
        if (!dfn.count(succ)) 
        {
            dfs(succ, dfn, order, idx, inStack, backedges);
        }
        else if (inStack[succ]) // succ在递归栈上，说明是回边
        {
            backedges.push_back({bb, succ});
        }
    }
    inStack[bb] = 0;
}

// 查找所有自然循环（基于回边）
vector<LoopInvariantCodeMotionPass::Loop> LoopInvariantCodeMotionPass::findLoops(Function *func) 
{
    vector<Loop> loops;
    auto& bbs = func->getBasicBlocks();
    if (bbs.empty()) return loops;

    // 1. DFS遍历，记录访问顺序和回边
    std::unordered_map<BasicBlock*, int> dfn, inStack;
    vector<BasicBlock*> order;
    int idx = 0;
    std::vector<std::pair<BasicBlock*, BasicBlock*>> backedges;
    dfs(bbs[0].get(), dfn, order, idx, inStack, backedges);

    // 2. 按循环头分组所有回边
    std::unordered_map<BasicBlock*, std::vector<BasicBlock*>> headerToBackedges;
    for (auto& [from, to] : backedges) {
        headerToBackedges[to].push_back(from);
    }

    // 3. 对每个循环头，合并所有回边，收集完整循环体
    for (auto& [header, backedges] : headerToBackedges)
    {
        std::unordered_set<BasicBlock*> loopBlocks;
        std::stack<BasicBlock*> stk;
        loopBlocks.insert(header);
        for (auto* from : backedges)
        {
            if (loopBlocks.insert(from).second)
                stk.push(from);
        }
        while (!stk.empty())
        {
            BasicBlock* cur = stk.top(); stk.pop();
            for (auto* pred : cur->getPredecessors())
            {
                if (!loopBlocks.count(pred))
                {
                    loopBlocks.insert(pred);
                    stk.push(pred);
                }
            }
        }
        // 关键：循环头必须有前驱在循环体外，才是真正的循环头
        bool hasOutsidePred = false;
        for (auto* pred : header->getPredecessors()) {
            if (loopBlocks.find(pred) == loopBlocks.end()) {
                hasOutsidePred = true;
                break;
            }
        }
        if (!hasOutsidePred) continue; // 跳过伪循环头

        // 去重
        bool duplicate = false;
        for (auto &l : loops) {
            std::set<BasicBlock*> s1(loopBlocks.begin(), loopBlocks.end());
            std::set<BasicBlock*> s2(l.blocks.begin(), l.blocks.end());
            if (l.header == header && s1 == s2) {
                duplicate = true;
                break;
            }
        }
        if (!duplicate) {
            Loop loop;
            loop.header = header;
            loop.blocks.assign(loopBlocks.begin(), loopBlocks.end());
            loops.push_back(loop);
        }
    }
    // 调试输出
    for(auto &loop : loops) 
    {
        cout << "Loop Header: " << loop.header->getName() << ", Blocks: ";
        for (auto *bb : loop.blocks)
            cout << bb->getName() << " ";
        cout << endl;
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
    BasicBlock *header = loop.header;
    BasicBlock *preheader = nullptr;
    int count = 0;
    for (auto *pred : header->getPredecessors()) {
        // preheader 必须不在循环体内
        if (std::find(loop.blocks.begin(), loop.blocks.end(), pred) == loop.blocks.end()) {
            preheader = pred;
            ++count;
        }
    }
    // 必须只有一个循环外前驱才安全
    if (count == 1)
        return preheader;
    return nullptr;
}
// 函数内联
bool FunctionInliningPass::runOnFunction(Function *caller) {
    bool changed = false;
    for (auto &bb : caller->getBasicBlocks()) {
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end(); ) {
            if (auto *call = dynamic_cast<CallInst*>(it->get())) {
                Function *callee = call->getCalledFunction();
                if (shouldInline(callee)) {
                    inlineAt(call, caller, bb.get(), it);
                    it = insts.erase(it); // 删除call指令
                    changed = true;
                    continue;
                }
            }
            ++it;
        }
    }
    return changed;
}
// 判断是否适合内联
bool FunctionInliningPass::shouldInline(Function *callee) {
    // 不内联递归/库函数/过大函数
    if (callee->isLibraryFunction() || callee->isRecursive() || callee->getInstructionCount()>20)
        return false;
    return true;
}
// 内联实现(目前还有问题)
void FunctionInliningPass::inlineAt(CallInst *call, Function *caller, BasicBlock *bb, std::vector<std::unique_ptr<Instruction>>::iterator it) {
    Function *callee = call->getCalledFunction();
    // 1. 参数映射
    std::unordered_map<Value*, Value*> valueMap;
    auto &params = callee->getArguments();
    const auto &args = call->getArguments();
    for (size_t i = 0; i < params.size(); ++i) {
        valueMap[params[i].get()] = args[i];
    }
    // 2. 复制被调函数的所有指令，重命名变量
    std::vector<std::unique_ptr<Instruction>> newInsts;
    for (auto &bbCallee : callee->getBasicBlocks()) {
        for (auto &instCallee : bbCallee->getInstructions()) {
            Instruction *newInst = instCallee->cloneWithRename(valueMap); // 你需要实现cloneWithRename
            // 替换操作数为映射后的
            for (size_t i = 0; i < newInst->getOperands().size(); ++i) {
                if (valueMap.count(newInst->getOperands()[i]))
                    newInst->setOperand(i, valueMap[newInst->getOperands()[i]]);
            }
            valueMap[instCallee.get()] = newInst;
            newInsts.push_back(std::unique_ptr<Instruction>(newInst));
        }
    }
    // 3. 处理返回值
    for (auto &inst : newInsts) {
        if (auto *ret = dynamic_cast<ReturnInst*>(inst.get())) {
            if (call->hasReturnValue()) {
                // 将ret的返回值赋给call的目标
                auto assign = std::make_unique<CopyInst>(ret->getReturnValue(), call->getName());
                bb->insert(std::move(assign), it - bb->getInstructions().begin());
            }
            // 移除ret指令本身
            inst.reset();
        }
    }
    // 4. 插入新指令到调用点
    for (auto &inst : newInsts) {
        if (inst) bb->insert(std::move(inst), it - bb->getInstructions().begin());
    }
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
                needToDelete.push_back(it->release()); 
                it = insts.erase(it);
                changed = true;
                continue;
            }
            vector<Value*>operands;
            // 2. 多输入phi，插入move/copy到前驱块末尾
            for (size_t i = 0; i < phi->getNumIncomingValues(); ++i) 
            {
                BasicBlock *pred = phi->getIncomingBlock(i);
                Value *val = phi->getIncomingValue(i);
                // 在终结指令前插入
                pred->insertBeforeTerminator(std::make_unique<CopyInst>(val, phi->getName()));
            }
            // 从基本块中删除原来指令，phi对应value仍然保留
            needToDelete.push_back(it->release());
            it = insts.erase(it);
            changed = true;
        }
    }
    // 遍历所有基本块的所有指令，如果替换后的copy指令源操作数名字与目的操作数名字相同则删除
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
            if (auto *copy = dynamic_cast<CopyInst *>(inst)) 
            {
                if (copy->getSource()->getName() == copy->getDest()->getName()) 
                {
                    // 删除无效的copy指令
                    needToDelete.push_back(it->release());
                    it = insts.erase(it);
                    changed = true;
                    continue;
                }
            }
            ++it;
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
        pm->addPass(std::make_unique<DeadCodeEliminationPass>());
        // 消除phi
        pm->addPass(std::make_unique<PhiEliminationPass>());        

    } 
    else if (level == OptimizationLevel::O2) 
    {
        pm->addPass(std::make_unique<DeadCodeEliminationPass>());
        pm->addPass(std::make_unique<PhiEliminationPass>());        
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>());
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>());
    }
    //以下为调试内容
    else if(level==OptimizationLevel::O10)
    {
        pm->addPass(std::make_unique<DeadCodeEliminationPass>());
    }
    else if(level==OptimizationLevel::O11)
    {
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>());
    }
    else if(level==OptimizationLevel::O12)
    {
        //必须要先消除phi才能进行循环不变量外提
        pm->addPass(std::make_unique<DeadCodeEliminationPass>());
        pm->addPass(std::make_unique<PhiEliminationPass>());
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>());
    }
    else if(level==OptimizationLevel::O13)
    {
        pm->addPass(std::make_unique<FunctionInliningPass>());
    }
    return pm;
}