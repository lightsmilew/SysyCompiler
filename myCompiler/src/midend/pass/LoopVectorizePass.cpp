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
    // 与 LoopUnrollingPass 保持一致：静态 trip count <= 20 的常量循环才会被完全展开
    static constexpr int kFullUnrollMaxTripCount = 20;

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



    // v 是否由 loop 内的指令定义。循环内定义的指令仅当其为纯计算指令
    // （无内存/调用副作用）且所有操作数递归循环不变时，才判定为循环不变量。
    // 例如行偏移 r*N_eff 可能被其他 pass 放入循环 header，但其操作数都在循环外，
    // 值不随迭代变化，仍应视为循环不变。
    // phi/load/store/call/alloca 等在循环内保守判定为循环内可变。
    static bool isLoopInvariantVal(Value *v, const Loop &loop,
                                   std::set<const Instruction *> &visiting)
    {
        if (!v)
            return true;
        if (dynamic_cast<Constant *>(v))
            return true;
        auto *inst = dynamic_cast<Instruction *>(v);
        if (!inst)
            return true; // 参数/全局变量
        if (!loop.containsInst(inst))
            return true; // 循环外定义
        if (visiting.count(inst))
            return false; // 成环（如 phi 自引用），保守判定为可变
        switch (inst->Op)
        {
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::SDiv:
        case Opcode::SRem:
        case Opcode::Sll:
        case Opcode::Sra:
        case Opcode::And:
        case Opcode::Or:
        case Opcode::Xor:
        case Opcode::Xnor:
        case Opcode::Addd:
        case Opcode::Muld:
        case Opcode::Mulhd:
        case Opcode::Slld:
        case Opcode::Srad:
        case Opcode::Sext:
        case Opcode::Trunc:
        case Opcode::SIToFP:
        case Opcode::FPToSI:
        case Opcode::BitCast:
        case Opcode::GetElementPtr:
        case Opcode::Copy:
        case Opcode::Select:
        case Opcode::PackI64:
            break; // 纯计算，继续递归检查操作数
        default:
            return false; // phi/load/store/call/alloca 等在循环内视为可变
        }
        visiting.insert(inst);
        for (auto *op : inst->getOperands())
        {
            if (!isLoopInvariantVal(op, loop, visiting))
            {
                visiting.erase(inst);
                return false;
            }
        }
        visiting.erase(inst);
        return true;
    }

    static bool isLoopInvariantVal(Value *v, const Loop &loop)
    {
        std::set<const Instruction *> visiting;
        return isLoopInvariantVal(v, loop, visiting);
    }

    // 将“循环内定义但循环不变”的纯计算指令链记入 covered。
    // 这些指令虽位于循环体内，但值不随迭代变化，被某个 store/load 表达式作为
    // 不变偏移访问，覆盖检查应放行（向量化时会被提升到 entry，不会悬挂）。
    static void recordInvariantChain(Value *v, const Loop &loop,
                                     std::set<Instruction *> &covered)
    {
        auto *inst = dynamic_cast<Instruction *>(v);
        if (!inst || !loop.containsInst(inst) || covered.count(inst))
            return;
        if (!isLoopInvariantVal(v, loop))
            return;
        covered.insert(inst);
        for (auto *op : inst->getOperands())
            recordInvariantChain(op, loop, covered);
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
            recordInvariantChain(r, loop, covered);
            if (offset)
                recordInvariantChain(offset, loop, covered);
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
                recordInvariantChain(r, loop, covered);
                if (offset)
                    recordInvariantChain(offset, loop, covered);
                base = rowGep->getPointerOperand();
                row = r;
                return true;
            }
            // 纯 1D：gep i32* base, idx
            if (!parseLinearIdx(indices[0], jIV, loop, offset))
                return false;
            if (offset)
                recordInvariantChain(offset, loop, covered);
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
            recordInvariantChain(val, loop, covered);
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

} // namespace

