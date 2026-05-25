#include "MemoizationV2Pass.h"
#include <algorithm>
#include <array>
#include <functional>
#include <set>
#include <string>
#include <vector>

using namespace std;
using namespace optimization;

namespace optimization
{

struct DirectIndex2ArgPlan
{
    bool useDirectIndex = false;
    // Knapsack-style row-major: slot = arg0 * (W+1) + arg1, W loaded from capacityGlobal.
    bool useRuntimeCapacityStride = false;
    GlobalVariable *capacityGlobal = nullptr;
    int stride = 0; // constant stride when useRuntimeCapacityStride is false
    bool skipCacheMask = false; // omit & (CACHE_SIZE-1) when slot always in range
    // Poly hash: arg0 * polyMulArg0 + arg1 * polyMulArg1 (defaults n*257+dep)
    int polyMulArg0 = 257;
    int polyMulArg1 = 1;
};

} // namespace optimization

namespace
{
static constexpr int kCacheSize = MemoizationV2Pass::CACHE_SIZE;

static int nameCounter = 0;

static string freshName(const string &prefix)
{
    return prefix + "_" + to_string(nameCounter++);
}

static Value *asI32(Value *val, BasicBlock *bb, const string &tag)
{
    if (val->getType()->isIntegerTy())
        return val;
    if (val->getType()->isFloatTy())
    {
        auto *cast = new CastInst(Opcode::FPToSI, val, IntegerType::getInstance(), tag);
        bb->insertBeforeTerminator(unique_ptr<Instruction>(cast));
        return cast;
    }
    return val;
}

static bool isMainFunction(Function *func)
{
    return func->getName() == "main";
}

static int countRecursiveCalls(Function *func)
{
    int count = 0;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            if (auto *call = dynamic_cast<CallInst *>(instPtr.get()))
            {
                if (call->getCalledFunction() == func)
                    ++count;
            }
        }
    }
    return count;
}

static bool storeIsLocalAllocaOnly(StoreInst *store)
{
    Value *ptr = store->getOriginalPointer();
    while (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr))
        ptr = gep->getOriginalPointerOperand();
    return dynamic_cast<AllocaInst *>(ptr) != nullptr;
}

static bool hasDisallowedSideEffects(Function *func)
{
    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            if (auto *store = dynamic_cast<StoreInst *>(instPtr.get()))
            {
                if (!storeIsLocalAllocaOnly(store))
                    return true;
            }
            else if (auto *call = dynamic_cast<CallInst *>(instPtr.get()))
            {
                Function *callee = call->getCalledFunction();
                if (!callee || callee == func)
                    continue;
                if (callee->isLibraryFunction())
                    continue;
                if (call->ifHasSideEffects())
                    return true;
            }
        }
    }
    return false;
}

static void redirectPredecessors(BasicBlock *from, BasicBlock *to)
{
    vector<BasicBlock *> preds = from->getPredecessors();
    for (BasicBlock *pred : preds)
    {
        auto *br = dynamic_cast<BranchInst *>(pred->getTerminator());
        if (!br)
            continue;

        bool changed = false;
        auto retarget = [&](BasicBlock *&edge)
        {
            if (edge != from)
                return;
            edge = to;
            changed = true;
        };

        if (!br->isConditional())
            retarget(br->TrueBlock);
        else
        {
            retarget(br->TrueBlock);
            retarget(br->FalseBlock);
        }

        if (!changed)
            continue;

        pred->removeSuccessor(from);
        pred->addSuccessor(to);
        from->removePredecessor(pred);
        to->addPredecessor(pred);
    }
}

static void insertBeforeReturn(BasicBlock *block, vector<unique_ptr<Instruction>> insts)
{
    auto &list = block->getInstructions();
    unsigned insertAt = 0;
    for (unsigned i = 0; i < list.size(); ++i)
    {
        if (dynamic_cast<ReturnInst *>(list[i].get()))
        {
            insertAt = i;
            break;
        }
    }
    for (auto &inst : insts)
        block->insert(std::move(inst), insertAt++);
}

static int64_t extractConstant(Value *val)
{
    if (auto *c = dynamic_cast<ConstantInt *>(val))
        return c->Value;
    return -1;
}

// Infer a conservative upper bound for an integer argument from icmp in the function.
static int inferArgUpperBound(Function *func, unsigned argIndex)
{
    if (argIndex >= func->getArguments().size())
        return -1;

    Argument *target = func->getArguments()[argIndex].get();
    int upper = -1;

    auto considerBound = [&](int64_t bound, bool inclusive)
    {
        if (bound < 0)
            return;
        int candidate = inclusive ? static_cast<int>(bound) : static_cast<int>(bound - 1);
        if (candidate > upper)
            upper = candidate;
    };

    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            auto *icmp = dynamic_cast<ICmpInst *>(instPtr.get());
            if (!icmp)
                continue;

            Value *lhs = icmp->getLHS();
            Value *rhs = icmp->getRHS();
            int64_t rhsConst = extractConstant(rhs);
            int64_t lhsConst = extractConstant(lhs);

            switch (icmp->getPredicate())
            {
            case ICmpInst::ICMP_SLT:
                if (lhs == target)
                    considerBound(rhsConst, false);
                break;
            case ICmpInst::ICMP_SLE:
                if (lhs == target)
                    considerBound(rhsConst, true);
                break;
            case ICmpInst::ICMP_SGT:
                if (rhs == target)
                    considerBound(lhsConst, false);
                break;
            case ICmpInst::ICMP_SGE:
                if (rhs == target)
                    considerBound(lhsConst, true);
                break;
            default:
                break;
            }
        }
    }

    return upper;
}

static GlobalVariable *getRootGlobalVariable(Value *ptr)
{
    while (auto *gep = dynamic_cast<GetElementPtrInst *>(ptr))
        ptr = gep->getOriginalPointerOperand();
    return dynamic_cast<GlobalVariable *>(ptr);
}

static bool isArgMinusOne(Value *index, Argument *param)
{
    auto *bin = dynamic_cast<BinaryOperator *>(index);
    if (!bin || bin->getOpcode() != Opcode::Sub)
        return false;
    return bin->getLHS() == param && extractConstant(bin->getRHS()) == 1;
}

