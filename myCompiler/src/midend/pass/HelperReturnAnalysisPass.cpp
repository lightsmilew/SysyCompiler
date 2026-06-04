#include "HelperReturnAnalysisPass.h"
#include "../irbuild/IRDataStructure.h"
#include <optional>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>
using namespace std;
using namespace optimization;

namespace
{
    static Value *stripCopy(Value *v)
    {
        while (auto *cpy = dynamic_cast<CopyInst *>(v))
            v = cpy->getSource();
        return v;
    }

    static ReturnInst *findReturn(BasicBlock *bb)
    {
        for (auto &instPtr : bb->getInstructions())
        {
            if (auto *ret = dynamic_cast<ReturnInst *>(instPtr.get()))
                return ret;
        }
        return nullptr;
    }

    static bool isSingleBlockIntHelper(Function *func)
    {
        if (!func || func->isLibraryFunction() || func->getBasicBlocks().size() != 1)
            return false;
        if (!func->getFunctionType() || !func->getFunctionType()->ReturnType->isIntegerTy())
            return false;

        BasicBlock *bb = func->getEntryBlock();
        ReturnInst *ret = findReturn(bb);
        if (!ret || !ret->getReturnValue())
            return false;

        for (auto &instPtr : bb->getInstructions())
        {
            Instruction *inst = instPtr.get();
            if (inst->getOpcode() == Opcode::Ret)
                continue;
            if (inst->isBinaryOp() || inst->getOpcode() == Opcode::Call)
                continue;
            return false;
        }
        return true;
    }

    static bool isHelperCandidate(Function *func)
    {
        if (!isSingleBlockIntHelper(func))
            return false;
        if (func->getName() == "main")
            return false;
        return true;
    }

    struct AffineExpr
    {
        unordered_map<Value *, int> terms;
        int constant = 0;

        static AffineExpr constantExpr(int c)
        {
            AffineExpr e;
            e.constant = c;
            return e;
        }

        static AffineExpr term(Value *v, int coeff)
        {
            AffineExpr e;
            if (coeff != 0)
                e.terms[stripCopy(v)] = coeff;
            return e;
        }

        void normalize()
        {
            for (auto it = terms.begin(); it != terms.end();)
            {
                if (it->second == 0)
                    it = terms.erase(it);
                else
                    ++it;
            }
        }

        AffineExpr operator+(const AffineExpr &o) const
        {
            AffineExpr r = *this;
            r.constant += o.constant;
            for (const auto &kv : o.terms)
                r.terms[kv.first] += kv.second;
            r.normalize();
            return r;
        }

        AffineExpr operator-(const AffineExpr &o) const
        {
            AffineExpr r = *this;
            r.constant -= o.constant;
            for (const auto &kv : o.terms)
                r.terms[kv.first] -= kv.second;
            r.normalize();
            return r;
        }

        bool operator==(const AffineExpr &o) const
        {
            return constant == o.constant && terms == o.terms;
        }

        bool isZero() const { return constant == 0 && terms.empty(); }
    };

    struct FuncSummary
    {
        bool analyzed = false;
        Value *retValue = nullptr;
    };

    static void clearBlockInstructions(BasicBlock *bb, vector<Value *> &needToDelete)
    {
        auto &insts = bb->getInstructions();
        for (auto &instPtr : insts)
        {
            instPtr->removeThisFromOperands();
            needToDelete.push_back(instPtr.release());
        }
        insts.clear();
    }

    static optional<AffineExpr> analyzeValue(Value *v, BasicBlock *bb,
                                             const unordered_set<Instruction *> &inBlock,
                                             unordered_set<Value *> &visiting)
    {
        v = stripCopy(v);
        if (auto *c = dynamic_cast<ConstantInt *>(v))
            return AffineExpr::constantExpr(c->Value);
        if (dynamic_cast<Argument *>(v))
            return AffineExpr::term(v, 1);

        auto *inst = dynamic_cast<Instruction *>(v);
        if (!inst || !inBlock.count(inst))
            return nullopt;

        if (visiting.count(v))
            return nullopt;
        visiting.insert(v);

        optional<AffineExpr> result;
        if (auto *bin = dynamic_cast<BinaryOperator *>(inst))
        {
            optional<AffineExpr> lhs =
                analyzeValue(bin->getLHS(), bb, inBlock, visiting);
            optional<AffineExpr> rhs =
                analyzeValue(bin->getRHS(), bb, inBlock, visiting);
            visiting.erase(v);
            if (!lhs || !rhs)
                return nullopt;
            if (bin->getOpcode() == Opcode::Add)
                result = *lhs + *rhs;
            else if (bin->getOpcode() == Opcode::Sub)
                result = *lhs - *rhs;
            else
                return nullopt;
            return result;
        }

        visiting.erase(v);
        return nullopt;
    }

