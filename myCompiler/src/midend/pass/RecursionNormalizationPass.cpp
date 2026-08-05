#include "RecursionNormalizationPass.h"
#include <unordered_map>
#include <unordered_set>

using namespace std;
using namespace optimization;

namespace
{
    static unique_ptr<Instruction> own(Instruction *inst) { return unique_ptr<Instruction>(inst); }

    static Value *stripCopy(Value *v)
    {
        while (auto *c = dynamic_cast<CopyInst *>(v))
            v = c->getSource();
        return v;
    }

    static void wireEdge(BasicBlock *from, BasicBlock *to)
    {
        from->addSuccessor(to);
        to->addPredecessor(from);
    }

    static bool isAddOfArgConst(Value *v, Value *arg, int &delta)
    {
        auto *add = dynamic_cast<BinaryOperator *>(stripCopy(v));
        if (!add || add->getOpcode() != Opcode::Add)
            return false;
        Value *l = stripCopy(add->getLHS());
        Value *r = stripCopy(add->getRHS());
        if (l == arg)
        {
            if (auto *c = dynamic_cast<ConstantInt *>(r))
            {
                delta = c->Value;
                return delta > 0;
            }
        }
        if (r == arg)
        {
            if (auto *c = dynamic_cast<ConstantInt *>(l))
            {
                delta = c->Value;
                return delta > 0;
            }
        }
        return false;
    }

    struct Pattern
    {
        Value *nArg = nullptr;
        Value *dArg = nullptr;
        int failConst = 0;
        bool hasFailConst = false;
        int recCalls = 0;
    };

    static bool matchPattern(Function *func, Pattern &pat)
    {
        if (!func || func->isLibraryFunction() || func->getName() == "main")
            return false;
        const auto &args = func->getArguments();
        if (args.size() != 2)
            return false;
        if (!args[0]->getType()->isIntegerTy() || !args[1]->getType()->isIntegerTy())
            return false;
        auto *retTy = func->getFunctionType();
        if (!retTy || !retTy->ReturnType->isIntegerTy())
            return false;

        pat.nArg = args[0].get();
        pat.dArg = args[1].get();
        pat.hasFailConst = false;
        pat.recCalls = 0;

        bool sawReturnDepth = false;
        for (auto &bbPtr : func->getBasicBlocks())
        {
            BasicBlock *bb = bbPtr.get();
            for (auto &instPtr : bb->getInstructions())
            {
                Instruction *inst = instPtr.get();
                if (auto *call = dynamic_cast<CallInst *>(inst))
                {
                    if (call->getCalledFunction() != func)
                        continue;
                    auto carg = call->getArguments();
                    if (carg.size() != 2)
                        return false;
                    int delta = 0;
                    if (!isAddOfArgConst(carg[1], pat.dArg, delta))
                        return false;
                    auto *ret = dynamic_cast<ReturnInst *>(bb->getTerminator());
                    if (!ret || ret->getReturnValue() != call)
                        return false;
                    ++pat.recCalls;
                    continue;
                }
                if (auto *ret = dynamic_cast<ReturnInst *>(inst))
                {
                    Value *rv = stripCopy(ret->getReturnValue());
                    if (!rv)
                        return false;
                    if (rv == pat.dArg)
                    {
                        sawReturnDepth = true;
                        continue;
                    }
                    if (auto *c = dynamic_cast<ConstantInt *>(rv))
                    {
                        if (!pat.hasFailConst)
                        {
                            pat.failConst = c->Value;
                            pat.hasFailConst = true;
                        }
                        else if (pat.failConst != c->Value)
                            return false;
                        continue;
                    }
                    if (auto *call = dynamic_cast<CallInst *>(rv))
                    {
                        if (call->getCalledFunction() == func)
                            continue; // handled with call above
                    }
                    return false;
                }
            }
        }
        if (!sawReturnDepth || pat.recCalls < 2)
            return false;

        // Depth arg may only appear in returns / depth+const for recursive calls.
        for (User *user : pat.dArg->getUsers())
        {
            auto *ui = dynamic_cast<Instruction *>(user);
            if (!ui)
                return false;
            if (dynamic_cast<ReturnInst *>(ui))
                continue;
            if (auto *add = dynamic_cast<BinaryOperator *>(ui))
            {
                if (add->getOpcode() == Opcode::Add)
                    continue; // used as dep+c
            }
            if (auto *call = dynamic_cast<CallInst *>(ui))
            {
                if (call->getCalledFunction() == func)
                    continue;
            }
            if (dynamic_cast<CopyInst *>(ui))
                continue;
            return false;
        }
        return true;
    }