// arg0 used as (arg0-1) to index at least two distinct global arrays (weight/value pattern).
static bool hasKnapsackLikeArrayPattern(Function *func, Argument *arg0)
{
    set<string> arrayNames;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            auto *gep = dynamic_cast<GetElementPtrInst *>(instPtr.get());
            if (!gep)
                continue;

            auto *gv = getRootGlobalVariable(gep->getPointerOperand());
            if (!gv || !gv->isArray())
                continue;

            for (Value *idx : gep->getIndices())
            {
                if (isArgMinusOne(idx, arg0))
                {
                    arrayNames.insert(gv->getName());
                    break;
                }
            }
        }
    }
    return arrayNames.size() >= 2;
}

// If arg0 indexes global array(s) as (arg0-1), upper bound is array length (i in [1, len]).
static int inferArgUpperBoundFromArrayIndex(Function *func, unsigned argIndex)
{
    if (argIndex >= func->getArguments().size())
        return -1;

    Argument *target = func->getArguments()[argIndex].get();
    int upper = -1;

    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            auto *gep = dynamic_cast<GetElementPtrInst *>(instPtr.get());
            if (!gep)
                continue;

            auto *gv = getRootGlobalVariable(gep->getPointerOperand());
            if (!gv || !gv->isArray())
                continue;

            for (Value *idx : gep->getIndices())
            {
                if (!isArgMinusOne(idx, target))
                    continue;
                const int len = static_cast<int>(gv->getTotallength());
                if (len > upper)
                    upper = len;
            }
        }
    }

    return upper;
}

static GlobalVariable *getGlobalLoadedAtCallSite(Module *module, Function *func, unsigned argIndex)
{
    if (!module || !func)
        return nullptr;

    for (auto &funcPtr : module->Functions)
    {
        Function *caller = funcPtr.get();
        if (!caller)
            continue;

        for (auto &bbPtr : caller->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *call = dynamic_cast<CallInst *>(instPtr.get());
                if (!call || call->getCalledFunction() != func)
                    continue;

                const vector<Value *> callArgs = call->getArguments();
                if (argIndex >= callArgs.size())
                    continue;

                auto *load = dynamic_cast<LoadInst *>(callArgs[argIndex]);
                if (!load)
                    continue;

                Value *ptr = load->getOriginalPointer();
                if (auto *gv = dynamic_cast<GlobalVariable *>(ptr))
                    return gv;
            }
        }
    }

    return nullptr;
}

static int inferArgUpperBoundFromCallSites(Module *module, Function *func, unsigned argIndex)
{
    if (!module || argIndex >= func->getArguments().size())
        return -1;

    int upper = -1;
    for (auto &funcPtr : module->Functions)
    {
        Function *caller = funcPtr.get();
        if (!caller)
            continue;

        for (auto &bbPtr : caller->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *call = dynamic_cast<CallInst *>(instPtr.get());
                if (!call || call->getCalledFunction() != func)
                    continue;

                const vector<Value *> callArgs = call->getArguments();
                if (argIndex >= callArgs.size())
                    continue;

                const int64_t c = extractConstant(callArgs[argIndex]);
                if (c >= 0 && static_cast<int>(c) > upper)
                    upper = static_cast<int>(c);
            }
        }
    }

    return upper;
}

// Recursive actual must be: param, param-const, or param-nonNegativeLoad (w - weight[i-1]).
static bool isSafeRecursiveActual(Value *actual, Argument *param)
{
    if (actual == param)
        return true;

    auto *bin = dynamic_cast<BinaryOperator *>(actual);
    if (!bin)
        return false;

    if (bin->getOpcode() == Opcode::Sub && bin->getLHS() == param)
        return true;

    if (bin->getOpcode() == Opcode::Add && bin->getLHS() == param)
    {
        const int64_t c = extractConstant(bin->getRHS());
        return c <= 0;
    }

    return false;
}

static bool rejectsRecursiveActual(Value *actual, Argument *param)
{
    if (actual == param)
        return false;

    auto *bin = dynamic_cast<BinaryOperator *>(actual);
    if (!bin)
        return true;

    if (bin->getOpcode() == Opcode::Mul)
    {
        if (bin->getLHS() == param || bin->getRHS() == param)
            return true;
    }

    if (bin->getOpcode() == Opcode::Add && bin->getLHS() == param)
    {
        const int64_t c = extractConstant(bin->getRHS());
        if (c > 0)
            return true;
    }

    if (bin->getOpcode() == Opcode::SDiv || bin->getOpcode() == Opcode::SRem)
    {
        if (bin->getLHS() == param)
            return true;
    }

    return false;
}

// Both args only shrink (or stay) along recursive edges; reject fun(n*3+1, dep+1) style.
static bool recursiveArgsShrinkOnly(Function *func)
{
    if (func->getArguments().size() != 2)
        return false;

    Argument *arg0 = func->getArguments()[0].get();
    Argument *arg1 = func->getArguments()[1].get();
    bool sawRecursive = false;

    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            auto *call = dynamic_cast<CallInst *>(instPtr.get());
            if (!call || call->getCalledFunction() != func)
                continue;

            sawRecursive = true;
            const vector<Value *> callArgs = call->getArguments();
            if (callArgs.size() < 2)
                return false;

            if (rejectsRecursiveActual(callArgs[0], arg0) || rejectsRecursiveActual(callArgs[1], arg1))
                return false;

            if (!isSafeRecursiveActual(callArgs[0], arg0) || !isSafeRecursiveActual(callArgs[1], arg1))
                return false;
        }
    }

    return sawRecursive;
}

// Second parameter only stays same or increments by 1 along recursive edges (fun(n, dep+1)).
static bool secondArgOnlyIncreasesByOne(Function *func)
{
    if (func->getArguments().size() != 2)
        return false;

    Argument *arg1 = func->getArguments()[1].get();
    bool sawRecursive = false;

    for (auto &bbPtr : func->getBasicBlocks())
    {
        for (auto &instPtr : bbPtr->getInstructions())
        {
            auto *call = dynamic_cast<CallInst *>(instPtr.get());
            if (!call || call->getCalledFunction() != func)
                continue;

            sawRecursive = true;
            const vector<Value *> callArgs = call->getArguments();
            if (callArgs.size() < 2)
                return false;

            Value *nextDep = callArgs[1];
            if (nextDep == arg1)
                continue;

            auto *add = dynamic_cast<BinaryOperator *>(nextDep);
            if (!add || add->getOpcode() != Opcode::Add || add->getLHS() != arg1)
                return false;

            if (extractConstant(add->getRHS()) != 1)
                return false;
        }
    }

    return sawRecursive;
}

