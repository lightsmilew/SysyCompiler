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

    static ConstantFloat *cf(float v)
    {
        return new ConstantFloat(FloatType::getInstance(), v);
    }

    static Type *elemTypeOf(Value *v)
    {
        if (!v || !v->getType())
            return IntegerType::getInstance();
        if (v->getType()->isFloatTy())
            return FloatType::getInstance();
        return IntegerType::getInstance();
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

    // 断开并移除给定基本块（从函数块列表删除）。
    // 返回 false 表示存在块外存活指令引用了待删块中的指令（变换不完整，
    // 若强行删除会产生悬垂指针，如 while 多轮向量化中 entry 的 rvv GEP
    // 引用外层循环 phi）。调用方应放弃本次变换。
    static bool removeLoopBlocks(Function *func, const vector<BasicBlock *> &blocks)
    {
        // 收集待删块内所有指令
        std::set<Instruction *> doomed;
        for (auto *bb : blocks)
        {
            if (!bb)
                continue;
            for (auto &instPtr : bb->getInstructions())
                doomed.insert(instPtr.get());
        }
        // 检查块外存活指令是否引用待删指令（phi 的 incoming 块引用除外）
        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            if (find(blocks.begin(), blocks.end(), bb) != blocks.end())
                continue;
            for (auto &instPtr : bb->getInstructions())
            {
                Instruction *inst = instPtr.get();
                for (unsigned i = 0; i < inst->getNumOperands(); ++i)
                {
                    Value *op = inst->getOperandByIndex(i);
                    if (auto *opInst = dynamic_cast<Instruction *>(op))
                        if (doomed.count(opInst))
                            return false;
                }
            }
        }
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
        return true;
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
//     ptr_iN  = gep ptr_i, advance_i     （advance_i = vl，或 vl * advances[i] 当 strides 访问）
//     cond    = icmp ne countN, 0
//     br cond, body, exit
//   exit:
//     br exitBlock
//
// advances 为可选参数：advances[i] 非空表示第 i 个指针为 strided 访问，
// 每块推进 vl * advances[i] 个元素；为空数组或对应项为 nullptr 时按单位步长推进 vl。
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
    const function<void(BasicBlock *, Value *, const vector<Value *> &)> &emitBody,
    const vector<Value *> &advances = {})
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
        Value *adv = vl;
        if (!advances.empty() && advances[i])
        {
            // strided 指针：每块推进 vl * stride 个元素
            adv = new BinaryOperator(Opcode::Mul, vl, advances[i], freshName("ptrStep"));
            body->addInstruction(own(static_cast<Instruction *>(adv)));
        }
        auto gep = new GetElementPtrInst(ptrPhis[i], vector<Value *>{adv}, freshName("ptrN"));
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
        case Opcode::FAdd:
        case Opcode::FSub:
        case Opcode::FMul:
        case Opcode::FDiv:
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

    // 尝试把操作数解析为 jIV 或 jIV * S（S 循环不变）；SOut=nullptr 表示单位步长
    static bool parseJFactor(Value *op, Value *jIV, const Loop &loop, Value *&SOut)
    {
        SOut = nullptr;
        op = stripCopy(op);
        if (matchesLoopIV(op, jIV))
            return true;
        auto *bin = dynamic_cast<BinaryOperator *>(op);
        if (!bin || bin->getOpcode() != Opcode::Mul)
            return false;
        Value *a = stripCopy(bin->getLHS());
        Value *b = stripCopy(bin->getRHS());
        bool aIsJ = matchesLoopIV(a, jIV);
        bool bIsJ = matchesLoopIV(b, jIV);
        Value *S = nullptr;
        if (aIsJ && !bIsJ)
            S = bin->getRHS();
        else if (bIsJ && !aIsJ)
            S = bin->getLHS();
        else
            return false;
        if (!isLoopInvariantVal(S, loop))
            return false;
        // 常量 S==1 视为单位步长（无 strided 访存）
        if (auto *c = dynamic_cast<ConstantInt *>(stripCopy(S)))
        {
            if (c->Value == 1)
                return true;
        }
        SOut = S;
        return true;
    }

    // 合成循环不变偏移 init + extra：常量为 0 时直接折叠，否则在 entry 创建 add。
    static Value *composeOffset(Value *init, Value *extra, BasicBlock *entry)
    {
        init = stripCopy(init);
        extra = stripCopy(extra);
        if (auto *c = dynamic_cast<ConstantInt *>(init))
            if (c->Value == 0)
                return extra;
        if (auto *c = dynamic_cast<ConstantInt *>(extra))
            if (c->Value == 0)
                return init;
        auto *add = new BinaryOperator(Opcode::Add, init, extra, freshName("stridedOff"));
        entry->insertBeforeTerminator(std::unique_ptr<Instruction>(add));
        return add;
    }

    // 识别循环 header 中的仿射归纳 phi（ISR 强度消减产物）：
    //   %p = phi [init, entry], [add(%p, step), latch]
    // 表示 p(j) = init + j*step，其中 init/step 循环不变。
    // 若 covered 非空，将 latch 增量链记入 covered（覆盖检查放行）。
    static bool parseAffinePhi(Value *v, const Loop &loop,
                               Value *&initOut, Value *&stepOut,
                               std::set<Instruction *> *covered = nullptr)
    {
        v = stripCopy(v);
        auto *phi = dynamic_cast<PhiInst *>(v);
        if (!phi || !loop.containsInst(phi))
            return false;
        Value *init = nullptr, *step = nullptr;
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            BasicBlock *pred = phi->getIncomingBlock(i);
            Value *incVal = phi->getIncomingValue(i);
            if (loop.containsBlock(pred))
            {
                Value *inc = stripCopy(incVal);
                auto *bin = dynamic_cast<BinaryOperator *>(inc);
                if (!bin || bin->getOpcode() != Opcode::Add)
                    return false;
                Value *a = stripCopy(bin->getLHS());
                Value *b = stripCopy(bin->getRHS());
                if (sameValue(a, phi))
                    step = bin->getRHS();
                else if (sameValue(b, phi))
                    step = bin->getLHS();
                else
                    return false;
                if (!isLoopInvariantVal(step, loop))
                    return false;
                if (covered)
                {
                    covered->insert(bin);
                    if (auto *stepInst = dynamic_cast<Instruction *>(step))
                        if (loop.containsInst(stepInst))
                            covered->insert(stepInst);
                }
            }
            else
            {
                init = incVal;
                if (!isLoopInvariantVal(init, loop))
                    return false;
            }
        }
        if (!init || !step)
            return false;
        initOut = init;
        stepOut = step;
        return true;
    }

    // 解析一维线性索引（列索引）：
    //   idx == jIV           → invOut=nullptr, strideOut=nullptr（单位步长）
    //   idx == jIV + inv     → invOut=inv,     strideOut=nullptr
    //   idx == jIV*S + inv   → invOut=inv,     strideOut=S（strided，步长 S 个元素）
    //   idx == jIV*S         → invOut=nullptr, strideOut=S
    //   idx == 仿射 phi      → invOut=init,    strideOut=step（ISR 消减后的 j*step）
    //   idx == add(phi, inv) → invOut=init+inv, strideOut=step
    // 步长 S 必须循环不变；jIV 只允许出现一次。
    static bool parseLinearIdx(Value *idx, Value *jIV, const Loop &loop,
                               Value *&invOut, Value *&strideOut,
                               std::set<Instruction *> *covered = nullptr,
                               BasicBlock *entry = nullptr,
                               Value *jInit = nullptr)
    {
        invOut = nullptr;
        strideOut = nullptr;
        idx = stripCopy(idx);
        if (matchesLoopIV(idx, jIV))
        {
            // IV 初值非零（如 j 从 mid 开始、l 从参数开始）：偏移必须包含初值，
            // 否则向量地址起点会指向数组头部，错误写越界。
            if (jInit)
            {
                Value *j = stripCopy(jInit);
                auto *c = dynamic_cast<ConstantInt *>(j);
                if (!c || c->Value != 0)
                    invOut = jInit;
            }
            return true;
        }
        // 纯仿射 phi
        {
            Value *init, *step;
            if (parseAffinePhi(idx, loop, init, step, covered))
            {
                invOut = init;
                strideOut = step;
                return true;
            }
        }
        // 纯 jIV*S 形式（如 j*2*stride = mul(jIV, mul(2,stride))）
        auto *pureMul = dynamic_cast<BinaryOperator *>(idx);
        if (pureMul && pureMul->getOpcode() == Opcode::Mul)
        {
            Value *s = nullptr;
            if (parseJFactor(pureMul, jIV, loop, s))
            {
                strideOut = s;
                return true;
            }
        }
        auto *bin = dynamic_cast<BinaryOperator *>(idx);
        if (!bin || bin->getOpcode() != Opcode::Add)
            return false;
        Value *a = stripCopy(bin->getLHS());
        Value *b = stripCopy(bin->getRHS());
        // add(仿射 phi, inv)：offset = init + inv
        {
            Value *init, *step;
            if (parseAffinePhi(a, loop, init, step, covered) &&
                isLoopInvariantVal(bin->getRHS(), loop))
            {
                strideOut = step;
                invOut = composeOffset(init, bin->getRHS(), entry);
                return true;
            }
            if (parseAffinePhi(b, loop, init, step, covered) &&
                isLoopInvariantVal(bin->getLHS(), loop))
            {
                strideOut = step;
                invOut = composeOffset(init, bin->getLHS(), entry);
                return true;
            }
        }
        Value *s1 = nullptr, *s2 = nullptr;
        bool aIsJ = parseJFactor(a, jIV, loop, s1);
        bool bIsJ = parseJFactor(b, jIV, loop, s2);
        if (aIsJ && !bIsJ && isLoopInvariantVal(bin->getRHS(), loop))
        {
            strideOut = s1;
            invOut = bin->getRHS();
            return true;
        }
        if (bIsJ && !aIsJ && isLoopInvariantVal(bin->getLHS(), loop))
        {
            strideOut = s2;
            invOut = bin->getLHS();
            return true;
        }
        return false;
    }

    // 解析数组访问地址（1D/2D/3D，支持嵌套 GEP），要求列索引为 jIV 的线性函数
    // （jIV + 不变偏移，或 jIV*S + 不变偏移，S 为循环不变步长）。
    // 输出 base=最外层数组基址，rowIdxs=逐层行索引（外层→内层），offset=列不变偏移，
    // stride=元素步长（nullptr 表示单位步长）。body 内的 GEP 会被记入 covered。
    // entry 为 j 循环 preheader（循环不变指令的插入点）。
    static bool parseVecAddr(Value *ptr, Value *jIV, const Loop &loop,
                             std::set<Instruction *> &covered,
                             Value *&base, std::vector<Value *> &rowIdxs,
                             Value *&offset, Value *&stride,
                             BasicBlock *entry,
                             Value *jInit = nullptr)
    {
        base = nullptr;
        rowIdxs.clear();
        offset = nullptr;
        stride = nullptr;

        auto recordGep = [&](GetElementPtrInst *g)
        {
            if (g && loop.containsInst(g))
                covered.insert(g);
        };

        // 从最内层 GEP 出发逐层收集 GEP 链（最内层在末尾）
        std::vector<GetElementPtrInst *> chain;
        Value *cur = stripCopy(ptr);
        while (auto *gep = dynamic_cast<GetElementPtrInst *>(stripCopy(cur)))
        {
            chain.push_back(gep);
            cur = gep->getPointerOperand();
        }
        if (chain.empty())
        {
            return false;
        }
        for (auto *g : chain)
            recordGep(g);

        // 最内层 GEP：最后一个索引是列索引（jIV 线性），其余为其行索引
        auto *inner = chain.back();
        auto innerIdx = inner->getIndices();
        if (innerIdx.empty())
            return false;
        if (!parseLinearIdx(innerIdx.back(), jIV, loop, offset, stride, &covered, entry, jInit))
        {
            return false;
        }
        // 覆盖列索引计算链（如 add(mul(jIV,S), inv)）中的指令：重建地址后这些
        // 指令成为死代码，随循环块一起删除，但覆盖检查必须放行。
        // phi（归纳变量）可能构成循环引用，需 visited 去重并跳过。
        std::set<Value *> idxVisited;
        std::function<void(Value *)> coverIdx;
        coverIdx = [&](Value *v)
        {
            if (!v || !idxVisited.insert(v).second)
                return;
            v = stripCopy(v);
            auto *inst = dynamic_cast<Instruction *>(v);
            if (!inst || dynamic_cast<PhiInst *>(inst))
                return;
            if (loop.containsInst(inst))
                covered.insert(inst);
            for (auto *op : inst->getOperands())
                coverIdx(op);
        };
        coverIdx(innerIdx.back());
        std::vector<Value *> rows;
        for (size_t k = 0; k + 1 < innerIdx.size(); ++k)
        {
            Value *r = stripCopy(innerIdx[k]);
            if (!isLoopInvariantVal(r, loop))
                return false;
            rows.push_back(r);
            recordInvariantChain(r, loop, covered);
        }

        // 外层 GEP：每个 GEP 的第一个索引为行索引（循环不变），
        // 其余索引必须为常量 0（行首选择，重建时省略）。
        for (int i = (int)chain.size() - 2; i >= 0; --i)
        {
            auto gidx = chain[i]->getIndices();
            if (gidx.empty())
                return false;
            Value *r = stripCopy(gidx[0]);
            if (!isLoopInvariantVal(r, loop))
                return false;
            for (size_t k = 1; k < gidx.size(); ++k)
            {
                auto *c = dynamic_cast<ConstantInt *>(stripCopy(gidx[k]));
                if (!c || c->Value != 0)
                    return false;
            }
            rows.push_back(r);
            recordInvariantChain(r, loop, covered);
        }
        // rows 从最内层行索引开始收集，需反转为外层→内层
        std::reverse(rows.begin(), rows.end());
        rowIdxs = std::move(rows);

        if (offset)
            recordInvariantChain(offset, loop, covered);
        if (stride)
            recordInvariantChain(stride, loop, covered);

        // base = 最外层 GEP 的指针操作数（数组基址）
        base = chain[0]->getPointerOperand();
        return true;
    }

    // 从标量表达式递归构建可向量化表达式树
    static bool buildElemExpr(Value *val, Value *jIV, const Loop &loop,
                              std::set<Instruction *> &covered,
                              std::unique_ptr<ElemExpr> &out,
                              BasicBlock *entry,
                              Value *jInit = nullptr)
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
        if (auto *c = dynamic_cast<ConstantFloat *>(raw))
        {
            auto e = std::make_unique<ElemExpr>();
            e->kind = ElemKind::CONST;
            e->isFloatConst = true;
            e->fval = c->Value;
            out = std::move(e);
            return true;
        }
        // sitofp i32 C → float 常量（h-10 中 `+ 1` 常见形态）
        if (auto *cast = dynamic_cast<CastInst *>(raw))
        {
            if (cast->getOpcode() == Opcode::SIToFP)
            {
                if (auto *c = dynamic_cast<ConstantInt *>(stripCopy(cast->getOperand())))
                {
                    auto e = std::make_unique<ElemExpr>();
                    e->kind = ElemKind::CONST;
                    e->isFloatConst = true;
                    e->fval = static_cast<float>(c->Value);
                    e->ir = cast;
                    covered.insert(cast);
                    out = std::move(e);
                    return true;
                }
            }
        }
        auto *inst = dynamic_cast<Instruction *>(raw);
        if (!inst)
        {
            if (isLoopInvariantVal(val, loop))
            {
                auto e = std::make_unique<ElemExpr>();
                e->kind = ElemKind::INVARIANT;
                e->scalar = val;
                out = std::move(e);
                return true;
            }
            return false;
        }
        if (auto *ld = dynamic_cast<LoadInst *>(inst))
        {
            Value *base, *offset, *stride;
            std::vector<Value *> rowIdxs;
            // 沿 j 的向量 load
            if (parseVecAddr(ld->getPointer(), jIV, loop, covered, base, rowIdxs, offset, stride,
                             entry, jInit))
            {
                auto e = std::make_unique<ElemExpr>();
                e->kind = ElemKind::LOAD;
                e->base = base;
                e->rowIdxs = rowIdxs;
                e->offset = offset;
                e->stride = stride;
                e->ir = ld;
                covered.insert(ld);
                out = std::move(e);
                return true;
            }
            // 地址相对 j 循环不变的标量 load（如 A[i][i]）→ 提升后 splat
            if (isLoopInvariantVal(ld->getPointer(), loop))
            {
                auto e = std::make_unique<ElemExpr>();
                e->kind = ElemKind::INVARIANT;
                e->scalar = ld;
                e->ir = ld;
                covered.insert(ld);
                recordInvariantChain(ld->getPointer(), loop, covered);
                out = std::move(e);
                return true;
            }
            return false;
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
        if (auto *bin = dynamic_cast<BinaryOperator *>(inst))
        {
            ElemKind k;
            switch (bin->getOpcode())
            {
            case Opcode::Add:
            case Opcode::FAdd: k = ElemKind::ADD; break;
            case Opcode::Sub:
            case Opcode::FSub: k = ElemKind::SUB; break;
            case Opcode::Mul:
            case Opcode::FMul: k = ElemKind::MUL; break;
            case Opcode::SDiv:
            case Opcode::FDiv: k = ElemKind::DIV; break;
            case Opcode::SRem: k = ElemKind::REM; break;
            case Opcode::Sll: k = ElemKind::SLL; break;
            case Opcode::Sra: k = ElemKind::SRA; break;
            default: return false;
            }
            std::unique_ptr<ElemExpr> lhs, rhs;
            if (!buildElemExpr(bin->getLHS(), jIV, loop, covered, lhs, entry, jInit))
                return false;
            if (!buildElemExpr(bin->getRHS(), jIV, loop, covered, rhs, entry, jInit))
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
        {
            return false;
        }
        BasicBlock *jHeader = jLoop.header;
        BasicBlock *jBody = getLoopLatch(jLoop);
        BasicBlock *jExit = getLoopExit(jLoop);
        if (!jHeader || !jBody || !jExit || jBody == jHeader)
        {
            return false;
        }

        Value *jIV = nullptr, *bound = nullptr;
        ICmpInst *cmp = nullptr;
        if (!getHeaderBoundCmp(jHeader, jIV, bound, cmp))
        {
            return false;
        }

        // 可被完全展开的常量小循环（如 while(i<5)）交给循环展开 inline 化，不做向量化
        if (isFullyUnrollableLoop(jLoop, jIV, bound))
        {
            return false;
        }

        vector<BasicBlock *> entryPreds;
        for (auto *pred : jHeader->getPredecessors())
        {
            if (!jLoop.containsBlock(pred))
                entryPreds.push_back(pred);
        }
        if (entryPreds.size() != 1)
        {
            return false;
        }
        BasicBlock *entry = entryPreds[0];
        if (!isValidVectorEntry(entry, jHeader))
        {
            return false;
        }

        // jIV 在 entry 边的初值（非零初值时向量指针起点与 trip count 都要包含它）
        Value *jInit = nullptr;
        if (auto *jPhi = dynamic_cast<PhiInst *>(stripCopy(jIV)))
        {
            for (unsigned i = 0; i < jPhi->getNumIncomingValues(); ++i)
            {
                if (jPhi->getIncomingBlock(i) == entry)
                {
                    jInit = stripCopy(jPhi->getIncomingValue(i));
                    break;
                }
            }
        }

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
        out.jInit = jInit;
        out.bound = bound;

        int storeCount = 0;
        for (auto &instPtr : jBody->getInstructions())
        {
            auto *st = dynamic_cast<StoreInst *>(instPtr.get());
            if (!st)
                continue;
            storeCount++;
            Value *base, *offset, *stride;
            std::vector<Value *> rowIdxs;
            if (!parseVecAddr(st->getPointer(), jIV, jLoop, covered, base, rowIdxs, offset, stride, entry, jInit))
            {
                return false;
            }
            ElemStore es;
            es.base = base;
            es.rowIdxs = rowIdxs;
            es.offset = offset;
            es.stride = stride;
            if (!buildElemExpr(st->getValueToStore(), jIV, jLoop, covered, es.expr, entry, jInit))
            {
                return false;
            }
            Type *stTy = elemTypeOf(st->getValueToStore());
            if (!out.elemTy)
                out.elemTy = stTy;
            else if (out.elemTy != stTy)
                return false;
            out.stores.push_back(std::move(es));
        }
        if (storeCount == 0)
        {
            return false;
        }
        if (!out.elemTy)
            out.elemTy = IntegerType::getInstance();

        // 覆盖检查：body 内所有非控制指令必须被某个 store 表达式访问
        for (auto *inst : computeInsts)
        {
            if (covered.count(inst) == 0)
            {
                return false;
            }
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

        // 安全护栏：若 jHeader/jBody 内的指令被「本次变换之外的存活块」引用
        // （例如多轮迭代中，内层向量化生成的 entry GEP 引用外层循环的值，
        //  随后外层循环又被向量化），删除旧循环会产生悬垂引用。
        // 这种情况下放弃本次向量化。
        {
            std::set<Instruction *> doomed;
            auto collect = [&](BasicBlock *bb)
            {
                if (!bb)
                    return;
                for (auto &ip : bb->getInstructions())
                    doomed.insert(ip.get());
            };
            collect(jHeader);
            collect(jBody);
            for (auto &bbPtr : func->getBasicBlocks())
            {
                BasicBlock *bb = bbPtr.get();
                if (bb == jHeader || bb == jBody)
                    continue;
                for (auto &ip : bb->getInstructions())
                {
                    Instruction *inst = ip.get();
                    // phi 的 incoming 值引用旧循环指令是合法场景：
                    // buildVectorLoop 会把 exitBlock 的 phi incoming 重连到新循环。
                    if (dynamic_cast<PhiInst *>(inst))
                        continue;
                    for (unsigned i = 0; i < inst->getNumOperands(); ++i)
                    {
                        Value *op = inst->getOperandByIndex(i);
                        if (auto *opInst = dynamic_cast<Instruction *>(op))
                            if (doomed.count(opInst))
                                return false;
                    }
                }
            }
        }

        // 循环内定义的“循环不变”纯计算标量链（如行偏移 r*N_eff 被其他 pass
        // 放入循环 header）需克隆到 entry：旧循环块随后会被删除，新向量指令
        // 不能引用其中的指令，否则 removeThisFromOperands 会清掉其操作数。
        std::map<Value *, Value *> hoistedScalars;
        // 判断 inst 是否物理位于将被删除的旧循环块（jHeader/jBody）内。
        // 不能用 containsByName：无名字指令（空 name）匹配会失效，
        // 导致克隆跳过、引用随旧循环删除而悬垂。
        auto inOldLoop = [&](Instruction *inst) -> bool
        {
            auto inBlock = [&](BasicBlock *bb)
            {
                if (!bb)
                    return false;
                for (auto &ip : bb->getInstructions())
                {
                    if (ip.get() == inst)
                        return true;
                }
                return false;
            };
            return inBlock(jHeader) || inBlock(jBody);
        };
        std::function<Value *(Value *)> hoistScalar = [&](Value *v) -> Value *
        {
            if (!v || dynamic_cast<Constant *>(v))
                return v;
            auto *inst = dynamic_cast<Instruction *>(v);
            if (!inst)
                return v;
            if (!inOldLoop(inst))
            {
                return v;
            }
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

        // 为所有地址（store + 表达式 load）构建行基址 GEP，去重。
        // 每个指针对应一个推进步长（advances[i]：nullptr=单位步长，否则为元素步长）。
        vector<Value *> ptrs;
        vector<Value *> advances;
        std::map<string, size_t> addrIndex;
        auto ensurePtr = [&](Value *base, const std::vector<Value *> &rowIdxs, Value *off,
                             Value *stride) -> size_t
        {
            // key 必须能区分常量索引（0 vs 1）：常量无名字，getName() 均为空，
            // 直接用 name 会导致不同行索引（如 buf[0][j] 与 buf[1][j]）被错误去重。
            auto valKey = [&](Value *v) -> string
            {
                v = stripCopy(v);
                if (auto *c = dynamic_cast<ConstantInt *>(v))
                    return "c" + to_string(c->Value);
                if (auto *c = dynamic_cast<ConstantFloat *>(v))
                    return "f" + to_string(c->Value);
                return v ? v->getName() : "0";
            };
            string key = valKey(base);
            for (auto *r : rowIdxs)
                key += "@" + valKey(r);
            key += "#" + valKey(off);
            key += "|" + valKey(stride);
            auto it = addrIndex.find(key);
            if (it != addrIndex.end())
                return it->second;
            Value *offVal = off ? hoistScalar(off) : ci(0);
            vector<Value *> idxs;
            for (auto *r : rowIdxs)
                idxs.push_back(hoistScalar(r));
            idxs.push_back(offVal);
            auto *gep = new GetElementPtrInst(base, idxs, freshName("row"));
            prependBeforeTerminator(entry, gep);
            size_t idx = ptrs.size();
            ptrs.push_back(gep);
            // 推进步长必须在 entry 克隆：原始 stride 可能位于将被删除的旧循环体内
            advances.push_back(stride ? hoistScalar(stride) : nullptr);
            addrIndex[key] = idx;
            return idx;
        };

        // 字节步长 = 元素步长 * 4（SEW=32）。常量直接折叠；非常量在 entry 生成 shl 2。
        std::map<Value *, Value *> byteStrideCache;
        auto byteStrideOf = [&](Value *strideElem) -> Value *
        {
            if (auto *c = dynamic_cast<ConstantInt *>(stripCopy(strideElem)))
                return ci(c->Value * 4);
            auto it = byteStrideCache.find(strideElem);
            if (it != byteStrideCache.end())
                return it->second;
            auto *bs = new BinaryOperator(Opcode::Sll, hoistScalar(strideElem), ci(2),
                                          freshName("bs"));
            prependBeforeTerminator(entry, bs);
            byteStrideCache[strideElem] = bs;
            return bs;
        };

        struct StoreInfo
        {
            size_t ptrIdx;
            Value *stride;
            std::unique_ptr<ElemExpr> expr;
        };
        vector<StoreInfo> stores;
        for (const auto &es : pat.stores)
        {
            size_t idx = ensurePtr(es.base, es.rowIdxs, es.offset, es.stride);
            StoreInfo si;
            si.ptrIdx = idx;
            si.stride = es.stride;
            // 深拷贝表达式树（ElemExpr 含 unique_ptr，需逐字段复制）
            std::function<void(const ElemExpr &, std::unique_ptr<ElemExpr> &)> cloneTree;
            cloneTree = [&](const ElemExpr &src, std::unique_ptr<ElemExpr> &dst)
            {
                auto e = std::make_unique<ElemExpr>();
                e->kind = src.kind;
                e->cval = src.cval;
                e->fval = src.fval;
                e->isFloatConst = src.isFloatConst;
                e->scalar = src.scalar;
                e->base = src.base;
                e->rowIdxs = src.rowIdxs;
                e->offset = src.offset;
                e->stride = src.stride;
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
                    loadPtrIdx[&e] = ensurePtr(e.base, e.rowIdxs, e.offset, e.stride);
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

        // trip count = bound - jInit。bound 可能位于将被删除的旧循环 header 内
        // （如 arrCopy 的 bound = load @n 由 LICM/内联放进 header），必须先提升到 entry，
        // 否则 removeLoopBlocks 因外部引用放弃删除、或删除后 countPhi 悬垂。
        Value *boundSafe = hoistScalar(bound);
        Value *count0 = boundSafe;
        if (pat.jInit)
        {
            Value *j = stripCopy(pat.jInit);
            auto *jc = dynamic_cast<ConstantInt *>(j);
            if (!jc || jc->Value != 0)
            {
                count0 = new BinaryOperator(Opcode::Sub, boundSafe, hoistScalar(pat.jInit),
                                            freshName("count0"));
                prependBeforeTerminator(entry, static_cast<Instruction *>(count0));
            }
        }

        buildVectorLoop(
            func, entry, jHeader, jExit, count0, jIV, boundSafe, ptrs, ptrNames,
            freshName("vbody"), freshName("vexit"),
            [&](BasicBlock *body, Value *vl, const vector<Value *> &ptrs)
            {
                Type *elemTy = pat.elemTy ? pat.elemTy : IntegerType::getInstance();
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
                        if (e.isFloatConst || elemTy->isFloatTy())
                            res = new VecSplatInst(cf(e.isFloatConst ? e.fval
                                                                     : static_cast<float>(e.cval)),
                                                   vl, FloatType::getInstance(), 16,
                                                   freshName("v"));
                        else
                            res = new VecSplatInst(ci(static_cast<int>(e.cval)), vl,
                                                   IntegerType::getInstance(), 16,
                                                   freshName("v"));
                        break;
                    case ElemKind::INVARIANT:
                        res = new VecSplatInst(hoistScalar(e.scalar), vl, elemTypeOf(e.scalar),
                                               16, freshName("v"));
                        break;
                    case ElemKind::LOAD:
                        if (e.stride)
                            res = new VecStridedLoadInst(
                                ptrs[loadPtrIdx[&e]], byteStrideOf(e.stride), vl,
                                elemTy, 16, freshName("v"));
                        else
                            res = new VecLoadInst(ptrs[loadPtrIdx[&e]], vl,
                                                  elemTy, 16, freshName("v"));
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
                        case ElemKind::DIV: op = Opcode::VecDiv; break;
                        case ElemKind::REM: op = Opcode::VecRem; break;
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
                    if (si.stride)
                        body->addInstruction(own(new VecStridedStoreInst(
                            result, ptrs[si.ptrIdx], byteStrideOf(si.stride), vl)));
                    else
                        body->addInstruction(own(new VecStoreInst(result, ptrs[si.ptrIdx], vl)));
                }
            },
            advances);

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

    // 与 LoopUnroll 一致：SLT 用 ceil((bound-init)/step)
    if (stepConst <= 0 || boundConst->Value <= initConst->Value)
        return false;
    int tripCount = (boundConst->Value - initConst->Value + stepConst - 1) / stepConst;
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

        // ---- CFG 重连：独立 vguard 做 count!=0 判断，保留 entry 其它出边 ----
        auto guard = new BasicBlock(freshName("vguard"), func);
        bbs.push_back(unique_ptr<BasicBlock>(guard));
        auto entryCond = new ICmpInst(ICmpInst::ICMP_NE, n5, ci(0), freshName("encond"));
        guard->addInstruction(own(entryCond));
        guard->addInstruction(own(new BranchInst(entryCond, body, exit)));
        // body/exit 的 phi 原先以 entry 为前驱，改为 guard
        countPhi->setIncomingBlock(0, guard);
        xPhi->setIncomingBlock(0, guard);
        sumPhi->setIncomingBlock(0, guard);
        retargetEntryEdge(entry, header, guard);
        wireEdge(guard, body);
        wireEdge(guard, exit);
        wireEdge(body, body);
        wireEdge(body, exit);
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
        // vexit 汇合：count==0 自 vguard（sumInit）；有趟自 vbody 取 sumN
        auto sumOut = new PhiInst(IntegerType::getInstance(), freshName("sumout"));
        sumOut->addIncoming(sumInit, guard);
        sumOut->addIncoming(sumN, body);
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

    // scale = A[i][k] / A[j][i]；无 skip-guard 时原 load 在 j 体内，需在 entry 新建 load
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

    Type *elemTy = nest.elemTy ? nest.elemTy : IntegerType::getInstance();
    bool isFloat = elemTy->isFloatTy();
    const bool isSubtract = nest.isSubtract;

    // 向量 splat 放在循环体内（紧跟循环 vsetvli 之后）：vmv.v.x 只写当前 VL
    // 范围内的元素，循环内执行才能保证 splat 与 load/store 的 VL 一致
    buildVectorLoop(
        func, entry, jHeader, jExit, nest.bound, nest.jIV, nest.bound, {cRowGep, bRowGep},
        {"cPtr", "bPtr"}, freshName("vbody"), freshName("vexit"),
        [scale, elemTy, isFloat, isSubtract](BasicBlock *body, Value *vl,
                                             const vector<Value *> &ptrs)
        {
            auto vs = new VecSplatInst(scale, vl, elemTy, 16, freshName("vs"));
            body->addInstruction(own(vs));
            auto vc = new VecLoadInst(ptrs[0], vl, elemTy, 16, freshName("vc"));
            body->addInstruction(own(vc));
            auto vb = new VecLoadInst(ptrs[1], vl, elemTy, 16, freshName("vb"));
            body->addInstruction(own(vb));
            Instruction *va = nullptr;
            if (isSubtract)
            {
                auto vm = new VecBinaryInst(Opcode::VecMul, vs, vb, freshName("vm"));
                body->addInstruction(own(vm));
                va = new VecBinaryInst(Opcode::VecSub, vc, vm, freshName("va"));
            }
            else
            {
                auto vm = new VecBinaryInst(Opcode::VecMul, vc, vs, freshName("vm"));
                body->addInstruction(own(vm));
                va = new VecBinaryInst(Opcode::VecAdd, vm, vb, freshName("va"));
            }
            body->addInstruction(own(va));
            body->addInstruction(own(new VecStoreInst(va, ptrs[0], vl)));
        });

    removeLoopBlocks(func, {jHeader, jBody});
    if (verbose)
        debugInfo << "LoopVectorize: vec scaled-row "
                  << (isSubtract ? "sub " : "fma ")
                  << (isFloat ? "float " : "") << "@ " << func->getName()
                  << " j=" << jHeader->getName() << "\n";
    return true;
}

bool LoopVectorizePass::findCopyBasedArrayMaxLoop(const Loop &loop, ArrayReducePattern &pat)
{
    if (loop.blocks.size() < 2 || loop.blocks.size() > 4)
        return false;

    // 在 loop 内找到含 load 与 max/min icmp 的 body 块
    BasicBlock *bodyBB = loop.header;
    LoadInst *ld = nullptr;
    ICmpInst *maxCmp = nullptr;
    for (auto *bb : loop.blocks)
    {
        LoadInst *blockLd = nullptr;
        ICmpInst *blockCmp = nullptr;
        for (auto &ip : bb->getInstructions())
        {
            if (auto *li = dynamic_cast<LoadInst *>(ip.get()))
                blockLd = li;
            if (auto *ci = dynamic_cast<ICmpInst *>(ip.get()))
            {
                if (ci->getPredicate() == ICmpInst::ICMP_SLT)
                    blockCmp = ci;
            }
        }
        if (blockLd && blockCmp)
        {
            ld = blockLd;
            maxCmp = blockCmp;
            bodyBB = bb;
            break;
        }
    }
    if (!ld || !maxCmp)
        return false;

    BasicBlock *header = bodyBB;
    BasicBlock *latch = getLoopLatch(loop);
    BasicBlock *exitBlock = getLoopExit(loop);
    if (!exitBlock)
    {
        auto *lbr = dynamic_cast<BranchInst *>(latch->getTerminator());
        if (lbr && lbr->isConditional())
        {
            for (BasicBlock *bb : {lbr->getTrueBlock(), lbr->getFalseBlock()})
            {
                if (bb && !loop.containsBlock(bb))
                {
                    exitBlock = bb;
                    break;
                }
            }
        }
    }
    if (!header || !latch || !exitBlock || header == latch)
        return false;

    auto *hbr = dynamic_cast<BranchInst *>(header->getTerminator());
    if (!hbr || !hbr->isConditional())
        return false;
    BasicBlock *thenBB = hbr->getTrueBlock();
    BasicBlock *elseBB = hbr->getFalseBlock();

    auto pickCopy = [](BasicBlock *bb) -> CopyInst *
    {
        CopyInst *found = nullptr;
        for (auto &ip : bb->getInstructions())
            if (auto *cpy = dynamic_cast<CopyInst *>(ip.get()))
                found = cpy;
        return found;
    };

    CopyInst *thenCopy = pickCopy(thenBB);
    if (!thenCopy)
        return false;

    CopyInst *elseArmCopy = nullptr;
    if (elseBB == latch)
    {
        for (auto &ip : header->getInstructions())
            if (auto *cpy = dynamic_cast<CopyInst *>(ip.get()))
                elseArmCopy = cpy;
    }
    else
    {
        elseArmCopy = pickCopy(elseBB);
    }
    if (!elseArmCopy)
        return false;

    Value *thenV = stripCopy(thenCopy->getSource());
    Value *elseV = stripCopy(elseArmCopy->getSource());
    bool thenIsLoad = (thenV == ld || sameValue(thenV, ld));
    bool elseIsLoad = (elseV == ld || sameValue(elseV, ld));
    if (thenIsLoad == elseIsLoad)
        return false;

    Value *mVal = thenIsLoad ? elseV : thenV;
    pat.kind = thenIsLoad ? ArrayReducePattern::Kind::Max : ArrayReducePattern::Kind::Min;

    CopyInst *mUpdate = nullptr;
    Value *jIV = nullptr;
    Value *bound = nullptr;
    ICmpInst *loopCmp = nullptr;
    for (auto &ip : latch->getInstructions())
    {
        if (auto *ci = dynamic_cast<ICmpInst *>(ip.get()))
        {
            if (ci->getPredicate() == ICmpInst::ICMP_SLT)
            {
                loopCmp = ci;
                jIV = ci->getLHS();
                bound = ci->getRHS();
            }
        }
        if (auto *cpy = dynamic_cast<CopyInst *>(ip.get()))
        {
            if (dynamic_cast<BinaryOperator *>(stripCopy(cpy->getSource())))
                continue;
            mUpdate = cpy;
        }
    }
    if (!mUpdate || !jIV || !bound || !loopCmp)
        return false;
    if (mUpdate->getType() && !mUpdate->getType()->isIntegerTy())
        return false;

    vector<BasicBlock *> entryPreds;
    for (auto *pred : header->getPredecessors())
        if (!loop.containsBlock(pred))
            entryPreds.push_back(pred);
    if (entryPreds.size() != 1)
        return false;
    BasicBlock *entry = entryPreds[0];
    if (!isValidVectorEntry(entry, header))
        return false;

    Value *mInit = nullptr;
    CopyInst *mAccCopy = nullptr;
    const string mName = mVal->getName();
    for (auto &ip : entry->getInstructions())
    {
        if (auto *cpy = dynamic_cast<CopyInst *>(ip.get()))
        {
            if (!mName.empty() && cpy->getName() == mName)
            {
                mInit = cpy->getSource();
                mAccCopy = cpy;
            }
        }
    }
    if (!mAccCopy && mInit)
    {
        for (auto &ip : entry->getInstructions())
        {
            if (auto *cpy = dynamic_cast<CopyInst *>(ip.get()))
            {
                if (sameValue(cpy->getSource(), mInit))
                {
                    mAccCopy = cpy;
                    break;
                }
            }
        }
    }
    if (!mInit || !mAccCopy)
        return false;

    if (isFullyUnrollableLoop(loop, jIV, bound))
        return false;

    std::set<Instruction *> covered;
    covered.insert(maxCmp);
    covered.insert(ld);
    covered.insert(thenCopy);
    covered.insert(elseArmCopy);
    covered.insert(mUpdate);
    covered.insert(loopCmp);
    for (auto *bb : loop.blocks)
    {
        for (auto &ip : bb->getInstructions())
        {
            if (auto *cpy = dynamic_cast<CopyInst *>(ip.get()))
                covered.insert(cpy);
            if (auto *add = dynamic_cast<BinaryOperator *>(ip.get()))
                if (add->getOpcode() == Opcode::Add)
                    covered.insert(add);
            if (auto *gep = dynamic_cast<GetElementPtrInst *>(ip.get()))
                covered.insert(gep);
        }
    }

    Value *base = nullptr, *offset = nullptr, *stride = nullptr;
    std::vector<Value *> rowIdxs;
    if (!parseVecAddr(ld->getPointer(), jIV, loop, covered, base, rowIdxs, offset, stride,
                      entry, nullptr))
        return false;

    pat.entry = entry;
    pat.header = header;
    pat.body = latch;
    pat.exitBlock = exitBlock;
    pat.loop = loop;
    pat.jIV = jIV;
    pat.jInit = nullptr;
    pat.bound = bound;
    pat.sum = mUpdate;
    pat.sumInit = mInit;
    pat.sumAcc = mAccCopy;
    pat.elemTy = IntegerType::getInstance();
    pat.square = false;
    pat.product = false;
    pat.base = base;
    pat.rowIdxs = rowIdxs;
    pat.offset = offset;
    pat.stride = stride;
    return true;
}

bool LoopVectorizePass::findArrayReduceLoop(const Loop &loop, ArrayReducePattern &pat)
{
    if (!isSimpleTwoBlockLoop(loop))
        return false;
    BasicBlock *header = loop.header;
    BasicBlock *body = getLoopLatch(loop);
    BasicBlock *exitBlock = getLoopExit(loop);
    if (!header || !body || !exitBlock || body == header)
        return false;

    Value *jIV = nullptr, *bound = nullptr;
    ICmpInst *cmp = nullptr;
    if (!getHeaderBoundCmp(header, jIV, bound, cmp))
        return false;
    if (isFullyUnrollableLoop(loop, jIV, bound))
        return false;

    vector<BasicBlock *> entryPreds;
    for (auto *pred : header->getPredecessors())
        if (!loop.containsBlock(pred))
            entryPreds.push_back(pred);
    if (entryPreds.size() != 1)
        return false;
    BasicBlock *entry = entryPreds[0];
    if (!isValidVectorEntry(entry, header))
        return false;

    // 禁止 body 内 store（纯归约）
    for (auto &ip : body->getInstructions())
        if (dynamic_cast<StoreInst *>(ip.get()))
            return false;

    // 找到 sum phi：header 中除 jIV / ISR 仿射伴生 phi 外的归约 phi
    PhiInst *sumPhi = nullptr;
    PhiInst *jPhi = dynamic_cast<PhiInst *>(stripCopy(jIV));
    for (auto &ip : header->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(ip.get());
        if (!phi || phi == jPhi)
            continue;
        Type *ty = phi->getType();
        if (!ty || (!ty->isIntegerTy() && !ty->isFloatTy()))
            continue;
        // 强度消减产生的伴生 IV：phi [init], [add(phi, invariant step)]
        // 归约 phi 的回边是 add(phi, load/…) 而非不变步长，不会被识别为仿射。
        {
            Value *ainit = nullptr, *astep = nullptr;
            if (parseAffinePhi(phi, loop, ainit, astep, nullptr))
                continue;
        }
        if (sumPhi)
            return false; // 多于一个候选
        sumPhi = phi;
    }
    if (!sumPhi)
        return false;
    // 浮点归约即使 vfredosum 也会因 strip-mining 分块改变结合律，无法保证与
    // 标量逐元累加 bit-exact（h-10 对拍要求十六进制浮点一致）。
    if (sumPhi->getType() && sumPhi->getType()->isFloatTy())
        return false;

    Value *sumInit = nullptr, *sumNext = nullptr;
    for (unsigned i = 0; i < sumPhi->getNumIncomingValues(); ++i)
    {
        if (sumPhi->getIncomingBlock(i) == entry)
            sumInit = stripCopy(sumPhi->getIncomingValue(i));
        else if (loop.containsBlock(sumPhi->getIncomingBlock(i)))
            sumNext = stripCopy(sumPhi->getIncomingValue(i));
    }
    if (!sumInit || !sumNext)
        return false;

    ArrayReducePattern::Kind reduceKind = ArrayReducePattern::Kind::Sum;
    bool square = false;
    bool product = false;
    LoadInst *ld = nullptr;
    LoadInst *ld2 = nullptr;
    std::set<Instruction *> covered;

    if (auto *add = dynamic_cast<BinaryOperator *>(sumNext))
    {
        bool isF = add->getOpcode() == Opcode::FAdd;
        if (add->getOpcode() != Opcode::Add && !isF)
            return false;

        Value *addL = stripCopy(add->getLHS());
        Value *addR = stripCopy(add->getRHS());
        Value *addend = nullptr;
        if (addL == sumPhi || sameValue(addL, sumPhi))
            addend = addR;
        else if (addR == sumPhi || sameValue(addR, sumPhi))
            addend = addL;
        else
            return false;

        ld = dynamic_cast<LoadInst *>(addend);
        if (!ld)
        {
            auto *mul = dynamic_cast<BinaryOperator *>(addend);
            if (!mul)
                return false;
            if (mul->getOpcode() != Opcode::Mul && mul->getOpcode() != Opcode::FMul)
                return false;
            Value *ml = stripCopy(mul->getLHS());
            Value *mr = stripCopy(mul->getRHS());
            ld = dynamic_cast<LoadInst *>(ml);
            ld2 = dynamic_cast<LoadInst *>(mr);
            if (!ld || !ld2)
                return false;
            if (ld == ld2 || sameValue(ld, ld2))
                square = true;
            else
                product = true;
        }

        covered.insert(add);
        covered.insert(ld);
        if (square || product)
        {
            if (auto *mul = dynamic_cast<BinaryOperator *>(addend))
                covered.insert(mul);
            if (product && ld2)
                covered.insert(ld2);
        }
    }
    else if (auto *sel = dynamic_cast<SelectInst *>(sumNext))
    {
        Value *tv = stripCopy(sel->getTrueValue());
        Value *fv = stripCopy(sel->getFalseValue());
        if (tv == sumPhi || sameValue(tv, sumPhi))
            ld = dynamic_cast<LoadInst *>(fv);
        else if (fv == sumPhi || sameValue(fv, sumPhi))
            ld = dynamic_cast<LoadInst *>(tv);
        else
            return false;
        if (!ld)
            return false;

        auto *icmp = dynamic_cast<ICmpInst *>(stripCopy(sel->getCondition()));
        if (!icmp)
            return false;
        Value *il = stripCopy(icmp->getLHS());
        Value *ir = stripCopy(icmp->getRHS());
        bool ldOnTrue = (tv == ld || sameValue(tv, ld));
        if (icmp->getPredicate() == ICmpInst::ICMP_SGT)
        {
            if ((il == ld || sameValue(il, ld)) && (ir == sumPhi || sameValue(ir, sumPhi)) &&
                ldOnTrue)
                reduceKind = ArrayReducePattern::Kind::Max;
            else if ((ir == ld || sameValue(ir, ld)) && (il == sumPhi || sameValue(il, sumPhi)) &&
                     !ldOnTrue)
                reduceKind = ArrayReducePattern::Kind::Max;
            else
                return false;
        }
        else if (icmp->getPredicate() == ICmpInst::ICMP_SLT)
        {
            if ((il == ld || sameValue(il, ld)) && (ir == sumPhi || sameValue(ir, sumPhi)) &&
                ldOnTrue)
                reduceKind = ArrayReducePattern::Kind::Min;
            else if ((ir == ld || sameValue(ir, ld)) && (il == sumPhi || sameValue(il, sumPhi)) &&
                     !ldOnTrue)
                reduceKind = ArrayReducePattern::Kind::Min;
            else if ((il == sumPhi || sameValue(il, sumPhi)) &&
                     (ir == ld || sameValue(ir, ld)) && ldOnTrue)
                reduceKind = ArrayReducePattern::Kind::Max;
            else if ((ir == sumPhi || sameValue(ir, sumPhi)) &&
                     (il == ld || sameValue(il, ld)) && !ldOnTrue)
                reduceKind = ArrayReducePattern::Kind::Max;
            else
                return false;
        }
        else
        {
            return false;
        }

        covered.insert(sel);
        covered.insert(icmp);
        covered.insert(ld);
    }
    else
    {
        return false;
    }

    Value *jInit = nullptr;
    if (jPhi)
    {
        for (unsigned i = 0; i < jPhi->getNumIncomingValues(); ++i)
            if (jPhi->getIncomingBlock(i) == entry)
                jInit = stripCopy(jPhi->getIncomingValue(i));
    }

    Value *base = nullptr, *offset = nullptr, *stride = nullptr;
    std::vector<Value *> rowIdxs;
    if (!parseVecAddr(ld->getPointer(), jIV, loop, covered, base, rowIdxs, offset, stride,
                      entry, jInit))
        return false;

    Value *base2 = nullptr, *offset2 = nullptr, *stride2 = nullptr;
    std::vector<Value *> rowIdxs2;
    if (product)
    {
        if (!parseVecAddr(ld2->getPointer(), jIV, loop, covered, base2, rowIdxs2, offset2,
                          stride2, entry, jInit))
            return false;
    }

    // 覆盖：body 内非控制指令均须被归约表达式覆盖
    for (auto &ip : body->getInstructions())
    {
        Instruction *inst = ip.get();
        if (isControlInst(inst, jIV, loop))
            continue;
        if (!covered.count(inst))
            return false;
    }

    pat.entry = entry;
    pat.header = header;
    pat.body = body;
    pat.exitBlock = exitBlock;
    pat.loop = loop;
    pat.jIV = jIV;
    pat.jInit = jInit;
    pat.bound = bound;
    pat.sum = sumPhi;
    pat.sumInit = sumInit;
    pat.elemTy = elemTypeOf(sumPhi);
    pat.square = square;
    pat.product = product;
    pat.base = base;
    pat.rowIdxs = rowIdxs;
    pat.offset = offset;
    pat.stride = stride;
    pat.base2 = base2;
    pat.rowIdxs2 = rowIdxs2;
    pat.offset2 = offset2;
    pat.stride2 = stride2;
    pat.kind = reduceKind;
    return true;
}

bool LoopVectorizePass::vectorizeArrayReduceLoop(Function *func, const ArrayReducePattern &pat)
{
    BasicBlock *entry = pat.entry;
    BasicBlock *header = pat.header;
    BasicBlock *bodyOld = pat.body;
    BasicBlock *exitBlock = pat.exitBlock;
    Type *elemTy = pat.elemTy ? pat.elemTy : IntegerType::getInstance();
    bool isFloat = elemTy->isFloatTy();

    std::map<Value *, Value *> hoistedScalars;
    auto inOldLoop = [&](Instruction *inst) -> bool
    {
        for (auto *bb : {header, bodyOld})
        {
            if (!bb)
                continue;
            for (auto &ip : bb->getInstructions())
                if (ip.get() == inst)
                    return true;
        }
        return false;
    };
    std::function<Value *(Value *)> hoistScalar = [&](Value *v) -> Value *
    {
        if (!v || dynamic_cast<Constant *>(v))
            return v;
        auto *inst = dynamic_cast<Instruction *>(v);
        if (!inst)
            return v;
        if (!inOldLoop(inst))
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

    Value *boundSafe = hoistScalar(pat.bound);
    Value *sumInit = hoistScalar(pat.sumInit);
    Value *count0 = boundSafe;
    if (pat.jInit)
    {
        Value *j = stripCopy(pat.jInit);
        auto *jc = dynamic_cast<ConstantInt *>(j);
        if (!jc || jc->Value != 0)
        {
            count0 = new BinaryOperator(Opcode::Sub, boundSafe, hoistScalar(pat.jInit),
                                        freshName("count0"));
            prependBeforeTerminator(entry, static_cast<Instruction *>(count0));
        }
    }

    auto makeRowPtr = [&](Value *base, const std::vector<Value *> &rows, Value *off,
                          const string &name) -> GetElementPtrInst *
    {
        Value *offVal = off ? hoistScalar(off) : ci(0);
        vector<Value *> idxs;
        for (auto *r : rows)
            idxs.push_back(hoistScalar(r));
        idxs.push_back(offVal);
        auto *rowPtr = new GetElementPtrInst(base, idxs, freshName(name));
        prependBeforeTerminator(entry, rowPtr);
        return rowPtr;
    };
    auto makeByteStride = [&](Value *strideElem) -> Value *
    {
        if (!strideElem)
            return nullptr;
        strideElem = hoistScalar(strideElem);
        if (auto *c = dynamic_cast<ConstantInt *>(stripCopy(strideElem)))
            return ci(c->Value * 4);
        auto *bs = new BinaryOperator(Opcode::Sll, strideElem, ci(2), freshName("bs"));
        prependBeforeTerminator(entry, static_cast<Instruction *>(bs));
        return bs;
    };

    auto *rowPtr = makeRowPtr(pat.base, pat.rowIdxs, pat.offset, "row");
    Value *strideElem = pat.stride ? hoistScalar(pat.stride) : nullptr;
    Value *byteStride = makeByteStride(pat.stride);

    GetElementPtrInst *rowPtr2 = nullptr;
    Value *strideElem2 = nullptr;
    Value *byteStride2 = nullptr;
    if (pat.product)
    {
        rowPtr2 = makeRowPtr(pat.base2, pat.rowIdxs2, pat.offset2, "row2");
        strideElem2 = pat.stride2 ? hoistScalar(pat.stride2) : nullptr;
        byteStride2 = makeByteStride(pat.stride2);
    }

    // 用 copy 链（与 rvv_dot 等已验证样例一致），不用 vguard+phi：
    // 后端对 guard→body 的 phi 边未插入并行拷贝，会导致 count/ptr 寄存器未初始化而崩溃。
    const string countNm = freshName("count");
    const string ptrNm = freshName("ptr");
    const string sumaccNm = freshName("sumacc");
    const string sumoutNm = freshName("sumout");
    string ptr2Nm;
    if (pat.product)
        ptr2Nm = freshName("ptr2");

    auto *countVar = new CopyInst(count0, countNm);
    auto *ptrVar = new CopyInst(rowPtr, ptrNm);
    auto *sumaccVar = new CopyInst(sumInit, sumaccNm);
    auto *sumoutVar = new CopyInst(sumInit, sumoutNm);
    prependBeforeTerminator(entry, countVar);
    prependBeforeTerminator(entry, ptrVar);
    prependBeforeTerminator(entry, sumaccVar);
    prependBeforeTerminator(entry, sumoutVar);

    CopyInst *ptrVar2 = nullptr;
    if (pat.product)
    {
        ptrVar2 = new CopyInst(rowPtr2, ptr2Nm);
        prependBeforeTerminator(entry, ptrVar2);
    }

    auto *nz = new ICmpInst(ICmpInst::ICMP_NE, count0, ci(0), freshName("encond"));
    prependBeforeTerminator(entry, nz);

    auto &bbs = func->getBasicBlocks();
    auto body = new BasicBlock(freshName("vbody"), func);
    auto exit = new BasicBlock(freshName("vexit"), func);
    bbs.push_back(unique_ptr<BasicBlock>(body));
    bbs.push_back(unique_ptr<BasicBlock>(exit));

    auto vl = new VecSetVlInst(countVar, 32, freshName("vl"));
    body->addInstruction(own(vl));

    Instruction *vload = nullptr;
    if (byteStride)
        vload = new VecStridedLoadInst(ptrVar, byteStride, vl, elemTy, 16, freshName("v"));
    else
        vload = new VecLoadInst(ptrVar, vl, elemTy, 16, freshName("v"));
    body->addInstruction(own(vload));

    Value *toReduce = vload;
    if (pat.square)
    {
        auto *vm = new VecBinaryInst(Opcode::VecMul, vload, vload, freshName("vsq"));
        body->addInstruction(own(vm));
        toReduce = vm;
    }
    else if (pat.product)
    {
        Instruction *vload2 = nullptr;
        if (byteStride2)
            vload2 = new VecStridedLoadInst(ptrVar2, byteStride2, vl, elemTy, 16, freshName("v2"));
        else
            vload2 = new VecLoadInst(ptrVar2, vl, elemTy, 16, freshName("v2"));
        body->addInstruction(own(vload2));
        auto *vm = new VecBinaryInst(Opcode::VecMul, vload, vload2, freshName("vdot"));
        body->addInstruction(own(vm));
        toReduce = vm;
    }
    Instruction *blockReduce = nullptr;
    if (pat.kind == ArrayReducePattern::Kind::Max)
        blockReduce = new VecReduceMaxInst(toReduce, vl, freshName("bmax"));
    else if (pat.kind == ArrayReducePattern::Kind::Min)
        blockReduce = new VecReduceMinInst(toReduce, vl, freshName("bmin"));
    else
        blockReduce = new VecReduceAddInst(toReduce, vl, freshName("bsum"));
    body->addInstruction(own(blockReduce));

    Instruction *sumN = nullptr;
    if (pat.kind == ArrayReducePattern::Kind::Sum)
    {
        if (isFloat)
            sumN = new BinaryOperator(Opcode::FAdd, sumaccVar, blockReduce, freshName("snew"));
        else
            sumN = new BinaryOperator(Opcode::Add, sumaccVar, blockReduce, freshName("snew"));
    }
    else if (pat.kind == ArrayReducePattern::Kind::Max)
    {
        auto *gt = new ICmpInst(ICmpInst::ICMP_SGT, blockReduce, sumaccVar, freshName("mxcmp"));
        body->addInstruction(own(gt));
        sumN = new SelectInst(gt, blockReduce, sumaccVar, freshName("snew"));
    }
    else
    {
        auto *lt = new ICmpInst(ICmpInst::ICMP_SLT, blockReduce, sumaccVar, freshName("mncmp"));
        body->addInstruction(own(lt));
        sumN = new SelectInst(lt, blockReduce, sumaccVar, freshName("snew"));
    }
    body->addInstruction(own(sumN));

    auto countN = new BinaryOperator(Opcode::Sub, countVar, vl, freshName("countn"));
    body->addInstruction(own(countN));
    body->addInstruction(own(new CopyInst(countN, countNm)));

    auto advancePtrCopy = [&](CopyInst *pVar, Value *strideE, const string &nm)
    {
        Value *adv = vl;
        if (strideE)
        {
            adv = new BinaryOperator(Opcode::Mul, vl, strideE, freshName("ptrStep"));
            body->addInstruction(own(static_cast<Instruction *>(adv)));
        }
        auto *ptrN = new GetElementPtrInst(pVar, vector<Value *>{adv}, freshName("ptrN"));
        body->addInstruction(own(ptrN));
        body->addInstruction(own(new CopyInst(ptrN, nm)));
    };
    advancePtrCopy(ptrVar, strideElem, ptrNm);
    if (pat.product)
        advancePtrCopy(ptrVar2, strideElem2, ptr2Nm);

    body->addInstruction(own(new CopyInst(sumN, sumaccNm)));
    body->addInstruction(own(new CopyInst(sumN, sumoutNm)));

    auto cond = new ICmpInst(ICmpInst::ICMP_NE, countN, ci(0), freshName("cond"));
    body->addInstruction(own(cond));
    body->addInstruction(own(new BranchInst(cond, body, exit)));

    exit->addInstruction(own(new BranchInst(exitBlock)));

    // entry：保留原条件出边，仅把「进旧 header」的边改到向量体；零元素走 vexit
    retargetEntryEdge(entry, header, body);
    // 在 entry 终结符前插入「count==0 则跳过向量体」分支（替换原单一 branch）
    {
        auto *term = entry->getTerminator();
        auto *br = dynamic_cast<BranchInst *>(term);
        if (br && br->isConditional())
        {
            BasicBlock *onTrue = br->getTrueBlock();
            BasicBlock *onFalse = br->getFalseBlock();
            if (onTrue == body)
            {
                // entry: br scalar_cond, body, onFalse  →  br scalar_cond, ventry, onFalse
                auto *ventry = new BasicBlock(freshName("ventry"), func);
                bbs.push_back(unique_ptr<BasicBlock>(ventry));
                ventry->addInstruction(own(new BranchInst(nz, body, exit)));
                wireEdge(ventry, body);
                wireEdge(ventry, exit);
                br->setTrueBlock(ventry);
                wireEdge(entry, ventry);
            }
            else if (onFalse == body)
            {
                auto *ventry = new BasicBlock(freshName("ventry"), func);
                bbs.push_back(unique_ptr<BasicBlock>(ventry));
                ventry->addInstruction(own(new BranchInst(nz, body, exit)));
                wireEdge(ventry, body);
                wireEdge(ventry, exit);
                br->setFalseBlock(ventry);
                wireEdge(entry, ventry);
            }
        }
    }
    wireEdge(body, body);
    wireEdge(body, exit);
    wireEdge(exit, exitBlock);

    for (auto &instPtr : exitBlock->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi)
            continue;
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            if (phi->getIncomingBlock(i) != header)
                continue;
            phi->setIncomingBlock(i, exit);
            if (sameValue(phi, pat.jIV) || feedsInductionVar(phi, pat.jIV) ||
                feedsInductionVar(pat.jIV, phi))
                phi->setIncomingValue(i, boundSafe);
            else if (sameValue(phi, pat.sum) || phi == pat.sum)
                phi->setIncomingValue(i, sumoutVar);
        }
    }

    // 替换循环外对旧累加器的引用（copy-based max/min 的 entry copy 或 sum phi）
    const string accNm =
        pat.sumAcc ? pat.sumAcc->getName() : (pat.sum ? pat.sum->getName() : string());
    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (inOldLoop(inst))
                continue;
            for (unsigned i = 0; i < inst->getNumOperands(); ++i)
            {
                Value *op = inst->getOperandByIndex(i);
                bool match = (op == pat.sumAcc || op == pat.sum);
                if (!match && !accNm.empty())
                {
                    if (auto *cpy = dynamic_cast<CopyInst *>(op))
                        match = (normalizeName(cpy->getName()) == normalizeName(accNm));
                }
                if (match)
                    inst->setOperandByIndex(i, sumoutVar);
            }
        }
    }
    // copy-based max/min：循环外 putint 引用 entry 上的 m 初值 copy，必须接到 sumout
    if (pat.kind == ArrayReducePattern::Kind::Max ||
        pat.kind == ArrayReducePattern::Kind::Min)
    {
        for (auto &instPtr : exitBlock->getInstructions())
        {
            auto *call = dynamic_cast<CallInst *>(instPtr.get());
            if (!call)
                continue;
            Function *callee = call->getCalledFunction();
            if (!callee)
                continue;
            const string &fn = callee->getName();
            if (fn != "putint" && fn != "putfloat")
                continue;
            for (unsigned i = 1; i < call->getNumOperands(); ++i)
                call->setOperandByIndex(i, sumoutVar);
        }
    }

    removeLoopBlocks(func, pat.loop.blocks);
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
            ArrayReducePattern rpat;
            if ((findArrayReduceLoop(loop, rpat) || findCopyBasedArrayMaxLoop(loop, rpat)) &&
                vectorizeArrayReduceLoop(func, rpat))
            {
                localChanged = true;
                changed = true;
                if (verbose)
                    debugInfo << "LoopVectorize: array-reduce loop @ " << func->getName()
                              << " header=" << rpat.header->getName() << "\n";
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
