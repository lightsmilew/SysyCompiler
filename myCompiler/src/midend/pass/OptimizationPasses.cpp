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
    for (auto &func : module->Functions) {
        for (auto &pass : passes) {
            if (verbose)
                std::cout << "[Optimization] Running " << pass->getName()
                          << " on function " << func->getName() << std::endl;
            changed |= pass->runOnFunction(func.get());
        }
    }
    return changed;
}

// ========== 死代码消除 ==========
bool DeadCodeEliminationPass::runOnFunction(Function *func) {
    std::unordered_set<Instruction *> liveInsts;
    markLiveInstructions(func, liveInsts);

    bool changed = false;
    for (auto &bb : func->getBasicBlocks()) {
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end();) {
            Instruction *inst = it->get();
            if (liveInsts.count(inst) == 0) {
                it = insts.erase(it);
                changed = true;
            } else {
                ++it;
            }
        }
    }
    return changed;
}

// 标记所有关键指令和其依赖为活跃
void DeadCodeEliminationPass::markLiveInstructions(Function *func, std::unordered_set<Instruction *> &liveInsts) {
    std::stack<Instruction *> worklist;
    // 1. 标记终结指令、store、call等有副作用的指令为活跃
    for (auto &bb : func->getBasicBlocks()) {
        for (auto &instPtr : bb->getInstructions()) {
            Instruction *inst = instPtr.get();
            if (isInstructionCritical(inst)) {
                liveInsts.insert(inst);
                worklist.push(inst);
            }
        }
    }
    // 2. 反向传播依赖
    while (!worklist.empty()) {
        Instruction *inst = worklist.top();
        worklist.pop();
        for (auto *op : inst->getOperands()) {
            if (auto *def = dynamic_cast<Instruction *>(op)) {
                if (liveInsts.insert(def).second) {
                    worklist.push(def);
                }
            }
        }
    }
}

// 判断指令是否为关键指令
bool DeadCodeEliminationPass::isInstructionCritical(Instruction *inst) {
    return inst->isTerminator() || inst->mayHaveSideEffects();
}

