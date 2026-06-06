#include "SRFixedPass.h"
#include <cmath>
using namespace std;
using namespace optimization;

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

    static bool isConstPowerOfTwo(int d, int &absD)
    {
        absD = d < 0 ? -d : d;
        return absD > 0 && (absD & (absD - 1)) == 0;
    }

    // n % 2^k == 0  <=>  (n & (2^k-1)) == 0；eq/ne 与 0、除数为 2 的幂，且 SRem 仅一个 use
    static bool tryFoldSRemPow2EqZero(ICmpInst *icmp, std::vector<std::unique_ptr<Instruction>> &insts,
                                      size_t icmpIndex, bool verbose, std::stringstream &debugInfo,
                                      BasicBlock *bb, bool &changed)
    {
        ICmpInst::Predicate pred = icmp->getPredicate();
        if (pred != ICmpInst::ICMP_EQ && pred != ICmpInst::ICMP_NE)
        {
            return false;
        }

        Value *cmpOther = nullptr;
        if (auto *zero = dynamic_cast<ConstantInt *>(icmp->getRHS());
            zero && zero->Value == 0)
        {
            cmpOther = icmp->getLHS();
        }
        else if (auto *zero = dynamic_cast<ConstantInt *>(icmp->getLHS());
                 zero && zero->Value == 0)
        {
            cmpOther = icmp->getRHS();
        }
        else
        {
            return false;
        }

        auto *srem = dynamic_cast<BinaryOperator *>(stripCopy(cmpOther));
        if (!srem || srem->getOpcode() != Opcode::SRem)
        {
            return false;
        }
        auto *divisor = dynamic_cast<ConstantInt *>(srem->getRHS());
        int absDivisor = 0;
        if (!divisor || !isConstPowerOfTwo(divisor->Value, absDivisor))
        {
            return false;
        }
        if (srem->getUsers().size() > 1)
        {
            return false;
        }

        Value *n = srem->getLHS();
        auto *ty = IntegerType::getInstance();
        int maskVal = absDivisor - 1;
        auto *andInst = new BinaryOperator(Opcode::And, n, new ConstantInt(ty, maskVal),
                                           icmp->getName() + "_pmod_and");
        insts.insert(insts.begin() + static_cast<ptrdiff_t>(icmpIndex),
                     std::unique_ptr<Instruction>(andInst));

        if (cmpOther == icmp->getLHS())
        {
            icmp->setOperandByIndex(0, andInst);
        }
        else
        {
            icmp->setOperandByIndex(1, andInst);
        }

        changed = true;
        if (verbose)
        {
            debugInfo << "SRFixedPass: Folded (n % " << absDivisor << ") == 0 to (n & " << maskVal
                      << ") == 0 in " << bb->getName() << ": " << icmp->toString() << "\n";
        }
        return true;
    }

    static int popcount32(uint32_t v) { return v ? __builtin_popcount(v) : 0; }

    static int ctz32(uint32_t v) { return v ? __builtin_ctz(v) : 0; }

    // C = 2^tz * (2^b0 + 2^b1)，即奇部仅含两个 1 位时可拆成两次移位再相加
    static bool tryMulAsTwoPowerSum(int32_t c, int &tz, int &sh0, int &sh1)
    {
        if (c == 0)
        {
            return false;
        }
        uint32_t absC = static_cast<uint32_t>(c < 0 ? -static_cast<int64_t>(c) : c);
        if (absC <= 1 || (absC & (absC - 1)) == 0)
        {
            return false;
        }
        tz = ctz32(absC);
        uint32_t odd = absC >> tz;
        if (popcount32(odd) != 2)
        {
            return false;
        }
        int b0 = ctz32(odd);
        uint32_t rest = odd & (odd - 1);
        int b1 = ctz32(rest);
        sh0 = tz + b0;
        sh1 = tz + b1;
        return true;
    }

    static bool replaceMulWithShiftAdd(BasicBlock *bb, vector<unique_ptr<Instruction>> &insts, int idx,
                                       Value *lhs, int32_t c, const string &instName, bool verbose,
                                       stringstream &debugInfo, vector<Value *> &needToDelete,
                                       bool &changed)
    {
        int tz = 0, sh0 = 0, sh1 = 0;
        (void)tz;
        if (!tryMulAsTwoPowerSum(c, tz, sh0, sh1))
        {
            return false;
        }

        auto *ty = IntegerType::getInstance();
        auto *shl0 = new BinaryOperator(Opcode::Sll, lhs, new ConstantInt(ty, sh0), instName + "_sr_sh0");
        auto *shl1 = new BinaryOperator(Opcode::Sll, lhs, new ConstantInt(ty, sh1), instName + "_sr_sh1");
        auto *sum = new BinaryOperator(Opcode::Add, shl0, shl1, instName + "_sr_sum");
        Instruction *result = sum;
        unique_ptr<Instruction> negHolder;
        if (c < 0)
        {
            negHolder = make_unique<BinaryOperator>(Opcode::Sub, new ConstantInt(ty, 0), sum, instName + "_sr_neg");
            result = negHolder.get();
        }

        Instruction *inst = insts[idx].get();
        inst->removeThisFromOperands();
        inst->replaceAllUsesWith(result);
        needToDelete.push_back(insts[idx].release());
        insts.erase(insts.begin() + idx);
        insts.insert(insts.begin() + idx, unique_ptr<Instruction>(shl0));
        insts.insert(insts.begin() + idx + 1, unique_ptr<Instruction>(shl1));
        insts.insert(insts.begin() + idx + 2, unique_ptr<Instruction>(sum));
        if (negHolder)
        {
            insts.insert(insts.begin() + idx + 3, std::move(negHolder));
        }

        changed = true;
        if (verbose)
        {
            debugInfo << "SRFixedPass: Replaced Mul " << c << " with shift-add (1<<"
                      << sh0 << " + 1<<" << sh1 << ") in " << bb->getName() << "\n";
        }
        return true;
    }
}

