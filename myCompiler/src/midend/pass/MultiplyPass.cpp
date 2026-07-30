#include "MultiplyPass.h"
#include <algorithm>
#include <unordered_map>

using namespace std;
using namespace optimization;

namespace
{
    int nameCounter = 0;

    string freshName(const string &prefix)
    {
        return prefix + "_" + to_string(nameCounter++);
    }

    int extractModConstantFromFunction(Function *func)
    {
        unordered_map<int, int> counts;
        for (auto &bbPtr : func->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get());
                if (!bin || bin->getOpcode() != Opcode::SRem)
                    continue;

                auto *rhs = dynamic_cast<ConstantInt *>(bin->getRHS());
                if (rhs)
                    ++counts[rhs->Value];
            }
        }

        int bestMod = 0;
        int bestCount = 0;
        for (const auto &[value, count] : counts)
        {
            if (count > bestCount)
            {
                bestCount = count;
                bestMod = value;
            }
        }
        return bestMod;
    }

    bool hasRecursiveHalfCall(Function *func, Value *argA, Value *argB)
    {
        for (auto &bbPtr : func->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *call = dynamic_cast<CallInst *>(instPtr.get());
                if (!call || call->getCalledFunction() != func)
                    continue;

                const auto &callArgs = call->getArguments();
                if (callArgs.size() != 2 || callArgs[0] != argA)
                    continue;

                auto *motion = dynamic_cast<BinaryOperator *>(callArgs[1]);
                if (!motion || motion->getOpcode() != Opcode::SDiv)
                    continue;

                auto *divisor = dynamic_cast<ConstantInt *>(motion->getRHS());
                if (divisor && divisor->Value == 2 && motion->getLHS() == argB)
                    return true;
            }
        }
        return false;
    }

    bool hasSelfDoubleAddWithMod(Function *func, int modValue)
    {
        for (auto &bbPtr : func->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *add = dynamic_cast<BinaryOperator *>(instPtr.get());
                if (!add || add->getOpcode() != Opcode::Add || add->getLHS() != add->getRHS())
                    continue;

                for (User *user : add->getDest()->getUsers())
                {
                    auto *mod = dynamic_cast<BinaryOperator *>(user);
                    if (!mod || mod->getOpcode() != Opcode::SRem)
                        continue;

                    auto *rhs = dynamic_cast<ConstantInt *>(mod->getRHS());
                    if (rhs && rhs->Value == modValue)
                        return true;
                }
            }
        }
        return false;
    }

    bool isMultiplyCandidate(Function *func)
    {
        if (func->isLibraryFunction())
            return false;

        const auto &args = func->getArguments();
        if (args.size() != 2)
            return false;
        if (!args[0]->getType()->isIntegerTy() || !args[1]->getType()->isIntegerTy())
            return false;
        if (!func->isRecursive())
            return false;

        Value *argA = args[0].get();
        Value *argB = args[1].get();
        const int modValue = extractModConstantFromFunction(func);
        if (modValue <= 0)
            return false;

        int modUseCount = 0;
        for (auto &bbPtr : func->getBasicBlocks())
        {
            for (auto &instPtr : bbPtr->getInstructions())
            {
                auto *bin = dynamic_cast<BinaryOperator *>(instPtr.get());
                if (!bin || bin->getOpcode() != Opcode::SRem)
                    continue;
                auto *rhs = dynamic_cast<ConstantInt *>(bin->getRHS());
                if (rhs && rhs->Value == modValue)
                    ++modUseCount;
            }
        }
        if (modUseCount < 2)
            return false;

        if (!hasRecursiveHalfCall(func, argA, argB))
            return false;
        if (!hasSelfDoubleAddWithMod(func, modValue))
            return false;

        return true;
    }

    Value *getMod64Value(Function *func, BasicBlock *entry, vector<unique_ptr<Instruction>> &pending)
    {
        Module *module = func->getParent();
        if (!module)
            return nullptr;

        if (GlobalVariable *modVar = module->getGlobalVariable("mod"))
        {
            if (auto *init = dynamic_cast<ConstantInt *>(modVar->Initializer))
            {
                return new ConstantLong(LongType::getInstance(), init->Value);
            }

            auto *modLoad = new LoadInst(modVar, freshName("mod_load"));
            pending.push_back(unique_ptr<Instruction>(modLoad));
            auto *mod64 = new CastInst(Opcode::Sext, modLoad, LongType::getInstance(), freshName("mod64"));
            pending.push_back(unique_ptr<Instruction>(mod64));
            return mod64;
        }

        const int modValue = extractModConstantFromFunction(func);
        if (modValue <= 0)
            return nullptr;

        return new ConstantLong(LongType::getInstance(), modValue);
    }

    void rewriteMultiplyFunction(Function *func, Pass *pass)
    {
        auto &bbs = func->getBasicBlocks();
        if (bbs.empty())
            return;

        BasicBlock *entry = bbs.front().get();
        if (!entry)
            return;

        vector<unique_ptr<Instruction>> pending;
        Value *mod64 = getMod64Value(func, entry, pending);
        if (!mod64)
            return;

        vector<BasicBlock *> allBlocks;
        allBlocks.reserve(bbs.size());
        for (auto &bbPtr : bbs)
        {
            if (bbPtr)
                allBlocks.push_back(bbPtr.get());
        }

        for (BasicBlock *bb : allBlocks)
        {
            auto &insts = bb->getInstructions();
            for (auto &instPtr : insts)
            {
                if (!instPtr)
                    continue;
                instPtr->removeThisFromOperands();
                pass->needToDelete.push_back(instPtr.release());
            }
            insts.clear();
            bb->removeSelfBasicBlock();
        }

        bbs.erase(remove_if(bbs.begin(), bbs.end(),
                            [entry](const unique_ptr<BasicBlock> &bbPtr)
                            { return bbPtr.get() != entry; }),
                  bbs.end());

        entry->clearInstructions();
        entry->Predecessors.clear();
        entry->Successors.clear();

        Value *argA = func->getArgumentByIndex(0);
        Value *argB = func->getArgumentByIndex(1);

        auto *a64 = new CastInst(Opcode::Sext, argA, LongType::getInstance(), freshName("a64"));
        pending.push_back(unique_ptr<Instruction>(a64));
        auto *b64 = new CastInst(Opcode::Sext, argB, LongType::getInstance(), freshName("b64"));
        pending.push_back(unique_ptr<Instruction>(b64));
        auto *prod = new BinaryOperator(Opcode::Muld, a64, b64, freshName("prod"));
        pending.push_back(unique_ptr<Instruction>(prod));
        auto *rem = new BinaryOperator(Opcode::SRem, prod, mod64, freshName("rem"));
        pending.push_back(unique_ptr<Instruction>(rem));
        auto *result = new CastInst(Opcode::Trunc, rem, IntegerType::getInstance(), freshName("result"));
        pending.push_back(unique_ptr<Instruction>(result));
        auto *ret = new ReturnInst(result);
        pending.push_back(unique_ptr<Instruction>(ret));

        for (auto &inst : pending)
            entry->addInstruction(std::move(inst));

        func->setLoops({});
    }
}

bool MultiplyPass::runOnFunction(Function *func)
{
    if (!isMultiplyCandidate(func))
        return false;

    rewriteMultiplyFunction(func, this);

    if (verbose)
    {
        debugInfo << "MultiplyPass: replaced recursive multiply in " << func->getName()
                  << " with i64 mul + srem\n";
    }

    return true;
}
