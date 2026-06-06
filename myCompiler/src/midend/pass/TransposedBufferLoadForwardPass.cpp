#include "TransposedBufferLoadForwardPass.h"

using namespace std;
using namespace optimization;
using namespace matrixStructure;

static bool isGeneratedLoad(LoadInst *load)
{
    auto *gep = dynamic_cast<GetElementPtrInst *>(load->getPointer());
    if (!gep)
        return false;
    const string &n = gep->getName();
    return n.find("transpose_col_gep") != string::npos || n.find("transpose_row_gep") != string::npos;
}

LoadInst *TransposedBufferLoadForwardPass::materializeTransposedLoad(BasicBlock *bb, Value *rowIdx,
                                                                     Value *colIdx, Value *newBase,
                                                                     unsigned insertIndex,
                                                                     const string &tag)
{
    auto *i32 = IntegerType::getInstance();
    auto *zero = new ConstantInt(i32, 0);
    const string rowName = "transpose_row_gep_" + tag;
    const string colName = "transpose_col_gep_" + tag;
    const string loadName = "transpose_load_" + tag;
    auto *rowGep = new GetElementPtrInst(newBase, vector<Value *>{colIdx, zero}, rowName);
    auto *elemGep = new GetElementPtrInst(rowGep, vector<Value *>{rowIdx}, colName);
    auto *newLoad = new LoadInst(elemGep, loadName);

    auto &insts = bb->getInstructions();
    insts.insert(insts.begin() + static_cast<int>(insertIndex), unique_ptr<Instruction>(rowGep));
    insts.insert(insts.begin() + static_cast<int>(insertIndex) + 1, unique_ptr<Instruction>(elemGep));
    insts.insert(insts.begin() + static_cast<int>(insertIndex) + 2, unique_ptr<Instruction>(newLoad));
    return newLoad;
}

bool TransposedBufferLoadForwardPass::tryForwardLoad(Function *func, LoadInst *load,
                                                     BasicBlock *bb,
                                                     const TransposeBufferRelation &rel)
{
    if (!isReachableFrom(rel.regionEntry, bb, rel.transposeLoopBlocks))
        return false;
    if (isGeneratedLoad(load))
        return false;

    Value *row = nullptr, *col = nullptr, *base = nullptr;
    if (!parse2DAccess(load->getPointer(), row, col, base))
        return false;

    Value *iIV = nullptr, *jIV = nullptr, *kIV = nullptr;
    if (!findIJKInductionVars(bb, func->getLoops(), iIV, jIV, kIV))
        return false;
    if (!isKJMatrixAccess(row, col, iIV, jIV, kIV))
        return false;

    auto &insts = bb->getInstructions();
    unsigned insertIndex = 0;
    for (; insertIndex < insts.size(); ++insertIndex)
    {
        if (insts[insertIndex].get() == load)
            break;
    }
    if (insertIndex >= insts.size())
        return false;

    auto logForward = [&](Value *dstBase) {
        if (verbose)
        {
            debugInfo << "TransposedBufferLoadForward: load " << base->getName() << "["
                      << row->getName() << "][" << col->getName() << "] -> "
                      << dstBase->getName() << "[" << col->getName() << "][" << row->getName()
                      << "] in " << bb->getName() << "\n";
        }
    };

    if (sameArray(base, rel.srcBuffer))
    {
        LoadInst *newLoad =
            materializeTransposedLoad(bb, row, col, rel.dstBuffer, insertIndex, "accum");
        load->replaceAllUsesWith(newLoad);
        load->removeThisFromOperands();
        needToDelete.push_back(insts[insertIndex + 3].release());
        insts.erase(insts.begin() + static_cast<int>(insertIndex) + 3);
        logForward(rel.dstBuffer);
        return true;
    }

    if (!sameArray(base, rel.dstBuffer))
        return false;

    vector<Instruction *> parityUsers;
    vector<Instruction *> accumUsers;
    vector<Instruction *> otherUsers;
    for (auto *user : load->getUsers())
    {
        auto *inst = dynamic_cast<Instruction *>(user);
        if (!inst)
            continue;
        switch (classifyKJLoadUser(inst, load, iIV, kIV, rel))
        {
        case KJLoadUseKind::ParityWithAIK:
            parityUsers.push_back(inst);
            break;
        case KJLoadUseKind::AccumWithBIK:
            accumUsers.push_back(inst);
            break;
        default:
            otherUsers.push_back(inst);
            break;
        }
    }

    auto replaceInUsers = [&](LoadInst *newLoad, const vector<Instruction *> &users) {
        for (auto *inst : users)
            inst->replaceOperand(load, newLoad);
    };

    const bool needParity = !parityUsers.empty() || !otherUsers.empty();
    const bool needAccum = !accumUsers.empty();

    LoadInst *parityLoad = nullptr;
    LoadInst *accumLoad = nullptr;
    unsigned cursor = insertIndex;
    if (needParity)
    {
        parityLoad = materializeTransposedLoad(bb, row, col, rel.srcBuffer, cursor, "parity");
        cursor += 3;
    }
    if (needAccum)
    {
        accumLoad = materializeTransposedLoad(bb, row, col, rel.dstBuffer, cursor, "accum");
        cursor += 3;
    }

    if (needParity)
    {
        replaceInUsers(parityLoad, parityUsers);
        replaceInUsers(parityLoad, otherUsers);
    }
    if (needAccum)
        replaceInUsers(accumLoad, accumUsers);

    load->removeThisFromOperands();
    const unsigned eraseIdx = insertIndex + (needParity ? 3u : 0u) + (needAccum ? 3u : 0u);
    needToDelete.push_back(insts[eraseIdx].release());
    insts.erase(insts.begin() + static_cast<int>(eraseIdx));

    if (needParity)
        logForward(rel.srcBuffer);
    if (needAccum)
        logForward(rel.dstBuffer);
    return needParity || needAccum;
}

