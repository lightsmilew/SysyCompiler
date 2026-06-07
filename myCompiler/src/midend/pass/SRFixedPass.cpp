#include "SRFixedPass.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>
using namespace std;
using namespace optimization;

namespace
{
    static Value *stripCopy(Value *v)
    {
        while (v)
        {
            if (auto *cpy = dynamic_cast<CopyInst *>(v))
            {
                v = cpy->getSource();
                continue;
            }
            break;
        }
        return v;
    }

    static bool isConstPowerOfTwo(int d, int &absD)
    {
        absD = d < 0 ? -d : d;
        return absD > 0 && (absD & (absD - 1)) == 0;
    }

    // n % 2^k == 0  <=>  (n & (2^k-1)) == 0；eq/ne 与 0、除数为 2 的幂，且 SRem 仅一个 use
    static bool tryFoldSRemPow2EqZero(ICmpInst *icmp, std::vector<std::unique_ptr<Instruction>> &insts,
                                      size_t icmpIndex, bool verbose, std::stringstream &debugInfo,
                                      BasicBlock *bb, bool &changed)
    {
        ICmpInst::Predicate pred = icmp->getPredicate();
        if (pred != ICmpInst::ICMP_EQ && pred != ICmpInst::ICMP_NE)
        {
            return false;
        }

        Value *cmpOther = nullptr;
        if (auto *zero = dynamic_cast<ConstantInt *>(icmp->getRHS());
            zero && zero->Value == 0)
        {
            cmpOther = icmp->getLHS();
        }
        else if (auto *zero = dynamic_cast<ConstantInt *>(icmp->getLHS());
                 zero && zero->Value == 0)
        {
            cmpOther = icmp->getRHS();
        }
        else
        {
            return false;
        }

        auto *srem = dynamic_cast<BinaryOperator *>(stripCopy(cmpOther));
        if (!srem || srem->getOpcode() != Opcode::SRem)
        {
            return false;
        }
        auto *divisor = dynamic_cast<ConstantInt *>(srem->getRHS());
        int absDivisor = 0;
        if (!divisor || !isConstPowerOfTwo(divisor->Value, absDivisor))
        {
            return false;
        }
        if (srem->getUsers().size() > 1)
        {
            return false;
        }

        Value *n = srem->getLHS();
        auto *ty = IntegerType::getInstance();
        int maskVal = absDivisor - 1;
        auto *andInst = new BinaryOperator(Opcode::And, n, new ConstantInt(ty, maskVal),
                                           icmp->getName() + "_pmod_and");
        insts.insert(insts.begin() + static_cast<ptrdiff_t>(icmpIndex),
                     std::unique_ptr<Instruction>(andInst));

        if (cmpOther == icmp->getLHS())
        {
            icmp->setOperandByIndex(0, andInst);
        }
        else
        {
            icmp->setOperandByIndex(1, andInst);
        }

        changed = true;
        if (verbose)
        {
            debugInfo << "SRFixedPass: Folded (n % " << absDivisor << ") == 0 to (n & " << maskVal
                      << ") == 0 in " << bb->getName() << ": " << icmp->toString() << "\n";
        }
        return true;
    }

    static int popcount32(uint32_t v) { return v ? __builtin_popcount(v) : 0; }

    static int ctz32(uint32_t v) { return v ? __builtin_ctz(v) : 0; }

    // C = 2^tz * (2^b0 + 2^b1)，即奇部仅含两个 1 位时可拆成两次移位再相加
    static bool tryMulAsTwoPowerSum(int32_t c, int &tz, int &sh0, int &sh1)
    {
        if (c == 0)
        {
            return false;
        }
        uint32_t absC = static_cast<uint32_t>(c < 0 ? -static_cast<int64_t>(c) : c);
        if (absC <= 1 || (absC & (absC - 1)) == 0)
        {
            return false;
        }
        tz = ctz32(absC);
        uint32_t odd = absC >> tz;
        if (popcount32(odd) != 2)
        {
            return false;
        }
        int b0 = ctz32(odd);
        uint32_t rest = odd & (odd - 1);
        int b1 = ctz32(rest);
        sh0 = tz + b0;
        sh1 = tz + b1;
        return true;
    }

    static bool replaceMulWithShiftAdd(BasicBlock *bb, vector<unique_ptr<Instruction>> &insts, int idx,
                                       Value *lhs, int32_t c, const string &instName, bool verbose,
                                       stringstream &debugInfo, vector<Value *> &needToDelete,
                                       bool &changed)
    {
        int tz = 0, sh0 = 0, sh1 = 0;
        (void)tz;
        if (!tryMulAsTwoPowerSum(c, tz, sh0, sh1))
        {
            return false;
        }

        auto *ty = IntegerType::getInstance();
        auto *shl0 = new BinaryOperator(Opcode::Sll, lhs, new ConstantInt(ty, sh0), instName + "_sr_sh0");
        auto *shl1 = new BinaryOperator(Opcode::Sll, lhs, new ConstantInt(ty, sh1), instName + "_sr_sh1");
        auto *sum = new BinaryOperator(Opcode::Add, shl0, shl1, instName + "_sr_sum");
        Instruction *result = sum;
        unique_ptr<Instruction> negHolder;
        if (c < 0)
        {
            negHolder = make_unique<BinaryOperator>(Opcode::Sub, new ConstantInt(ty, 0), sum, instName + "_sr_neg");
            result = negHolder.get();
        }

        Instruction *inst = insts[idx].get();
        inst->removeThisFromOperands();
        inst->replaceAllUsesWith(result);
        needToDelete.push_back(insts[idx].release());
        insts.erase(insts.begin() + idx);
        insts.insert(insts.begin() + idx, unique_ptr<Instruction>(shl0));
        insts.insert(insts.begin() + idx + 1, unique_ptr<Instruction>(shl1));
        insts.insert(insts.begin() + idx + 2, unique_ptr<Instruction>(sum));
        if (negHolder)
        {
            insts.insert(insts.begin() + idx + 3, std::move(negHolder));
        }

        changed = true;
        if (verbose)
        {
            debugInfo << "SRFixedPass: Replaced Mul " << c << " with shift-add (1<<"
                      << sh0 << " + 1<<" << sh1 << ") in " << bb->getName() << "\n";
        }
        return true;
    }

    struct RangeInfo
    {
        bool hasLower = false;
        bool hasUpper = false;
        int64_t lower = 0;
        int64_t upper = 0; // inclusive
    };

    static RangeInfo rangeFromConst(int64_t v)
    {
        RangeInfo r;
        r.hasLower = r.hasUpper = true;
        r.lower = r.upper = v;
        return r;
    }

    static RangeInfo joinRangeAdd(const RangeInfo &a, const RangeInfo &b)
    {
        RangeInfo r;
        if (a.hasLower && b.hasLower)
        {
            r.hasLower = true;
            r.lower = a.lower + b.lower;
        }
        if (a.hasUpper && b.hasUpper)
        {
            r.hasUpper = true;
            r.upper = a.upper + b.upper;
        }
        return r;
    }

