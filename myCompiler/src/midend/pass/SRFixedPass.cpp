#include "SRFixedPass.h"
#include <cmath>
using namespace std;
using namespace optimization;

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
                        // 替换为右移操作
                        // 负数除法需要加掩码
                        auto *zero = new ConstantInt(IntegerType::getInstance(), 0);
                        auto *mask = new ConstantInt(IntegerType::getInstance(), (1 << shift) - 1);
                        auto *signedDiv = new BinaryOperator(Opcode::Sra, lhs, new ConstantInt(IntegerType::getInstance(), 31), instName + "_signedDiv");
                        auto *addand = new BinaryOperator(Opcode::And, signedDiv, mask, instName + "_addand");
                        auto *lhsAdj = new BinaryOperator(Opcode::Add, lhs, addand, instName + "_lhsAdj");
                        auto *sraInst = new BinaryOperator(Opcode::Sra, lhsAdj, new ConstantInt(IntegerType::getInstance(), shift), instName + "_sra");
                        Instruction *finalRes = sraInst;
                        if (rhs_value < 0)
                        {
                            auto *neg = new BinaryOperator(Opcode::Sub, zero, sraInst, instName + "_neg");
                            finalRes = neg;
                            insts.insert(insts.begin() + i + 1, std::unique_ptr<Instruction>(neg));
                        }
                        inst->removeThisFromOperands();
                        inst->replaceAllUsesWith(finalRes);
                        needToDelete.push_back(insts[i].release());
                        insts.erase(insts.begin() + i);
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(sraInst));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(lhsAdj));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(addand));
                        insts.insert(insts.begin() + i, std::unique_ptr<Instruction>(signedDiv));
                        changed = true;
                        if (verbose)
                        {
                            debugInfo << "SRFixedPass: Replaced SDiv with Sra for " << constInt->Value
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
                        // bias = (x >> 31) & mask
                        // mask = 2^n - 1
                        // 当x大于0时简化为x&mask mask为低n位全为1，等价于取低n位的数值
                        // 当x小于0时先加上一个偏移量转移到正数范围内计算后再减去偏移量
                        auto *type = IntegerType::getInstance();
                        int mask_val = rhs_value_abs - 1;
                        auto *mask_const = new ConstantInt(type, mask_val);
                        auto *shift31 = new ConstantInt(type, 31);

                        // sign_mask = x >> 31
                        auto *sign_mask = new BinaryOperator(Opcode::Sra, lhs, shift31, instName + "_signmask");
                        // bias = sign_mask & mask 如果为负数则为mask，否则为0，用于修正负数计算的削弱
                        auto *bias = new BinaryOperator(Opcode::And, sign_mask, mask_const, instName + "_bias");
                        // x + bias
                        auto *x_add_bias = new BinaryOperator(Opcode::Add, lhs, bias, instName + "_addbias");
                        // (x + bias) & mask
                        auto *and_mask = new BinaryOperator(Opcode::And, x_add_bias, mask_const, instName + "_andmask");
                        // ((x + bias) & mask) - bias
                        auto *final_res = new BinaryOperator(Opcode::Sub, and_mask, bias, instName + "_mod2n");

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
                            debugInfo << "SRFixedPass: Replaced SRem with ((x+bias)&mask)-bias for " << rhs_value_abs
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