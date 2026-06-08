#include "GraphColorRegisterAllocator.h"
#include <limits>

using namespace RISCV;

namespace
{
  bool registersEqual(const shared_ptr<RISCVRegister> &lhs,
                      const shared_ptr<RISCVRegister> &rhs)
  {
    return lhs && rhs && *lhs == *rhs;
  }
} // namespace

vector<int> GraphColorRegisterAllocator::collectCallSiteIndices() const
{
  vector<int> callSiteIndices;
  unordered_map<shared_ptr<RISCVInstruction>, int> instrIndex;
  int instrNum = 0;

  vector<shared_ptr<RISCVBasicBlock>> postOrder = getPostOrder(currentFunc);
  for (auto it = postOrder.rbegin(); it != postOrder.rend(); ++it)
  {
    auto bb = *it;
    for (const auto &instr : bb->getInstructions())
    {
      instrIndex[instr] = instrNum++;
    }
  }

  callSiteIndices.reserve(currentFunc->getBasicBlocks().size());
  for (const auto &bb : currentFunc->getBasicBlocks())
  {
    for (const auto &instr : bb->getInstructions())
    {
      if (instr->getOpcode() != RISCVOpcode::CALL)
      {
        continue;
      }
      auto it = instrIndex.find(instr);
      if (it != instrIndex.end())
      {
        callSiteIndices.push_back(it->second);
      }
    }
  }
  return callSiteIndices;
}

bool GraphColorRegisterAllocator::isCalleeSavedColor(
    const shared_ptr<RISCVRegister> &color) const
{
  if (!color || !color->isPhysical())
  {
    return false;
  }

  const auto &pool = getAvailableColors(color->getType());
  for (size_t i = 0; i < pool.size(); ++i)
  {
    if (pool[i]->getPhysicalReg() == color->getPhysicalReg())
    {
      if (color->getType() == RegisterType::INT)
      {
        return i >= 15; // T0-T6, A0-A7, then S0-S11
      }
      return i >= 20; // FT0-FT11, FA0-FA7, then FS0-FS11
    }
  }
  return false;
}

bool GraphColorRegisterAllocator::isLiveAcrossCall(
    shared_ptr<RISCVRegister> reg, const vector<int> &callSiteIndices) const
{
  const auto &livenessInfo = currentFunc->getLivenessInfo();
  for (int callPos : callSiteIndices)
  {
    if (livenessInfo.isLiveAt(reg, callPos))
    {
      return true;
    }
  }
  return false;
}

int GraphColorRegisterAllocator::getMaxLoopDepthForReg(
    shared_ptr<RISCVRegister> reg) const
{
  const auto &loopInfo = currentFunc->getLoopInfo();
  int maxDepth = 0;

  for (const auto &bb : currentFunc->getBasicBlocks())
  {
    bool touched = false;
    for (const auto &instr : bb->getInstructions())
    {
      for (const auto &useReg : instr->getUseRegisters())
      {
        if (registersEqual(useReg, reg))
        {
          touched = true;
          break;
        }
      }
      if (touched)
      {
        break;
      }
      for (const auto &defReg : instr->getDefRegisters())
      {
        if (registersEqual(defReg, reg))
        {
          touched = true;
          break;
        }
      }
      if (touched)
      {
        break;
      }
    }

    if (touched)
    {
      maxDepth = std::max(maxDepth, loopInfo.getDepth(bb));
    }
  }

  return maxDepth;
}

shared_ptr<RISCVRegister> GraphColorRegisterAllocator::getPrecoloredCoalesceHint(
    shared_ptr<RISCVRegister> reg) const
{
  shared_ptr<RISCVRegister> cur = reg;
  std::unordered_set<shared_ptr<RISCVRegister>, RegisterHash, RegisterEqual>
      visited;
  while (cur)
  {
    if (isPrecolored(cur))
    {
      return cur;
    }

    auto keep = coalescingManager.getKeepRegister(cur);
    if (!keep || registersEqual(keep, cur) || visited.count(keep))
    {
      break;
    }
    visited.insert(cur);
    cur = keep;
  }

  for (const auto &move : moveList.getAllMoves())
  {
    if (registersEqual(move.dst, reg) && isPrecolored(move.src))
    {
      return move.src;
    }
    if (registersEqual(move.src, reg) && isPrecolored(move.dst))
    {
      return move.dst;
    }
  }

  return nullptr;
}

unordered_set<RISCVRegister::PhysicalReg>
GraphColorRegisterAllocator::collectUsedCalleeSavedColors() const
{
  unordered_set<RISCVRegister::PhysicalReg> used;
  for (const auto &entry : allocation)
  {
    const auto &color = entry.second;
    if (isCalleeSavedColor(color))
    {
      used.insert(color->getPhysicalReg());
    }
  }
  return used;
}

int GraphColorRegisterAllocator::scoreColorCandidate(
    bool liveAcrossCall, int maxLoopDepth,
    const shared_ptr<RISCVRegister> &coalesceHint,
    const shared_ptr<RISCVRegister> &color, size_t colorIndex,
    const unordered_set<RISCVRegister::PhysicalReg> &usedCalleeSaved) const
{
  if (coalesceHint &&
      color->getPhysicalReg() == coalesceHint->getPhysicalReg())
  {
    return -10000;
  }

  const bool calleeSaved = isCalleeSavedColor(color);
  int score = static_cast<int>(colorIndex);

  if (liveAcrossCall)
  {
    if (calleeSaved)
    {
      score -= 500;
      if (usedCalleeSaved.count(color->getPhysicalReg()))
      {
        score -= 200;
      }
    }
    else
    {
      score += 1000;
    }
  }
  else
  {
    if (calleeSaved)
    {
      score += 300;
      if (usedCalleeSaved.count(color->getPhysicalReg()))
      {
        score -= 100;
      }
    }
    else
    {
      score -= 300;
      if (maxLoopDepth > 0)
      {
        score -= 30 * maxLoopDepth;
      }
    }
  }

  return score;
}

shared_ptr<RISCVRegister> GraphColorRegisterAllocator::selectBiasedColor(
    shared_ptr<RISCVRegister> reg,
    const vector<shared_ptr<RISCVRegister>> &availableColors,
    const vector<bool> &colorUsed, const vector<int> &callSiteIndices) const
{
  const bool liveAcrossCall = isLiveAcrossCall(reg, callSiteIndices);
  const int maxLoopDepth = getMaxLoopDepthForReg(reg);
  const auto coalesceHint = getPrecoloredCoalesceHint(reg);
  const auto usedCalleeSaved = collectUsedCalleeSavedColors();

  shared_ptr<RISCVRegister> bestColor = nullptr;
  int bestScore = std::numeric_limits<int>::max();

  for (size_t i = 0; i < availableColors.size(); ++i)
  {
    if (colorUsed[i])
    {
      continue;
    }

    const auto &color = availableColors[i];
    const int score = scoreColorCandidate(liveAcrossCall, maxLoopDepth,
                                          coalesceHint, color, i,
                                          usedCalleeSaved);
    if (score < bestScore)
    {
      bestScore = score;
      bestColor = color;
    }
  }

  return bestColor;
}