    static RangeInfo analyzeValueRange(Value *v, Function *func, int modHint,
                                       unordered_set<Value *> &visiting,
                                       BasicBlock *useBB,
                                       unordered_set<string> &nameGuard);

    static RangeInfo tryRangeFromPositiveModuloResult(Value *v, int modHint, Function *func,
                                                      unordered_set<Value *> &visiting,
                                                      BasicBlock *useBB,
                                                      unordered_set<string> &nameGuard);

    static bool hasZeroInitIncrementSlot(Value *v, Function *func,
                                         unordered_set<Value *> &visiting);

    static RangeInfo analyzeIncrementSlotRange(Value *v, Function *func,
                                               unordered_set<Value *> &visiting,
                                               int widenUpper);

    static bool copySourceIsNamedSelfIncrement(CopyInst *cpy, const string &name);

    // 控制流汇合：下界取 min、上界取 max；任一路径缺界则无法证明
    static RangeInfo joinRangeAtMergePoint(const RangeInfo &a, const RangeInfo &b)
    {
        RangeInfo r;
        if (a.hasLower && b.hasLower)
        {
            r.hasLower = true;
            r.lower = min(a.lower, b.lower);
        }
        if (a.hasUpper && b.hasUpper)
        {
            r.hasUpper = true;
            r.upper = max(a.upper, b.upper);
        }
        return r;
    }

    // 同名 copy 汇合：init 0 可能只贡献下界，上界可从任一有界分支取得
    static RangeInfo joinRangeNamedCopyMerge(const RangeInfo &a, const RangeInfo &b)
    {
        RangeInfo r;
        if (a.hasLower && b.hasLower)
        {
            r.hasLower = true;
            r.lower = min(a.lower, b.lower);
        }
        else if (a.hasLower)
        {
            r.hasLower = true;
            r.lower = a.lower;
        }
        else if (b.hasLower)
        {
            r.hasLower = true;
            r.lower = b.lower;
        }
        if (a.hasUpper && b.hasUpper)
        {
            r.hasUpper = true;
            r.upper = max(a.upper, b.upper);
        }
        else if (a.hasUpper)
        {
            r.hasUpper = true;
            r.upper = a.upper;
        }
        else if (b.hasUpper)
        {
            r.hasUpper = true;
            r.upper = b.upper;
        }
        return r;
    }

    static Value *findNamedCopySourceInPred(BasicBlock *pred, const string &name)
    {
        if (!pred)
            return nullptr;
        auto &insts = pred->getInstructions();
        for (int i = static_cast<int>(insts.size()) - 1; i >= 0; --i)
        {
            auto *cpy = dynamic_cast<CopyInst *>(insts[i].get());
            if (cpy && cpy->getName() == name)
                return cpy->getSource();
        }
        return nullptr;
    }

    static CopyInst *findNamedCopyInPred(BasicBlock *pred, const string &name)
    {
        if (!pred)
            return nullptr;
        auto &insts = pred->getInstructions();
        for (int i = static_cast<int>(insts.size()) - 1; i >= 0; --i)
        {
            auto *cpy = dynamic_cast<CopyInst *>(insts[i].get());
            if (cpy && cpy->getName() == name)
                return cpy;
        }
        return nullptr;
    }

    // phi 消除后：只合并 use 块各前驱末尾的同名 copy（不用全函数扫描）
    static RangeInfo analyzePhiCopyDefsAtUse(Function *func, BasicBlock *useBB, const string &name,
                                             int modHint, unordered_set<Value *> &visiting,
                                             unordered_set<string> &nameGuard)
    {
        if (!useBB || name.empty())
            return {};

        RangeInfo merged;
        bool any = false;
        for (BasicBlock *pred : useBB->getPredecessors())
        {
            auto *edgeCopy = findNamedCopyInPred(pred, name);
            if (!edgeCopy)
                return {};
            if (copySourceIsNamedSelfIncrement(edgeCopy, name))
                return {};

            Value *src = edgeCopy->getSource();
            any = true;
            RangeInfo r =
                analyzeValueRange(src, func, modHint, visiting, nullptr, nameGuard);
            if ((!r.hasUpper || !r.hasLower) && r.hasLower && r.lower >= 0)
            {
                unordered_set<Value *> slotVisiting;
                if (hasZeroInitIncrementSlot(src, func, slotVisiting))
                {
                    int widen = modHint > 0 ? modHint - 1 : -1;
                    RangeInfo slotR =
                        analyzeIncrementSlotRange(src, func, slotVisiting, widen);
                    if (slotR.hasLower && slotR.hasUpper)
                        r = slotR;
                }
            }
            if (!merged.hasLower && !merged.hasUpper)
                merged = r;
            else
                merged = joinRangeAtMergePoint(merged, r);
        }
        return any ? merged : RangeInfo{};
    }

    static bool copySourceIsNamedSelfIncrement(CopyInst *cpy, const string &name)
    {
        auto *add = dynamic_cast<BinaryOperator *>(stripCopy(cpy->getSource()));
        if (!add || add->getOpcode() != Opcode::Add)
            return false;
        auto usesNamed = [&](Value *op) {
            op = stripCopy(op);
            if (auto *c = dynamic_cast<CopyInst *>(op))
                return c->getName() == name;
            return op->getName() == name;
        };
        return usesNamed(add->getLHS()) || usesNamed(add->getRHS());
    }

    // 常量 0 的 init copy 只贡献下界，不收紧上界（避免与单步 +1 合并成 [0,1]）
    static void mergeConstInitLower(RangeInfo &merged, int64_t value)
    {
        if (!merged.hasLower)
        {
            merged.hasLower = true;
            merged.lower = value;
        }
        else
            merged.lower = min(merged.lower, value);
        if (value != 0)
        {
            if (!merged.hasUpper)
            {
                merged.hasUpper = true;
                merged.upper = value;
            }
            else
                merged.upper = max(merged.upper, value);
        }
    }

    // 优先 use 块前驱 copy；否则全函数同名 copy，汇合时用 sound join
    static RangeInfo analyzeNamedCopyDefRanges(Function *func, const string &name, int modHint,
                                               unordered_set<Value *> &visiting,
                                               unordered_set<string> &nameGuard,
                                               BasicBlock *useBB = nullptr)
    {
        if (name.empty() || nameGuard.count(name))
            return {};
        nameGuard.insert(name);

        RangeInfo merged;
        if (useBB)
        {
            merged = analyzePhiCopyDefsAtUse(func, useBB, name, modHint, visiting, nameGuard);
            if (merged.hasLower && merged.hasUpper)
            {
                nameGuard.erase(name);
                return merged;
            }
            merged = {};
        }

        bool any = false;
        for (const auto &bbPtr : func->getBasicBlocks())
        {
            for (const auto &instPtr : bbPtr->getInstructions())
            {
                auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
                if (!cpy || cpy->getName() != name)
                    continue;
                if (copySourceIsNamedSelfIncrement(cpy, name))
                    continue;
                any = true;
                Value *src = stripCopy(cpy->getSource());
                if (auto *c = dynamic_cast<ConstantInt *>(src))
                {
                    mergeConstInitLower(merged, c->Value);
                    continue;
                }
                RangeInfo r = analyzeValueRange(cpy->getSource(), func, modHint, visiting, nullptr,
                                                nameGuard);
                if (!r.hasLower || !r.hasUpper)
                {
                    RangeInfo modR = tryRangeFromPositiveModuloResult(
                        cpy->getSource(), modHint, func, visiting, nullptr, nameGuard);
                    if (modR.hasLower && modR.hasUpper)
                        r = modR;
                }
                if (!merged.hasLower && !merged.hasUpper)
                    merged = r;
                else
                    merged = joinRangeAtMergePoint(merged, r);
            }
        }
        nameGuard.erase(name);
        return any ? merged : RangeInfo{};
    }