    static Value *remapOperand(Value *v, const unordered_map<Value *, Value *> &vMap)
    {
        if (!v)
            return nullptr;
        auto it = vMap.find(v);
        return it == vMap.end() ? v : it->second;
    }
} // namespace

bool RecursionNormalizationPass::runOnFunction(Function *func)
{
    Pattern pat;
    if (!matchPattern(func, pat))
        return false;
    Module *module = func->getParent();
    if (!module)
        return false;

    // Avoid transforming the same function twice.
    if (func->getName().find("__rn") != string::npos)
        return false;
    if (module->getFunction(func->getName() + "__rn"))
        return false;

    auto *i32 = IntegerType::getInstance();
    auto *stepsTy = new FunctionType(i32, vector<Type *>{i32});
    Function *steps = module->addFunction(stepsTy, func->getName() + "__rn");
    Argument *stepsN = steps->addArgument(i32, "n");

    unordered_map<BasicBlock *, BasicBlock *> bbMap;
    unordered_map<Value *, Value *> vMap;
    vMap[pat.nArg] = stepsN;

    for (auto &bbPtr : func->getBasicBlocks())
        bbMap[bbPtr.get()] = steps->addBasicBlock(bbPtr->getName());

    auto *zero = new ConstantInt(i32, 0);
    auto *negOne = new ConstantInt(i32, -1);

    // First pass: clone non-terminator / non-special; handle returns&calls in second structure.
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *oldBB = bbPtr.get();
        BasicBlock *newBB = bbMap[oldBB];
        for (auto &instPtr : oldBB->getInstructions())
        {
            Instruction *oi = instPtr.get();
            if (dynamic_cast<ReturnInst *>(oi) || dynamic_cast<BranchInst *>(oi))
                continue;
            if (auto *call = dynamic_cast<CallInst *>(oi))
            {
                if (call->getCalledFunction() == func)
                    continue; // rebuilt below with non-tail form
            }

            Instruction *ni = oi->clone();
            ni->setName(oi->getName() + "_st");
            for (unsigned op = 0; op < ni->getNumOperands(); ++op)
            {
                Value *oldOp = oi->getOperandByIndex(op);
                // Drop uses of depth arg: should not appear in cloned non-call/ret ops.
                if (oldOp == pat.dArg)
                    ni->setOperandByIndex(op, zero);
                else
                    ni->setOperandByIndex(op, remapOperand(oldOp, vMap));
            }
            vMap[oi] = ni;
            newBB->addInstruction(own(ni));
        }
    }

    // Terminators / recursive returns.
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *oldBB = bbPtr.get();
        BasicBlock *newBB = bbMap[oldBB];
        auto *term = oldBB->getTerminator();
        if (auto *br = dynamic_cast<BranchInst *>(term))
        {
            if (br->isConditional())
            {
                Value *cond = remapOperand(br->getCondition(), vMap);
                BasicBlock *t = bbMap[br->getTrueBlock()];
                BasicBlock *f = bbMap[br->getFalseBlock()];
                newBB->addInstruction(own(new BranchInst(cond, t, f)));
                wireEdge(newBB, t);
                wireEdge(newBB, f);
            }
            else
            {
                BasicBlock *t = bbMap[br->getTrueBlock()];
                newBB->addInstruction(own(new BranchInst(t)));
                wireEdge(newBB, t);
            }
            continue;
        }

        auto *ret = dynamic_cast<ReturnInst *>(term);
        if (!ret)
            continue;
        Value *rv = stripCopy(ret->getReturnValue());

        // Tail recursive call: return steps(n') + delta, propagating failure.
        if (auto *call = dynamic_cast<CallInst *>(rv))
        {
            if (call->getCalledFunction() == func)
            {
                auto carg = call->getArguments();
                int delta = 0;
                isAddOfArgConst(carg[1], pat.dArg, delta);
                Value *nextN = remapOperand(stripCopy(carg[0]), vMap);
                auto *rec = new CallInst(steps, vector<Value *>{nextN}, oldBB->getName() + "_rec");
                newBB->addInstruction(own(rec));
                auto *isFail = new ICmpInst(ICmpInst::ICMP_SLT, rec, zero, oldBB->getName() + "_fail");
                newBB->addInstruction(own(isFail));
                BasicBlock *failBB = steps->addBasicBlock(oldBB->getName() + "_retfail");
                BasicBlock *okBB = steps->addBasicBlock(oldBB->getName() + "_retok");
                newBB->addInstruction(own(new BranchInst(isFail, failBB, okBB)));
                wireEdge(newBB, failBB);
                wireEdge(newBB, okBB);
                failBB->addInstruction(own(new ReturnInst(rec)));
                auto *dlt = new ConstantInt(i32, delta);
                auto *sum = new BinaryOperator(Opcode::Add, rec, dlt, oldBB->getName() + "_rn");
                okBB->addInstruction(own(sum));
                okBB->addInstruction(own(new ReturnInst(sum)));
                continue;
            }
        }

        if (rv == pat.dArg)
        {
            newBB->addInstruction(own(new ReturnInst(zero)));
            continue;
        }
        if (dynamic_cast<ConstantInt *>(rv))
        {
            newBB->addInstruction(own(new ReturnInst(negOne)));
            continue;
        }
        // Fallback
        newBB->addInstruction(own(new ReturnInst(remapOperand(rv, vMap))));
    }

    // Replace original function body with wrapper: s=steps(n); s<0?failConst: d+s
    {
        auto &blocks = func->getBasicBlocks();
        for (auto &bbPtr : blocks)
        {
            auto &insts = bbPtr->getInstructions();
            while (!insts.empty())
            {
                insts.back()->removeThisFromOperands();
                insts.pop_back();
            }
            bbPtr->removeSelfBasicBlock();
        }
        blocks.clear();

        BasicBlock *entry = func->addBasicBlock("dpts_entry");
        BasicBlock *failBB = func->addBasicBlock("dpts_fail");
        BasicBlock *okBB = func->addBasicBlock("dpts_ok");
        auto *call = new CallInst(steps, vector<Value *>{pat.nArg}, "steps_call");
        entry->addInstruction(own(call));
        auto *cmp = new ICmpInst(ICmpInst::ICMP_SLT, call, zero, "steps_fail");
        entry->addInstruction(own(cmp));
        entry->addInstruction(own(new BranchInst(cmp, failBB, okBB)));
        wireEdge(entry, failBB);
        wireEdge(entry, okBB);
        int fc = pat.hasFailConst ? pat.failConst : 0;
        failBB->addInstruction(own(new ReturnInst(new ConstantInt(i32, fc))));
        auto *sum = new BinaryOperator(Opcode::Add, call, pat.dArg, "steps_plus_d");
        okBB->addInstruction(own(sum));
        okBB->addInstruction(own(new ReturnInst(sum)));
    }

    if (verbose)
        debugInfo << "RecursionNormalization: rewrote " << func->getName() << " → "
                  << steps->getName() << "\n";
    return true;
}
