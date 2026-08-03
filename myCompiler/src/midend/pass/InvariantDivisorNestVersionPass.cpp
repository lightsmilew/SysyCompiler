#include "InvariantDivisorNestVersionPass.h"
#include "ControlFlowAnalysis.h"
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

using namespace std;
using namespace optimization;

namespace
{
    static unique_ptr<Instruction> own(Instruction *inst) { return unique_ptr<Instruction>(inst); }

    static void wireEdge(BasicBlock *from, BasicBlock *to)
    {
        from->addSuccessor(to);
        to->addPredecessor(from);
    }

    static Value *stripCopy(Value *v)
    {
        while (auto *c = dynamic_cast<CopyInst *>(v))
            v = c->getSource();
        return v;
    }

    static bool isLoopInvariant(Value *v, const Loop &loop)
    {
        if (!v)
            return false;
        if (dynamic_cast<Constant *>(v))
            return true;
        auto *inst = dynamic_cast<Instruction *>(v);
        if (!inst)
            return true; // args / globals
        return !loop.containsInst(inst);
    }

    static unsigned nestDepth(const Loop &loop, const vector<Loop> &loops)
    {
        unsigned depth = 1;
        for (const auto &outer : loops)
        {
            if (outer.header == loop.header)
                continue;
            if (!outer.containsBlock(loop.header))
                continue;
            if (outer.blocks.size() > loop.blocks.size())
                ++depth;
        }
        return depth;
    }

    static const Loop *findOutermostContaining(BasicBlock *bb, const vector<Loop> &loops)
    {
        const Loop *best = nullptr;
        for (const auto &loop : loops)
        {
            if (!loop.containsBlock(bb))
                continue;
            if (!best || loop.blocks.size() > best->blocks.size())
                best = &loop;
        }
        return best;
    }

    static void clearSuccessors(BasicBlock *bb)
    {
        for (BasicBlock *succ : vector<BasicBlock *>(bb->getSuccessors()))
        {
            bb->removeSuccessor(succ);
            succ->removePredecessor(bb);
        }
    }

    static Value *remapValue(Value *v, const unordered_map<Value *, Value *> &vMap)
    {
        if (!v)
            return nullptr;
        auto it = vMap.find(v);
        return it == vMap.end() ? v : it->second;
    }

    static BasicBlock *remapBB(BasicBlock *bb, const unordered_map<BasicBlock *, BasicBlock *> &bbMap)
    {
        if (!bb)
            return nullptr;
        auto it = bbMap.find(bb);
        return it == bbMap.end() ? bb : it->second;
    }