    static optional<AffineExpr> analyzeReturnExpr(Function *func)
    {
        BasicBlock *bb = func->getEntryBlock();
        ReturnInst *ret = findReturn(bb);
        if (!ret)
            return nullopt;
        for (auto &instPtr : bb->getInstructions())
        {
            if (dynamic_cast<CallInst *>(instPtr.get()))
                return nullopt;
        }
        unordered_set<Instruction *> inBlock;
        for (auto &instPtr : bb->getInstructions())
            inBlock.insert(instPtr.get());
        unordered_set<Value *> visiting;
        return analyzeValue(ret->getReturnValue(), bb, inBlock, visiting);
    }

    static vector<Value *> collectTerms(const AffineExpr &e, int coeff,
                                        const vector<Value *> &params)
    {
        vector<Value *> out;
        for (Value *p : params)
        {
            auto it = e.terms.find(stripCopy(p));
            if (it != e.terms.end() && it->second == coeff)
                out.push_back(p);
        }
        return out;
    }

    static Value *materializeAddChain(vector<Value *> vals, const string &namePrefix)
    {
        if (vals.empty())
            return nullptr;
        Value *cur = vals[0];
        for (size_t i = 1; i < vals.size(); ++i)
            cur = new BinaryOperator(Opcode::Add, cur, vals[i], namePrefix + to_string(i));
        return cur;
    }

    static Value *materializeAffine(Function *func, const AffineExpr &e)
    {
        const string tag = func->getName() + "_ret";
        auto *i32 = IntegerType::getInstance();

        if (e.isZero())
            return new ConstantInt(i32, 0);

        vector<Value *> params;
        for (const auto &argPtr : func->getArguments())
            params.push_back(argPtr.get());

        vector<Value *> pos = collectTerms(e, 1, params);
        vector<Value *> neg = collectTerms(e, -1, params);

        if (e.constant == 0 && neg.size() >= 2 && pos.empty())
        {
            bool allNegOne = true;
            for (const auto &kv : e.terms)
            {
                if (kv.second != -1)
                {
                    allNegOne = false;
                    break;
                }
            }
            if (allNegOne && (int)neg.size() == (int)e.terms.size())
            {
                Value *sum = materializeAddChain(neg, tag + "_sum");
                return new BinaryOperator(Opcode::Sub, new ConstantInt(i32, 0), sum,
                                          tag + "_sub0");
            }
        }

        if (e.constant == 0 && pos.size() >= 2 && neg.empty())
        {
            bool allOne = true;
            for (const auto &kv : e.terms)
            {
                if (kv.second != 1)
                {
                    allOne = false;
                    break;
                }
            }
            if (allOne && (int)pos.size() == (int)e.terms.size())
                return materializeAddChain(pos, tag + "_add");
        }

        if (e.constant == -1 && e.terms.size() == 1 && neg.size() == 1 && pos.empty())
            return new BinaryOperator(Opcode::Sub, new ConstantInt(i32, -1), neg[0], tag + "_not");

        // 一般线性组合：const + Σ coeff*arg
        Value *cur = new ConstantInt(i32, e.constant);
        vector<pair<Value *, int>> items(e.terms.begin(), e.terms.end());
        for (size_t i = 0; i < items.size(); ++i)
        {
            Value *arg = items[i].first;
            int k = items[i].second;
            Value *termVal = arg;
            if (k > 1)
            {
                for (int t = 1; t < k; ++t)
                    termVal = new BinaryOperator(Opcode::Add, termVal, arg, tag + "_dup" + to_string(i));
            }
            else if (k < -1)
            {
                Value *subZero = new BinaryOperator(Opcode::Sub, new ConstantInt(i32, 0), arg,
                                                    tag + "_neg" + to_string(i));
                for (int t = 1; t < -k; ++t)
                    subZero = new BinaryOperator(Opcode::Add, subZero,
                                                 new BinaryOperator(Opcode::Sub, new ConstantInt(i32, 0),
                                                                    arg, tag + "_neg" + to_string(i)),
                                                 tag + "_negdup" + to_string(i));
                termVal = subZero;
            }
            else if (k == -1)
                termVal = new BinaryOperator(Opcode::Sub, new ConstantInt(i32, 0), arg,
                                             tag + "_neg" + to_string(i));

            cur = new BinaryOperator(Opcode::Add, cur, termVal, tag + "_acc" + to_string(i));
        }
        return cur;
    }