    static bool allCallSitesPassConstantZero(Function *callee, unsigned argIndex)
    {
        Module *module = callee->getParent();
        if (!module || argIndex >= callee->getArguments().size())
            return false;

        bool found = false;
        for (const auto &funcPtr : module->Functions)
        {
            Function *caller = funcPtr.get();
            if (!caller || caller == callee)
                continue;
            for (const auto &bbPtr : caller->getBasicBlocks())
            {
                for (const auto &instPtr : bbPtr->getInstructions())
                {
                    auto *call = dynamic_cast<CallInst *>(instPtr.get());
                    if (!call || call->getCalledFunction() != callee)
                        continue;
                    const auto &args = call->getArguments();
                    if (argIndex >= args.size())
                        return false;
                    auto *init = dynamic_cast<ConstantInt *>(stripCopy(args[argIndex]));
                    if (!init || init->Value != 0)
                        return false;
                    found = true;
                }
            }
        }
        return found;
    }

    // 结构证明：v 属于「初值 0、仅经 copy / add(·,非负常数) / phi 更新」的槽位
    static bool hasZeroInitIncrementSlot(Value *v, Function *func,
                                         unordered_set<Value *> &visiting)
    {
        v = stripCopy(v);
        if (!v)
            return false;

        if (auto *c = dynamic_cast<ConstantInt *>(v))
            return c->Value == 0;

        if (auto *arg = dynamic_cast<Argument *>(v))
            return arg->getType()->isIntegerTy() &&
                   allCallSitesPassConstantZero(arg->Parent, arg->ArgNo);

        if (visiting.count(v))
            return true;

        visiting.insert(v);
        bool ok = false;

        if (auto *cpy = dynamic_cast<CopyInst *>(v))
            ok = hasZeroInitIncrementSlot(cpy->getSource(), func, visiting);
        else if (auto *add = dynamic_cast<BinaryOperator *>(v))
        {
            if (add->getOpcode() == Opcode::Add)
            {
                auto *rc = dynamic_cast<ConstantInt *>(stripCopy(add->getRHS()));
                if (rc && rc->Value >= 0)
                    ok = hasZeroInitIncrementSlot(add->getLHS(), func, visiting);
                else
                {
                    auto *lc = dynamic_cast<ConstantInt *>(stripCopy(add->getLHS()));
                    if (lc && lc->Value >= 0)
                        ok = hasZeroInitIncrementSlot(add->getRHS(), func, visiting);
                }
            }
        }
        else if (auto *phi = dynamic_cast<PhiInst *>(v))
        {
            if (phi->getNumIncomingValues() == 0)
            {
                visiting.erase(v);
                return false;
            }
            ok = true;
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                if (!hasZeroInitIncrementSlot(phi->getIncomingValue(i), func, visiting))
                {
                    ok = false;
                    break;
                }
            }
        }

