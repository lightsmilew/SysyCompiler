#include "StrengthReductionPass.h"
using namespace std;
using namespace optimization;

std::pair<int64_t, int> StrengthReductionPass::compute_magic(int32_t d)
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
bool StrengthReductionPass::runOnFunction(Function *func)
{
    bool changed = false;
    for (auto &bb : func->getBasicBlocks())
    {
        auto &insts = bb->getInstructions();
        // 用下标逆序遍历，避免迭代器失效
        for (int i = insts.size() - 1; i >= 0; --i)
        {
            Instruction *inst = insts[i].get();
            if (inst && inst->getOpcode() == Opcode::Mul)
            {
                Value *lhs = inst->getOperands()[0];
                Value *rhs = inst->getOperands()[1];
                if (auto *constInt = dynamic_cast<ConstantInt *>(rhs))
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
                            debugInfo << "Strength Reduction: Replaced Mul with 0 in " << bb->getName() << "\n";
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
                        auto *shlInst = new BinaryOperator(Opcode::Sll, lhs, new ConstantInt(IntegerType::getInstance(), shift), inst->getName() + "_sll");
                        inst->removeThisFromOperands();
                        inst->replaceAllUsesWith(shlInst);
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(shlInst));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "Strength Reduction: Replaced Mul with Sll for " << constInt->Value
                                      << " in " << bb->getName() << "\n";
                        }
                    }
                    // 不需要处理不是2的幂次方情况，因为会降低性能
                }
            }
            else if (inst && inst->getOpcode() == Opcode::SDiv)
            {
                Value *lhs = inst->getOperands()[0];
                Value *rhs = inst->getOperands()[1];
                if (auto *constInt = dynamic_cast<ConstantInt *>(rhs))
                {
                    int d = constInt->Value;
                    if (d != 0 && (d & (d - 1)) == 0)
                    {
                        // 2的幂，直接算数右移
                        int shift = 0;
                        int val = d;
                        while (val > 1)
                        {
                            val >>= 1;
                            shift++;
                        }
                        // 替换为右移操作
                        // 负数除法需要加掩码
                        auto *zero = new ConstantInt(IntegerType::getInstance(), 0);
                        auto *mask = new ConstantInt(IntegerType::getInstance(), (1 << shift) - 1);
                        auto *signedDiv = new BinaryOperator(Opcode::Sra, lhs, new ConstantInt(IntegerType::getInstance(), 31), inst->getName() + "_signedDiv");
                        auto *addand = new BinaryOperator(Opcode::And, signedDiv, mask, inst->getName() + "_addand");
                        auto *lhsAdj = new BinaryOperator(Opcode::Add, lhs, addand, inst->getName() + "_lhsAdj");
                        auto *sraInst = new BinaryOperator(Opcode::Sra, lhsAdj, new ConstantInt(IntegerType::getInstance(), shift), inst->getName() + "_sra");
                        inst->removeThisFromOperands();
                        inst->replaceAllUsesWith(sraInst);
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sraInst));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhsAdj));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(addand));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(signedDiv));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "Strength Reduction: Replaced SDiv with Sra for " << constInt->Value
                                      << " in " << bb->getName() << "\n";
                        }
                    }
                    // 只处理常数且不是0、1、-1、2的幂
                    if (d != 0 && d != 1 && d != -1 && (d & (d - 1)) != 0)
                    {
                        // 计算magic和shift
                        auto [magic, shift] = compute_magic(d);
                        auto *type = IntegerType::getInstance();
                        // 1. 扩展lhs为64位
                        auto *lhs64 = new CastInst(Opcode::Sext, lhs, LongType::getInstance(), lhs->getName() + "_to64");
                        // 2. 乘以magic
                        auto *magic_const = new ConstantLong(LongType::getInstance(), magic);
                        auto *mul = new BinaryOperator(Opcode::Muld, lhs64, magic_const, inst->getName() + "_mulmagic");
                        // 3. 取高32位（算术右移32+shift位）
                        auto *shiftnum = new ConstantLong(LongType::getInstance(), 32 + shift);
                        auto *sra_div = new BinaryOperator(Opcode::Srad, mul, shiftnum, inst->getName() + "_sra_div");
                        // 5. 截断回32位
                        auto *q0 = new CastInst(Opcode::Trunc, sra_div, type, inst->getName() + "_divmagic");
                        // 6. 修正：被除数为负时，结果加1
                        // sign = (lhs < 0) ? 1 : 0
                        auto *zero = new ConstantInt(type, 0);
                        auto *sign = new ICmpInst(ICmpInst::ICMP_SLT, lhs, zero, inst->getName() + "_divsign");
                        // q = q0 + sign
                        auto *q = new BinaryOperator(Opcode::Add, q0, sign, inst->getName() + "_divmagic_fix");

                        inst->replaceAllUsesWith(q);
                        inst->removeThisFromOperands();
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        // 按顺序插入新指令
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(q));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sign));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(q0));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sra_div));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(mul));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhs64));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "Strength Reduction: Replaced SDiv with magic number+sign fix for " << d
                                      << " (magic=" << magic << ", shift=" << shift << ") in " << bb->getName() << "\n";
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
                    int d = constInt->Value;
                    if (d > 0 && (d & (d - 1)) == 0)
                    {
                        // x % 2^n == ((x + bias) & mask) - bias
                        // bias = (x >> 31) & mask
                        int n = 0, tmp = d;
                        while (tmp > 1)
                        {
                            tmp >>= 1;
                            n++;
                        }
                        auto *type = IntegerType::getInstance();
                        int mask_val = d - 1;
                        auto *mask_const = new ConstantInt(type, mask_val);
                        auto *shift31 = new ConstantInt(type, 31);

                        // sign_mask = x >> 31
                        auto *sign_mask = new BinaryOperator(Opcode::Sra, lhs, shift31, inst->getName() + "_signmask");
                        // bias = sign_mask & mask
                        auto *bias = new BinaryOperator(Opcode::And, sign_mask, mask_const, inst->getName() + "_bias");
                        // x + bias
                        auto *x_add_bias = new BinaryOperator(Opcode::Add, lhs, bias, inst->getName() + "_addbias");
                        // (x + bias) & mask
                        auto *and_mask = new BinaryOperator(Opcode::And, x_add_bias, mask_const, inst->getName() + "_andmask");
                        // ((x + bias) & mask) - bias
                        auto *final_res = new BinaryOperator(Opcode::Sub, and_mask, bias, inst->getName() + "_mod2n");

                        inst->removeThisFromOperands();
                        inst->replaceAllUsesWith(final_res);
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        // 按顺序插入新指令
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(final_res));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(and_mask));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(x_add_bias));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(bias));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sign_mask));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "Strength Reduction: Replaced SRem with ((x+bias)&mask)-bias for " << d
                                      << " in " << bb->getName() << "\n";
                        }
                    }
                    if (d > 0 && (d & (d - 1)) != 0)
                    {
                        // 1. 计算magic和shift
                        auto [magic, shift] = compute_magic(d);
                        auto *type = IntegerType::getInstance();
                        // 2. 扩展lhs为64位
                        auto *lhs64 = new CastInst(Opcode::Sext, lhs, LongType::getInstance(), lhs->getName() + "_to64");
                        // 3. 乘以magic
                        auto *magic_const = new ConstantLong(LongType::getInstance(), magic);
                        auto *mul = new BinaryOperator(Opcode::Muld, lhs64, magic_const, inst->getName() + "_mulmagic");
                        // 4. 取高32位（算术右移32+shift位）
                        auto *shiftnum = new ConstantLong(LongType::getInstance(), 32 + shift);
                        auto *sra_div = new BinaryOperator(Opcode::Srad, mul, shiftnum, inst->getName() + "_sra_div");
                        // 5. 截断回32位
                        auto *q0 = new CastInst(Opcode::Trunc, sra_div, type, inst->getName() + "_divmagic");
                        // 6. 修正：被除数为负时，结果加1
                        auto *zero = new ConstantInt(type, 0);
                        auto *sign = new ICmpInst(ICmpInst::ICMP_SLT, lhs, zero, inst->getName() + "_divsign");
                        auto *q = new BinaryOperator(Opcode::Add, q0, sign, inst->getName() + "_divmagic_fix");
                        // 7. rem = lhs - q * d
                        auto *d_const = new ConstantInt(type, d);
                        auto *q_mul_d = new BinaryOperator(Opcode::Mul, q, d_const, inst->getName() + "_qmul");
                        auto *rem = new BinaryOperator(Opcode::Sub, lhs, q_mul_d, inst->getName() + "_remmagic");

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
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(mul));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhs64));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "Strength Reduction: Replaced SRem with magic number division for " << d
                                      << " (magic=" << magic << ", shift=" << shift << ") in " << bb->getName() << "\n";
                        }
                    }
                    if (d < 0)
                    {
                        d = 0 - d;
                        if ((d & (d - 1)) == 0)
                            continue;
                        // 1. 计算magic和shift
                        auto [magic, shift] = compute_magic(d);
                        auto *type = IntegerType::getInstance();
                        // 2. 扩展lhs为64位
                        auto *lhs64 = new CastInst(Opcode::Sext, lhs, LongType::getInstance(), lhs->getName() + "_to64");
                        // 3. 乘以magic
                        auto *magic_const = new ConstantLong(LongType::getInstance(), magic);
                        auto *mul = new BinaryOperator(Opcode::Muld, lhs64, magic_const, inst->getName() + "_mulmagic");
                        // 4. 取高32位（算术右移32+shift位）
                        auto *shiftnum = new ConstantLong(LongType::getInstance(), 32 + shift);
                        auto *sra_div = new BinaryOperator(Opcode::Srad, mul, shiftnum, inst->getName() + "_sra_div");
                        // 5. 截断回32位
                        auto *q0 = new CastInst(Opcode::Trunc, sra_div, type, inst->getName() + "_divmagic");
                        // 6. 修正：被除数为负时，结果加1
                        auto *zero = new ConstantInt(type, 0);
                        auto *sign = new ICmpInst(ICmpInst::ICMP_SLT, lhs, zero, inst->getName() + "_divsign");
                        auto *q = new BinaryOperator(Opcode::Add, q0, sign, inst->getName() + "_divmagic_fix");
                        // 7. rem = lhs - q * d
                        auto *d_const = new ConstantInt(type, d);
                        auto *q_mul_d = new BinaryOperator(Opcode::Mul, q, d_const, inst->getName() + "_qmul");
                        auto *rem = new BinaryOperator(Opcode::Sub, lhs, q_mul_d, inst->getName() + "_remmagic");

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
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(mul));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhs64));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "Strength Reduction: Replaced SRem with magic number division for " << d
                                      << " (magic=" << magic << ", shift=" << shift << ") in " << bb->getName() << "\n";
                        }
                    }
                }
            }
        }
    }
    return changed;
}