// 识别可向量化的逐元素循环
bool LoopVectorizePass::findElementwiseLoop(const Loop &jLoop, ElemLoopPattern &pat)
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

        // 可被完全展开的常量小循环（如 while(i<5)）交给循环展开 inline 化，不做向量化
        if (isFullyUnrollableLoop(jLoop, jIV, bound))
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
    bool LoopVectorizePass::vectorizeElementwiseLoop(Function *func, const ElemLoopPattern &pat)
    {
        BasicBlock *entry = pat.entry;
        BasicBlock *jHeader = pat.jHeader;
        BasicBlock *jBody = pat.jBody;
        BasicBlock *jExit = pat.jExit;
        Value *jIV = pat.jIV;
        Value *bound = pat.bound;

        // 循环内定义的“循环不变”纯计算标量链（如行偏移 r*N_eff 被其他 pass
        // 放入循环 header）需克隆到 entry：旧循环块随后会被删除，新向量指令
        // 不能引用其中的指令，否则 removeThisFromOperands 会清掉其操作数。
        std::map<Value *, Value *> hoistedScalars;
        std::function<Value *(Value *)> hoistScalar = [&](Value *v) -> Value *
        {
            if (!v || dynamic_cast<Constant *>(v))
                return v;
            auto *inst = dynamic_cast<Instruction *>(v);
            if (!inst)
                return v;
            if (!jHeader->containsByName(inst->getName()) &&
                !jBody->containsByName(inst->getName()))
                return v;
            auto it = hoistedScalars.find(inst);
            if (it != hoistedScalars.end())
                return it->second;
            Instruction *clone = inst->clone();
            clone->setName(clone->getName() + "_lvh");
            for (unsigned i = 0; i < clone->getOperands().size(); ++i)
                clone->setOperandByIndex(i, hoistScalar(inst->getOperandByIndex(i)));
            prependBeforeTerminator(entry, clone);
            hoistedScalars[inst] = clone;
            return clone;
        };

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
            Value *offVal = off ? hoistScalar(off) : ci(0);
            GetElementPtrInst *gep = nullptr;
            if (row)
                gep = new GetElementPtrInst(base, vector<Value *>{hoistScalar(row), offVal},
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
                        res = new VecSplatInst(hoistScalar(e.scalar), vl, IntegerType::getInstance(),
                                               16, freshName("v"));
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

    // ============================================================
    // 标量链循环向量化：while (x < t) { sum = (sum + f(x) + 1) % mod; x += d; }
    // 无数组访存的纯标量计算链循环。
    // 生成 strip-mining 向量循环：
    //   body: count=phi(N,entry),(countN,body); xCur=phi(xInit,entry),(xN,body);
    //         sumAcc=phi(sumInit,entry),(sumN,body);
    //         vl=vecsetvl(count,32);
    //         vec_x = splat(xCur) + vid(vl)*splat(step);
    //         vec_f = f_vec(vec_x); blockSum = vredsum(vec_f+1);
    //         sumN = (sumAcc + blockSum%mod) % mod; xN = xCur + vl*step;
    //         countN = count - vl; br countN!=0, body, exit
    //   exit: br exitBlock（exitBlock 中对旧 sum 的 use 改为 sumOut phi）
    // ============================================================

    namespace
    {
    static bool dependsOn(Value *v, Value *target, set<Value *> &visited)
    {
        if (!v || !target)
            return false;
        if (v == target)
            return true;
        if (visited.count(v))
            return false;
        visited.insert(v);
        auto *inst = dynamic_cast<Instruction *>(v);
        if (!inst)
            return false;
        for (auto *op : inst->getOperands())
        {
            if (dependsOn(op, target, visited))
                return true;
        }
        return false;
    }

    static bool dependsOn(Value *v, Value *target)
    {
        set<Value *> visited;
        return dependsOn(v, target, visited);
    }

    // 从 sumNext 反向剥离 srem/add 累积链，提取 fx（不依赖 sum 的计算链输出）与 mod
    static bool peelSumChain(Value *cur, Value *sum, Value *&fxOut, Value *&modOut)
    {
        fxOut = nullptr;
        modOut = nullptr;
        int guard = 0;
        while (cur && guard++ < 64)
        {
            cur = stripCopy(cur);
            auto *bin = dynamic_cast<BinaryOperator *>(cur);
            if (!bin)
                break;
            if (bin->getOpcode() == Opcode::SRem)
            {
                modOut = stripCopy(bin->getRHS());
                cur = stripCopy(bin->getLHS());
                continue;
            }
            if (bin->getOpcode() == Opcode::Add || bin->getOpcode() == Opcode::Sub)
            {
                Value *a = stripCopy(bin->getLHS());
                Value *b = stripCopy(bin->getRHS());
                bool aDep = dependsOn(a, sum);
                bool bDep = dependsOn(b, sum);
                if (aDep && !bDep)
                {
                    if (a == sum)
                    {
                        fxOut = b;
                        return true;
                    }
                    cur = a;
                    continue;
                }
                if (bDep && !aDep)
                {
                    if (b == sum)
                    {
                        fxOut = a;
                        return true;
                    }
                    cur = b;
                    continue;
                }
                break;
            }
            break;
        }
        return false;
    }

    static bool isLaneLocalVal(Value *v, Value *x, const Loop &loop,
                               const set<Instruction *> &laneInsts)
    {
        if (v == x)
            return true;
        if (dynamic_cast<Constant *>(v))
            return true;
        auto *inst = dynamic_cast<Instruction *>(v);
        if (!inst)
            return true; // 参数/全局等非指令值 → 循环不变
        if (!loop.containsInst(inst))
            return true; // 循环外定义 → 循环不变
        return laneInsts.count(inst) > 0;
    }

} // namespace

// 判断循环是否可被 LoopUnrollingPass 完全展开（条件参考 LoopUnroll 的 tripCount 计算）：
//   - bound / init / step 均为常量
//   - 0 < tripCount <= kFullUnrollMaxTripCount
// 可完全展开的小循环交给循环展开 inline 化，不向量化。
bool LoopVectorizePass::isFullyUnrollableLoop(const Loop &loop, Value *jIV, Value *bound)
{
    auto *boundConst = dynamic_cast<ConstantInt *>(stripCopy(bound));
    if (!boundConst)
        return false;

    // 归纳变量必须是 header 中的 phi（elementwise/scalar-chain 均为 phi 归纳）
    auto *phi = dynamic_cast<PhiInst *>(stripCopy(jIV));
    if (!phi)
        return false;

    // 提取 init（非循环块 incoming）与 step（循环块 incoming）
    Value *init = nullptr;
    Value *stepVal = nullptr;
    for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
    {
        BasicBlock *bb = phi->getIncomingBlock(i);
        if (bb && loop.containsBlock(bb))
            stepVal = phi->getIncomingValue(i);
        else
            init = phi->getIncomingValue(i);
    }
    if (!init || !stepVal)
        return false;
    auto *initConst = dynamic_cast<ConstantInt *>(stripCopy(init));
    if (!initConst)
        return false;

    // step：循环内 incoming 形如 add(phi, 常量)
    int stepConst = 1;
    auto *stepBin = dynamic_cast<BinaryOperator *>(stripCopy(stepVal));
    if (stepBin && stepBin->getOpcode() == Opcode::Add)
    {
        Value *a = stripCopy(stepBin->getLHS());
        Value *b = stripCopy(stepBin->getRHS());
        Value *constOp = nullptr;
        if (a == phi)
            constOp = stripCopy(stepBin->getRHS());
        else if (b == phi)
            constOp = stripCopy(stepBin->getLHS());
        else
            return false;
        auto *sc = dynamic_cast<ConstantInt *>(constOp);
        if (!sc)
            return false;
        stepConst = sc->Value;
    }
    else if (auto *sc = dynamic_cast<ConstantInt *>(stripCopy(stepVal)))
    {
        stepConst = sc->Value;
    }
    else
    {
        return false; // step 非常量，LoopUnroll 无法静态计算 tripCount
    }
    if (stepConst <= 0)
        return false;

    // LoopUnroll 完全展开仅对 SLT/SLE 且 tripCount 为小常量（<=20）生效
    int tripCount = (boundConst->Value - initConst->Value) / stepConst;
    return tripCount > 0 && tripCount <= kFullUnrollMaxTripCount;
}

bool LoopVectorizePass::findScalarChainLoop(const Loop &loop, ScalarChainPattern &pat)
{
        BasicBlock *header = loop.header;
        BasicBlock *latch = getLoopLatch(loop);
        BasicBlock *exitBlock = getLoopExit(loop);
       
        if (!header || !latch || !exitBlock || latch == exitBlock)
        {

            return false;
        }

        vector<BasicBlock *> entryPreds;
        for (auto *pred : header->getPredecessors())
        {
            if (!loop.containsBlock(pred))
                entryPreds.push_back(pred);
        }
        if (entryPreds.size() != 1)
        {

            return false;
        }
        BasicBlock *entry = entryPreds[0];

        // ---- 归纳变量与归约变量 ----
        Value *x = nullptr, *xInit = nullptr, *step = nullptr;
        Value *sum = nullptr, *sumInit = nullptr, *fx = nullptr, *mod = nullptr;
        for (auto &instPtr : header->getInstructions())
        {
            auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
            if (!phi)
                continue;
            Value *phiEntryVal = nullptr, *phiBodyVal = nullptr;
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                auto *bb = phi->getIncomingBlock(i);
                if (bb && !loop.containsBlock(bb))
                    phiEntryVal = phi->getIncomingValue(i);
                else
                    phiBodyVal = phi->getIncomingValue(i);
            }
            if (!phiEntryVal || !phiBodyVal)
                continue;

            // 归纳变量：循环内 incoming = copy/add(phi, 循环不变)
            Value *body = stripCopy(phiBodyVal);
            auto *bin = dynamic_cast<BinaryOperator *>(body);
            if (bin && bin->getOpcode() == Opcode::Add)
            {
                Value *a = stripCopy(bin->getLHS());
                Value *b = stripCopy(bin->getRHS());
                if (a == phi && isLoopInvariantVal(bin->getRHS(), loop))
                {
                    x = phi;
                    xInit = phiEntryVal;
                    step = bin->getRHS();
                    continue;
                }
                if (b == phi && isLoopInvariantVal(bin->getLHS(), loop))
                {
                    x = phi;
                    xInit = phiEntryVal;
                    step = bin->getLHS();
                    continue;
                }
            }

            // 归约变量：循环内 incoming 依赖 phi
            if (dependsOn(phiBodyVal, phi))
            {
                Value *fxTmp = nullptr, *modTmp = nullptr;
                if (peelSumChain(phiBodyVal, phi, fxTmp, modTmp))
                {
                    sum = phi;
                    sumInit = phiEntryVal;
                    fx = fxTmp;
                    mod = modTmp;
                }
            }
        }
        if (!x || !sum || !fx || !mod)
        {

            return false;
        }
        if (!dependsOn(fx, x) || dependsOn(fx, sum))
        {

            return false;
        }
        if (!isLoopInvariantVal(mod, loop))
        {

            return false;
        }
        // sumInit 必须为常量 0（保证 N==0 时 sum 保持初值；向量循环 count=0 时 body
        // 仍会执行一次 sum 更新，0 % mod == 0 不变）
        auto *ciSumInit = dynamic_cast<ConstantInt *>(stripCopy(sumInit));
        if (!ciSumInit || ciSumInit->Value != 0)
        {

            return false;
        }

        // ---- 循环条件：header（cond-header 形式）或 latch 终结 br 的 icmp(x', bound) ----
        Value *bound = nullptr;
        {
            BranchInst *br = nullptr;
            auto *hbr = dynamic_cast<BranchInst *>(header->getTerminator());
            auto *lbr = dynamic_cast<BranchInst *>(latch->getTerminator());
            if (hbr && hbr->isConditional())
                br = hbr;
            else if (lbr && lbr->isConditional())
                br = lbr;
            if (!br)
            {

                return false;
            }
            auto *cond = dynamic_cast<ICmpInst *>(stripCopy(br->getCondition()));
            if (!cond)
            {

                return false;
            }
            Value *op1 = stripCopy(cond->getLHS());
            Value *op2 = stripCopy(cond->getRHS());
            auto isXAffine = [&](Value *op) -> bool
            {
                if (op == x)
                    return true;
                auto *b = dynamic_cast<BinaryOperator *>(op);
                return b && b->getOpcode() == Opcode::Add &&
                       (stripCopy(b->getLHS()) == x || stripCopy(b->getRHS()) == x);
            };
            if (isXAffine(op1) && isLoopInvariantVal(cond->getRHS(), loop))
                bound = cond->getRHS();
            else if (isXAffine(op2) && isLoopInvariantVal(cond->getLHS(), loop))
                bound = cond->getLHS();
            else
            {

                return false;
            }
        }

        // 可被完全展开的常量小循环交给循环展开 inline 化，不做向量化
        if (isFullyUnrollableLoop(loop, x, bound))
            return false;

        // ---- 收集 lane 独立计算链（从 x 前向 BFS） ----
        set<Instruction *> laneInsts;
        vector<Value *> worklist;
        worklist.push_back(x);
        while (!worklist.empty())
        {
            Value *v = worklist.back();
            worklist.pop_back();
            for (auto *user : v->getUsers())
            {
                auto *inst = dynamic_cast<Instruction *>(user);
                if (!inst || !loop.containsInst(inst) || laneInsts.count(inst))
                    continue;
                bool ok = false;
                switch (inst->Op)
                {
                case Opcode::Copy:
                case Opcode::Add:
                case Opcode::Sub:
                case Opcode::Mul:
                case Opcode::Sll:
                case Opcode::Sra:
                case Opcode::SDiv:
                case Opcode::SRem:
                case Opcode::ICmp:
                case Opcode::Select:
                case Opcode::Phi:
                    ok = true;
                    break;
                default:
                    break;
                }
                if (!ok)
                    continue;
                bool allLocal = true;
                for (auto *op : inst->getOperands())
                {
                    if (!isLaneLocalVal(op, x, loop, laneInsts))
                    {
                        allLocal = false;
                        break;
                    }
                }
                if (!allLocal)
                    continue;
                laneInsts.insert(inst);
                worklist.push_back(inst);
            }
        }

        // ---- 识别 max/min 结构（独立优化函数，同时区分 max 与 min）----
        collectMaxMinPhis(loop, x, laneInsts, pat);

        // ---- 收集归约链（依赖 sum 的指令） ----
        set<Instruction *> sumChain;
        for (auto *bb : loop.blocks)
        {
            for (auto &instPtr : bb->getInstructions())
            {
                Instruction *inst = instPtr.get();
                if (inst == x || inst == sum)
                    continue;
                if (dependsOn(inst, sum))
                    sumChain.insert(inst);
            }
        }

        // ---- 覆盖检查：所有指令必须属于 控制/phi/lane/max/归约 之一 ----
        for (auto *bb : loop.blocks)
        {
            for (auto &instPtr : bb->getInstructions())
            {
                Instruction *inst = instPtr.get();
                if (inst == x || inst == sum)
                    continue;
                if (dynamic_cast<BranchInst *>(inst) || dynamic_cast<ReturnInst *>(inst))
                    continue;
                if (laneInsts.count(inst))
                    continue;
                if (sumChain.count(inst))
                    continue;
                if (pat.maxMinPhis.count(inst))
                    continue;
                if (dynamic_cast<PhiInst *>(inst))
                {
                    return false; // 额外 phi 不支持
                }
                return false;
            }
        }

        // ---- 循环外 use 检查：被删除指令（sum 除外）若有循环外 user 则不支持 ----
        for (auto *bb : loop.blocks)
        {
            for (auto &instPtr : bb->getInstructions())
            {
                Instruction *inst = instPtr.get();
                if (inst == sum)
                    continue;
                for (auto *user : inst->getUsers())
                {
                    auto *uinst = dynamic_cast<Instruction *>(user);
                    if (!uinst)
                        return false;
                    if (!loop.containsInst(uinst))
                        return false; // 循环外 use（sum 除外）
                }
            }
        }

        pat.entry = entry;
        pat.header = header;
        pat.latch = latch;
        pat.exitBlock = exitBlock;
        pat.loop = loop;
        pat.x = x;
        pat.xInit = xInit;
        pat.step = step;
        pat.bound = bound;
        pat.sum = sum;
        pat.sumInit = sumInit;
        pat.fx = fx;
        pat.mod = mod;
        pat.bodyBlocks = loop.blocks;
        pat.laneInsts = laneInsts;
        pat.sumChain = sumChain;
        return true;
    }

void LoopVectorizePass::collectMaxMinPhis(const Loop &loop, Value *x,
                                          const std::set<Instruction *> &laneInsts,
                                          ScalarChainPattern &pat)
{
    for (auto *bb : loop.blocks)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            auto *cmp = dynamic_cast<ICmpInst *>(instPtr.get());
            if (!cmp || (cmp->getPredicate() != ICmpInst::ICMP_SLT &&
                         cmp->getPredicate() != ICmpInst::ICMP_SGT))
                continue;
            Value *a = cmp->getLHS();
            Value *b = cmp->getRHS();
            if (!isLaneLocalVal(a, x, loop, laneInsts) ||
                !isLaneLocalVal(b, x, loop, laneInsts))
                continue;
            for (auto *user : cmp->getUsers())
            {
                auto *br = dynamic_cast<BranchInst *>(user);
                if (!br || !br->isConditional() || br->getCondition() != cmp)
                    continue;
                BasicBlock *thenBB = br->getTrueBlock();
                BasicBlock *elseBB = br->getFalseBlock();
                if (!loop.containsBlock(thenBB) || !loop.containsBlock(elseBB))
                    continue;
                // then/else 都无条件跳到同一 after
                auto *tBr = dynamic_cast<BranchInst *>(thenBB->getTerminator());
                auto *eBr = dynamic_cast<BranchInst *>(elseBB->getTerminator());
                if (!tBr || tBr->isConditional() || !eBr || eBr->isConditional())
                    continue;
                if (tBr->getTrueBlock() != eBr->getTrueBlock())
                    continue;
                BasicBlock *afterBB = tBr->getTrueBlock();
                if (!loop.containsBlock(afterBB))
                    continue;
                // after 块中找 phi：incoming (thenBB→a|b, elseBB→另一)
                for (auto &i2 : afterBB->getInstructions())
                {
                    auto *phi = dynamic_cast<PhiInst *>(i2.get());
                    if (!phi || phi->getNumIncomingValues() != 2)
                        continue;
                    Value *tv = nullptr, *ev = nullptr;
                    bool hasThen = false, hasElse = false;
                    for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
                    {
                        if (phi->getIncomingBlock(k) == thenBB)
                        {
                            tv = stripCopy(phi->getIncomingValue(k));
                            hasThen = true;
                        }
                        else if (phi->getIncomingBlock(k) == elseBB)
                        {
                            ev = stripCopy(phi->getIncomingValue(k));
                            hasElse = true;
                        }
                    }
                    if (!hasThen || !hasElse || !tv || !ev)
                        continue;
                    // 区分 max/min：slt 时 then 生效（选 b → max，选 a → min）；
                    // sgt 时 then 生效（选 a → max，选 b → min）
                    bool isMax = false;
                    bool selA = (tv == a && ev == b);
                    bool selB = (tv == b && ev == a);
                    if (cmp->getPredicate() == ICmpInst::ICMP_SLT)
                    {
                        if (selB)
                            isMax = true;
                        else if (!selA)
                            continue;
                    }
                    else // ICMP_SGT
                    {
                        if (selA)
                            isMax = true;
                        else if (!selB)
                            continue;
                    }
                    pat.maxMinPhis[phi] = {a, b, isMax};
                    break;
                }
            }
        }
    }
}