        visiting.erase(v);
        return ok;
    }

    // 初值 0 的非负增量槽位区间；widenUpper>=0 时回边才放宽上界（仅用于 mod 累加器的 inc 操作数）
    static RangeInfo analyzeIncrementSlotRange(Value *v, Function *func,
                                               unordered_set<Value *> &visiting,
                                               int widenUpper = -1)
    {
        v = stripCopy(v);
        if (!v)
            return {};

        if (auto *c = dynamic_cast<ConstantInt *>(v))
        {
            if (c->Value < 0)
                return {};
            return rangeFromConst(c->Value);
        }

        if (visiting.count(v))
        {
            if (widenUpper >= 0)
            {
                RangeInfo widened;
                widened.hasLower = true;
                widened.lower = 0;
                widened.hasUpper = true;
                widened.upper = static_cast<int64_t>(widenUpper);
                return widened;
            }
            return {};
        }
        visiting.insert(v);

        RangeInfo result;

        if (auto *cpy = dynamic_cast<CopyInst *>(v))
        {
            result = analyzeIncrementSlotRange(cpy->getSource(), func, visiting, widenUpper);
        }
        else if (auto *arg = dynamic_cast<Argument *>(v))
        {
            if (arg->getType()->isIntegerTy() &&
                allCallSitesPassConstantZero(arg->Parent, arg->ArgNo))
            {
                result = rangeFromConst(0);
            }
            else if (widenUpper >= 0 && hasZeroInitIncrementSlot(arg, func, visiting))
            {
                result.hasLower = true;
                result.lower = 0;
                result.hasUpper = true;
                result.upper = static_cast<int64_t>(widenUpper);
            }
        }
        else if (auto *add = dynamic_cast<BinaryOperator *>(v))
        {
            if (add->getOpcode() == Opcode::Add)
            {
                auto *rc = dynamic_cast<ConstantInt *>(stripCopy(add->getRHS()));
                if (rc && rc->Value >= 0)
                {
                    result = joinRangeAdd(
                        analyzeIncrementSlotRange(add->getLHS(), func, visiting, widenUpper),
                        rangeFromConst(rc->Value));
                }
                else
                {
                    auto *lc = dynamic_cast<ConstantInt *>(stripCopy(add->getLHS()));
                    if (lc && lc->Value >= 0)
                    {
                        result = joinRangeAdd(
                            rangeFromConst(lc->Value),
                            analyzeIncrementSlotRange(add->getRHS(), func, visiting, widenUpper));
                    }
                }
            }
        }

        else if (auto *phi = dynamic_cast<PhiInst *>(v))
        {
            bool any = false;
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                RangeInfo inc = analyzeIncrementSlotRange(phi->getIncomingValue(i), func, visiting,
                                                          widenUpper);
                if (!inc.hasLower && !inc.hasUpper)
                    continue;
                any = true;
                if (!result.hasLower && !result.hasUpper)
                    result = inc;
                else
                    result = joinRangeAtMergePoint(result, inc);
            }
            (void)any;
        }

        visiting.erase(v);
        return result;
    }

    // select(orig >= M ? orig - M : orig) 且 orig 已知非负且 < 2M → 结果 ∈ [0, M-1]
    static bool tryRangeOfPositiveCondModSelect(SelectInst *sel, int modHint,
                                                Function *func, unordered_set<Value *> &visiting,
                                                unordered_set<string> &nameGuard, RangeInfo &out)
    {
        if (modHint <= 0)
            return false;
        auto *cmp = dynamic_cast<ICmpInst *>(stripCopy(sel->getCondition()));
        auto *sub = dynamic_cast<BinaryOperator *>(stripCopy(sel->getTrueValue()));
        Value *orig = stripCopy(sel->getFalseValue());
        if (!cmp || cmp->getPredicate() != ICmpInst::ICMP_SGE || !sub ||
            sub->getOpcode() != Opcode::Sub || stripCopy(sub->getLHS()) != orig)
            return false;
        auto *modC = dynamic_cast<ConstantInt *>(stripCopy(sub->getRHS()));
        if (!modC || modC->Value != modHint)
            return false;

        RangeInfo origR = analyzeValueRange(orig, func, modHint, visiting, nullptr, nameGuard);
        if (!origR.hasLower || origR.lower < 0 || !origR.hasUpper)
            return false;
        if (origR.upper >= 2LL * modHint)
            return false;

        out.hasLower = true;
        out.lower = 0;
        out.hasUpper = true;
        out.upper = static_cast<int64_t>(modHint) - 1;
        return true;
    }

    static RangeInfo positiveModRange(int modHint)
    {
        RangeInfo r;
        r.hasLower = true;
        r.lower = 0;
        r.hasUpper = true;
        r.upper = static_cast<int64_t>(modHint) - 1;
        return r;
    }

    static bool isSameModuloResult(Value *v, int modHint)
    {
        if (modHint <= 0)
            return false;
        v = stripCopy(v);
        auto *bin = dynamic_cast<BinaryOperator *>(v);
        if (!bin)
            return false;
        if (bin->getOpcode() == Opcode::SRem)
        {
            auto *modC = dynamic_cast<ConstantInt *>(bin->getRHS());
            return modC && modC->Value == modHint;
        }
        if (bin->getOpcode() == Opcode::Sub)
        {
            auto *mul = dynamic_cast<BinaryOperator *>(stripCopy(bin->getRHS()));
            auto *modC =
                mul && mul->getOpcode() == Opcode::Mul
                    ? dynamic_cast<ConstantInt *>(stripCopy(mul->getRHS()))
                    : nullptr;
            return modC && modC->Value == modHint;
        }
        return false;
    }

    // 累加器 copy-of-srem：lhs 为 add(同名 acc, inc) 且 inc 非负 → [0,M-1]
    static bool proveModuloAccumulatorSrem(Value *sremInst, int modHint, const string &accName,
                                           Function *func, unordered_set<Value *> &visiting,
                                           unordered_set<string> &nameGuard)
    {
        if (!isSameModuloResult(sremInst, modHint))
            return false;

        auto *srem = dynamic_cast<BinaryOperator *>(stripCopy(sremInst));
        if (!srem)
            return false;

        auto *add = dynamic_cast<BinaryOperator *>(stripCopy(srem->getLHS()));
        if (!add || add->getOpcode() != Opcode::Add)
            return false;

        auto namesMatch = [&](Value *op) {
            op = stripCopy(op);
            if (auto *c = dynamic_cast<CopyInst *>(op))
                return c->getName() == accName;
            return op->getName() == accName;
        };
        Value *accOp = add->getLHS();
        if (!namesMatch(accOp) && !isSameModuloResult(accOp, modHint))
            return false;

        RangeInfo incR =
            analyzeValueRange(add->getRHS(), func, modHint, visiting, nullptr, nameGuard);
        if ((!incR.hasLower || incR.lower < 0) && modHint > 0)
        {
            unordered_set<Value *> slotVisiting;
            if (hasZeroInitIncrementSlot(add->getRHS(), func, slotVisiting))
                incR = analyzeIncrementSlotRange(add->getRHS(), func, slotVisiting, modHint - 1);
        }
        return incR.hasLower && incR.lower >= 0;
    }

    // 累加器同名 copy：仅合并 mod 结果；跳过自增 copy；init 0 只贡献下界
    static RangeInfo rangeFromNamedModuloCopies(Function *func, const string &name, int modHint,
                                                unordered_set<Value *> &visiting,
                                                unordered_set<string> &nameGuard)
    {
        if (name.empty() || nameGuard.count(name))
            return {};
        nameGuard.insert(name);

        RangeInfo merged;
        bool any = false;
        for (const auto &bbPtr : func->getBasicBlocks())
        {
            for (const auto &instPtr : bbPtr->getInstructions())
            {
                auto *cpy = dynamic_cast<CopyInst *>(instPtr.get());
                if (!cpy || cpy->getName() != name)
                    continue;
                if (copySourceIsNamedSelfIncrement(cpy, name))
                    continue;
                any = true;
                Value *src = stripCopy(cpy->getSource());
                if (auto *c = dynamic_cast<ConstantInt *>(src))
                {
                    mergeConstInitLower(merged, c->Value);
                    continue;
                }
                if (proveModuloAccumulatorSrem(src, modHint, name, func, visiting, nameGuard))
                {
                    RangeInfo r = positiveModRange(modHint);
                    if (!merged.hasLower && !merged.hasUpper)
                        merged = r;
                    else
                        merged = joinRangeNamedCopyMerge(merged, r);
                    continue;
                }
                if (isSameModuloResult(src, modHint))
                {
                    RangeInfo r = positiveModRange(modHint);
                    if (!merged.hasLower && !merged.hasUpper)
                        merged = r;
                    else
                        merged = joinRangeNamedCopyMerge(merged, r);
                    continue;
                }
                if (auto *sel = dynamic_cast<SelectInst *>(src))
                {
                    RangeInfo selR;
                    if (tryRangeOfPositiveCondModSelect(sel, modHint, func, visiting, nameGuard,
                                                        selR))
                    {
                        if (!merged.hasLower && !merged.hasUpper)
                            merged = selR;
                        else
                            merged = joinRangeNamedCopyMerge(merged, selR);
                    }
                    continue;
                }
                RangeInfo r = analyzeValueRange(cpy->getSource(), func, modHint, visiting, nullptr,
                                                nameGuard);
                if (r.hasLower && r.hasUpper)
                {
                    if (!merged.hasLower && !merged.hasUpper)
                        merged = r;
                    else
                        merged = joinRangeNamedCopyMerge(merged, r);
                }
            }
        }
        nameGuard.erase(name);
        if (!any || !merged.hasLower || !merged.hasUpper)
            return {};
        return merged;
    }

    // srem(x,M) / remmagic-sub 的结果：在 x>=0 时为 [0,M-1]
    static RangeInfo tryRangeFromPositiveModuloResult(Value *v, int modHint, Function *func,
                                                      unordered_set<Value *> &visiting,
                                                      BasicBlock *useBB,
                                                      unordered_set<string> &nameGuard)
    {
        if (modHint <= 0)
            return {};

        v = stripCopy(v);
        auto *bin = dynamic_cast<BinaryOperator *>(v);
        if (!bin)
            return {};

        if (bin->getOpcode() == Opcode::SRem)
        {
            auto *modC = dynamic_cast<ConstantInt *>(bin->getRHS());
            if (!modC || modC->Value != modHint)
                return {};
        }
        else if (bin->getOpcode() == Opcode::Sub)
        {
            if (!isSameModuloResult(bin, modHint))
                return {};
        }
        else
            return {};

        auto proveNonNegative = [&](Value *lhs) -> bool {
            RangeInfo lr = analyzeValueRange(lhs, func, modHint, visiting, useBB, nameGuard);
            if (lr.hasLower && lr.lower >= 0)
                return true;

            auto *add = dynamic_cast<BinaryOperator *>(stripCopy(lhs));
            if (!add || add->getOpcode() != Opcode::Add)
                return false;

            RangeInfo incR = analyzeValueRange(add->getRHS(), func, modHint, visiting, useBB, nameGuard);
            if ((!incR.hasLower || incR.lower < 0) && modHint > 0)
            {
                unordered_set<Value *> slotVisiting;
                if (hasZeroInitIncrementSlot(add->getRHS(), func, slotVisiting))
                    incR = analyzeIncrementSlotRange(add->getRHS(), func, slotVisiting,
                                                     modHint - 1);
            }

            RangeInfo accR = analyzeValueRange(add->getLHS(), func, modHint, visiting, useBB, nameGuard);
            if (isSameModuloResult(add->getLHS(), modHint))
            {
                accR = positiveModRange(modHint);
            }
            else if ((!accR.hasLower || !accR.hasUpper) && !add->getLHS()->getName().empty())
                accR = rangeFromNamedModuloCopies(func, add->getLHS()->getName(), modHint, visiting,
                                                  nameGuard);

            return accR.hasLower && accR.lower >= 0 && accR.hasUpper && incR.hasLower &&
                   incR.lower >= 0;
        };

        if (proveNonNegative(bin->getLHS()))
            return positiveModRange(modHint);
        return {};
    }

    static RangeInfo analyzeValueRange(Value *v, Function *func, int modHint,
                                       unordered_set<Value *> &visiting,
                                       BasicBlock *useBB,
                                       unordered_set<string> &nameGuard)
    {
        if (!v)
            return {};

        if (auto *c = dynamic_cast<ConstantInt *>(v))
            return rangeFromConst(c->Value);

        // phi 消除后：use 仍指向空 phi，按同名 copy 汇合求界
        if (auto *phi = dynamic_cast<PhiInst *>(v))
        {
            if (phi->getNumIncomingValues() == 0 && !phi->getName().empty())
            {
                return analyzeNamedCopyDefRanges(func, phi->getName(), modHint, visiting, nameGuard,
                                                 useBB);
            }
        }

        // 1. 同名 copy 汇合（phi 消除后多前驱 copy 共享名字）；否则沿 copy 源传播
        if (auto *cpy = dynamic_cast<CopyInst *>(v))
        {
            if (!cpy->getName().empty())
            {
                RangeInfo named = analyzeNamedCopyDefRanges(func, cpy->getName(), modHint, visiting,
                                                            nameGuard, useBB);
                if (named.hasLower && named.hasUpper)
                    return named;
            }
            return analyzeValueRange(cpy->getSource(), func, modHint, visiting, useBB, nameGuard);
        }

        v = stripCopy(v);
        if (!v)
            return {};

        if (auto *c = dynamic_cast<ConstantInt *>(v))
            return rangeFromConst(c->Value);

        RangeInfo moduloR =
            tryRangeFromPositiveModuloResult(v, modHint, func, visiting, useBB, nameGuard);
        if (moduloR.hasLower && moduloR.hasUpper)
            return moduloR;

        if (visiting.count(v))
        {
            if (useBB && !v->getName().empty())
            {
                RangeInfo merged = analyzeNamedCopyDefRanges(func, v->getName(), modHint, visiting,
                                                             nameGuard, useBB);
                if (merged.hasLower && merged.hasUpper)
                    return merged;
            }
            return {};
        }
        visiting.insert(v);

        RangeInfo result;

        if (auto *arg = dynamic_cast<Argument *>(v))
        {
            if (arg->getType()->isIntegerTy())
            {
                unordered_set<Value *> slotVisiting;
                int widen = modHint > 0 ? modHint - 1 : -1;
                result = analyzeIncrementSlotRange(arg, func, slotVisiting, widen);
            }
            visiting.erase(v);
            return result;
        }

        if (auto *bin = dynamic_cast<BinaryOperator *>(v))
        {
            if (bin->getOpcode() == Opcode::Add)
            {
                auto rangeOfAddOperand = [&](Value *op) -> RangeInfo {
                    if (isSameModuloResult(op, modHint))
                        return positiveModRange(modHint);
                    RangeInfo r =
                        analyzeValueRange(op, func, modHint, visiting, useBB, nameGuard);
                    if ((!r.hasLower || !r.hasUpper) && !op->getName().empty())
                    {
                        RangeInfo merged = rangeFromNamedModuloCopies(func, op->getName(), modHint,
                                                                      visiting, nameGuard);
                        if (merged.hasLower && merged.hasUpper)
                            r = merged;
                    }
                    if ((!r.hasLower || !r.hasUpper) && useBB && !op->getName().empty())
                    {
                        RangeInfo merged = analyzeNamedCopyDefRanges(func, op->getName(), modHint,
                                                                      visiting, nameGuard, useBB);
                        if (merged.hasLower && merged.hasUpper)
                            r = merged;
                    }
                    return r;
                };
                result = joinRangeAdd(rangeOfAddOperand(bin->getLHS()),
                                      rangeOfAddOperand(bin->getRHS()));
            }
            else if (bin->getOpcode() == Opcode::Sub)
            {
                auto *mul = dynamic_cast<BinaryOperator *>(stripCopy(bin->getRHS()));
                auto *modC =
                    mul && mul->getOpcode() == Opcode::Mul
                        ? dynamic_cast<ConstantInt *>(stripCopy(mul->getRHS()))
                        : nullptr;
                if (modHint > 0 && modC && modC->Value == modHint)
                {
                    RangeInfo dividend =
                        analyzeValueRange(bin->getLHS(), func, modHint, visiting, nullptr, nameGuard);
                    if (dividend.hasLower && dividend.lower >= 0)
                    {
                        result.hasLower = true;
                        result.lower = 0;
                        result.hasUpper = true;
                        result.upper = static_cast<int64_t>(modHint) - 1;
                        visiting.erase(v);
                        return result;
                    }
                }

                RangeInfo lhs = analyzeValueRange(bin->getLHS(), func, modHint, visiting, nullptr, nameGuard);
                RangeInfo rhs = analyzeValueRange(bin->getRHS(), func, modHint, visiting, nullptr, nameGuard);
                if (lhs.hasLower && rhs.hasUpper)
                {
                    result.hasLower = true;
                    result.lower = lhs.lower - rhs.upper;
                }
                if (lhs.hasUpper && rhs.hasLower)
                {
                    result.hasUpper = true;
                    result.upper = lhs.upper - rhs.lower;
                }
            }
            else if (bin->getOpcode() == Opcode::SRem)
            {
                auto *modC = dynamic_cast<ConstantInt *>(bin->getRHS());
                if (modC && modC->Value > 0)
                {
                    RangeInfo lhs = analyzeValueRange(bin->getLHS(), func, modHint, visiting, nullptr, nameGuard);
                    if (lhs.hasLower && lhs.lower >= 0)
                    {
                        result.hasLower = true;
                        result.lower = 0;
                        result.hasUpper = true;
                        result.upper = static_cast<int64_t>(modC->Value) - 1;
                    }
                }
            }
        }
        else if (auto *sel = dynamic_cast<SelectInst *>(v))
        {
            if (tryRangeOfPositiveCondModSelect(sel, modHint, func, visiting, nameGuard, result))
            {
                visiting.erase(v);
                return result;
            }

            RangeInfo t = analyzeValueRange(sel->getTrueValue(), func, modHint, visiting, nullptr, nameGuard);
            RangeInfo f = analyzeValueRange(sel->getFalseValue(), func, modHint, visiting, nullptr, nameGuard);
            if (t.hasLower && f.hasLower)
            {
                result.hasLower = true;
                result.lower = min(t.lower, f.lower);
            }
            if (t.hasUpper && f.hasUpper)
            {
                result.hasUpper = true;
                result.upper = max(t.upper, f.upper);
            }
        }
        else if (auto *phi = dynamic_cast<PhiInst *>(v))
        {
            bool any = false;
            for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
            {
                RangeInfo inc =
                    analyzeValueRange(phi->getIncomingValue(i), func, modHint, visiting, useBB,
                                      nameGuard);
                if (!inc.hasLower && !inc.hasUpper)
                    continue;
                any = true;
                if (!result.hasLower && !result.hasUpper)
                    result = inc;
                else
                    result = joinRangeAtMergePoint(result, inc);
            }
            if (!any && phi->getNumIncomingValues() == 0 && useBB && !v->getName().empty())
            {
                result = analyzeNamedCopyDefRanges(func, v->getName(), modHint, visiting, nameGuard,
                                                   useBB);
            }
        }
        else if (auto *call = dynamic_cast<CallInst *>(v))
        {
            Function *callee = call->getCalledFunction();
            if (callee && !callee->isLibraryFunction() && call->getType()->isIntegerTy())
            {
                result.hasLower = true;
                result.lower = 0;
                if (modHint > 0)
                {
                    result.hasUpper = true;
                    result.upper = static_cast<int64_t>(modHint) - 1;
                }
            }
        }

        visiting.erase(v);

        if ((!result.hasLower || !result.hasUpper) && useBB && !v->getName().empty())
        {
            RangeInfo merged =
                analyzeNamedCopyDefRanges(func, v->getName(), modHint, visiting, nameGuard, useBB);
            if (merged.hasLower && merged.hasUpper)
                return merged;
        }

        return result;
    }

    // add(同名 mod 累加槽, inc)：acc ∈ [0,M-1]、inc 在 use 处各前驱有界 → 可 cond-sub
    static bool tryProveModuloAccumulateAddRange(Value *lhs, int mod, Function *func,
                                                 BasicBlock *useBB,
                                                 unordered_set<Value *> &visiting,
                                                 unordered_set<string> &nameGuard, RangeInfo &out)
    {
        auto *add = dynamic_cast<BinaryOperator *>(stripCopy(lhs));
        if (!add || add->getOpcode() != Opcode::Add || !useBB)
            return false;

        RangeInfo accR;
        Value *accOp = stripCopy(add->getLHS());
        if (isSameModuloResult(accOp, mod))
            accR = positiveModRange(mod);
        else if (!accOp->getName().empty())
            accR = rangeFromNamedModuloCopies(func, accOp->getName(), mod, visiting, nameGuard);
        if (!accR.hasLower || !accR.hasUpper)
            accR = analyzeValueRange(accOp, func, mod, visiting, useBB, nameGuard);
        if (!accR.hasLower || accR.lower < 0 || !accR.hasUpper)
            return false;

        RangeInfo incR;
        Value *incOp = stripCopy(add->getRHS());
        if (!incOp->getName().empty())
            incR = analyzePhiCopyDefsAtUse(func, useBB, incOp->getName(), mod, visiting, nameGuard);
        if (!incR.hasLower || !incR.hasUpper)
            incR = analyzeValueRange(incOp, func, mod, visiting, useBB, nameGuard);
        if (!incR.hasLower || incR.lower < 0 || !incR.hasUpper)
            return false;

        out = joinRangeAdd(accR, incR);
        return out.hasLower && out.hasUpper && out.lower >= 0;
    }

    // When 0 <= lhs < k*mod is proven, use k conditional subtract(s) instead of mulh magic rem.
    static bool tryReplaceSRemWithPositiveCondSub(vector<unique_ptr<Instruction>> &insts, int idx,
                                                  Value *lhs, int mod, const string &instName,
                                                  Function *func, BasicBlock *useBB, bool verbose,
                                                  stringstream &debugInfo,
                                                  vector<Value *> &needToDelete, bool &changed)
    {
        if (mod <= 0)
            return false;

        unordered_set<Value *> visiting;
        unordered_set<string> nameGuard;
        RangeInfo range;
        if (!tryProveModuloAccumulateAddRange(lhs, mod, func, useBB, visiting, nameGuard, range))
            range = analyzeValueRange(lhs, func, mod, visiting, useBB, nameGuard);

        if (!range.hasLower || range.lower < 0 || !range.hasUpper)
            return false;

        // 仅常量 0 可安全视为 [0,0]；变量经单条 copy 链得到的 [0,0] 不可信
        if (range.lower == 0 && range.upper == 0 &&
            !dynamic_cast<ConstantInt *>(stripCopy(lhs)))
            return false;

        const int64_t mod64 = mod;
        int subs = 0;
        if (range.upper < 2 * mod64)
            subs = 1;
        else if (range.upper < 3 * mod64)
            subs = 2;
        else
            return false;

        auto *type = IntegerType::getInstance();
        auto *modConst = new ConstantInt(type, mod);
        Value *cur = lhs;
        vector<unique_ptr<Instruction>> built;
        built.reserve(static_cast<size_t>(subs * 3));

        for (int s = 0; s < subs; ++s)
        {
            const string tag = instName + "_pms" + to_string(s);
            auto *cmp = new ICmpInst(ICmpInst::ICMP_SGE, cur, modConst, tag + "_cmp");
            auto *sub = new BinaryOperator(Opcode::Sub, cur, modConst, tag + "_sub");
            auto *sel = new SelectInst(cmp, sub, cur, tag + "_sel");
            built.push_back(unique_ptr<Instruction>(cmp));
            built.push_back(unique_ptr<Instruction>(sub));
            built.push_back(unique_ptr<Instruction>(sel));
            cur = sel;
        }

        Instruction *inst = insts[idx].get();
        inst->removeThisFromOperands();
        inst->replaceAllUsesWith(cur);
        needToDelete.push_back(insts[idx].release());
        insts.erase(insts.begin() + idx);
        insts.insert(insts.begin() + idx, std::make_move_iterator(built.begin()),
                     std::make_move_iterator(built.end()));
        changed = true;
        if (verbose)
        {
            debugInfo << "SRFixedPass: Replaced SRem with " << subs << " positive cond-sub for mod "
                      << mod << " (range [" << range.lower << "," << range.upper << "]) in "
                      << func->getName() << "\n";
        }
        return true;
    }
}