    static void rewriteBody(Function *func, Value *retVal, vector<Value *> &needToDelete)
    {
        BasicBlock *bb = func->getEntryBlock();
        clearBlockInstructions(bb, needToDelete);

        vector<Instruction *> pending;
        function<void(Value *)> collect = [&](Value *v) {
            v = stripCopy(v);
            if (dynamic_cast<ConstantInt *>(v) || dynamic_cast<Argument *>(v))
                return;
            auto *inst = dynamic_cast<Instruction *>(v);
            if (!inst)
                return;
            for (Value *op : inst->getOperands())
                collect(op);
            if (find(pending.begin(), pending.end(), inst) == pending.end())
                pending.push_back(inst);
        };
        collect(retVal);

        for (Instruction *inst : pending)
            bb->addInstruction(std::unique_ptr<Instruction>(inst));
        bb->addInstruction(std::make_unique<ReturnInst>(retVal));
    }

    static Value *cloneExprWithMap(Value *v, unordered_map<Value *, Value *> &vmap,
                                   vector<unique_ptr<Instruction>> &built,
                                   unordered_map<Value *, Value *> &memo)
    {
        v = stripCopy(v);
        if (memo.count(v))
            return memo[v];
        if (vmap.count(v))
            return vmap[v];
        if (dynamic_cast<ConstantInt *>(v) || dynamic_cast<Argument *>(v))
            return v;

        if (auto *bin = dynamic_cast<BinaryOperator *>(v))
        {
            Value *lhs = cloneExprWithMap(bin->getLHS(), vmap, built, memo);
            Value *rhs = cloneExprWithMap(bin->getRHS(), vmap, built, memo);
            auto *cloned =
                new BinaryOperator(bin->getOpcode(), lhs, rhs, bin->getName() + "_cloned");
            built.emplace_back(cloned);
            memo[v] = cloned;
            return cloned;
        }
        return v;
    }

    static bool inlineSummarizedCalls(Function *func,
                                      const unordered_map<Function *, FuncSummary> &summaries,
                                      vector<Value *> &needToDelete, bool verbose,
                                      std::stringstream &debugInfo)
    {
        BasicBlock *bb = func->getEntryBlock();
        auto &insts = bb->getInstructions();
        bool changed = false;

        for (size_t i = 0; i < insts.size();)
        {
            auto *call = dynamic_cast<CallInst *>(insts[i].get());
            if (!call || !call->getCalledFunction())
            {
                ++i;
                continue;
            }
            Function *callee = call->getCalledFunction();
            auto it = summaries.find(callee);
            if (it == summaries.end() || !it->second.analyzed || !it->second.retValue)
            {
                ++i;
                continue;
            }

            unordered_map<Value *, Value *> vmap;
            const auto &cparams = callee->getArguments();
            const auto &args = call->getArguments();
            for (size_t j = 0; j < cparams.size() && j < args.size(); ++j)
                vmap[cparams[j].get()] = args[j];

            vector<unique_ptr<Instruction>> built;
            unordered_map<Value *, Value *> memo;
            Value *inlined = cloneExprWithMap(it->second.retValue, vmap, built, memo);

            call->replaceAllUsesWith(inlined);
            call->removeThisFromOperands();
            needToDelete.push_back(insts[i].release());
            insts.erase(insts.begin() + static_cast<long>(i));
            insts.insert(insts.begin() + static_cast<long>(i),
                           std::make_move_iterator(built.begin()),
                           std::make_move_iterator(built.end()));
            changed = true;
            if (verbose)
            {
                debugInfo << "HelperReturnAnalysis: inlined " << callee->getName() << " into "
                          << func->getName() << "\n";
            }
        }
        return changed;
    }