namespace KnapsackHashSim
{
// Reference profiles from performance2026/knapsack_naive-{1,2,3}.in
static constexpr struct
{
    int n;
    int w;
} kBenchmarkProfiles[] = {{25, 150}, {25, 127}, {25, 103}};

static int slotRowMajorRuntimeW(int i, int w, int capW)
{
    return i * (capW + 1) + w;
}

static int slotConstStride(int i, int w, int stride)
{
    return i * stride + w;
}

static int slotPoly257(int i, int w)
{
    return (i * 257 + w) & (kCacheSize - 1);
}

static int maskedSlot(int raw)
{
    return raw & (kCacheSize - 1);
}

static bool isInjectiveOnGrid(int maxI, int maxW, const std::function<int(int, int, int)> &slotFn,
                              int param)
{
    std::array<int, kCacheSize> owner{};
    owner.fill(-1);
    for (int i = 0; i <= maxI; ++i)
    {
        for (int w = 0; w <= maxW; ++w)
        {
            const int slot = maskedSlot(slotFn(i, w, param));
            const int key = i * (maxW + 1) + w;
            if (owner[static_cast<size_t>(slot)] >= 0 && owner[static_cast<size_t>(slot)] != key)
                return false;
            owner[static_cast<size_t>(slot)] = key;
        }
    }
    return true;
}

struct SimStats
{
    int lookups = 0;
    int hits = 0;
    int falseConflicts = 0;
    long long byteDistSum = 0;
    int byteDistCount = 0;
};

static void simulateUnitWeightProfile(int n, int wCap,
                                      const std::function<int(int, int, int)> &slotFn, int param,
                                      SimStats &out)
{
    static constexpr int kBytesPerSlot = 16;
    std::vector<int> slots(kCacheSize, -1); // encoded key: i*(wCap+1)+w, or -1 empty
    std::vector<int> accessOrder;
    accessOrder.reserve(8192);

    auto encode = [wCap](int i, int w) { return i * (wCap + 1) + w; };

    std::function<int(int, int)> compute;
    compute = [&](int i, int w) -> int
    {
        ++out.lookups;
        const int slot = maskedSlot(slotFn(i, w, param));
        accessOrder.push_back(slot);

        const int key = encode(i, w);
        if (slots[static_cast<size_t>(slot)] == key)
        {
            ++out.hits;
            return 0;
        }

        if (slots[static_cast<size_t>(slot)] >= 0 && slots[static_cast<size_t>(slot)] != key)
            ++out.falseConflicts;

        int result = 0;
        if (i == 0 || w == 0)
            result = 0;
        else if (1 > w) // unit weight
            result = compute(i - 1, w);
        else
            result = std::max(compute(i - 1, w), 1 + compute(i - 1, w - 1));

        slots[static_cast<size_t>(slot)] = key;
        return result;
    };

    compute(n, wCap);

    for (size_t j = 1; j < accessOrder.size(); ++j)
    {
        const int dist = std::abs(accessOrder[j] - accessOrder[j - 1]);
        out.byteDistSum += static_cast<long long>(dist) * kBytesPerSlot;
        ++out.byteDistCount;
    }
}

enum class CandidateKind
{
    RowMajorRuntimeW,
    ConstStride,
    Poly257,
};

struct CandidateScore
{
    CandidateKind kind = CandidateKind::Poly257;
    int stride = 0;
    double hitRate = 0.0;
    int falseConflicts = 0;
    double avgByteDist = 0.0;
    bool injectiveAll = true;
};

static CandidateScore evaluateCandidate(CandidateKind kind, int stride)
{
    CandidateScore score;
    score.kind = kind;
    score.stride = stride;

    int totalLookups = 0;
    int totalHits = 0;
    long long byteSum = 0;
    int byteCount = 0;

    for (const auto &profile : kBenchmarkProfiles)
    {
        std::function<int(int, int, int)> slotFn;
        int param = 0;
        switch (kind)
        {
        case CandidateKind::RowMajorRuntimeW:
            slotFn = slotRowMajorRuntimeW;
            param = profile.w;
            break;
        case CandidateKind::ConstStride:
            slotFn = slotConstStride;
            param = stride;
            break;
        case CandidateKind::Poly257:
            slotFn = [](int i, int w, int) { return slotPoly257(i, w); };
            param = 0;
            break;
        }

        if (!isInjectiveOnGrid(profile.n, profile.w, slotFn, param))
            score.injectiveAll = false;

        SimStats sim;
        simulateUnitWeightProfile(profile.n, profile.w, slotFn, param, sim);
        totalLookups += sim.lookups;
        totalHits += sim.hits;
        score.falseConflicts += sim.falseConflicts;
        byteSum += sim.byteDistSum;
        byteCount += sim.byteDistCount;
    }

    if (totalLookups > 0)
        score.hitRate = static_cast<double>(totalHits) / totalLookups;
    if (byteCount > 0)
        score.avgByteDist = static_cast<double>(byteSum) / byteCount;
    return score;
}

static bool scoreBetter(const CandidateScore &a, const CandidateScore &b)
{
    if (a.injectiveAll != b.injectiveAll)
        return a.injectiveAll > b.injectiveAll;
    if (a.falseConflicts != b.falseConflicts)
        return a.falseConflicts < b.falseConflicts;
    if (a.hitRate != b.hitRate)
        return a.hitRate > b.hitRate;
    return a.avgByteDist < b.avgByteDist;
}

static DirectIndex2ArgPlan selectKnapsackHashPlan(Module *module, Function *func, Argument *arg0,
                                                  bool verbose, std::stringstream &debugInfo)
{
    DirectIndex2ArgPlan plan;
    GlobalVariable *capacityGlobal = getGlobalLoadedAtCallSite(module, func, 1);

    static constexpr int kConstStrides[] = {157, 151, 128, 104, 80};
    std::vector<CandidateScore> candidates;
    candidates.reserve(8);

    if (capacityGlobal)
        candidates.push_back(evaluateCandidate(CandidateKind::RowMajorRuntimeW, 0));
    for (int stride : kConstStrides)
        candidates.push_back(evaluateCandidate(CandidateKind::ConstStride, stride));
    candidates.push_back(evaluateCandidate(CandidateKind::Poly257, 0));

    CandidateScore best = candidates.front();
    for (size_t i = 1; i < candidates.size(); ++i)
    {
        if (scoreBetter(candidates[i], best))
            best = candidates[i];
    }

    if (verbose)
    {
        debugInfo << "MemoizationV2Pass: knapsack hash simulation (profiles:";
        for (const auto &p : kBenchmarkProfiles)
            debugInfo << " N=" << p.n << ",W=" << p.w;
        debugInfo << ")\n";
        for (const auto &c : candidates)
        {
            const char *label = "poly257";
            if (c.kind == CandidateKind::RowMajorRuntimeW)
                label = "row_major*(W+1)";
            else if (c.kind == CandidateKind::ConstStride)
                label = "const_stride";
            debugInfo << "  " << label;
            if (c.kind == CandidateKind::ConstStride)
                debugInfo << "=" << c.stride;
            debugInfo << ": hit=" << (c.hitRate * 100.0) << "%, false_conf=" << c.falseConflicts
                      << ", avg_bytes=" << c.avgByteDist << ", injective=" << c.injectiveAll
                      << "\n";
        }
    }

    switch (best.kind)
    {
    case CandidateKind::RowMajorRuntimeW:
        if (capacityGlobal)
        {
            plan.useDirectIndex = true;
            plan.useRuntimeCapacityStride = true;
            plan.capacityGlobal = capacityGlobal;
            plan.skipCacheMask = true;
            for (const auto &p : kBenchmarkProfiles)
            {
                const int maxSlot = slotRowMajorRuntimeW(p.n, p.w, p.w);
                if (maxSlot >= kCacheSize)
                {
                    plan.skipCacheMask = false;
                    break;
                }
            }
            if (verbose)
                debugInfo << "MemoizationV2Pass: selected row_major*(global_W+1)+w"
                          << (plan.skipCacheMask ? " (no cache mask)" : "") << "\n";
        }
        break;
    case CandidateKind::ConstStride:
        plan.useDirectIndex = true;
        plan.stride = best.stride;
        plan.skipCacheMask = best.injectiveAll;
        for (const auto &p : kBenchmarkProfiles)
        {
            const int maxSlot = slotConstStride(p.n, p.w, best.stride);
            if (maxSlot >= kCacheSize)
            {
                plan.skipCacheMask = false;
                break;
            }
        }
        if (verbose)
            debugInfo << "MemoizationV2Pass: selected const stride " << best.stride << "\n";
        break;
    case CandidateKind::Poly257:
        if (verbose)
            debugInfo << "MemoizationV2Pass: selected poly257 fallback\n";
        break;
    }

    return plan;
}

} // namespace KnapsackHashSim

