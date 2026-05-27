#include "CSEPass.h"
using namespace std;
using namespace optimization;

namespace
{
static string operandKey(Value *op)
{
    if (auto *ci = dynamic_cast<ConstantInt *>(op))
        return "int:" + to_string(ci->Value);
    if (auto *cl = dynamic_cast<ConstantLong *>(op))
        return "long:" + to_string(cl->Value);
    if (auto *cf = dynamic_cast<ConstantFloat *>(op))
        return "float:" + to_string(cf->Value);
    return "var:" + op->getName();
}

// defBB 是 useBB 的唯一前驱，且 phi 定义在 defBB 内、位于 defInst 之前时，
// useBB 沿该边使用的 phi 值与 defBB 中 defInst 处相同（如 while.cond 算完再进 body）。
static bool phiOperandUnchangedOnSinglePredEdge(BasicBlock *defBB, BasicBlock *useBB,
                                                PhiInst *phi, Instruction *defInst)
{
    if (!defBB || !useBB || !phi || !defInst)
        return false;
    const auto &preds = useBB->getPredecessors();
    if (preds.size() != 1 || preds[0] != defBB)
        return false;
    const int phiOrder = defBB->getInstructionOrder(phi);
    const int defOrder = defBB->getInstructionOrder(defInst);
    return phiOrder >= 0 && defOrder >= 0 && phiOrder <= defOrder;
}
} // namespace