// ========== 常量折叠 ==========
bool ConstantFoldingPass::runOnFunction(Function *func) {
    bool changed = false;
    for (auto &bb : func->getBasicBlocks()) {
        auto &insts = bb->getInstructions();
        for (auto it = insts.begin(); it != insts.end(); ++it) {
            Instruction *inst = it->get();
            // 二元运算常量折叠
            if (auto *binOp = dynamic_cast<BinaryOperator *>(inst)) {
                if (isConstant(binOp->getOperand(0)) && isConstant(binOp->getOperand(1))) {
                    Value *folded = foldBinaryOperation(binOp);
                    if (folded) {
                        inst->replaceAllUsesWith(folded);
                        it = insts.erase(it);
                        changed = true;
                        if (it == insts.end()) break;
                    }
                }
            }
            // 比较指令常量折叠
            else if (auto *cmp = dynamic_cast<ICmpInst *>(inst)) {
                if (isConstant(cmp->getOperand(0)) && isConstant(cmp->getOperand(1))) {
                    Value *folded = foldComparison(cmp);
                    if (folded) {
                        inst->replaceAllUsesWith(folded);
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
Value *ConstantFoldingPass::foldBinaryOperation(BinaryOperator *binOp) {
    bool isFloat = binOp->getType()->isFloatTy();
    if (isFloat) 
    {
        // 处理浮点数的常量折叠
        auto *lhs = dynamic_cast<ConstantFloat *>(binOp->getOperand(0));
        auto *rhs = dynamic_cast<ConstantFloat *>(binOp->getOperand(1));
        if (!lhs || !rhs) return nullptr;
        float l = lhs->Value, r = rhs->Value;
        float res = 0.0f;
        switch (binOp->getOpcode()) {
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
    auto *lhs = dynamic_cast<ConstantInt *>(binOp->getOperand(0));
    auto *rhs = dynamic_cast<ConstantInt *>(binOp->getOperand(1));
    if (!lhs || !rhs) return nullptr;
    int l = lhs->Value, r = rhs->Value;
    int res = 0;
    switch (binOp->getOpcode()) {
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
Value *ConstantFoldingPass::foldComparison(ICmpInst *cmpInst) {
    bool isFloat = cmpInst->getType()->isFloatTy();
    if (isFloat) {
        // 处理浮点数的比较
        auto *lhs = dynamic_cast<ConstantFloat *>(cmpInst->getOperand(0));
        auto *rhs = dynamic_cast<ConstantFloat *>(cmpInst->getOperand(1));
        if (!lhs || !rhs) return nullptr;
        float l = lhs->Value, r = rhs->Value;
        bool res = false;
        switch (cmpInst->getPredicate()) {
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
    auto *lhs = dynamic_cast<ConstantInt *>(cmpInst->getOperand(0));
    auto *rhs = dynamic_cast<ConstantInt *>(cmpInst->getOperand(1));
    if (!lhs || !rhs) return nullptr;
    int l = lhs->Value, r = rhs->Value;
    bool res = false;
    switch (cmpInst->getPredicate()) {
        case ICmpInst::Predicate::ICMP_EQ: res = (l == r); break;
        case ICmpInst::Predicate::ICMP_NE: res = (l != r); break;
        case ICmpInst::Predicate::ICMP_SLT: res = (l < r); break;
        case ICmpInst::Predicate::ICMP_SLE: res = (l <= r); break;
        case ICmpInst::Predicate::ICMP_SGT: res = (l > r); break;
        case ICmpInst::Predicate::ICMP_SGE: res = (l >= r); break;
        default: return nullptr;
    }
    return new ConstantInt(IntegerType::getInstance(), res ? 1 : 0);
}

bool ConstantFoldingPass::isConstant(Value *val) {
    return dynamic_cast<ConstantInt *>(val) != nullptr;
}
int ConstantFoldingPass::getConstantValue(Value *val) {
    if (auto *c = dynamic_cast<ConstantInt *>(val)) return c->Value;
    return 0;
}

// ========== 公共子表达式消除 ==========
bool CommonSubexpressionEliminationPass::runOnFunction(Function *func) {
    exprMap.clear();
    bool changed = false;
    for (auto &bb : func->getBasicBlocks()) {
        for (auto &instPtr : bb->getInstructions()) {
            Instruction *inst = instPtr.get();
            if (!canBeCommonSubexpression(inst)) continue;
            auto key = getExpressionKey(inst);
            auto it = exprMap.find(key);
            if (it != exprMap.end()) {
                inst->replaceAllUsesWith(it->second);
                changed = true;
            } else {
                exprMap[key] = inst;
            }
        }
    }
    return changed;
}
// 为每个指令生成唯一的表达式键
std::pair<std::string, std::vector<Value *>> CommonSubexpressionEliminationPass::getExpressionKey(Instruction *inst) {
    std::vector<Value *> ops = inst->getOperands();
    return {inst->getOpcodeName(), ops};
}
// 判断指令是否可以作为公共子表达式
bool CommonSubexpressionEliminationPass::canBeCommonSubexpression(Instruction *inst) {
    // 只处理无副作用的二元运算,不包括Store Call Ret Br
    return inst->isBinaryOp() && !inst->mayHaveSideEffects();
}
// 哈希函数，用于表达式键的哈希表
std::size_t CommonSubexpressionEliminationPass::ExpressionHash::operator()(const std::pair<std::string, std::vector<Value *>> &expr) const {
    std::size_t h = std::hash<std::string>()(expr.first);
    for (auto *v : expr.second) h ^= std::hash<void *>()(v);
    return h;
}

// ========== 复制传播 ==========
bool CopyPropagationPass::runOnFunction(Function *func) {
    copyMap.clear();
    collectCopies(func);
    bool changed = false;
    for (auto &bb : func->getBasicBlocks()) {
        for (auto &instPtr : bb->getInstructions()) {
            Instruction *inst = instPtr.get();
            for (size_t i = 0; i < inst->getNumOperands(); ++i) {
                Value *op = inst->getOperand(i);
                Value *newOp = followCopyChain(op);
                if (newOp != op) {
                    inst->setOperand(i, newOp);
                    changed = true;
                }
            }
        }
    }
    return changed;
}
// 收集所有的mov/copy指令
void CopyPropagationPass::collectCopies(Function *func) {
    for (auto &bb : func->getBasicBlocks()) {
        for (auto &instPtr : bb->getInstructions()) {
            Instruction *inst = instPtr.get();
            // 假设mov/copy指令为: %a = %b
            if (inst->isCopy()) {
                copyMap[inst] = inst->getOperand(0);
            }
        }
    }
}
// 跟踪复制链，直到找到最终的值
Value *CopyPropagationPass::followCopyChain(Value *val) {
    while (copyMap.count(val)) val = copyMap[val];
    return val;
}

// ========== 基本块合并 ==========
bool BasicBlockMergePass::runOnFunction(Function *func) {
    bool changed = false;
    auto &bbs = func->getBasicBlocks();
    for (size_t i = 0; i + 1 < bbs.size(); ++i) {
        BasicBlock *bb1 = bbs[i].get();
        BasicBlock *bb2 = bbs[i + 1].get();
        if (canMergeBlocks(bb1, bb2)) {
            mergeBlocks(bb1, bb2);
            changed = true;
        }
    }
    return changed;
}
bool BasicBlockMergePass::canMergeBlocks(BasicBlock *bb1, BasicBlock *bb2) {
    // bb1 只有一个后继且是bb2，bb2只有一个前驱且是bb1
    return bb1->getSuccessors().size() == 1 && bb1->getSuccessors()[0] == bb2 &&
           bb2->getPredecessors().size() == 1 && bb2->getPredecessors()[0] == bb1;
}
void BasicBlockMergePass::mergeBlocks(BasicBlock *bb1, BasicBlock *bb2) {
    // 合并指令
    for (auto &inst : bb2->getInstructions()) {
        bb1->addInstruction(std::move(inst));
    }
    bb2->getInstructions().clear();
    // 更新控制流图（可根据你的IR结构完善）
}

// ========== mem2reg（内存提升到SSA）Pass ==========
bool Mem2RegPass::runOnFunction(Function *func) {
    // 这里只给出思路伪代码，实际实现需结合你的IR结构
    // 1. 找出所有可提升的alloca
    // 2. 分析store/load，插入phi
    // 3. 重命名变量，替换load/store
    // 4. 删除alloca/store/load
    // 详见LLVM PromoteMemoryToRegister.cpp
    return false;
}

// ========== SSA构造/phi消除等可类似实现 ==========

// ========== 优化管道工厂 ==========
std::unique_ptr<PassManager> optimization::createOptimizationPipeline(OptimizationLevel level, bool verbose) {
    auto pm = std::make_unique<PassManager>(verbose);
    if (level == OptimizationLevel::O0) {
        // 无优化
    } else if (level == OptimizationLevel::O1) {
        pm->addPass(std::make_unique<ConstantFoldingPass>());
        pm->addPass(std::make_unique<CopyPropagationPass>());
        pm->addPass(std::make_unique<DeadCodeEliminationPass>());
    } else if (level == OptimizationLevel::O2) {
        pm->addPass(std::make_unique<ConstantFoldingPass>());
        pm->addPass(std::make_unique<CopyPropagationPass>());
        pm->addPass(std::make_unique<CommonSubexpressionEliminationPass>());
        pm->addPass(std::make_unique<DeadCodeEliminationPass>());
        pm->addPass(std::make_unique<BasicBlockMergePass>());
        pm->addPass(std::make_unique<LoopInvariantCodeMotionPass>());
        pm->addPass(std::make_unique<Mem2RegPass>());
        // 可继续添加SSA构造、phi消除等
    }
    return pm;
}