    static bool versionNest(Function *func, const Loop &loop, Value *divisor, unsigned verId,
                            stringstream &debugInfo, bool verbose)
    {
        BasicBlock *preheader = loop.getPreheader();
        if (!preheader || !loop.header)
            return false;
        if (loop.blocks.size() > 48)
            return false;

        unordered_set<BasicBlock *> inLoop(loop.blocks.begin(), loop.blocks.end());
        auto *three = new ConstantInt(IntegerType::getInstance(), 3);
        const string tag = "_d3v" + to_string(verId);

        unordered_map<BasicBlock *, BasicBlock *> bbMap;
        for (BasicBlock *bb : loop.blocks)
            bbMap[bb] = func->addBasicBlock(bb->getName() + tag);

        unordered_map<Value *, Value *> vMap;

        // Clone instructions (operands still point at originals; remapped next).
        for (BasicBlock *oldBB : loop.blocks)
        {
            BasicBlock *newBB = bbMap[oldBB];
            for (auto &instPtr : oldBB->getInstructions())
            {
                Instruction *oldInst = instPtr.get();
                Instruction *cloned = oldInst->clone();
                cloned->setName(oldInst->getName() + tag);
                vMap[oldInst] = cloned;
                newBB->addInstruction(own(cloned));
            }
        }

        // Remap operands / branch targets / phi incomings; specialize sdiv rhs.
        for (BasicBlock *oldBB : loop.blocks)
        {
            BasicBlock *newBB = bbMap[oldBB];
            auto &newInsts = newBB->getInstructions();
            auto &oldInsts = oldBB->getInstructions();
            for (size_t i = 0; i < newInsts.size(); ++i)
            {
                Instruction *ni = newInsts[i].get();
                Instruction *oi = oldInsts[i].get();

                if (auto *br = dynamic_cast<BranchInst *>(ni))
                {
                    if (br->isConditional())
                    {
                        br->setOperandByIndex(0, remapValue(br->getCondition(), vMap));
                        br->setTrueBlock(remapBB(br->getTrueBlock(), bbMap));
                        br->setFalseBlock(remapBB(br->getFalseBlock(), bbMap));
                    }
                    else
                    {
                        br->setTrueBlock(remapBB(br->getTrueBlock(), bbMap));
                    }
                    continue;
                }

                if (auto *phi = dynamic_cast<PhiInst *>(ni))
                {
                    auto *oldPhi = static_cast<PhiInst *>(oi);
                    // Rebuild incomings with remapped values/blocks.
                    while (phi->getNumIncomingValues() > 0)
                        phi->removeIncoming(0);
                    for (unsigned k = 0; k < oldPhi->getNumIncomingValues(); ++k)
                    {
                        Value *iv = remapValue(oldPhi->getIncomingValue(k), vMap);
                        BasicBlock *ib = oldPhi->getIncomingBlock(k);
                        if (inLoop.count(ib))
                            ib = bbMap[ib];
                        // preheader incoming stays as preheader (shared); remapped below after split
                        phi->addIncoming(iv, ib);
                    }
                    continue;
                }

                for (unsigned op = 0; op < ni->getNumOperands(); ++op)
                    ni->setOperandByIndex(op, remapValue(ni->getOperandByIndex(op), vMap));

                if (auto *bin = dynamic_cast<BinaryOperator *>(ni))
                {
                    if (bin->getOpcode() == Opcode::SDiv &&
                        stripCopy(oi->getOperandByIndex(1)) == divisor)
                    {
                        bin->setOperandByIndex(1, three);
                    }
                }
            }
        }

        // Wire CFG edges among cloned blocks + exits outside the nest.
        for (BasicBlock *oldBB : loop.blocks)
        {
            BasicBlock *newBB = bbMap[oldBB];
            clearSuccessors(newBB);
            auto *br = dynamic_cast<BranchInst *>(newBB->getTerminator());
            if (!br)
                continue;
            if (br->getTrueBlock())
                wireEdge(newBB, br->getTrueBlock());
            if (br->isConditional() && br->getFalseBlock())
                wireEdge(newBB, br->getFalseBlock());
        }

        // Exit phis: cloned latch/exits also feed the same outside successors.
        for (BasicBlock *oldBB : loop.blocks)
        {
            BasicBlock *newBB = bbMap[oldBB];
            auto *br = dynamic_cast<BranchInst *>(oldBB->getTerminator());
            if (!br)
                continue;
            vector<BasicBlock *> outs;
            if (br->getTrueBlock() && !inLoop.count(br->getTrueBlock()))
                outs.push_back(br->getTrueBlock());
            if (br->isConditional() && br->getFalseBlock() && !inLoop.count(br->getFalseBlock()))
                outs.push_back(br->getFalseBlock());
            for (BasicBlock *exitBB : outs)
            {
                for (auto &instPtr : exitBB->getInstructions())
                {
                    auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
                    if (!phi)
                        break;
                    for (unsigned k = 0; k < phi->getNumIncomingValues(); ++k)
                    {
                        if (phi->getIncomingBlock(k) != oldBB)
                            continue;
                        Value *v = phi->getIncomingValue(k);
                        Value *nv = remapValue(v, vMap);
                        // Avoid duplicate incoming from same newBB
                        bool exists = false;
                        for (unsigned t = 0; t < phi->getNumIncomingValues(); ++t)
                            if (phi->getIncomingBlock(t) == newBB)
                            {
                                exists = true;
                                break;
                            }
                        if (!exists)
                            phi->addIncoming(nv, newBB);
                    }
                }
            }
        }

        // Preheader: br (d==3) ? fast_header : slow_header
        auto *brPre = dynamic_cast<BranchInst *>(preheader->getTerminator());
        if (!brPre || brPre->isConditional())
            return false;
        if (brPre->getTrueBlock() != loop.header)
            return false;

        auto *eq3 = new ICmpInst(ICmpInst::ICMP_EQ, divisor, three, "divnest_eq3_" + to_string(verId));
        auto &preInsts = preheader->getInstructions();
        preInsts.insert(preInsts.end() - 1, own(eq3));

        BasicBlock *fastHeader = bbMap[loop.header];
        preInsts.back()->removeThisFromOperands();
        preInsts.pop_back();
        clearSuccessors(preheader);
        preheader->addInstruction(own(new BranchInst(eq3, fastHeader, loop.header)));
        wireEdge(preheader, fastHeader);
        wireEdge(preheader, loop.header);

        // Live-outs: merge defs used after the nest (fast path otherwise leaves stale i/j).
        unordered_set<BasicBlock *> cloneBlocks;
        for (auto &kv : bbMap)
            cloneBlocks.insert(kv.second);

        auto findBB = [&](Instruction *inst) -> BasicBlock * {
            for (auto &bbPtr : func->getBasicBlocks())
                for (auto &ip : bbPtr->getInstructions())
                    if (ip.get() == inst)
                        return bbPtr.get();
            return nullptr;
        };

        vector<pair<BasicBlock *, BasicBlock *>> exitingEdges;
        for (BasicBlock *oldBB : loop.blocks)
        {
            auto *br = dynamic_cast<BranchInst *>(oldBB->getTerminator());
            if (!br)
                continue;
            auto consider = [&](BasicBlock *target) {
                if (target && !inLoop.count(target))
                    exitingEdges.emplace_back(oldBB, target);
            };
            consider(br->getTrueBlock());
            if (br->isConditional())
                consider(br->getFalseBlock());
        }

        unordered_set<Instruction *> liveOuts;
        for (BasicBlock *oldBB : loop.blocks)
        {
            for (auto &instPtr : oldBB->getInstructions())
            {
                Instruction *def = instPtr.get();
                if (dynamic_cast<BranchInst *>(def))
                    continue;
                for (User *user : def->getUsers())
                {
                    auto *ui = dynamic_cast<Instruction *>(user);
                    if (!ui)
                        continue;
                    BasicBlock *userBB = findBB(ui);
                    if (!userBB || inLoop.count(userBB) || cloneBlocks.count(userBB))
                        continue;
                    liveOuts.insert(def);
                    break;
                }
            }
        }

        for (Instruction *def : liveOuts)
        {
            auto it = vMap.find(def);
            if (it == vMap.end())
                continue;
            Value *cloneDef = it->second;

            unordered_map<BasicBlock *, vector<pair<Value *, BasicBlock *>>> perExit;
            for (auto &edge : exitingEdges)
            {
                BasicBlock *origPred = edge.first;
                BasicBlock *exitBB = edge.second;
                perExit[exitBB].push_back({def, origPred});
                perExit[exitBB].push_back({cloneDef, bbMap[origPred]});
            }
            for (auto &kv : perExit)
            {
                BasicBlock *exitBB = kv.first;
                auto *phi = new PhiInst(def->getType(), def->getName() + "_merge" + tag);
                unordered_set<BasicBlock *> seenPred;
                for (auto &inc : kv.second)
                {
                    if (!seenPred.insert(inc.second).second)
                        continue;
                    phi->addIncoming(inc.first, inc.second);
                }
                if (phi->getNumIncomingValues() < 2)
                {
                    delete phi;
                    continue;
                }
                exitBB->getInstructions().insert(exitBB->getInstructions().begin(), own(phi));

                vector<Instruction *> users;
                for (User *user : def->getUsers())
                    if (auto *ui = dynamic_cast<Instruction *>(user))
                        if (ui != phi)
                            users.push_back(ui);
                for (Instruction *ui : users)
                {
                    BasicBlock *userBB = findBB(ui);
                    if (!userBB || inLoop.count(userBB) || cloneBlocks.count(userBB))
                        continue;
                    for (unsigned op = 0; op < ui->getNumOperands(); ++op)
                        if (ui->getOperandByIndex(op) == def)
                            ui->setOperandByIndex(op, phi);
                }
            }
        }

        if (verbose)
            debugInfo << "InvariantDivisorNestVersion: versioned nest at " << loop.header->getName()
                      << " for divisor, fast=" << fastHeader->getName()
                      << " liveOuts=" << liveOuts.size() << "\n";
        return true;
    }
} // namespace