    static bool analyzeAndRewriteHelper(Function *func, FuncSummary &summary,
                                        vector<Value *> &needToDelete, bool verbose,
                                        std::stringstream &debugInfo)
    {
        if (!isHelperCandidate(func))
            return false;

        optional<AffineExpr> aff = analyzeReturnExpr(func);
        if (!aff)
            return false;

        Value *materialized = materializeAffine(func, *aff);
        if (!materialized)
            return false;

        rewriteBody(func, materialized, needToDelete);
        summary = {true, findReturn(func->getEntryBlock())->getReturnValue()};

        if (verbose)
        {
            debugInfo << "HelperReturnAnalysis: " << func->getName();
            if (aff->isZero())
                debugInfo << " -> ret 0\n";
            else if (aff->constant == -1 && aff->terms.size() == 1)
                debugInfo << " -> sub(-1, arg)\n";
            else if (aff->constant == 0 && !aff->terms.empty())
            {
                bool allNeg = true, allPos = true;
                for (const auto &kv : aff->terms)
                {
                    if (kv.second != -1)
                        allNeg = false;
                    if (kv.second != 1)
                        allPos = false;
                }
                if (allNeg)
                    debugInfo << " -> sub(0, add(...))\n";
                else if (allPos)
                    debugInfo << " -> add(...)\n";
                else
                    debugInfo << " -> linear form\n";
            }
            else
                debugInfo << " -> linear form\n";
        }
        return true;
    }

    static vector<Function *> topoOrderHelpers(Module *module)
    {
        vector<Function *> helpers;
        unordered_set<Function *> helperSet;
        for (auto &fPtr : module->Functions)
        {
            Function *f = fPtr.get();
            if (isHelperCandidate(f))
            {
                helpers.push_back(f);
                helperSet.insert(f);
            }
        }

        vector<Function *> order;
        unordered_set<Function *> done;
        bool progress = true;
        while (order.size() < helpers.size() && progress)
        {
            progress = false;
            for (Function *f : helpers)
            {
                if (done.count(f))
                    continue;
                bool ready = true;
                for (auto &instPtr : f->getEntryBlock()->getInstructions())
                {
                    auto *call = dynamic_cast<CallInst *>(instPtr.get());
                    if (!call || !call->getCalledFunction())
                        continue;
                    Function *callee = call->getCalledFunction();
                    if (helperSet.count(callee) && !done.count(callee))
                    {
                        ready = false;
                        break;
                    }
                }
                if (!ready)
                    continue;
                order.push_back(f);
                done.insert(f);
                progress = true;
            }
        }
        for (Function *f : helpers)
        {
            if (!done.count(f))
                order.push_back(f);
        }
        return order;
    }
} // namespace

bool HelperReturnAnalysisPass::runOnModule(Module *module)
{
    if (!module)
        return false;

    bool changed = false;
    unordered_map<Function *, FuncSummary> summaries;
    vector<Function *> order = topoOrderHelpers(module);

    for (Function *func : order)
    {
        inlineSummarizedCalls(func, summaries, needToDelete, verbose, debugInfo);

        FuncSummary summary;
        if (analyzeAndRewriteHelper(func, summary, needToDelete, verbose, debugInfo))
        {
            summaries[func] = summary;
            changed = true;
            continue;
        }

        if (!summaries.count(func) || !summaries[func].analyzed)
        {
            inlineSummarizedCalls(func, summaries, needToDelete, verbose, debugInfo);
            if (analyzeAndRewriteHelper(func, summary, needToDelete, verbose, debugInfo))
            {
                summaries[func] = summary;
                changed = true;
            }
        }
    }
    return changed;
}