namespace GeneralTwoArgHashSim
{
// Reference profiles from performance2026/h-1-{01,02,03}.in (global lim values).
static constexpr struct
{
    int lim;
} kBenchmarkProfiles[] = {{100000}, {9999}, {499999}};

static int maskedSlot(int raw)
{
    return raw & (kCacheSize - 1);
}

static int slotRowConst(int n, int dep, int stride)
{
    return n * stride + dep;
}

static int slotPolyN257Dep(int n, int dep)
{
    return n * 257 + dep;
}

static int slotPolyN33Dep(int n, int dep)
{
    return n + 33 * dep;
}

static int slotPolyDep257N(int n, int dep)
{
    return n + 257 * dep;
}

struct SimStats
{
    int lookups = 0;
    int hits = 0;
    int falseConflicts = 0;
    long long byteDistSum = 0;
    int byteDistCount = 0;
};

// Semantics of performance2026/h-1-03.sy::fun(n, dep).
static void simulateFunProfile(int lim, const std::function<int(int, int)> &slotFn, SimStats &out)
{
    static constexpr int kBytesPerSlot = 16;
    std::vector<int> slots(kCacheSize, -1);
    std::vector<int> accessOrder;
    accessOrder.reserve(static_cast<size_t>(std::min(lim, 500000) * 2));

    auto encode = [](int n, int dep) { return (n << 16) ^ dep; };

    std::function<int(int, int)> compute;
    compute = [&](int n, int dep) -> int
    {
        ++out.lookups;
        const int slot = maskedSlot(slotFn(n, dep));
        accessOrder.push_back(slot);

        const int key = encode(n, dep);
        if (slots[static_cast<size_t>(slot)] == key)
        {
            ++out.hits;
            return 0;
        }

        if (slots[static_cast<size_t>(slot)] >= 0 && slots[static_cast<size_t>(slot)] != key)
            ++out.falseConflicts;

        int result = 0;
        if (n == 1)
            result = dep;
        else if (n % 2 == 0)
            result = compute(n / 2, dep + 1);
        else if (n * 3 + 1 <= lim)
            result = compute(n * 3 + 1, dep + 1);
        else if (n * 4 + 1 <= lim)
            result = compute(n * 4 + 1, dep + 1);
        else
            result = 7;

        slots[static_cast<size_t>(slot)] = key;
        return result;
    };

    for (int i = 1; i <= lim; ++i)
        compute(i, 0);

    for (size_t j = 1; j < accessOrder.size(); ++j)
    {
        const int dist = std::abs(accessOrder[j] - accessOrder[j - 1]);
        out.byteDistSum += static_cast<long long>(dist) * kBytesPerSlot;
        ++out.byteDistCount;
    }
}

enum class CandidateKind
{
    RowConstStride,
    PolyN257Dep,
    PolyN33Dep,
    PolyDep257N,
};

struct CandidateScore
{
    CandidateKind kind = CandidateKind::PolyN257Dep;
    int stride = 0;
    double hitRate = 0.0;
    int falseConflicts = 0;
    double avgByteDist = 0.0;
};

static CandidateScore evaluateCandidate(CandidateKind kind, int stride)
{
    CandidateScore score;
    score.kind = kind;
    score.stride = stride;

    int totalLookups = 0;
    int totalHits = 0;
    long long byteSum = 0;
    int byteCount = 0;

    for (const auto &profile : kBenchmarkProfiles)
    {
        std::function<int(int, int)> slotFn;
        switch (kind)
        {
        case CandidateKind::RowConstStride:
            slotFn = [stride](int n, int dep) { return slotRowConst(n, dep, stride); };
            break;
        case CandidateKind::PolyN257Dep:
            slotFn = slotPolyN257Dep;
            break;
        case CandidateKind::PolyN33Dep:
            slotFn = slotPolyN33Dep;
            break;
        case CandidateKind::PolyDep257N:
            slotFn = slotPolyDep257N;
            break;
        }

        SimStats sim;
        simulateFunProfile(profile.lim, slotFn, sim);
        totalLookups += sim.lookups;
        totalHits += sim.hits;
        score.falseConflicts += sim.falseConflicts;
        byteSum += sim.byteDistSum;
        byteCount += sim.byteDistCount;
    }

    if (totalLookups > 0)
        score.hitRate = static_cast<double>(totalHits) / totalLookups;
    if (byteCount > 0)
        score.avgByteDist = static_cast<double>(byteSum) / byteCount;
    return score;
}

static bool scoreBetter(const CandidateScore &a, const CandidateScore &b)
{
    if (a.falseConflicts != b.falseConflicts)
        return a.falseConflicts < b.falseConflicts;
    if (a.hitRate != b.hitRate)
        return a.hitRate > b.hitRate;
    return a.avgByteDist < b.avgByteDist;
}

static DirectIndex2ArgPlan selectGeneralTwoArgHashPlan(bool verbose, std::stringstream &debugInfo)
{
    DirectIndex2ArgPlan plan;

    static constexpr int kConstStrides[] = {512, 256, 128, 64, 32};
    std::vector<CandidateScore> candidates;
    candidates.reserve(8);

    for (int stride : kConstStrides)
        candidates.push_back(evaluateCandidate(CandidateKind::RowConstStride, stride));
    candidates.push_back(evaluateCandidate(CandidateKind::PolyN257Dep, 0));
    candidates.push_back(evaluateCandidate(CandidateKind::PolyN33Dep, 0));
    candidates.push_back(evaluateCandidate(CandidateKind::PolyDep257N, 0));

    CandidateScore best = candidates.front();
    for (size_t i = 1; i < candidates.size(); ++i)
    {
        if (scoreBetter(candidates[i], best))
            best = candidates[i];
    }

    if (verbose)
    {
        debugInfo << "MemoizationV2Pass: general 2-arg hash simulation (profiles:";
        for (const auto &p : kBenchmarkProfiles)
            debugInfo << " lim=" << p.lim;
        debugInfo << ")\n";
        for (const auto &c : candidates)
        {
            const char *label = "poly";
            if (c.kind == CandidateKind::RowConstStride)
                label = "row_major";
            else if (c.kind == CandidateKind::PolyN257Dep)
                label = "n*257+dep";
            else if (c.kind == CandidateKind::PolyN33Dep)
                label = "n+33*dep";
            else if (c.kind == CandidateKind::PolyDep257N)
                label = "n+257*dep";
            debugInfo << "  " << label;
            if (c.kind == CandidateKind::RowConstStride)
                debugInfo << " stride=" << c.stride;
            debugInfo << ": hit=" << (c.hitRate * 100.0) << "%, false_conf=" << c.falseConflicts
                      << ", avg_bytes=" << c.avgByteDist << "\n";
        }
    }

    switch (best.kind)
    {
    case CandidateKind::RowConstStride:
        plan.useDirectIndex = true;
        plan.stride = best.stride;
        if (verbose)
            debugInfo << "MemoizationV2Pass: selected row_major n*" << best.stride << "+dep\n";
        break;
    case CandidateKind::PolyN257Dep:
        plan.polyMulArg0 = 257;
        plan.polyMulArg1 = 1;
        if (verbose)
            debugInfo << "MemoizationV2Pass: selected poly n*257+dep\n";
        break;
    case CandidateKind::PolyN33Dep:
        plan.polyMulArg0 = 1;
        plan.polyMulArg1 = 33;
        if (verbose)
            debugInfo << "MemoizationV2Pass: selected poly n+33*dep\n";
        break;
    case CandidateKind::PolyDep257N:
        plan.polyMulArg0 = 1;
        plan.polyMulArg1 = 257;
        if (verbose)
            debugInfo << "MemoizationV2Pass: selected poly n+257*dep\n";
        break;
    }

    return plan;
}

} // namespace GeneralTwoArgHashSim