// ========== 公共子表达式消除 ==========
bool CommonSubexpressionEliminationPass::runOnFunction(Function *func)
{
    std::unordered_map<BasicBlock *, BasicBlock *> idom;
    idom = ControlFlowAnalysis::analyze(func);
    bool changed = false;
    bool localChanged;
    // 递归消除
    do
    {
        exprMap.clear();
        localChanged = false;
        for (auto &bb : func->getBasicBlocks())
        {
            auto &insts = bb->getInstructions();
            for (auto it = insts.begin(); it != insts.end();)
            {
                Instruction *inst = it->get();
                if (!canBeCommonSubexpression(inst, bb.get()))
                {
                    ++it;
                    continue;
                }
                auto key = getExpressionKey(inst);
                auto found = exprMap.find(key);
                if (found != exprMap.end())
                {
                    // 只有原表达式所在基本块支配当前基本块时才可消除
                    BasicBlock *defBB = found->second.second;
                    Instruction *defInst = found->second.first;
                    //  修改1
                    //  如果查到的load在本load之前的块则跳过(load仅支持同基本块消除)
                    //  或者如果操作数有load指令，则不消除
                    if (inst->getOpcode() == Opcode::Load)
                    {
                        // 如果不是同一个基本块，判断路径上是否有store指令
                        // 否则之间判断同一个基本块中间是否有store指令进行修改
                        if ((defBB != bb.get() && ControlFlowAnalysis::hasStoreOnPath(defBB, bb.get(), dynamic_cast<LoadInst *>(inst)->getOriginalPointer(), defInst, inst)) || (defBB == bb.get() && !CanLoadCSE(inst, found->second.first, bb.get())))
                        //if(defBB != bb.get()||!CanLoadCSE(inst, found->second.first, bb.get()))
                        {
                            // 如果不能消除，则更新exprMap
                            exprMap[key] = {inst, bb.get()};
                            ++it;
                            continue;
                        }
                    }
                    // 修改2
                    // 判断表达式操作数是否有load，如果有load，且defBB!=bb，则不消除
                    // 此时表示该load指令的地址有可能被跨块修改，保守起见不进行消除
                    bool CanNotCSEWithLoadOrPhiOperand = false;
                    for (auto *op : inst->getOperands())
                    {
                        if (auto *loadInst = dynamic_cast<LoadInst *>(op))
                        {
                            if (defBB != bb.get() && ControlFlowAnalysis::hasStoreOnPath(defBB, bb.get(), loadInst->getOriginalPointer(), defInst, inst))
                            //if(defBB != bb.get())
                            {
                                CanNotCSEWithLoadOrPhiOperand = true;
                                break;
                            }
                            else if(defBB == bb.get())
                            {

                                // 判断是否有store对该地址进行修改
                                Value *addr = recognizeMode == 0 ? loadInst->getOriginalPointer() : loadInst->getPointer();
                                int pos1 = bb->getInstructionOrder(defInst);
                                int pos2 = bb->getInstructionOrder(inst);
                                // 检查两条指令之间是否有store指令修改了地址
                                if (pos1 > pos2)
                                {
                                    // 如果defInst在inst之后，则不消除
                                    CanNotCSEWithLoadOrPhiOperand = true;
                                    break;
                                }
                                auto &insts = bb->getInstructions();
                                for (int i = pos1 + 1; i < pos2; i++)
                                {
                                    if (auto *storeInst = dynamic_cast<StoreInst *>(insts[i].get()))
                                    {
                                        Value *storeAddr = recognizeMode == 0 ? storeInst->getOriginalPointer() : storeInst->getPointer();
                                        if (isSameAddr(storeAddr, addr))
                                        {
                                            CanNotCSEWithLoadOrPhiOperand = true;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                        // 跨块且实参为 phi：默认保守；唯一前驱边且 phi 在 defBB 头部则值未变
                        else if (auto *phiInst = dynamic_cast<PhiInst *>(op))
                        {
                            if (defBB != bb.get())
                            {
                                const bool unchangedOnEdge = phiOperandUnchangedOnSinglePredEdge(
                                    defBB, bb.get(), phiInst, defInst);
                                if (!unchangedOnEdge &&
                                    ControlFlowAnalysis::hasPhiInputOnPath(
                                        defBB, bb.get(), phiInst, defInst, inst))
                                {
                                    CanNotCSEWithLoadOrPhiOperand = true;
                                    break;
                                }
                            }
                        }
                    }
                    if (CanNotCSEWithLoadOrPhiOperand)
                    {
                        // 不过可以更新exprMap，为后续可能的消除做准备
                        exprMap[key] = {inst, bb.get()};
                        ++it;
                        continue;
                    }
                    // 进入消除过程
                    if (defBB == bb.get() || ControlFlowAnalysis::dominates(idom, defBB, bb.get()))
                    {
                        inst->replaceAllUsesWith(found->second.first);
                        if (verbose)
                        {
                            debugInfo << inst->toString() << " replaced with "
                                      << found->second.first->toString() << " in "
                                      << bb->getName() << " in func " << func->getName() << endl;
                        }
                        // 从操作数的user列表中移除自己
                        inst->removeThisFromOperands();
                        needToDelete.push_back(it->release());
                        it = insts.erase(it);
                        localChanged = true;
                        changed = true;
                        continue;
                    }
                }
                exprMap[key] = {inst, bb.get()};
                ++it;
                
            }
        }
    } while (localChanged);
    return changed;
}
// 为每个指令生成唯一的表达式键
std::pair<std::string, std::vector<std::string>> CommonSubexpressionEliminationPass::getExpressionKey(Instruction *inst)
{
    std::vector<std::string> ops;
    // 如果是getelementptr指令，特殊处理，补的0不需要添加
    if (auto *gep = dynamic_cast<GetElementPtrInst *>(inst))
    {
        int usefulOperandCount = static_cast<int>(gep->getNumOperands()) - std::max(0, gep->num_addedzero);
        for (int i = 0; i < usefulOperandCount; i++)
            ops.push_back(operandKey(gep->getOperandByIndex(i)));
        return {inst->getOpcodeName(), ops};
    }
    for (auto *op : inst->getOperands())
        ops.push_back(operandKey(op));
    return {inst->getOpcodeName(), ops};
}

// 判断指令是否可以作为公共子表达式
bool CommonSubexpressionEliminationPass::canBeCommonSubexpression(Instruction *inst, BasicBlock *bb)
{
    // 处理无副作用的二元运算、getelementptr、load以及无副作用的call
    // 不包括Store Ret Br
    return (inst->isBinaryOp() ||
            inst->getOpcode() == Opcode::PackI64 ||
            inst->getOpcode() == Opcode::GetElementPtr ||
            inst->getOpcode() == Opcode::Load ||
            // inst->getOpcode() == Opcode::FPToSI || inst->getOpcode() == Opcode::SIToFP ||
            // inst->getOpcode() == Opcode::BitCast || inst->getOpcode() == Opcode::Sext || inst->getOpcode() == Opcode::Trunc ||
            // inst->getOpcode() == Opcode::ICmp || inst->getOpcode() == Opcode::FCmp ||
            (inst->getOpcode() == Opcode::Call && !dynamic_cast<CallInst *>(inst)->ifHasSideEffects()));
}

bool CommonSubexpressionEliminationPass::CanLoadCSE(Instruction *inst, Instruction *map_inst, BasicBlock *bb)
{
    // 允许同一基本块内的load做CSE，且store和load之间没有其他store
    // 跨基本块在外部判断
    auto *loadInst = dynamic_cast<LoadInst *>(inst);
    auto *mapLoadInst = dynamic_cast<LoadInst *>(map_inst);
    if (!loadInst || !mapLoadInst)
        return false;
    Value *addr = recognizeMode == 0 ? loadInst->getOriginalPointer() : loadInst->getPointer();
    if (!addr)
        return false;
    int pos1 = bb->getInstructionOrder(map_inst);
    int pos2 = bb->getInstructionOrder(inst);
    // error:没找到指令
    if (pos1 == -1 || pos2 == -1 || pos1 >= pos2)
        return false; // map_inst必须在inst之前
    auto &insts = bb->getInstructions();
    // 检查load之前是否有store，load之后不能有store
    for (int i = pos1 + 1; i < pos2; ++i)
    {
        if (auto *store = dynamic_cast<StoreInst *>(insts[i].get()))
        {
            Value *storeAddr = recognizeMode == 0 ? store->getOriginalPointer() : store->getPointer();
            if (isSameAddr(storeAddr, addr))
            {
                return false; // 两条load之间有store，不能CSE
            }
        }
    }
    // 没有store，可以CSE
    return true;
}

// 哈希函数，用于表达式键的哈希表
std::size_t CommonSubexpressionEliminationPass::ExpressionHash::operator()(const std::pair<std::string, std::vector<std::string>> &expr) const
{
    std::size_t h = std::hash<std::string>()(expr.first);
    for (const auto &s : expr.second)
        h ^= std::hash<std::string>()(s) + 0x9e3779b9 + (h << 6) + (h >> 2);
    return h;
}
