#include "LoopVectorizePass.h"
#include "ControlFlowAnalysis.h"
#include "../../common/CompilerConfig.h"
#include <algorithm>
#include <functional>
#include <map>
#include <set>

using namespace std;
using namespace optimization;
using namespace matrixStructure;

namespace
{
    static ConstantInt *ci(int v)
    {
        return new ConstantInt(IntegerType::getInstance(), v);
    }

    static unique_ptr<Instruction> own(Instruction *inst)
    {
        return unique_ptr<Instruction>(inst);
    }

    static void wireEdge(BasicBlock *from, BasicBlock *to)
    {
        from->addSuccessor(to);
        to->addPredecessor(from);
    }

    // 在终结指令前插入指令
    static void prependBeforeTerminator(BasicBlock *bb, Instruction *inst)
    {
        auto &insts = bb->getInstructions();
        insts.insert(insts.end() - 1, unique_ptr<Instruction>(inst));
    }

    // 把 bb 的终结指令替换为无条件 br target
    static void replaceTerminatorWithBr(BasicBlock *bb, BasicBlock *target)
    {
        auto &insts = bb->getInstructions();
        if (!insts.empty() && insts.back()->Op == Opcode::Br)
        {
            insts.back()->removeThisFromOperands();
            insts.pop_back();
        }
        insts.push_back(unique_ptr<Instruction>(new BranchInst(target)));
    }

    // 判断 entry 是否为 j-loop 唯一入口：终结指令无条件跳 jHeader，
    // 或条件分支的一个目标为 jHeader（skip-guard 情形）
    static bool isValidVectorEntry(BasicBlock *entry, BasicBlock *jHeader)
    {
        auto *br = dynamic_cast<BranchInst *>(entry->getTerminator());
        if (!br)
            return false;
        if (!br->isConditional())
            return br->getTrueBlock() == jHeader;
        return br->getTrueBlock() == jHeader || br->getFalseBlock() == jHeader;
    }

    // 把 entry → oldHeader 边改为 entry → newBody，保留 entry 其余出边
    static bool retargetEntryEdge(BasicBlock *entry, BasicBlock *oldHeader, BasicBlock *newBody)
    {
        auto *br = dynamic_cast<BranchInst *>(entry->getTerminator());
        if (!br)
            return false;
        if (!br->isConditional())
        {
            if (br->getTrueBlock() != oldHeader)
                return false;
            replaceTerminatorWithBr(entry, newBody);
        }
        else
        {
            if (br->getTrueBlock() == oldHeader)
                br->setTrueBlock(newBody);
            else if (br->getFalseBlock() == oldHeader)
                br->setFalseBlock(newBody);
            else
                return false;
        }
        entry->removeSuccessor(oldHeader);
        oldHeader->removePredecessor(entry);
        wireEdge(entry, newBody);
        return true;
    }

    // 断开并移除给定基本块（从函数块列表删除）
    static void removeLoopBlocks(Function *func, const vector<BasicBlock *> &blocks)
    {
        for (auto *bb : blocks)
        {
            if (!bb)
                continue;
            // 主动清理 use 列表：Instruction 析构不自动 removeThisFromOperands，
            // 若留下悬空 user，后续 feedsInductionVar 等遍历 getUsers() 会崩溃。
            for (auto &instPtr : bb->getInstructions())
            {
                instPtr->removeThisFromOperands();
            }
            bb->removeSelfBasicBlock();
        }
        auto &bbs = func->getBasicBlocks();
        bbs.erase(remove_if(bbs.begin(), bbs.end(),
                            [&](const unique_ptr<BasicBlock> &p)
                            {
                                return find(blocks.begin(), blocks.end(), p.get()) != blocks.end();
                            }),
                  bbs.end());
    }

    static int freshId = 0;