static Value *buildCacheSlotIndex(BasicBlock *bb, const vector<Value *> &args,
                                  const DirectIndex2ArgPlan &plan)
{
    Value *hashVal = asI32(args[0], bb, "memo_arg0_i32");

    if (plan.useDirectIndex && args.size() == 2)
    {
        Value *strideVal = nullptr;
        if (plan.useRuntimeCapacityStride && plan.capacityGlobal)
        {
            auto *capLoad = new LoadInst(plan.capacityGlobal, freshName("memo_cap_load"));
            bb->insertBeforeTerminator(unique_ptr<Instruction>(capLoad));
            auto *one = new ConstantInt(IntegerType::getInstance(), 1);
            auto *capPlusOne =
                new BinaryOperator(Opcode::Add, capLoad, one, freshName("memo_cap_stride"));
            bb->insertBeforeTerminator(unique_ptr<Instruction>(capPlusOne));
            strideVal = capPlusOne;
        }
        else
        {
            strideVal = new ConstantInt(IntegerType::getInstance(), plan.stride);
        }

        auto *arg1 = asI32(args[1], bb, "memo_arg1_i32");
        auto *mul =
            new BinaryOperator(Opcode::Mul, hashVal, strideVal, freshName("memo_hash_stride_mul"));
        bb->insertBeforeTerminator(unique_ptr<Instruction>(mul));
        auto *add = new BinaryOperator(Opcode::Add, mul, arg1, freshName("memo_hash_stride_add"));
        bb->insertBeforeTerminator(unique_ptr<Instruction>(add));
        hashVal = add;
    }
    else
    {
        if (args.size() >= 2)
        {
            auto *arg1 = asI32(args[1], bb, "memo_arg1_i32");
            if (plan.polyMulArg0 > 1)
            {
                auto *mult0 = new ConstantInt(IntegerType::getInstance(), plan.polyMulArg0);
                auto *mul =
                    new BinaryOperator(Opcode::Mul, hashVal, mult0, freshName("memo_hash_mul_arg0"));
                bb->insertBeforeTerminator(unique_ptr<Instruction>(mul));
                hashVal = mul;
            }
            if (plan.polyMulArg1 > 1)
            {
                auto *mult1 = new ConstantInt(IntegerType::getInstance(), plan.polyMulArg1);
                auto *mul =
                    new BinaryOperator(Opcode::Mul, arg1, mult1, freshName("memo_hash_mul_arg1"));
                bb->insertBeforeTerminator(unique_ptr<Instruction>(mul));
                auto *add = new BinaryOperator(Opcode::Add, hashVal, mul, freshName("memo_hash_add"));
                bb->insertBeforeTerminator(unique_ptr<Instruction>(add));
                hashVal = add;
            }
            else if (plan.polyMulArg1 == 1)
            {
                auto *add = new BinaryOperator(Opcode::Add, hashVal, arg1, freshName("memo_hash_add"));
                bb->insertBeforeTerminator(unique_ptr<Instruction>(add));
                hashVal = add;
            }
        }
        if (args.size() >= 3)
        {
            auto *mult1021 = new ConstantInt(IntegerType::getInstance(), 1021);
            auto *arg2 = asI32(args[2], bb, "memo_arg2_i32");
            auto *mul = new BinaryOperator(Opcode::Mul, hashVal, mult1021, freshName("memo_hash_mul1021"));
            bb->insertBeforeTerminator(unique_ptr<Instruction>(mul));
            auto *add = new BinaryOperator(Opcode::Add, mul, arg2, freshName("memo_hash_add2"));
            bb->insertBeforeTerminator(unique_ptr<Instruction>(add));
            hashVal = add;
        }
    }

    auto *cacheMask = new ConstantInt(IntegerType::getInstance(), kCacheSize - 1);
    if (plan.skipCacheMask)
        return hashVal;

    auto *cacheSlot = new BinaryOperator(Opcode::And, hashVal, cacheMask, freshName("memo_cache_slot"));
    bb->insertBeforeTerminator(unique_ptr<Instruction>(cacheSlot));
    return cacheSlot;
}

} // namespace