bool TransposedBufferLoadForwardPass::runOnFunction(Function *func)
{
    const MatrixFunctionAnalysis *analysis = getAnalysis(func);
    if (!analysis || !analysis->transposePair || !analysis->transposePair->valid)
        return false;

    const TransposeBufferRelation &rel = *analysis->transposePair;
    func->setLoops(ControlFlowAnalysis::findLoops(func));

    vector<pair<LoadInst *, BasicBlock *>> candidates;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        for (auto &instPtr : bb->getInstructions())
        {
            if (auto *load = dynamic_cast<LoadInst *>(instPtr.get()))
                candidates.emplace_back(load, bb);
        }
    }

    stable_sort(candidates.begin(), candidates.end(),
                [&](const pair<LoadInst *, BasicBlock *> &lhs,
                    const pair<LoadInst *, BasicBlock *> &rhs) {
                    Value *r = nullptr, *c = nullptr, *baseL = nullptr;
                    Value *r2 = nullptr, *c2 = nullptr, *baseR = nullptr;
                    parse2DAccess(lhs.first->getPointer(), r, c, baseL);
                    parse2DAccess(rhs.first->getPointer(), r2, c2, baseR);
                    const bool lSrc = sameArray(baseL, rel.srcBuffer);
                    const bool rSrc = sameArray(baseR, rel.srcBuffer);
                    if (lSrc != rSrc)
                        return lSrc;
                    return false;
                });

    auto stillInBlock = [](LoadInst *load, BasicBlock *bb) {
        for (auto &instPtr : bb->getInstructions())
            if (instPtr.get() == load)
                return true;
        return false;
    };

    bool changed = false;
    for (auto &[load, bb] : candidates)
    {
        if (!stillInBlock(load, bb))
            continue;
        if (tryForwardLoad(func, load, bb, rel))
            changed = true;
    }

    if (changed && verbose)
        debugInfo << "TransposedBufferLoadForward: applied transpose load forwarding in "
                  << func->getName() << "\n";
    return changed;
}