    static string freshName(const string &prefix)
    {
        return "rvv_" + prefix + "_" + to_string(freshId++);
    }


// 在 entry（原 j 循环 preheader）与 exitBlock（原 j 循环 exit）之间插入
// strip-mining 向量循环。
//
//   body:
//     count   = phi [count0, entry], [countN, body]
//     ptr_i   = phi [ptr_i0, entry], [ptr_iN, body]
//     vl      = vecsetvl count, 32
//     ... emitBody(body, vl, ptrs) ...
//     countN  = sub count, vl
//     ptr_iN  = gep ptr_i, vl
//     cond    = icmp ne countN, 0
//     br cond, body, exit
//   exit:
//     br exitBlock
//
// 原 j 循环的 header/latch 被移除；exitBlock 的 phi 中原由 oldHeader 提供的
// incoming 改由 exit 提供（jIV 相关 phi 的值改为 bound）。
static void buildVectorLoop(
    Function *func,
    BasicBlock *entry,
    BasicBlock *oldHeader,
    BasicBlock *exitBlock,
    Value *count0,
    Value *jIV,
    Value *bound,
    const vector<Value *> &ptr0s,
    const vector<string> &ptrNames,
    const string &bodyName,
    const string &exitName,
    const function<void(BasicBlock *, Value *, const vector<Value *> &)> &emitBody)
{
    auto &bbs = func->getBasicBlocks();
    auto body = new BasicBlock(bodyName, func);
    auto exit = new BasicBlock(exitName, func);
    bbs.push_back(unique_ptr<BasicBlock>(body));
    bbs.push_back(unique_ptr<BasicBlock>(exit));

    // ---- body 指令 ----
    auto countPhi = new PhiInst(IntegerType::getInstance(), freshName("count"));
    body->addInstruction(own(countPhi));
    countPhi->addIncoming(count0, entry);

    vector<PhiInst *> ptrPhis;
    for (size_t i = 0; i < ptr0s.size(); ++i)
    {
        auto phi = new PhiInst(ptr0s[i]->getType(), freshName(ptrNames[i]));
        body->addInstruction(own(phi));
        phi->addIncoming(ptr0s[i], entry);
        ptrPhis.push_back(phi);
    }

    auto vl = new VecSetVlInst(countPhi, 32, freshName("vl"));
    body->addInstruction(own(vl));

    vector<Value *> ptrs(ptrPhis.begin(), ptrPhis.end());

    emitBody(body, vl, ptrs);

    auto countN = new BinaryOperator(Opcode::Sub, countPhi, vl, freshName("countN"));
    body->addInstruction(own(countN));
    countPhi->addIncoming(countN, body);

    for (size_t i = 0; i < ptrPhis.size(); ++i)
    {
        auto gep = new GetElementPtrInst(ptrPhis[i], vector<Value *>{vl}, freshName("ptrN"));
        body->addInstruction(own(gep));
        ptrPhis[i]->addIncoming(gep, body);
    }

    auto cond = new ICmpInst(ICmpInst::ICMP_NE, countN, ci(0), freshName("cond"));
    body->addInstruction(own(cond));
    body->addInstruction(own(new BranchInst(cond, body, exit)));

    // ---- exit 指令 ----
    exit->addInstruction(own(new BranchInst(exitBlock)));

    // ---- CFG 重连 ----
    retargetEntryEdge(entry, oldHeader, body);

    wireEdge(body, body);
    wireEdge(body, exit);
    wireEdge(exit, exitBlock);

    // exitBlock 的 phi：原来自 oldHeader 的 incoming 改由 exit 提供
    for (auto &instPtr : exitBlock->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi)
            continue;
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            if (phi->getIncomingBlock(i) != oldHeader)
                continue;
            phi->setIncomingBlock(i, exit);
            if (sameValue(phi, jIV) || feedsInductionVar(phi, jIV) ||
                feedsInductionVar(jIV, phi))
                phi->setIncomingValue(i, bound);
        }
    }
}

    // ============================================================
    // 通用逐元素表达式循环识别与向量化
    //
    // 匹配模式：简单两块循环（header+latch），循环体对数组行做逐元素变换，
    // 例如 C[i][j] = A[i][j] * 2 + B[i][j] * 3、M[i][j] = v（常量/循环不变填充）、
    // dst[base+i] = src[i]（拷贝）。
    // 约束：
    //   - 所有 load/store 的列索引必须形如 jIV + 循环不变偏移（同一行连续内存）；
    //   - store 值表达式只含 常量/循环不变值/load/add/sub/mul/sll/srl/sra；
    //   - 循环体内非控制指令必须全部被表达式覆盖（防止破坏循环外 SSA 使用）。
    // ============================================================

    enum class ElemKind { CONST, INVARIANT, LOAD, ADD, SUB, MUL, SLL, SRL, SRA };

    struct ElemExpr
    {
        ElemKind kind = ElemKind::CONST;
        int64_t cval = 0;             // CONST
        Value *scalar = nullptr;      // INVARIANT：循环不变标量
        Value *base = nullptr;        // LOAD：数组基址
        Value *row = nullptr;         // LOAD：行索引（1D 为 nullptr）
        Value *offset = nullptr;      // LOAD：列偏移（jIV 之外的不变部分）
        Instruction *ir = nullptr;    // 源 IR 指令（LOAD/Binary），用于覆盖检查
        std::unique_ptr<ElemExpr> lhs, rhs;
    };

    struct ElemStore
    {
        Value *base = nullptr;
        Value *row = nullptr;
        Value *offset = nullptr;
        std::unique_ptr<ElemExpr> expr;
    };

    struct ElemLoopPattern
    {
        BasicBlock *entry = nullptr;
        BasicBlock *jHeader = nullptr;
        BasicBlock *jBody = nullptr;
        BasicBlock *jExit = nullptr;
        Value *jIV = nullptr;
        Value *bound = nullptr;
        std::vector<ElemStore> stores;
    };

    // v 是否由 loop 内的指令定义
    static bool isLoopInvariantVal(Value *v, const Loop &loop)
    {
        auto *inst = dynamic_cast<Instruction *>(v);
        if (!inst)
            return true;
        return !loop.containsInst(inst);
    }

    // 解析一维线性索引：idx == jIV + inv（inv 循环不变；idx == jIV 时 invOut = nullptr）
    static bool parseLinearIdx(Value *idx, Value *jIV, const Loop &loop, Value *&invOut)
    {
        invOut = nullptr;
        idx = stripCopy(idx);
        if (matchesLoopIV(idx, jIV))
            return true;
        auto *bin = dynamic_cast<BinaryOperator *>(idx);
        if (!bin || bin->getOpcode() != Opcode::Add)
            return false;
        Value *a = stripCopy(bin->getLHS());
        Value *b = stripCopy(bin->getRHS());
        bool aIsJ = matchesLoopIV(a, jIV);
        bool bIsJ = matchesLoopIV(b, jIV);
        if (aIsJ && !bIsJ && isLoopInvariantVal(bin->getRHS(), loop))
        {
            invOut = bin->getRHS();
            return true;
        }
        if (bIsJ && !aIsJ && isLoopInvariantVal(bin->getLHS(), loop))
        {
            invOut = bin->getLHS();
            return true;
        }
        return false;
    }

    // 解析数组访问地址（1D/2D，支持嵌套 GEP），要求列索引形如 jIV + 不变偏移。
    // body 内的 GEP 指令会被记入 covered，保证覆盖检查通过。
    static bool parseVecAddr(Value *ptr, Value *jIV, const Loop &loop,
                             std::set<Instruction *> &covered,
                             Value *&base, Value *&row, Value *&offset)
    {
        base = row = offset = nullptr;
        auto *gep = dynamic_cast<GetElementPtrInst *>(stripCopy(ptr));
        if (!gep)
            return false;

        auto recordGep = [&](GetElementPtrInst *g)
        {
            if (g && loop.containsInst(g))
                covered.insert(g);
        };
        recordGep(gep);

        auto indices = gep->getIndices();
        // 2D：gep [N x T]* base, row, col
        if (indices.size() == 2)
        {
            Value *r = stripCopy(indices[0]);
            if (!isLoopInvariantVal(r, loop))
                return false;
            if (!parseLinearIdx(indices[1], jIV, loop, offset))
                return false;
            base = gep->getPointerOperand();
            row = r;
            return true;
        }
        if (indices.size() == 1)
        {
            // 嵌套 GEP：gep i32* rowGep, col，其中 rowGep = gep [N x T]* base, row, 0
            auto *rowGep = dynamic_cast<GetElementPtrInst *>(stripCopy(gep->getPointerOperand()));
            if (rowGep && rowGep->getIndices().size() == 2)
            {
                recordGep(rowGep);
                Value *r = stripCopy(rowGep->getIndices()[0]);
                if (!isLoopInvariantVal(r, loop))
                    return false;
                if (!parseLinearIdx(indices[0], jIV, loop, offset))
                    return false;
                base = rowGep->getPointerOperand();
                row = r;
                return true;
            }
            // 纯 1D：gep i32* base, idx
            if (!parseLinearIdx(indices[0], jIV, loop, offset))
                return false;
            base = gep->getPointerOperand();
            return true;
        }
        return false;
    }

    // 从标量表达式递归构建可向量化表达式树
    static bool buildElemExpr(Value *val, Value *jIV, const Loop &loop,
                              std::set<Instruction *> &covered,
                              std::unique_ptr<ElemExpr> &out)
    {
        Value *raw = stripCopy(val);
        if (auto *c = dynamic_cast<ConstantInt *>(raw))
        {
            auto e = std::make_unique<ElemExpr>();
            e->kind = ElemKind::CONST;
            e->cval = c->Value;
            out = std::move(e);
            return true;
        }
        if (isLoopInvariantVal(val, loop))
        {
            auto e = std::make_unique<ElemExpr>();
            e->kind = ElemKind::INVARIANT;
            e->scalar = val;
            out = std::move(e);
            return true;
        }
        auto *inst = dynamic_cast<Instruction *>(raw);
        if (!inst)
            return false;
        if (auto *ld = dynamic_cast<LoadInst *>(inst))
        {
            Value *base, *row, *offset;
            if (!parseVecAddr(ld->getPointer(), jIV, loop, covered, base, row, offset))
                return false;
            auto e = std::make_unique<ElemExpr>();
            e->kind = ElemKind::LOAD;
            e->base = base;
            e->row = row;
            e->offset = offset;
            e->ir = ld;
            covered.insert(ld);
            out = std::move(e);
            return true;
        }
        if (auto *bin = dynamic_cast<BinaryOperator *>(inst))
        {
            ElemKind k;
            switch (bin->getOpcode())
            {
            case Opcode::Add: k = ElemKind::ADD; break;
            case Opcode::Sub: k = ElemKind::SUB; break;
            case Opcode::Mul: k = ElemKind::MUL; break;
            case Opcode::Sll: k = ElemKind::SLL; break;
            case Opcode::Sra: k = ElemKind::SRA; break;
            default: return false;
            }
            std::unique_ptr<ElemExpr> lhs, rhs;
            if (!buildElemExpr(bin->getLHS(), jIV, loop, covered, lhs))
                return false;
            if (!buildElemExpr(bin->getRHS(), jIV, loop, covered, rhs))
                return false;
            auto e = std::make_unique<ElemExpr>();
            e->kind = k;
            e->ir = bin;
            e->lhs = std::move(lhs);
            e->rhs = std::move(rhs);
            covered.insert(bin);
            out = std::move(e);
            return true;
        }
        return false;
    }

    // 判断是否为可豁免的循环控制指令（其值不被循环外使用）
    static bool isControlInst(Instruction *inst, Value *jIV, const Loop &loop)
    {
        if (dynamic_cast<BranchInst *>(inst))
            return true;
        if (dynamic_cast<ICmpInst *>(inst))
            return true;
        if (dynamic_cast<PhiInst *>(inst))
            return true;
        if (dynamic_cast<CopyInst *>(inst))
            return !inst->hasExternalUse(loop);
        if (auto *bin = dynamic_cast<BinaryOperator *>(inst))
        {
            if (bin->getOpcode() == Opcode::Add)
            {
                Value *a = stripCopy(bin->getLHS());
                Value *b = stripCopy(bin->getRHS());
                if ((matchesLoopIV(a, jIV) || matchesLoopIV(b, jIV)) &&
                    !bin->hasExternalUse(loop))
                    return true;
            }
        }
        return false;
    }

    // 识别可向量化的逐元素循环
    static bool findElementwiseLoop(const Loop &jLoop, ElemLoopPattern &pat)
    {
        if (!isSimpleTwoBlockLoop(jLoop))
            return false;
        BasicBlock *jHeader = jLoop.header;
        BasicBlock *jBody = getLoopLatch(jLoop);
        BasicBlock *jExit = getLoopExit(jLoop);
        if (!jHeader || !jBody || !jExit || jBody == jHeader)
            return false;

        Value *jIV = nullptr, *bound = nullptr;
        ICmpInst *cmp = nullptr;
        if (!getHeaderBoundCmp(jHeader, jIV, bound, cmp))
            return false;

        vector<BasicBlock *> entryPreds;
        for (auto *pred : jHeader->getPredecessors())
        {
            if (!jLoop.containsBlock(pred))
                entryPreds.push_back(pred);
        }
        if (entryPreds.size() != 1)
            return false;
        BasicBlock *entry = entryPreds[0];
        if (!isValidVectorEntry(entry, jHeader))
            return false;

        std::set<Instruction *> covered;
        std::vector<Instruction *> computeInsts;
        for (auto &instPtr : jBody->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (isControlInst(inst, jIV, jLoop))
                continue;
            if (dynamic_cast<StoreInst *>(inst))
                continue; // store 是待向量化的目标，不属于需覆盖的计算指令
            computeInsts.push_back(inst);
        }

        ElemLoopPattern out;
        out.entry = entry;
        out.jHeader = jHeader;
        out.jBody = jBody;
        out.jExit = jExit;
        out.jIV = jIV;
        out.bound = bound;

        int storeCount = 0;
        for (auto &instPtr : jBody->getInstructions())
        {
            auto *st = dynamic_cast<StoreInst *>(instPtr.get());
            if (!st)
                continue;
            storeCount++;
            Value *base, *row, *offset;
            if (!parseVecAddr(st->getPointer(), jIV, jLoop, covered, base, row, offset))
                return false;
            ElemStore es;
            es.base = base;
            es.row = row;
            es.offset = offset;
            if (!buildElemExpr(st->getValueToStore(), jIV, jLoop, covered, es.expr))
                return false;
            out.stores.push_back(std::move(es));
        }
        if (storeCount == 0)
            return false;

        // 覆盖检查：body 内所有非控制指令必须被某个 store 表达式访问
        for (auto *inst : computeInsts)
        {
            if (covered.count(inst) == 0)
                return false;
        }

        pat = std::move(out);
        return true;
    }

    // 构建 strip-mining 向量循环并填充逐元素计算
    static bool vectorizeElementwiseLoop(Function *func, const ElemLoopPattern &pat)
    {
        BasicBlock *entry = pat.entry;
        BasicBlock *jHeader = pat.jHeader;
        BasicBlock *jBody = pat.jBody;
        BasicBlock *jExit = pat.jExit;
        Value *jIV = pat.jIV;
        Value *bound = pat.bound;

        // 为所有地址（store + 表达式 load）构建行基址 GEP，去重
        vector<Value *> ptrs;
        std::map<string, size_t> addrIndex;
        auto ensurePtr = [&](Value *base, Value *row, Value *off) -> size_t
        {
            string key = base->getName();
            if (row)
                key += "@" + row->getName();
            key += "#" + (off ? off->getName() : "0");
            auto it = addrIndex.find(key);
            if (it != addrIndex.end())
                return it->second;
            Value *offVal = off ? off : ci(0);
            GetElementPtrInst *gep = nullptr;
            if (row)
                gep = new GetElementPtrInst(base, vector<Value *>{row, offVal},
                                            freshName("row"));
            else
                gep = new GetElementPtrInst(base, vector<Value *>{offVal}, freshName("row"));
            prependBeforeTerminator(entry, gep);
            size_t idx = ptrs.size();
            ptrs.push_back(gep);
            addrIndex[key] = idx;
            return idx;
        };

        struct StoreInfo
        {
            size_t ptrIdx;
            std::unique_ptr<ElemExpr> expr;
        };
        vector<StoreInfo> stores;
        for (const auto &es : pat.stores)
        {
            size_t idx = ensurePtr(es.base, es.row, es.offset);
            StoreInfo si;
            si.ptrIdx = idx;
            // 深拷贝表达式树（ElemExpr 含 unique_ptr，需逐字段复制）
            std::function<void(const ElemExpr &, std::unique_ptr<ElemExpr> &)> cloneTree;
            cloneTree = [&](const ElemExpr &src, std::unique_ptr<ElemExpr> &dst)
            {
                auto e = std::make_unique<ElemExpr>();
                e->kind = src.kind;
                e->cval = src.cval;
                e->scalar = src.scalar;
                e->base = src.base;
                e->row = src.row;
                e->offset = src.offset;
                e->ir = src.ir;
                if (src.lhs)
                    cloneTree(*src.lhs, e->lhs);
                if (src.rhs)
                    cloneTree(*src.rhs, e->rhs);
                dst = std::move(e);
            };
            cloneTree(*es.expr, si.expr);
            stores.push_back(std::move(si));
        }

        // 收集表达式中的 LOAD 节点并分配 ptr 索引
        std::map<const ElemExpr *, size_t> loadPtrIdx;
        for (const auto &si : stores)
        {
            std::function<void(const ElemExpr &)> collect;
            collect = [&](const ElemExpr &e)
            {
                if (e.kind == ElemKind::LOAD)
                    loadPtrIdx[&e] = ensurePtr(e.base, e.row, e.offset);
                if (e.lhs)
                    collect(*e.lhs);
                if (e.rhs)
                    collect(*e.rhs);
            };
            collect(*si.expr);
        }

        vector<string> ptrNames;
        for (size_t i = 0; i < ptrs.size(); ++i)
            ptrNames.push_back("rowPtr" + to_string(i));

        buildVectorLoop(
            func, entry, jHeader, jExit, bound, jIV, bound, ptrs, ptrNames,
            freshName("vbody"), freshName("vexit"),
            [&](BasicBlock *body, Value *vl, const vector<Value *> &ptrs)
            {
                std::map<const ElemExpr *, Value *> cache;
                std::function<Value *(const ElemExpr &)> translate;
                translate = [&](const ElemExpr &e) -> Value *
                {
                    auto it = cache.find(&e);
                    if (it != cache.end())
                        return it->second;
                    Instruction *res = nullptr;
                    switch (e.kind)
                    {
                    case ElemKind::CONST:
                        res = new VecSplatInst(ci(e.cval), vl, IntegerType::getInstance(), 16,
                                               freshName("v"));
                        break;
                    case ElemKind::INVARIANT:
                        res = new VecSplatInst(e.scalar, vl, IntegerType::getInstance(), 16,
                                               freshName("v"));
                        break;
                    case ElemKind::LOAD:
                        res = new VecLoadInst(ptrs[loadPtrIdx[&e]], vl,
                                              IntegerType::getInstance(), 16, freshName("v"));
                        break;
                    default:
                    {
                        Value *l = translate(*e.lhs);
                        Value *r = translate(*e.rhs);
                        Opcode op;
                        switch (e.kind)
                        {
                        case ElemKind::ADD: op = Opcode::VecAdd; break;
                        case ElemKind::SUB: op = Opcode::VecSub; break;
                        case ElemKind::MUL: op = Opcode::VecMul; break;
                        case ElemKind::SLL: op = Opcode::VecSll; break;
                        case ElemKind::SRL: op = Opcode::VecSrl; break;
                        case ElemKind::SRA: op = Opcode::VecSra; break;
                        default: op = Opcode::VecAdd; break;
                        }
                        res = new VecBinaryInst(op, l, r, freshName("v"));
                        break;
                    }
                    }
                    body->addInstruction(own(res));
                    cache[&e] = res;
                    return res;
                };

                for (auto &si : stores)
                {
                    Value *result = translate(*si.expr);
                    body->addInstruction(own(new VecStoreInst(result, ptrs[si.ptrIdx], vl)));
                }
            });

        removeLoopBlocks(func, {jHeader, jBody});
        return true;
    }
} // namespace
bool LoopVectorizePass::vectorizeScaledRowUpdate(Function *func, const ScaledRowUpdateNest &nest)
{
    if (!nest.valid || !nest.jLoop || !nest.jHeader || !nest.bound)
        return false;

    BasicBlock *jHeader = nest.jHeader;
    BasicBlock *jBody = getLoopLatch(*nest.jLoop);
    BasicBlock *jExit = getLoopExit(*nest.jLoop);
    if (!jHeader || !jExit)
        return false;

    // 唯一的循环外前驱（j-loop preheader），其终结指令必须为无条件跳转 jHeader
    vector<BasicBlock *> entryPreds;
    for (auto *pred : jHeader->getPredecessors())
    {
        if (!nest.jLoop->containsBlock(pred))
            entryPreds.push_back(pred);
    }
    if (entryPreds.size() != 1)
        return false;
    BasicBlock *entry = entryPreds[0];
    if (!isValidVectorEntry(entry, jHeader))
        return false;

    // C[i][*] 行基址、B[k][*] 行基址（在 entry 中新建，保证支配向量循环）
    auto cRowGep = new GetElementPtrInst(nest.cArray, vector<Value *>{nest.iIV, ci(0)},
                                         freshName("cRow"));
    auto bRowGep = new GetElementPtrInst(nest.bArray, vector<Value *>{nest.kIV, ci(0)},
                                         freshName("bRow"));
    prependBeforeTerminator(entry, cRowGep);
    prependBeforeTerminator(entry, bRowGep);

    // scale = A[i][k]；无 skip-guard 时原 load 在 j 体内，需在 entry 新建 load
    Value *scale = nest.aLoad;
    if (!nest.hasSkipGuard || !scale)
    {
        auto aGep = new GetElementPtrInst(nest.aArray, vector<Value *>{nest.iIV, nest.kIV},
                                          freshName("aIdx"));
        auto ld = new LoadInst(aGep, freshName("scale"));
        prependBeforeTerminator(entry, aGep);
        prependBeforeTerminator(entry, ld);
        scale = ld;
    }

    // 向量 splat 放在循环体内（紧跟循环 vsetvli 之后）：vmv.v.x 只写当前 VL
    // 范围内的元素，循环内执行才能保证 splat 与 load/store 的 VL 一致
    buildVectorLoop(
        func, entry, jHeader, jExit, nest.bound, nest.jIV, nest.bound, {cRowGep, bRowGep},
        {"cPtr", "bPtr"}, freshName("vbody"), freshName("vexit"),
        [scale](BasicBlock *body, Value *vl, const vector<Value *> &ptrs)
        {
            auto vs = new VecSplatInst(scale, vl, IntegerType::getInstance(), 16,
                                       freshName("vs"));
            body->addInstruction(own(vs));
            auto vc = new VecLoadInst(ptrs[0], vl, IntegerType::getInstance(), 16,
                                      freshName("vc"));
            body->addInstruction(own(vc));
            auto vb = new VecLoadInst(ptrs[1], vl, IntegerType::getInstance(), 16,
                                      freshName("vb"));
            body->addInstruction(own(vb));
            auto vm = new VecBinaryInst(Opcode::VecMul, vc, vs, freshName("vm"));
            body->addInstruction(own(vm));
            auto va = new VecBinaryInst(Opcode::VecAdd, vm, vb, freshName("va"));
            body->addInstruction(own(va));
            body->addInstruction(own(new VecStoreInst(va, ptrs[0], vl)));
        });

    removeLoopBlocks(func, {jHeader, jBody});
    if (verbose)
        debugInfo << "LoopVectorize: vec C=A*C+B @ " << func->getName()
                  << " j=" << jHeader->getName() << "\n";
    return true;
}