bool InvariantDivisorNestVersionPass::runOnFunction(Function *func)
{
    if (!func || func->isLibraryFunction())
        return false;

    func->setLoops(ControlFlowAnalysis::findLoops(func));
    const auto &loops = func->getLoops();
    if (loops.empty())
        return false;

    struct Site
    {
        BasicBlock *bb;
        Value *rhs;
        const Loop *outer;
    };
    vector<Site> sites;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        for (auto &instPtr : bb->getInstructions())
        {
            auto *sdiv = dynamic_cast<BinaryOperator *>(instPtr.get());
            if (!sdiv || sdiv->getOpcode() != Opcode::SDiv)
                continue;
            Value *rhs = stripCopy(sdiv->getRHS());
            if (dynamic_cast<ConstantInt *>(rhs))
                continue;
            const Loop *inner = nullptr;
            for (const auto &loop : loops)
            {
                if (!loop.containsBlock(bb))
                    continue;
                if (!isLoopInvariant(rhs, loop))
                    continue;
                if (!inner || loop.blocks.size() < inner->blocks.size())
                    inner = &loop;
            }
            if (!inner || nestDepth(*inner, loops) < 3)
                continue;
            const Loop *outer = findOutermostContaining(bb, loops);
            if (!outer || !isLoopInvariant(rhs, *outer))
                continue;
            sites.push_back({bb, rhs, outer});
        }
    }
    if (sites.empty())
        return false;

    unordered_map<Value *, int> counts;
    for (auto &s : sites)
        counts[s.rhs]++;

    // One versioning per (outer header, divisor)
    unordered_set<BasicBlock *> doneHeaders;
    unsigned id = 0;
    bool changed = false;
    for (auto &s : sites)
    {
        if (counts[s.rhs] < 1)
            continue;
        if (!doneHeaders.insert(s.outer->header).second)
            continue;
        const Loop *live = nullptr;
        for (const auto &loop : func->getLoops())
            if (loop.header == s.outer->header)
            {
                live = &loop;
                break;
            }
        if (!live || !live->getPreheader())
            continue;
        if (versionNest(func, *live, s.rhs, id++, debugInfo, verbose))
        {
            changed = true;
            func->setLoops(ControlFlowAnalysis::findLoops(func));
        }
    }
    return changed;
}
