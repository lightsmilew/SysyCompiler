#include "ValueRangeAnalysis.h"
#include <algorithm>
#include <map>
#include <unordered_set>
#include <vector>
using namespace std;

namespace optimization
{
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

struct CallSiteActualArg
{
    Function *caller = nullptr;
    Value *arg = nullptr;
};

static map<pair<Argument *, int>, RangeInfo> argCallSiteRangeCache;

static RangeInfo rangeFromCallSiteActualArg(const CallSiteActualArg &site, Argument *param,
                                            const RangeInfo &paramHint, Function *func,
                                            int modHint, unordered_set<Value *> &visiting,
                                            unordered_set<string> &nameGuard)
{
    Value *actualArg = stripCopy(site.arg);
    if (!actualArg || actualArg == param || !site.caller)
        return {};

    if (auto *sra = dynamic_cast<BinaryOperator *>(actualArg))
    {
        if (sra->getOpcode() == Opcode::Sra)
        {
            auto *shiftC = dynamic_cast<ConstantInt *>(stripCopy(sra->getRHS()));
            if (shiftC && shiftC->Value >= 0 && shiftC->Value < 31 &&
                stripCopy(sra->getLHS()) == param)
            {
                if (paramHint.hasLower && paramHint.lower >= 0)
                {
                    RangeInfo r;
                    r.hasLower = true;
                    r.lower = paramHint.lower >> shiftC->Value;
                    if (paramHint.hasUpper)
                    {
                        r.hasUpper = true;
                        r.upper = paramHint.upper >> shiftC->Value;
                    }
                    return r;
                }
                return {};
            }
        }
    }

    return analyzeValueRange(actualArg, site.caller, modHint, visiting, nullptr, nameGuard);
}

static RangeInfo analyzeArgumentFromCallSites(Argument *arg, Function *func, int modHint,
                                              unordered_set<Value *> &visiting,
                                              unordered_set<string> &nameGuard)
{
    Module *module = func->getParent();
    if (!module)
        return {};

    auto cacheKey = make_pair(arg, modHint);
    if (auto cached = argCallSiteRangeCache.find(cacheKey); cached != argCallSiteRangeCache.end())
        return cached->second;

    if (visiting.count(arg))
        return {};
    visiting.insert(arg);

    unsigned argIndex = arg->ArgNo;
    vector<CallSiteActualArg> callArgs;
    callArgs.reserve(8);

    for (const auto &funcPtr : module->Functions)
    {
        Function *caller = funcPtr.get();
        if (!caller)
            continue;
        for (const auto &bbPtr : caller->getBasicBlocks())
        {
            for (const auto &instPtr : bbPtr->getInstructions())
            {
                auto *call = dynamic_cast<CallInst *>(instPtr.get());
                if (!call || call->getCalledFunction() != func)
                    continue;
                const auto &args = call->getArguments();
                if (argIndex >= args.size())
                {
                    visiting.erase(arg);
                    return {};
                }
                callArgs.push_back({caller, args[argIndex]});
            }
        }
    }

    const unsigned paramCount = func->getArguments().size();
    const int maxRounds = paramCount > 16 ? 1 : 4;

    RangeInfo merged;
    bool any = false;
    for (int round = 0; round < maxRounds; ++round)
    {
        RangeInfo next = merged;
        bool nextAny = any;
        for (const CallSiteActualArg &site : callArgs)
        {
            RangeInfo r =
                rangeFromCallSiteActualArg(site, arg, merged, func, modHint, visiting, nameGuard);
            if (!r.hasLower && !r.hasUpper)
                continue;
            nextAny = true;
            if (!next.hasLower && !next.hasUpper)
                next = r;
            else
                next = joinRangeAtMergePoint(next, r);
        }
        if (!nextAny && round == 0)
            break;
        const bool stable =
            next.hasLower == merged.hasLower && next.hasUpper == merged.hasUpper &&
            (!next.hasLower || next.lower == merged.lower) &&
            (!next.hasUpper || next.upper == merged.upper);
        merged = next;
        any = nextAny;
        if (stable)
            break;
    }

    visiting.erase(arg);
    RangeInfo result = any ? merged : RangeInfo{};
    argCallSiteRangeCache.emplace(cacheKey, result);
    return result;
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

// SRem(x,M) 的 C remainder 结果域是 (-M, M) 而非 [0, M-1]：
// 仅当 x 可证明非负时才取 [0, M-1]；否则保守 (-M, M)。
// 避免 (sumAcc + blockSum%mod) 这类含负数 remainder 的和被错误证明非负，
// 进而在 SRFixedPass 中被替换成一次条件减（负数会被漏减）。
static RangeInfo moduloOperandRange(Value *op, int modHint, Function *func,
                                    unordered_set<Value *> &visiting, BasicBlock *useBB,
                                    unordered_set<string> &nameGuard)
{
    if (modHint <= 0)
        return {};
    auto *bin = dynamic_cast<BinaryOperator *>(stripCopy(op));
    if (bin && bin->getOpcode() == Opcode::SRem)
    {
        RangeInfo lhsR =
            analyzeValueRange(bin->getLHS(), func, modHint, visiting, useBB, nameGuard);
        if (!lhsR.hasLower || lhsR.lower < 0)
        {
            RangeInfo conservative;
            conservative.hasLower = conservative.hasUpper = true;
            conservative.lower = -(static_cast<int64_t>(modHint) - 1);
            conservative.upper = static_cast<int64_t>(modHint) - 1;
            return conservative;
        }
    }
    return positiveModRange(modHint);
}

// SRFixedPass pms: res = sub(cur, and(sub(0, icmp sge cur, M), M))
static bool isPositiveCondSubModResult(const BinaryOperator *resSub, int modHint)
{
    if (!resSub || resSub->getOpcode() != Opcode::Sub || modHint <= 0)
        return false;

    Value *orig = stripCopy(resSub->getLHS());
    auto *adjust = dynamic_cast<BinaryOperator *>(stripCopy(resSub->getRHS()));
    if (!adjust || adjust->getOpcode() != Opcode::And)
        return false;

    ConstantInt *modC = nullptr;
    Value *maskVal = nullptr;
    if (auto *c = dynamic_cast<ConstantInt *>(stripCopy(adjust->getLHS())))
    {
        if (c->Value == modHint)
        {
            modC = c;
            maskVal = adjust->getRHS();
        }
    }
    if (!modC)
    {
        if (auto *c = dynamic_cast<ConstantInt *>(stripCopy(adjust->getRHS())))
        {
            if (c->Value == modHint)
            {
                modC = c;
                maskVal = adjust->getLHS();
            }
        }
    }
    if (!modC)
        return false;

    auto *maskSub = dynamic_cast<BinaryOperator *>(stripCopy(maskVal));
    if (!maskSub || maskSub->getOpcode() != Opcode::Sub)
        return false;

    auto *zero = dynamic_cast<ConstantInt *>(stripCopy(maskSub->getLHS()));
    if (!zero || zero->Value != 0)
        return false;

    auto *cmp = dynamic_cast<ICmpInst *>(stripCopy(maskSub->getRHS()));
    if (!cmp || cmp->getPredicate() != ICmpInst::ICMP_SGE)
        return false;

    auto *cmpMod = dynamic_cast<ConstantInt *>(stripCopy(cmp->getRHS()));
    if (!cmpMod || cmpMod->Value != modHint)
        return false;

    return stripCopy(cmp->getLHS()) == orig;
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
        if (modC && modC->Value == modHint)
            return true;
        return isPositiveCondSubModResult(bin, modHint);
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
                RangeInfo r = moduloOperandRange(src, modHint, func, visiting, nullptr, nameGuard);
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
            accR = moduloOperandRange(add->getLHS(), modHint, func, visiting, useBB, nameGuard);
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
            result = analyzeArgumentFromCallSites(arg, func, modHint, visiting, nameGuard);
            if (!result.hasLower && !result.hasUpper)
            {
                unordered_set<Value *> slotVisiting;
                int widen = modHint > 0 ? modHint - 1 : -1;
                result = analyzeIncrementSlotRange(arg, func, slotVisiting, widen);
            }
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
                    return moduloOperandRange(op, modHint, func, visiting, useBB, nameGuard);
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
        else if (bin->getOpcode() == Opcode::Sra)
        {
            auto *shiftC = dynamic_cast<ConstantInt *>(stripCopy(bin->getRHS()));
            if (shiftC && shiftC->Value >= 0 && shiftC->Value < 31)
            {
                RangeInfo lhsR =
                    analyzeValueRange(bin->getLHS(), func, modHint, visiting, useBB, nameGuard);
                if (lhsR.hasLower && lhsR.lower >= 0)
                {
                    result.hasLower = true;
                    result.lower = lhsR.lower >> shiftC->Value;
                    if (lhsR.hasUpper)
                    {
                        result.hasUpper = true;
                        result.upper = lhsR.upper >> shiftC->Value;
                    }
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
static bool proveModuloAccumulateAddRangeImpl(Value *lhs, int mod, Function *func,
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
        accR = moduloOperandRange(accOp, mod, func, visiting, useBB, nameGuard);
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

} // namespace

RangeInfo ValueRangeAnalysis::analyze(Value *v, Function *func, int modHint, BasicBlock *useBB)
{
    if (!v || !func)
        return {};
    unordered_set<Value *> visiting;
    unordered_set<string> nameGuard;
    return analyzeValueRange(v, func, modHint, visiting, useBB, nameGuard);
}

bool ValueRangeAnalysis::proveNonNegative(Value *v, Function *func, BasicBlock *useBB, int modHint)
{
    RangeInfo r = analyze(v, func, modHint, useBB);
    return r.hasLower && r.lower >= 0;
}

bool ValueRangeAnalysis::tryProveModuloAccumulateAddRange(Value *lhs, int mod, Function *func,
                                                          BasicBlock *useBB, RangeInfo &out)
{
    if (!lhs || !func || !useBB)
        return false;
    unordered_set<Value *> visiting;
    unordered_set<string> nameGuard;
    return proveModuloAccumulateAddRangeImpl(lhs, mod, func, useBB, visiting, nameGuard, out);
}

} // namespace optimization
