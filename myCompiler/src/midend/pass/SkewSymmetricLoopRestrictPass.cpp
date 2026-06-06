#include "SkewSymmetricLoopRestrictPass.h"

using namespace std;
using namespace optimization;
using namespace matrixStructure;

bool SkewSymmetricLoopRestrictPass::restrictNest(const SkewSymmetricMatrixNest &nest)
{
    if (!nest.valid || !nest.jInitFromZero)
        return false;

    const SquareIJLoopNest &loops = nest.nest;
    Value *iIV = loops.iIV;
    Value *jIV = loops.jIV;
    bool changed = false;

    if (auto *initCopy = findJZeroInitCopy(loops, jIV))
    {
        initCopy->setOperandByIndex(0, iIV);
        changed = true;
        if (verbose)
            debugInfo << "SkewSymmetricLoopRestrict: j init copy 0 -> i\n";
    }

    for (auto &instPtr : loops.jLoop->header->getInstructions())
    {
        auto *phi = dynamic_cast<PhiInst *>(instPtr.get());
        if (!phi || !sameValue(phi, jIV))
            continue;
        for (unsigned i = 0; i < phi->getNumIncomingValues(); ++i)
        {
            BasicBlock *from = phi->getIncomingBlock(i);
            if (loops.jLoop->containsBlock(from))
                continue;
            if (!isZeroInit(phi->getIncomingValue(i)))
                continue;
            phi->setIncomingValue(i, iIV);
            changed = true;
            if (verbose)
                debugInfo << "SkewSymmetricLoopRestrict: j phi init 0 -> i from "
                          << from->getName() << "\n";
        }
    }

    return changed;
}

bool SkewSymmetricLoopRestrictPass::runOnFunction(Function *func)
{
    const MatrixFunctionAnalysis *analysis = getAnalysis(func);
    if (!analysis || analysis->skewSymmetricNests.empty())
        return false;

    bool changed = false;
    for (const auto &nest : analysis->skewSymmetricNests)
    {
        if (restrictNest(nest))
        {
            changed = true;
            if (verbose)
            {
                debugInfo << "SkewSymmetricLoopRestrict: tightened j lower bound to i in "
                          << func->getName() << "\n";
            }
        }
    }

    if (changed)
        func->setLoops(ControlFlowAnalysis::findLoops(func));
    return changed;
}