Value *LoopVectorizePass::translateToVec(Value *v, BasicBlock *body, Value *vl, Value *xVec,
                                         ScalarChainPattern &pat, map<Value *, Value *> &cache)
{
        auto it = cache.find(v);
        if (it != cache.end())
            return it->second;
        if (!v)
            return nullptr;
        if (v == pat.x)
        {
            cache[v] = xVec;
            return xVec;
        }
        if (dynamic_cast<Constant *>(v))
        {
            auto *sp = new VecSplatInst(v, vl, IntegerType::getInstance(), 16, freshName("v"));
            body->addInstruction(own(sp));
            cache[v] = sp;
            return sp;
        }
        auto *inst = dynamic_cast<Instruction *>(v);
        if (!inst)
            return nullptr;
        if (!pat.loop.containsInst(inst))
        {
            // 循环不变标量（参数/外部定义）→ splat
            auto *sp = new VecSplatInst(v, vl, IntegerType::getInstance(), 16, freshName("v"));
            body->addInstruction(own(sp));
            cache[v] = sp;
            return sp;
        }

        // max/min 结构的 phi 值 → vecmax/vecmin
        auto mc = pat.maxMinPhis.find(inst);
        if (mc != pat.maxMinPhis.end())
        {
            Value *va = translateToVec(std::get<0>(mc->second), body, vl, xVec, pat, cache);
            Value *vb = translateToVec(std::get<1>(mc->second), body, vl, xVec, pat, cache);
            if (!va || !vb)
                return nullptr;
            auto *res = new VecBinaryInst(std::get<2>(mc->second) ? Opcode::VecMax : Opcode::VecMin,
                                          va, vb, freshName("v"));
            body->addInstruction(own(static_cast<Instruction *>(res)));
            cache[v] = res;
            return res;
        }

        Value *res = nullptr;
        switch (inst->Op)
        {
        case Opcode::Copy:
            res = translateToVec(static_cast<CopyInst *>(inst)->getSource(), body, vl, xVec, pat, cache);
            break;
        case Opcode::Add:
        case Opcode::Sub:
        case Opcode::Mul:
        case Opcode::Sll:
        case Opcode::Sra:
        case Opcode::SDiv:
        case Opcode::SRem:
        {
            auto *bin = static_cast<BinaryOperator *>(inst);
            Value *l = translateToVec(bin->getLHS(), body, vl, xVec, pat, cache);
            Value *r = translateToVec(bin->getRHS(), body, vl, xVec, pat, cache);
            if (!l || !r)
                return nullptr;
            Opcode op;
            switch (inst->Op)
            {
            case Opcode::Add: op = Opcode::VecAdd; break;
            case Opcode::Sub: op = Opcode::VecSub; break;
            case Opcode::Mul: op = Opcode::VecMul; break;
            case Opcode::Sll: op = Opcode::VecSll; break;
            case Opcode::Sra: op = Opcode::VecSra; break;
            case Opcode::SDiv: op = Opcode::VecDiv; break;
            case Opcode::SRem: op = Opcode::VecRem; break;
            default: return nullptr;
            }
            res = new VecBinaryInst(op, l, r, freshName("v"));
            body->addInstruction(own(static_cast<Instruction *>(res)));
            break;
        }
        case Opcode::Select:
        {
            auto *sel = static_cast<SelectInst *>(inst);
            Value *c = stripCopy(sel->getCondition());
            auto *cmp = dynamic_cast<ICmpInst *>(c);
            Value *tv = sel->getTrueValue();
            Value *fv = sel->getFalseValue();
            if (cmp && cmp->getPredicate() == ICmpInst::ICMP_SLT)
            {
                Value *va = translateToVec(cmp->getLHS(), body, vl, xVec, pat, cache);
                Value *vb = translateToVec(cmp->getRHS(), body, vl, xVec, pat, cache);
                if (!va || !vb)
                    return nullptr;
                // select(a<b, b, a) → max；select(a<b, a, b) → min
                if (tv == cmp->getRHS() && fv == cmp->getLHS())
                {
                    res = new VecBinaryInst(Opcode::VecMax, va, vb, freshName("v"));
                    body->addInstruction(own(static_cast<Instruction *>(res)));
                }
                else if (tv == cmp->getLHS() && fv == cmp->getRHS())
                {
                    res = new VecBinaryInst(Opcode::VecMin, va, vb, freshName("v"));
                    body->addInstruction(own(static_cast<Instruction *>(res)));
                }
                else
                    return nullptr;
            }
            else
                return nullptr;
            break;
        }
        default:
            return nullptr;
        }
        if (!res)
            return nullptr;
        cache[v] = res;
        return res;
    }