bool LoopVectorizePass::runOnFunction(Function *func)
{
    if (!CompilerConfig::enableRVV || !func || func->isLibraryFunction())
        return false;

    // 结构分析（内含 findLoops），随后逐个替换 j 内层循环
    MatrixFunctionAnalysis analysis = matrixStructure::analyzeFunction(func);

    bool changed = false;
    // 复制 nests：变换会破坏 func->Loops，不能边遍历边用引用
    auto scaled = analysis.scaledRowUpdateNests;
    for (const auto &nest : scaled)
    {
        if (vectorizeScaledRowUpdate(func, nest))
            changed = true;
    }

    // 通用逐元素循环（含零填充、常量/循环不变填充、逐元素表达式、拷贝）。
    // 每处理一个循环就重新分析循环结构，避免重复处理同一循环。
    bool localChanged = true;
    while (localChanged)
    {
        localChanged = false;
        func->setLoops(ControlFlowAnalysis::findLoops(func));
        auto loops = func->getLoops(); // 拷贝，避免遍历中失效
        for (const auto &loop : loops)
        {
            ElemLoopPattern pat;
            if (findElementwiseLoop(loop, pat) &&
                vectorizeElementwiseLoop(func, pat))
            {
                localChanged = true;
                changed = true;
                if (verbose)
                    debugInfo << "LoopVectorize: elementwise loop @ " << func->getName()
                              << " header=" << pat.jHeader->getName() << "\n";
                break;
            }
        }
    }

    func->setLoops(ControlFlowAnalysis::findLoops(func));
    matrixStructure::clearAnalysis(func);
    return changed;
}