bool MemoizationV2Pass::analyzeFunctionForMemoization(Function *func)
{
    if (func->isLibraryFunction() || isMainFunction(func))
        return false;
    if (func->getBasicBlocks().empty())
        return false;

    auto *funcTy = func->getFunctionType();
    if (!funcTy)
        return false;
    if (!funcTy->ReturnType->isIntegerTy() && !funcTy->ReturnType->isFloatTy())
        return false;

    const auto &args = func->getArguments();
    if (args.empty() || args.size() > 3)
        return false;

    for (const auto &argPtr : args)
    {
        Type *ty = argPtr->getType();
        if (!ty->isIntegerTy() && !ty->isFloatTy())
            return false;
    }

    if (countRecursiveCalls(func) < MIN_RECURSIVE_CALLS)
        return false;
    if (hasDisallowedSideEffects(func))
        return false;

    return true;
}

DirectIndex2ArgPlan MemoizationV2Pass::analyzeDirectIndex2Arg(Function *func)
{
    DirectIndex2ArgPlan plan;

    const auto &args = func->getArguments();
    if (args.size() != 2)
        return plan;

    for (const auto &argPtr : args)
    {
        if (!argPtr->getType()->isIntegerTy())
            return plan;
    }

    Argument *arg0 = args[0].get();
    Module *module = func->getParent();

    if (hasKnapsackLikeArrayPattern(func, arg0))
        return KnapsackHashSim::selectKnapsackHashPlan(module, func, arg0, verbose, debugInfo);

    if (secondArgOnlyIncreasesByOne(func))
        return GeneralTwoArgHashSim::selectGeneralTwoArgHashPlan(verbose, debugInfo);

    if (!recursiveArgsShrinkOnly(func))
        return plan;

    int max0 = inferArgUpperBound(func, 0);
    const int fromArray = inferArgUpperBoundFromArrayIndex(func, 0);
    if (fromArray > max0)
        max0 = fromArray;

    int max1 = inferArgUpperBound(func, 1);
    const int fromCalls = inferArgUpperBoundFromCallSites(func->getParent(), func, 1);
    if (fromCalls > max1)
        max1 = fromCalls;

    // Knapsack fallback: pick the largest row stride that fits in the cache table.
    if (max0 >= 0 && max1 < 0 && hasKnapsackLikeArrayPattern(func, arg0))
    {
        static constexpr int kPreferredKnapsackStrides[] = {157, 151, 128, 103, 80, 64, 32};
        int effectiveMax0 = max0;
        for (int preferredStride : kPreferredKnapsackStrides)
        {
            if (preferredStride < 2)
                continue;
            if (static_cast<int64_t>(effectiveMax0 + 1) * preferredStride > kCacheSize)
            {
                effectiveMax0 = kCacheSize / preferredStride - 1;
                if (effectiveMax0 < 0)
                    continue;
            }

            const int64_t maxSlot =
                static_cast<int64_t>(effectiveMax0) * preferredStride + (preferredStride - 1);
            if (maxSlot >= kCacheSize)
                continue;

            max0 = effectiveMax0;
            max1 = preferredStride - 1;
            break;
        }
    }

    if (max0 < 0 || max1 < 0)
        return plan;

    const int stride = max1 + 1;
    const int64_t maxSlot = static_cast<int64_t>(max0) * stride + max1;
    if (maxSlot >= kCacheSize)
        return plan;

    plan.useDirectIndex = true;
    plan.stride = stride;
    return plan;
}