std::pair<int64_t, int> SRFixedPass::compute_magic(int32_t d)
{
    const uint64_t two32 = 1ULL << 32;
    uint32_t ad = (d > 0) ? d : -d;

    // 构造 anc: 小于 2^32 的、最接近 2^32 的 |d| 的倍数
    uint64_t anc;
    if (d > 0)
        anc = two32 - 1 - (two32 - 1) % ad;
    else
        anc = two32 - (two32 % ad);

    int p = 32;
    uint64_t q1 = two32 / anc;
    uint64_t r1 = two32 % anc;
    uint64_t q2 = two32 / ad;
    uint64_t r2 = two32 % ad;

    uint64_t delta;
    do
    {
        p++;
        q1 *= 2;
        r1 *= 2;
        if (r1 >= anc)
        {
            q1++;
            r1 -= anc;
        }
        q2 *= 2;
        r2 *= 2;
        if (r2 >= ad)
        {
            q2++;
            r2 -= ad;
        }
        delta = ad - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));

    int64_t magic = q2 + 1;
    if (d < 0)
        magic = -magic;
    int shift = p - 32;

    return {magic, shift};
}
bool SRFixedPass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        for (size_t i = 0; i < insts.size(); ++i)
        {
            if (auto *icmp = dynamic_cast<ICmpInst *>(insts[i].get()))
            {
                tryFoldSRemPow2EqZero(icmp, insts, i, verbose, debugInfo, bb, changed);
            }
        }
    }

    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        // 用下标逆序遍历，避免迭代器失效
        for (int i = insts.size() - 1; i >= 0; --i)
        {
            Instruction *inst = insts[i].get();
            auto instName = inst->getName();
            if (inst && inst->getOpcode() == Opcode::Mul)
            {
                Value *lhs = inst->getOperands()[0];
                Value *rhs = inst->getOperands()[1];
                ConstantInt *constInt = dynamic_cast<ConstantInt *>(rhs);
                if (!constInt)
                {
                    constInt = dynamic_cast<ConstantInt *>(lhs);
                    if (constInt)
                    {
                        lhs = rhs;
                    }
                }
                if (constInt)
                {
                    if (constInt->Value == 0)
                    {
                        // 乘以0，直接替换为0
                        auto *zero = new ConstantInt(IntegerType::getInstance(), 0);
                        inst->replaceAllUsesWith(zero);
                        inst->removeThisFromOperands();
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced Mul with 0 in " << bb->getName() << "\n";
                        }
                        continue;
                    }
                    else if (constInt->Value != 0 && (constInt->Value & (constInt->Value - 1)) == 0)
                    {
                        // 2的幂，直接左移
                        int shift = 0;
                        int val = constInt->Value;
                        while (val > 1)
                        {
                            val >>= 1;
                            shift++;
                        }
                        // 替换为左移操作
                        auto *shlInst = new BinaryOperator(Opcode::Sll, lhs, new ConstantInt(IntegerType::getInstance(), shift), instName + "_sll");
                        inst->removeThisFromOperands();
                        inst->replaceAllUsesWith(shlInst);
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(shlInst));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced Mul with Sll for " << constInt->Value
                                      << " in " << bb->getName() << "\n";
                        }
                        continue;
                    }
                    else if (replaceMulWithShiftAdd(bb.get(), insts, i, lhs, constInt->Value, instName, verbose,
                                                    debugInfo, needToDelete, changed))
                    {
                        continue;
                    }
                }
            }
            else if (inst && inst->getOpcode() == Opcode::SDiv)
            {
                Value *lhs = inst->getOperands()[0];
                Value *rhs = inst->getOperands()[1];
                if (auto *constInt = dynamic_cast<ConstantInt *>(rhs))
                {
                    int rhs_value_abs = abs(constInt->Value);
                    int rhs_value = constInt->Value;
                    if (rhs_value != -1 && (rhs_value_abs & (rhs_value_abs - 1)) == 0)
                    {
                        // 2的幂，直接算数右移
                        int shift = 0;
                        int val = rhs_value_abs;
                        while (val > 1)
                        {
                            val >>= 1;
                            shift++;
                        }
                        // 负数除法：先加 bias 再右移；bias = (x>>31)&mask
                        auto *type = IntegerType::getInstance();
                        auto *zero = new ConstantInt(type, 0);
                        auto *shiftConst = new ConstantInt(type, shift);
                        std::unique_ptr<Instruction> signedDivHolder;
                        Instruction *bias = nullptr;
                        if (rhs_value_abs == 2)
                        {
                            // mask==1 时 (x>>31)&1 等价于 slt(x,0)
                            bias = new ICmpInst(ICmpInst::ICMP_SLT, lhs, zero, instName + "_bias");
                        }
                        else
                        {
                            auto *mask = new ConstantInt(type, (1 << shift) - 1);
                            signedDivHolder = std::make_unique<BinaryOperator>(
                                Opcode::Sra, lhs, new ConstantInt(type, 31), instName + "_signedDiv");
                            bias = new BinaryOperator(Opcode::And, signedDivHolder.get(), mask, instName + "_bias");
                        }
                        auto *lhsAdj = new BinaryOperator(Opcode::Add, lhs, bias, instName + "_lhsAdj");
                        auto *sraInst = new BinaryOperator(Opcode::Sra, lhsAdj, shiftConst, instName + "_sra");
                        std::unique_ptr<Instruction> negHolder;
                        Instruction *finalRes = sraInst;
                        if (rhs_value < 0)
                        {
                            negHolder = std::make_unique<BinaryOperator>(Opcode::Sub, zero, sraInst, instName + "_neg");
                            finalRes = negHolder.get();
                        }
                        inst->removeThisFromOperands();
                        inst->replaceAllUsesWith(finalRes);
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sraInst));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhsAdj));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(bias));
                        size_t pow2DivInstCount = 3;
                        if (signedDivHolder)
                        {
                            insts.insert(insts.begin() + i, std::move(signedDivHolder));
                            pow2DivInstCount++;
                        }
                        if (negHolder)
                        {
                            insts.insert(insts.begin() + i + pow2DivInstCount, std::move(negHolder));
                        }
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced SDiv with Sra for " << constInt->Value
                                      << (rhs_value_abs == 2 ? " (slt bias)" : "")
                                      << " in " << bb->getName() << "\n";
                        }
                    }
                    // 只处理常数且不是0、1、-1、2的幂
                    else if (rhs_value != -1 && (rhs_value_abs & (rhs_value_abs - 1)) != 0)
                    {
                        // 计算magic和shift
                        auto [magic, shift] = compute_magic(rhs_value_abs);
                        auto *type = IntegerType::getInstance();
                        // 1. 扩展lhs为64位
                        auto *lhs64 = new CastInst(Opcode::Sext, lhs, LongType::getInstance(), instName + "_to64");
                        // 2. lhs左移32位
                        auto *lhs_sll = new BinaryOperator(Opcode::Slld, lhs64, new ConstantLong(LongType::getInstance(), 32), instName + "_sll32");
                        // 3. 乘以magic
                        auto *magic_const = new ConstantLong(LongType::getInstance(), magic);
                        auto *mulh = new BinaryOperator(Opcode::Mulhd, lhs_sll, magic_const, instName + "_mulhmagic");
                        // 4. 取高64位（算术右移shift位）
                        auto *shiftnum = new ConstantLong(LongType::getInstance(), shift);
                        auto *sra_div = new BinaryOperator(Opcode::Srad, mulh, shiftnum, instName + "_sra_div");
                        // 5. 截断回32位
                        auto *q0 = new CastInst(Opcode::Trunc, sra_div, type, instName + "_divmagic");
                        // 6. 修正：被除数为负时，结果加1
                        // sign = (lhs < 0) ? 1 : 0
                        auto *zero = new ConstantInt(type, 0);
                        auto *sign = new ICmpInst(ICmpInst::ICMP_SLT, lhs, zero, instName + "_divsign");
                        // q = q0 + sign
                        auto *q = new BinaryOperator(Opcode::Add, q0, sign, instName + "_divmagic_fix");
                        Instruction *finalRes = q;
                        if (rhs_value < 0)
                        {
                            auto *neg = new BinaryOperator(Opcode::Sub, zero, q, instName + "_neg");
                            finalRes = neg;
                            insts.insert(insts.begin() + i + 1, std::unique_ptr<Instruction>(neg));
                        }
                        inst->replaceAllUsesWith(finalRes);
                        inst->removeThisFromOperands();
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        // 按顺序插入新指令
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(q));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sign));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(q0));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sra_div));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(mulh));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhs_sll));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhs64));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced SDiv with magic number+sign fix for " << rhs_value_abs
                                      << " (magic=" << magic << ", shift=" << shift << ") in " << bb->getName() << "\n";
                        }
                    }
                    else if (rhs_value == -1)
                    {
                        // 除以-1，等价于0-lhs
                        auto *zero = new ConstantInt(IntegerType::getInstance(), 0);
                        auto *neg = new BinaryOperator(Opcode::Sub, zero, lhs, instName + "_neg");
                        inst->replaceAllUsesWith(neg);
                        inst->removeThisFromOperands();
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(neg));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced SDiv by -1 with neg in " << bb->getName() << "\n";
                        }
                    }
                }
            }
            // 新增：2的幂次方取模优化
            else if (inst && inst->getOpcode() == Opcode::SRem)
            {
                Value *lhs = inst->getOperands()[0];
                Value *rhs = inst->getOperands()[1];
                if (auto *constInt = dynamic_cast<ConstantInt *>(rhs))
                {
                    // 这里如果是INT_MIN可能会有问题，abs会溢出,应该不会无聊到模一个最小值吧
                    int rhs_value_abs = abs(constInt->Value);
                    if ((rhs_value_abs & (rhs_value_abs - 1)) == 0)
                    {
                        // x % 2^n == ((x + bias) & mask) - bias
                        // bias = (x >> 31) & mask；mask==1 时 bias = slt(x,0)
                        auto *type = IntegerType::getInstance();
                        int mask_val = rhs_value_abs - 1;
                        auto *mask_const = new ConstantInt(type, mask_val);
                        auto *zero = new ConstantInt(type, 0);
                        std::unique_ptr<Instruction> signMaskHolder;
                        Instruction *bias = nullptr;
                        if (rhs_value_abs == 2)
                        {
                            bias = new ICmpInst(ICmpInst::ICMP_SLT, lhs, zero, instName + "_bias");
                        }
                        else
                        {
                            signMaskHolder = std::make_unique<BinaryOperator>(
                                Opcode::Sra, lhs, new ConstantInt(type, 31), instName + "_signmask");
                            bias = new BinaryOperator(Opcode::And, signMaskHolder.get(), mask_const, instName + "_bias");
                        }
                        auto *x_add_bias = new BinaryOperator(Opcode::Add, lhs, bias, instName + "_addbias");
                        auto *and_mask = new BinaryOperator(Opcode::And, x_add_bias, mask_const, instName + "_andmask");
                        auto *final_res = new BinaryOperator(Opcode::Sub, and_mask, bias, instName + "_mod2n");

                        inst->removeThisFromOperands();
                        inst->replaceAllUsesWith(final_res);
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(final_res));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(and_mask));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(x_add_bias));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(bias));
                        if (signMaskHolder)
                        {
                            insts.insert(insts.begin() + i, std::move(signMaskHolder));
                        }
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced SRem with ((x+bias)&mask)-bias for " << rhs_value_abs
                                      << (rhs_value_abs == 2 ? " (slt bias)" : "")
                                      << " in " << bb->getName() << "\n";
                        }
                    }
                    if ((rhs_value_abs & (rhs_value_abs - 1)) != 0)
                    {
                        if (tryReplaceSRemWithPositiveCondSub(insts, i, lhs, rhs_value_abs, instName,
                                                              func, bb.get(), verbose, debugInfo, needToDelete,
                                                              changed))
                        {
                            continue;
                        }
                        // 1. 计算magic和shift
                        auto [magic, shift] = compute_magic(rhs_value_abs);
                        auto *type = IntegerType::getInstance();
                        // 2. 扩展lhs为64位
                        auto *lhs64 = new CastInst(Opcode::Sext, lhs, LongType::getInstance(), instName + "_to64");
                        // 3. lhs左移32位
                        auto *lhs_sll = new BinaryOperator(Opcode::Slld, lhs64, new ConstantLong(LongType::getInstance(), 32), instName + "_sll32");
                        // 4. 乘以magic
                        auto *magic_const = new ConstantLong(LongType::getInstance(), magic);
                        auto *mulh = new BinaryOperator(Opcode::Mulhd, lhs_sll, magic_const, instName + "_mulmagic");
                        // 5. 取高64位（算术右移shift位）
                        auto *shiftnum = new ConstantLong(LongType::getInstance(), shift);
                        auto *sra_div = new BinaryOperator(Opcode::Srad, mulh, shiftnum, instName + "_sra_div");
                        // 6. 截断回32位
                        auto *q0 = new CastInst(Opcode::Trunc, sra_div, type, instName + "_divmagic");
                        // 由于魔数法的基础是向下取整例如-7/3=-3而不是-2，因此需要修正加一
                        // 7. 修正：被除数为负时，结果加1
                        auto *zero = new ConstantInt(type, 0);
                        auto *sign = new ICmpInst(ICmpInst::ICMP_SLT, lhs, zero, instName + "_divsign");
                        auto *q = new BinaryOperator(Opcode::Add, q0, sign, instName + "_divmagic_fix");
                        // 8. rem = lhs - q * d
                        auto *d_const = new ConstantInt(type, rhs_value_abs);
                        auto *q_mul_d = new BinaryOperator(Opcode::Mul, q, d_const, instName + "_qmul");
                        auto *rem = new BinaryOperator(Opcode::Sub, lhs, q_mul_d, instName + "_remmagic");

                        inst->replaceAllUsesWith(rem);
                        inst->removeThisFromOperands();
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        // 按顺序插入新指令
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(rem));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(q_mul_d));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(q));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sign));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(q0));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sra_div));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(mulh));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhs_sll));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhs64));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced SRem with magic number division for " << rhs_value_abs
                                      << " (magic=" << magic << ", shift=" << shift << ") in " << bb->getName() << "\n";
                        }
                    }
                }
            }
        }
    }
    return changed;
}