std::pair<int64_t, int> SRFixedPass::compute_magic(int32_t d)
{
    const uint64_t two32 = 1ULL << 32;
    uint32_t ad = (d > 0) ? d : -d;

    // 构造 anc: 小于 2^32 的、最接近 2^32 的 |d| 的倍数
    uint64_t anc;
    if (d > 0)
        anc = two32 - 1 - (two32 - 1) % ad;
    else
        anc = two32 - (two32 % ad);

    int p = 32;
    uint64_t q1 = two32 / anc;
    uint64_t r1 = two32 % anc;
    uint64_t q2 = two32 / ad;
    uint64_t r2 = two32 % ad;

    uint64_t delta;
    do
    {
        p++;
        q1 *= 2;
        r1 *= 2;
        if (r1 >= anc)
        {
            q1++;
            r1 -= anc;
        }
        q2 *= 2;
        r2 *= 2;
        if (r2 >= ad)
        {
            q2++;
            r2 -= ad;
        }
        delta = ad - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));

    int64_t magic = q2 + 1;
    if (d < 0)
        magic = -magic;
    int shift = p - 32;

    return {magic, shift};
}
bool SRFixedPass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bbPtr : func->getBasicBlocks())
    {
        BasicBlock *bb = bbPtr.get();
        auto &insts = bb->getInstructions();
        for (size_t i = 0; i < insts.size(); ++i)
        {
            if (auto *icmp = dynamic_cast<ICmpInst *>(insts[i].get()))
            {
                tryFoldSRemPow2EqZero(icmp, insts, i, verbose, debugInfo, bb, changed);
            }
        }
    }

    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        // 用下标逆序遍历，避免迭代器失效
        for (int i = insts.size() - 1; i >= 0; --i)
        {
            Instruction *inst = insts[i].get();
            auto instName = inst->getName();
            if (inst && inst->getOpcode() == Opcode::Mul)
            {
                Value *lhs = inst->getOperands()[0];
                Value *rhs = inst->getOperands()[1];
                ConstantInt *constInt = dynamic_cast<ConstantInt *>(rhs);
                if (!constInt)
                {
                    constInt = dynamic_cast<ConstantInt *>(lhs);
                    if (constInt)
                    {
                        lhs = rhs;
                    }
                }
                if (constInt)
                {
                    if (constInt->Value == 0)
                    {
                        // 乘以0，直接替换为0
                        auto *zero = new ConstantInt(IntegerType::getInstance(), 0);
                        inst->replaceAllUsesWith(zero);
                        inst->removeThisFromOperands();
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced Mul with 0 in " << bb->getName() << "\n";
                        }
                        continue;
                    }
                    else if (constInt->Value != 0 && (constInt->Value & (constInt->Value - 1)) == 0)
                    {
                        // 2的幂，直接左移
                        int shift = 0;
                        int val = constInt->Value;
                        while (val > 1)
                        {
                            val >>= 1;
                            shift++;
                        }
                        // 替换为左移操作
                        auto *shlInst = new BinaryOperator(Opcode::Sll, lhs, new ConstantInt(IntegerType::getInstance(), shift), instName + "_sll");
                        inst->removeThisFromOperands();
                        inst->replaceAllUsesWith(shlInst);
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(shlInst));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced Mul with Sll for " << constInt->Value
                                      << " in " << bb->getName() << "\n";
                        }
                        continue;
                    }
                    else if (replaceMulWithShiftAdd(bb.get(), insts, i, lhs, constInt->Value, instName, verbose,
                                                    debugInfo, needToDelete, changed))
                    {
                        continue;
                    }
                }
            }
            else if (inst && inst->getOpcode() == Opcode::SDiv)
            {
                Value *lhs = inst->getOperands()[0];
                Value *rhs = inst->getOperands()[1];
                if (auto *constInt = dynamic_cast<ConstantInt *>(rhs))
                {
                    int rhs_value_abs = abs(constInt->Value);
                    int rhs_value = constInt->Value;
                    if (rhs_value != -1 && (rhs_value_abs & (rhs_value_abs - 1)) == 0)
                    {
                        // 2的幂，直接算数右移
                        int shift = 0;
                        int val = rhs_value_abs;
                        while (val > 1)
                        {
                            val >>= 1;
                            shift++;
                        }
                        // 负数除法：先加 bias 再右移；bias = (x>>31)&mask
                        auto *type = IntegerType::getInstance();
                        auto *zero = new ConstantInt(type, 0);
                        auto *shiftConst = new ConstantInt(type, shift);
                        std::unique_ptr<Instruction> signedDivHolder;
                        Instruction *bias = nullptr;
                        if (rhs_value_abs == 2)
                        {
                            // mask==1 时 (x>>31)&1 等价于 slt(x,0)
                            bias = new ICmpInst(ICmpInst::ICMP_SLT, lhs, zero, instName + "_bias");
                        }
                        else
                        {
                            auto *mask = new ConstantInt(type, (1 << shift) - 1);
                            signedDivHolder = std::make_unique<BinaryOperator>(
                                Opcode::Sra, lhs, new ConstantInt(type, 31), instName + "_signedDiv");
                            bias = new BinaryOperator(Opcode::And, signedDivHolder.get(), mask, instName + "_bias");
                        }
                        auto *lhsAdj = new BinaryOperator(Opcode::Add, lhs, bias, instName + "_lhsAdj");
                        auto *sraInst = new BinaryOperator(Opcode::Sra, lhsAdj, shiftConst, instName + "_sra");
                        std::unique_ptr<Instruction> negHolder;
                        Instruction *finalRes = sraInst;
                        if (rhs_value < 0)
                        {
                            negHolder = std::make_unique<BinaryOperator>(Opcode::Sub, zero, sraInst, instName + "_neg");
                            finalRes = negHolder.get();
                        }
                        inst->removeThisFromOperands();
                        inst->replaceAllUsesWith(finalRes);
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sraInst));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhsAdj));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(bias));
                        size_t pow2DivInstCount = 3;
                        if (signedDivHolder)
                        {
                            insts.insert(insts.begin() + i, std::move(signedDivHolder));
                            pow2DivInstCount++;
                        }
                        if (negHolder)
                        {
                            insts.insert(insts.begin() + i + pow2DivInstCount, std::move(negHolder));
                        }
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced SDiv with Sra for " << constInt->Value
                                      << (rhs_value_abs == 2 ? " (slt bias)" : "")
                                      << " in " << bb->getName() << "\n";
                        }
                    }
                    // 只处理常数且不是0、1、-1、2的幂
                    else if (rhs_value != -1 && (rhs_value_abs & (rhs_value_abs - 1)) != 0)
                    {
                        // 计算magic和shift
                        auto [magic, shift] = compute_magic(rhs_value_abs);
                        auto *type = IntegerType::getInstance();
                        // 1. 扩展lhs为64位
                        auto *lhs64 = new CastInst(Opcode::Sext, lhs, LongType::getInstance(), instName + "_to64");
                        // 2. lhs左移32位
                        auto *lhs_sll = new BinaryOperator(Opcode::Slld, lhs64, new ConstantLong(LongType::getInstance(), 32), instName + "_sll32");
                        // 3. 乘以magic
                        auto *magic_const = new ConstantLong(LongType::getInstance(), magic);
                        auto *mulh = new BinaryOperator(Opcode::Mulhd, lhs_sll, magic_const, instName + "_mulhmagic");
                        // 4. 取高64位（算术右移shift位）
                        auto *shiftnum = new ConstantLong(LongType::getInstance(), shift);
                        auto *sra_div = new BinaryOperator(Opcode::Srad, mulh, shiftnum, instName + "_sra_div");
                        // 5. 截断回32位
                        auto *q0 = new CastInst(Opcode::Trunc, sra_div, type, instName + "_divmagic");
                        // 6. 修正：被除数为负时，结果加1
                        // sign = (lhs < 0) ? 1 : 0
                        auto *zero = new ConstantInt(type, 0);
                        auto *sign = new ICmpInst(ICmpInst::ICMP_SLT, lhs, zero, instName + "_divsign");
                        // q = q0 + sign
                        auto *q = new BinaryOperator(Opcode::Add, q0, sign, instName + "_divmagic_fix");
                        Instruction *finalRes = q;
                        if (rhs_value < 0)
                        {
                            auto *neg = new BinaryOperator(Opcode::Sub, zero, q, instName + "_neg");
                            finalRes = neg;
                            insts.insert(insts.begin() + i + 1, std::unique_ptr<Instruction>(neg));
                        }
                        inst->replaceAllUsesWith(finalRes);
                        inst->removeThisFromOperands();
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        // 按顺序插入新指令
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(q));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sign));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(q0));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sra_div));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(mulh));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhs_sll));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhs64));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced SDiv with magic number+sign fix for " << rhs_value_abs
                                      << " (magic=" << magic << ", shift=" << shift << ") in " << bb->getName() << "\n";
                        }
                    }
                    else if (rhs_value == -1)
                    {
                        // 除以-1，等价于0-lhs
                        auto *zero = new ConstantInt(IntegerType::getInstance(), 0);
                        auto *neg = new BinaryOperator(Opcode::Sub, zero, lhs, instName + "_neg");
                        inst->replaceAllUsesWith(neg);
                        inst->removeThisFromOperands();
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(neg));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced SDiv by -1 with neg in " << bb->getName() << "\n";
                        }
                    }
                }
            }
            // 新增：2的幂次方取模优化
            else if (inst && inst->getOpcode() == Opcode::SRem)
            {
                Value *lhs = inst->getOperands()[0];
                Value *rhs = inst->getOperands()[1];
                if (auto *constInt = dynamic_cast<ConstantInt *>(rhs))
                {
                    // 这里如果是INT_MIN可能会有问题，abs会溢出,应该不会无聊到模一个最小值吧
                    int rhs_value_abs = abs(constInt->Value);
                    if ((rhs_value_abs & (rhs_value_abs - 1)) == 0)
                    {
                        // x % 2^n == ((x + bias) & mask) - bias
                        // bias = (x >> 31) & mask；mask==1 时 bias = slt(x,0)
                        auto *type = IntegerType::getInstance();
                        int mask_val = rhs_value_abs - 1;
                        auto *mask_const = new ConstantInt(type, mask_val);
                        auto *zero = new ConstantInt(type, 0);
                        std::unique_ptr<Instruction> signMaskHolder;
                        Instruction *bias = nullptr;
                        if (rhs_value_abs == 2)
                        {
                            bias = new ICmpInst(ICmpInst::ICMP_SLT, lhs, zero, instName + "_bias");
                        }
                        else
                        {
                            signMaskHolder = std::make_unique<BinaryOperator>(
                                Opcode::Sra, lhs, new ConstantInt(type, 31), instName + "_signmask");
                            bias = new BinaryOperator(Opcode::And, signMaskHolder.get(), mask_const, instName + "_bias");
                        }
                        auto *x_add_bias = new BinaryOperator(Opcode::Add, lhs, bias, instName + "_addbias");
                        auto *and_mask = new BinaryOperator(Opcode::And, x_add_bias, mask_const, instName + "_andmask");
                        auto *final_res = new BinaryOperator(Opcode::Sub, and_mask, bias, instName + "_mod2n");

                        inst->removeThisFromOperands();
                        inst->replaceAllUsesWith(final_res);
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(final_res));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(and_mask));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(x_add_bias));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(bias));
                        if (signMaskHolder)
                        {
                            insts.insert(insts.begin() + i, std::move(signMaskHolder));
                        }
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced SRem with ((x+bias)&mask)-bias for " << rhs_value_abs
                                      << (rhs_value_abs == 2 ? " (slt bias)" : "")
                                      << " in " << bb->getName() << "\n";
                        }
                    }
                    if ((rhs_value_abs & (rhs_value_abs - 1)) != 0)
                    {
                        // 1. 计算magic和shift
                        auto [magic, shift] = compute_magic(rhs_value_abs);
                        auto *type = IntegerType::getInstance();
                        // 2. 扩展lhs为64位
                        auto *lhs64 = new CastInst(Opcode::Sext, lhs, LongType::getInstance(), instName + "_to64");
                        // 3. lhs左移32位
                        auto *lhs_sll = new BinaryOperator(Opcode::Slld, lhs64, new ConstantLong(LongType::getInstance(), 32), instName + "_sll32");
                        // 4. 乘以magic
                        auto *magic_const = new ConstantLong(LongType::getInstance(), magic);
                        auto *mulh = new BinaryOperator(Opcode::Mulhd, lhs_sll, magic_const, instName + "_mulmagic");
                        // 5. 取高64位（算术右移shift位）
                        auto *shiftnum = new ConstantLong(LongType::getInstance(), shift);
                        auto *sra_div = new BinaryOperator(Opcode::Srad, mulh, shiftnum, instName + "_sra_div");
                        // 6. 截断回32位
                        auto *q0 = new CastInst(Opcode::Trunc, sra_div, type, instName + "_divmagic");
                        // 由于魔数法的基础是向下取整例如-7/3=-3而不是-2，因此需要修正加一
                        // 7. 修正：被除数为负时，结果加1
                        auto *zero = new ConstantInt(type, 0);
                        auto *sign = new ICmpInst(ICmpInst::ICMP_SLT, lhs, zero, instName + "_divsign");
                        auto *q = new BinaryOperator(Opcode::Add, q0, sign, instName + "_divmagic_fix");
                        // 8. rem = lhs - q * d
                        auto *d_const = new ConstantInt(type, rhs_value_abs);
                        auto *q_mul_d = new BinaryOperator(Opcode::Mul, q, d_const, instName + "_qmul");
                        auto *rem = new BinaryOperator(Opcode::Sub, lhs, q_mul_d, instName + "_remmagic");

                        inst->replaceAllUsesWith(rem);
                        inst->removeThisFromOperands();
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        // 按顺序插入新指令
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(rem));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(q_mul_d));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(q));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sign));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(q0));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sra_div));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(mulh));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhs_sll));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhs64));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced SRem with magic number division for " << rhs_value_abs
                                      << " (magic=" << magic << ", shift=" << shift << ") in " << bb->getName() << "\n";
                        }
                    }
                }
            }
        }
    }
    return changed;
}