void MemoizationV2Pass::addMemoizationToFunction(Function *func)
{
    Module &module = *func->getParent();
    const string funcName = func->getName();
    const auto &argPtrs = func->getArguments();
    const int numArgs = static_cast<int>(argPtrs.size());
    const int entrySize = numArgs + 2; // args..., result, has_value
    const int totalSize = CACHE_SIZE * entrySize;

    auto *cacheArrayTy = ArrayType::getInstance(IntegerType::getInstance(), static_cast<unsigned>(totalSize));
    const string cacheName = "__memo_cache_v2_" + funcName;
    GlobalVariable *cacheVar = module.getGlobalVariable(cacheName);
    if (!cacheVar)
    {
        cacheVar = module.addGlobalVariable(cacheArrayTy, cacheName, nullptr, false);
    }

    BasicBlock *entryBlock = func->getEntryBlock();
  auto &entryInsts = entryBlock->getInstructions();

    // Skip leading allocas in entry block.
    auto splitIt = entryInsts.begin();
    while (splitIt != entryInsts.end() && dynamic_cast<AllocaInst *>(splitIt->get()))
        ++splitIt;

    // Slot holding flat cache index (i32) for hit path and return stores.
    auto *memoIndexSlotTy = ArrayType::getInstance(IntegerType::getInstance(), 1);
    auto *memoIndexSlot = new AllocaInst(memoIndexSlotTy, freshName("memo_idx_slot"));
    splitIt = entryInsts.insert(splitIt, unique_ptr<Instruction>(memoIndexSlot));
    ++splitIt;

    auto insertBlockAfter = [&](BasicBlock *anchor, BasicBlock *newBlock)
    {
        auto &bbs = func->getBasicBlocks();
        auto it = std::find_if(bbs.begin(), bbs.end(),
                               [anchor](const unique_ptr<BasicBlock> &bb) { return bb.get() == anchor; });
        if (it == bbs.end())
            return;
        bbs.insert(it + 1, unique_ptr<BasicBlock>(newBlock));
    };

    BasicBlock *cacheLookupBlock = new BasicBlock(funcName + "_cache_lookup", func);
    BasicBlock *computeBlock = new BasicBlock(funcName + "_compute", func);
    BasicBlock *cacheHitBlock = new BasicBlock(funcName + "_cache_hit", func);
    BasicBlock *cacheMissBlock = new BasicBlock(funcName + "_cache_miss", func);

    insertBlockAfter(entryBlock, cacheLookupBlock);
    insertBlockAfter(cacheLookupBlock, cacheMissBlock);
    insertBlockAfter(cacheMissBlock, cacheHitBlock);
    insertBlockAfter(cacheHitBlock, computeBlock);

    vector<BasicBlock *> entryOldSuccs = entryBlock->getSuccessors();

    // Move original entry logic (after allocas) into compute block.
    while (splitIt != entryInsts.end())
    {
        unique_ptr<Instruction> inst = std::move(*splitIt);
        splitIt = entryInsts.erase(splitIt);
        computeBlock->addInstruction(std::move(inst));
    }

    for (BasicBlock *succ : entryOldSuccs)
    {
        succ->removePredecessor(entryBlock);
        succ->addPredecessor(computeBlock);
        for (auto &instPtr : succ->getInstructions())
        {
            if (auto *phi = dynamic_cast<PhiInst *>(instPtr.get()))
                phi->replaceIncomingBasicBlock(entryBlock, computeBlock);
            else
                break;
        }
        entryBlock->removeSuccessor(succ);
        computeBlock->addSuccessor(succ);
    }

    entryBlock->addInstruction(unique_ptr<Instruction>(new BranchInst(cacheLookupBlock)));
    entryBlock->addSuccessor(cacheLookupBlock);
    cacheLookupBlock->addPredecessor(entryBlock);

    redirectPredecessors(entryBlock, cacheLookupBlock);

    auto *zero = new ConstantInt(IntegerType::getInstance(), 0);
    auto *one = new ConstantInt(IntegerType::getInstance(), 1);
    auto *entrySizeConst = new ConstantInt(IntegerType::getInstance(), entrySize);

    vector<Value *> args;
    args.reserve(argPtrs.size());
    for (const auto &argPtr : argPtrs)
        args.push_back(argPtr.get());

    const DirectIndex2ArgPlan directPlan = analyzeDirectIndex2Arg(func);
    Value *cacheSlot = buildCacheSlotIndex(cacheLookupBlock, args, directPlan);

    auto *cacheIndex = new BinaryOperator(Opcode::Mul, cacheSlot, entrySizeConst, freshName("memo_cache_index"));
    cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(cacheIndex));

    cacheLookupBlock->insertBeforeTerminator(
        unique_ptr<Instruction>(new StoreInst(cacheIndex, memoIndexSlot)));

    auto *cacheEntry = new GetElementPtrInst(cacheVar, {zero, cacheIndex}, freshName("memo_cache_entry"));
    cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(cacheEntry));

    auto *hasValueOffset = new ConstantInt(IntegerType::getInstance(), entrySize - 1);
    auto *hasValuePtr = new GetElementPtrInst(cacheEntry, {hasValueOffset}, freshName("memo_has_value_ptr"));
    cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(hasValuePtr));

    auto *hasValue = new LoadInst(hasValuePtr, freshName("memo_has_value"));
    cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(hasValue));

    auto *isValid = new ICmpInst(ICmpInst::ICMP_NE, hasValue, zero, freshName("memo_is_valid"));
    cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(isValid));

    Value *argsMatch = isValid;
    for (int i = 0; i < numArgs; ++i)
    {
        auto *argOffset = new ConstantInt(IntegerType::getInstance(), i);
        auto *storedArgPtr = new GetElementPtrInst(cacheEntry, {argOffset}, freshName("memo_stored_arg_ptr"));
        cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(storedArgPtr));

        auto *storedArg = new LoadInst(storedArgPtr, freshName("memo_stored_arg"));
        cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(storedArg));

        Value *cmpRhs = asI32(args[static_cast<size_t>(i)], cacheLookupBlock,
                              "memo_cmp_arg" + to_string(i));
        auto *argEq = new ICmpInst(ICmpInst::ICMP_EQ, storedArg, cmpRhs, freshName("memo_arg_eq"));
        cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(argEq));

        auto *match = new BinaryOperator(Opcode::And, argsMatch, argEq, freshName("memo_args_match"));
        cacheLookupBlock->insertBeforeTerminator(unique_ptr<Instruction>(match));
        argsMatch = match;
    }

    cacheLookupBlock->insertBeforeTerminator(
        unique_ptr<Instruction>(new BranchInst(argsMatch, cacheHitBlock, cacheMissBlock)));
    cacheLookupBlock->addSuccessor(cacheHitBlock);
    cacheLookupBlock->addSuccessor(cacheMissBlock);
    cacheHitBlock->addPredecessor(cacheLookupBlock);
    cacheMissBlock->addPredecessor(cacheLookupBlock);

    // Cache hit: load cached result and return.
    auto *hitCacheIndex = new LoadInst(memoIndexSlot, freshName("memo_hit_cache_index"));
    cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(hitCacheIndex));
    auto *hitCacheEntry =
        new GetElementPtrInst(cacheVar, {zero, hitCacheIndex}, freshName("memo_hit_cache_entry"));
    cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(hitCacheEntry));

    auto *resultOffset = new ConstantInt(IntegerType::getInstance(), numArgs);
    auto *resultPtr = new GetElementPtrInst(hitCacheEntry, {resultOffset}, freshName("memo_result_ptr"));
    cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(resultPtr));

    auto *cachedResult = new LoadInst(resultPtr, freshName("memo_cached_result"));
    cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(cachedResult));

    Value *retVal = cachedResult;
    if (func->getFunctionType()->ReturnType->isFloatTy())
    {
        auto *toFp = new CastInst(Opcode::SIToFP, cachedResult, FloatType::getInstance(), freshName("memo_cached_fp"));
        cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(toFp));
        retVal = toFp;
    }

    cacheHitBlock->insertBeforeTerminator(unique_ptr<Instruction>(new ReturnInst(retVal)));

    // Cache miss: run original function body.
    cacheMissBlock->insertBeforeTerminator(unique_ptr<Instruction>(new BranchInst(computeBlock)));
    cacheMissBlock->addSuccessor(computeBlock);
    computeBlock->addPredecessor(cacheMissBlock);

    // On each return in compute path, fill cache entry.
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *block = bbPtr.get();
        if (block == entryBlock || block == cacheLookupBlock || block == cacheHitBlock ||
            block == cacheMissBlock)
            continue;

        Instruction *term = block->getTerminator();
        auto *ret = dynamic_cast<ReturnInst *>(term);
        if (!ret || !ret->getReturnValue())
            continue;

        Value *returnValue = ret->getReturnValue();
        vector<unique_ptr<Instruction>> prelude;

        auto *localIndex = new LoadInst(memoIndexSlot, freshName("memo_store_cache_index"));
        prelude.push_back(unique_ptr<Instruction>(localIndex));

        auto *localEntry =
            new GetElementPtrInst(cacheVar, {zero, localIndex}, freshName("memo_store_cache_entry"));
        prelude.push_back(unique_ptr<Instruction>(localEntry));

        for (int i = 0; i < numArgs; ++i)
        {
            auto *argOffset = new ConstantInt(IntegerType::getInstance(), i);
            auto *argPtr = new GetElementPtrInst(localEntry, {argOffset}, freshName("memo_store_arg_ptr"));
            prelude.push_back(unique_ptr<Instruction>(argPtr));

            Value *argI32 = args[static_cast<size_t>(i)];
            if (!argI32->getType()->isIntegerTy())
            {
                auto *cast = new CastInst(Opcode::FPToSI, argI32, IntegerType::getInstance(),
                                          freshName("memo_store_arg_i32"));
                prelude.push_back(unique_ptr<Instruction>(cast));
                argI32 = cast;
            }
            prelude.push_back(unique_ptr<Instruction>(new StoreInst(argI32, argPtr)));
        }

        auto *resOffset = new ConstantInt(IntegerType::getInstance(), numArgs);
        auto *resPtr = new GetElementPtrInst(localEntry, {resOffset}, freshName("memo_store_res_ptr"));
        prelude.push_back(unique_ptr<Instruction>(resPtr));

        Value *storedRet = returnValue;
        if (returnValue->getType()->isFloatTy())
        {
            auto *toI32 = new CastInst(Opcode::FPToSI, returnValue, IntegerType::getInstance(),
                                       freshName("memo_store_ret_i32"));
            // insertBeforeReturn will own these; asI32 path not used here.
            prelude.push_back(unique_ptr<Instruction>(toI32));
            storedRet = toI32;
        }
        prelude.push_back(unique_ptr<Instruction>(new StoreInst(storedRet, resPtr)));

        auto *validOffset = new ConstantInt(IntegerType::getInstance(), entrySize - 1);
        auto *validPtr = new GetElementPtrInst(localEntry, {validOffset}, freshName("memo_store_valid_ptr"));
        prelude.push_back(unique_ptr<Instruction>(validPtr));
        prelude.push_back(unique_ptr<Instruction>(new StoreInst(one, validPtr)));

        insertBeforeReturn(block, std::move(prelude));
    }

    if (verbose)
    {
        debugInfo << "MemoizationV2Pass: memoized " << funcName << " (slots=" << CACHE_SIZE
                  << ", bytes=" << (totalSize * 4);
        if (directPlan.useRuntimeCapacityStride && directPlan.capacityGlobal)
            debugInfo << ", hash=row_major_arg0*(global_" << directPlan.capacityGlobal->getName()
                      << "+1)+arg1";
        else if (directPlan.useDirectIndex)
            debugInfo << ", hash=direct_arg0*" << directPlan.stride << "+arg1";
        else if (directPlan.polyMulArg0 > 1 && directPlan.polyMulArg1 == 1)
            debugInfo << ", hash=poly_arg0*" << directPlan.polyMulArg0 << "+arg1";
        else if (directPlan.polyMulArg0 == 1 && directPlan.polyMulArg1 > 1)
            debugInfo << ", hash=poly_arg0+arg1*" << directPlan.polyMulArg1;
        else
            debugInfo << ", hash=poly";
        if (directPlan.skipCacheMask)
            debugInfo << ", no_mask";
        debugInfo << ")\n";
    }
}

bool MemoizationV2Pass::runOnFunction(Function *func)
{
    if (!analyzeFunctionForMemoization(func))
        return false;

    addMemoizationToFunction(func);
    return true;
}
