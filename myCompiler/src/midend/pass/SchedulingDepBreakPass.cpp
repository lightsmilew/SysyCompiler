#include "SchedulingDepBreakPass.h"
#include <vector>

using namespace std;
using namespace optimization;

namespace
{
constexpr const char *kSchedTag = "sched_ilp";

Value *stripCopy(Value *v)
{
    while (auto *cpy = dynamic_cast<CopyInst *>(v))
        v = cpy->getSource();
    return v;
}

bool sameValue(Value *a, Value *b) { return stripCopy(a) == stripCopy(b); }

unique_ptr<Instruction> own(Instruction *inst) { return unique_ptr<Instruction>(inst); }

void markNoCSE(Instruction *inst)
{
    if (inst)
        inst->NoCSE = true;
}

BasicBlock *getLoopBody(const Loop &loop)
{
    auto *header = loop.header;
    if (!header)
        return nullptr;
    auto *br = dynamic_cast<BranchInst *>(header->getTerminator());
    if (!br || !br->isConditional())
        return nullptr;
    auto *body = br->getTrueBlock();
    if (!body || !loop.containsBlock(body))
        return nullptr;
    return body;
}

struct AddRec
{
    BinaryOperator *inst = nullptr;
    Value *lhs = nullptr;
    Value *rhs = nullptr;
};

bool isInductionStepAdd(const AddRec &rec)
{
    if (dynamic_cast<ConstantInt *>(rec.rhs) &&
        dynamic_cast<ConstantInt *>(rec.rhs)->Value == 1)
        return dynamic_cast<PhiInst *>(rec.lhs) != nullptr;
    if (dynamic_cast<ConstantInt *>(rec.lhs) &&
        dynamic_cast<ConstantInt *>(rec.lhs)->Value == 1)
        return dynamic_cast<PhiInst *>(rec.rhs) != nullptr;
    return false;
}

vector<AddRec> collectBodyAdds(BasicBlock *body)
{
    vector<AddRec> adds;
    for (auto &instPtr : body->getInstructions())
    {
        auto *add = dynamic_cast<BinaryOperator *>(instPtr.get());
        if (!add || add->getOpcode() != Opcode::Add)
            continue;
        AddRec rec{add, stripCopy(add->getLHS()), stripCopy(add->getRHS())};
        if (isInductionStepAdd(rec))
            continue;
        adds.push_back(rec);
    }
    return adds;
}

bool findDependentPattern(const vector<AddRec> &adds, AddRec &outA, AddRec &outD, Value *&dOld,
                          Value *&aOld, Value *&bOld)
{
    if (adds.size() != 4)
        return false;

    for (const auto &candD : adds)
    {
        BinaryOperator *inner = nullptr;
        Value *dVal = nullptr;
        if (dynamic_cast<BinaryOperator *>(candD.lhs))
        {
            inner = dynamic_cast<BinaryOperator *>(candD.lhs);
            dVal = candD.rhs;
        }
        else if (dynamic_cast<BinaryOperator *>(candD.rhs))
        {
            inner = dynamic_cast<BinaryOperator *>(candD.rhs);
            dVal = candD.lhs;
        }
        else
            continue;

        if (!inner || inner->getOpcode() != Opcode::Add)
            continue;

        const AddRec *addA = nullptr;
        for (const auto &rec : adds)
        {
            if (rec.inst == inner)
            {
                addA = &rec;
                break;
            }
        }
        if (!addA)
            continue;

        aOld = addA->lhs;
        bOld = addA->rhs;
        dOld = dVal;

        const AddRec *addB = nullptr;
        const AddRec *addC = nullptr;
        for (const auto &rec : adds)
        {
            if (rec.inst == candD.inst || rec.inst == addA->inst)
                continue;
            if (sameValue(rec.lhs, bOld))
                addB = &rec;
            else if (addB && sameValue(rec.lhs, addB->rhs) && sameValue(rec.rhs, dOld))
                addC = &rec;
        }
        if (!addB || !addC)
            continue;

        outA = *addA;
        outD = candD;
        (void)addC;
        return true;
    }
    return false;
}

void insertBeforeInst(BasicBlock *body, Instruction *before, Instruction *inst)
{
    unsigned idx = static_cast<unsigned>(body->getInstructionOrder(before));
    body->insert(own(inst), idx);
}

bool rewriteDependentAdd(BasicBlock *body, const AddRec &addD, Value *dOld, Value *aOld, Value *bOld)
{
    auto *da = new BinaryOperator(Opcode::Add, dOld, aOld, string(kSchedTag) + "_da");
    markNoCSE(da);
    insertBeforeInst(body, addD.inst, da);

    auto *dnew = new BinaryOperator(Opcode::Add, da, bOld, string(kSchedTag) + "_d");
    markNoCSE(dnew);
    insertBeforeInst(body, addD.inst, dnew);

    addD.inst->replaceAllUsesWith(dnew);
    addD.inst->removeThisFromOperands();
    auto &insts = body->getInstructions();
    for (auto it = insts.begin(); it != insts.end(); ++it)
    {
        if (it->get() == addD.inst)
        {
            it->release();
            insts.erase(it);
            break;
        }
    }
    return true;
}

} // namespace

bool SchedulingDepBreakPass::tryBreakLoop(Function * /*func*/, const Loop &loop)
{
    BasicBlock *body = getLoopBody(loop);
    if (!body)
        return false;

    auto adds = collectBodyAdds(body);
    AddRec addA, addD;
    Value *dOld = nullptr, *aOld = nullptr, *bOld = nullptr;
    if (!findDependentPattern(adds, addA, addD, dOld, aOld, bOld))
        return false;

    for (const auto &rec : adds)
        markNoCSE(rec.inst);

    if (!rewriteDependentAdd(body, addD, dOld, aOld, bOld))
        return false;

    if (verbose)
    {
        debugInfo << "SchedulingDepBreak: d'=d+a_old+b_old in " << body->getName()
                  << " (NoCSE marked)\n";
    }
    return true;
}

bool SchedulingDepBreakPass::runOnFunction(Function *func)
{
    bool changed = false;
    func->setLoops(ControlFlowAnalysis::findLoops(func));
    for (const auto &loop : func->getLoops())
    {
        if (tryBreakLoop(func, loop))
            changed = true;
    }
    return changed;
}