bool LoopVectorizePass::vectorizeScalarChainLoop(Function *func, const Loop &loop)
{
        ScalarChainPattern pat;
        if (!findScalarChainLoop(loop, pat))
            return false;

        BasicBlock *entry = pat.entry;
        BasicBlock *header = pat.header;
        BasicBlock *exitBlock = pat.exitBlock;
        Value *x = pat.x, *xInit = pat.xInit, *step = pat.step, *bound = pat.bound;
        Value *sum = pat.sum, *sumInit = pat.sumInit, *fx = pat.fx, *mod = pat.mod;

        // 循环内定义的“循环不变”纯计算标量链提升到 entry（旧循环块将被删除）。
        // isLoopInvariantVal 递归判定后，step/bound/mod 等可能位于循环 header。
        std::map<Value *, Value *> hoistedScalars;
        std::function<Value *(Value *)> hoistScalar = [&](Value *v) -> Value *
        {
            if (!v || dynamic_cast<Constant *>(v))
                return v;
            auto *inst = dynamic_cast<Instruction *>(v);
            if (!inst || !pat.loop.containsInst(inst))
                return v;
            auto it = hoistedScalars.find(inst);
            if (it != hoistedScalars.end())
                return it->second;
            Instruction *clone = inst->clone();
            clone->setName(clone->getName() + "_lvh");
            for (unsigned i = 0; i < clone->getOperands().size(); ++i)
                clone->setOperandByIndex(i, hoistScalar(inst->getOperandByIndex(i)));
            prependBeforeTerminator(entry, clone);
            hoistedScalars[inst] = clone;
            return clone;
        };
        xInit = hoistScalar(xInit);
        step = hoistScalar(step);
        bound = hoistScalar(bound);
        mod = hoistScalar(mod);
        sumInit = hoistScalar(sumInit);

        // ---- entry 中计算 N = (xInit < bound) ? (bound - xInit + step - 1) / step : 0 ----
        auto *cmpN = new ICmpInst(ICmpInst::ICMP_SLT, xInit, bound, freshName("ncond"));
        auto *n1 = new BinaryOperator(Opcode::Sub, bound, xInit, freshName("nd"));
        auto *n2 = new BinaryOperator(Opcode::Add, n1, step, freshName("np"));
        auto *n3 = new BinaryOperator(Opcode::Sub, n2, ci(1), freshName("nm"));
        auto *n4 = new BinaryOperator(Opcode::SDiv, n3, step, freshName("nq"));
        auto *n5 = new SelectInst(cmpN, n4, ci(0), freshName("n"));
        prependBeforeTerminator(entry, cmpN);
        prependBeforeTerminator(entry, n1);
        prependBeforeTerminator(entry, n2);
        prependBeforeTerminator(entry, n3);
        prependBeforeTerminator(entry, n4);
        prependBeforeTerminator(entry, n5);

        auto &bbs = func->getBasicBlocks();
        auto body = new BasicBlock(freshName("vbody"), func);
        auto exit = new BasicBlock(freshName("vexit"), func);
        bbs.push_back(unique_ptr<BasicBlock>(body));
        bbs.push_back(unique_ptr<BasicBlock>(exit));

        auto countPhi = new PhiInst(IntegerType::getInstance(), freshName("count"));
        auto xPhi = new PhiInst(IntegerType::getInstance(), freshName("xcur"));
        auto sumPhi = new PhiInst(IntegerType::getInstance(), freshName("sumacc"));
        body->addInstruction(own(countPhi));
        body->addInstruction(own(xPhi));
        body->addInstruction(own(sumPhi));
        countPhi->addIncoming(n5, entry);
        xPhi->addIncoming(xInit, entry);
        sumPhi->addIncoming(sumInit, entry);

        auto vl = new VecSetVlInst(countPhi, 32, freshName("vl"));
        body->addInstruction(own(vl));

        // vec_x = splat(xCur) + vid(vl) * splat(step)
        auto vid = new VecVidInst(vl, IntegerType::getInstance(), 16, freshName("vid"));
        body->addInstruction(own(vid));
        auto dVec = new VecSplatInst(step, vl, IntegerType::getInstance(), 16, freshName("dvec"));
        body->addInstruction(own(dVec));
        auto kVec = new VecBinaryInst(Opcode::VecMul, vid, dVec, freshName("kvec"));
        body->addInstruction(own(kVec));
        auto xVec = new VecSplatInst(xPhi, vl, IntegerType::getInstance(), 16, freshName("xvec"));
        body->addInstruction(own(xVec));
        auto xVec2 = new VecBinaryInst(Opcode::VecAdd, xVec, kVec, freshName("xv2"));
        body->addInstruction(own(xVec2));

        // 翻译 fx 计算链
        map<Value *, Value *> cache;
        Value *fxVec = translateToVec(fx, body, vl, xVec2, pat, cache);
        if (!fxVec)
        {
            // 清理已创建的块
            bbs.erase(remove_if(bbs.begin(), bbs.end(),
                                [&](const unique_ptr<BasicBlock> &p)
                                {
                                    return p.get() == body || p.get() == exit;
                                }),
                      bbs.end());
            return false;
        }

        // vec_f + 1，然后横向归约
        auto oneVec = new VecSplatInst(ci(1), vl, IntegerType::getInstance(), 16, freshName("one"));
        body->addInstruction(own(oneVec));
        auto fp1 = new VecBinaryInst(Opcode::VecAdd, fxVec, oneVec, freshName("fp1"));
        body->addInstruction(own(fp1));
        auto blockSum = new VecReduceAddInst(fp1, vl, freshName("bsum"));
        body->addInstruction(own(blockSum));

        // sumN = (sumAcc + blockSum % mod) % mod —— 先对 blockSum 取模防止溢出
        auto bm = new BinaryOperator(Opcode::SRem, blockSum, mod, freshName("bsm"));
        auto sAdd = new BinaryOperator(Opcode::Add, sumPhi, bm, freshName("sadd"));
        auto sumN = new BinaryOperator(Opcode::SRem, sAdd, mod, freshName("snew"));
        body->addInstruction(own(bm));
        body->addInstruction(own(sAdd));
        body->addInstruction(own(sumN));
        sumPhi->addIncoming(sumN, body);

        // countN = count - vl；xN = xCur + vl*step
        auto countN = new BinaryOperator(Opcode::Sub, countPhi, vl, freshName("countn"));
        body->addInstruction(own(countN));
        countPhi->addIncoming(countN, body);
        auto vld = new BinaryOperator(Opcode::Mul, vl, step, freshName("vld"));
        body->addInstruction(own(vld));
        auto xN = new BinaryOperator(Opcode::Add, xPhi, vld, freshName("xn"));
        body->addInstruction(own(xN));
        xPhi->addIncoming(xN, body);

        auto cond = new ICmpInst(ICmpInst::ICMP_NE, countN, ci(0), freshName("cond"));
        body->addInstruction(own(cond));
        body->addInstruction(own(new BranchInst(cond, body, exit)));

        // ---- exit 块：br exitBlock ----
        exit->addInstruction(own(new BranchInst(exitBlock)));

        // ---- CFG 重连 ----
        retargetEntryEdge(entry, header, body);
        // entry 原条件分支依赖已删除的循环头 phi。改为 count != 0 才进入向量循环体；
        // count == 0（无迭代）直接跳 vexit，避免 vl=0 下向量指令（vmv.x.s 读累加器）
        // 读取未初始化寄存器
        {
            auto &entryInsts = entry->getInstructions();
            if (!entryInsts.empty() && entryInsts.back()->Op == Opcode::Br)
                entryInsts.back()->removeThisFromOperands();
            entryInsts.pop_back();
            auto entryCond = new ICmpInst(ICmpInst::ICMP_NE, n5, ci(0), freshName("encond"));
            entry->addInstruction(own(entryCond));
            entry->addInstruction(own(new BranchInst(entryCond, body, exit)));
        }
        wireEdge(body, body);
        wireEdge(body, exit);
        wireEdge(entry, exit);
        wireEdge(exit, exitBlock);

        // ---- exitBlock 的 phi：旧循环内 incoming 改由 exit 提供 ----
        for (auto &instPtr : exitBlock->getInstructions())
        {
            auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
            if (!phi)
                continue;
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                auto *inBB = phi->getIncomingBlock(i);
                if (inBB && pat.loop.containsBlock(inBB))
                    phi->setIncomingBlock(i, exit);
            }
        }

        // ---- exitBlock 中对旧 sum 的循环外 use 替换 ----
        // vexit 是汇合点：count==0 时从 entry 直接到（sum 保持 sumInit），
        // 否则从 vbody 到（sum 取向量累加器 sumPhi 的最终值）
        auto sumOut = new PhiInst(IntegerType::getInstance(), freshName("sumout"));
        sumOut->addIncoming(sumInit, entry);
        sumOut->addIncoming(sumPhi, body);
        prependBeforeTerminator(exit, sumOut);
        for (auto *user : sum->getUsers())
        {
            auto *uinst = dynamic_cast<Instruction *>(user);
            if (!uinst || pat.loop.containsInst(uinst))
                continue;
            uinst->replaceOperand(sum, sumOut);
        }

        removeLoopBlocks(func, pat.bodyBlocks);
        return true;
    }

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
            // 无数组访存的纯标量链循环（x += d 归纳 + f(x) 计算 + sum 归约）
            if (vectorizeScalarChainLoop(func, loop))
            {
                localChanged = true;
                changed = true;
                if (verbose)
                    debugInfo << "LoopVectorize: scalar-chain loop @ " << func->getName()
                              << " header=" << loop.header->getName() << "\n";
                break;
            }
        }
    }

    func->setLoops(ControlFlowAnalysis::findLoops(func));
    matrixStructure::clearAnalysis(func);
    return changed;
}
