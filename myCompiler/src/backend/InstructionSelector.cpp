#include "InstructionSelector.h"
#include "common/CompilerConfig.h"
#include <algorithm>
#include <map>
using namespace RISCV;

namespace
{
constexpr int kRvvAllocaZeroMinBytes = 32;
}
// RISC-V 参数寄存器映射常量
const vector<RISCVRegister::PhysicalReg> INT_PARAM_REGS = {
    RISCVRegister::PhysicalReg::A0, RISCVRegister::PhysicalReg::A1,
    RISCVRegister::PhysicalReg::A2, RISCVRegister::PhysicalReg::A3,
    RISCVRegister::PhysicalReg::A4, RISCVRegister::PhysicalReg::A5,
    RISCVRegister::PhysicalReg::A6, RISCVRegister::PhysicalReg::A7};

const vector<RISCVRegister::PhysicalReg> FLOAT_PARAM_REGS = {
    RISCVRegister::PhysicalReg::FA0, RISCVRegister::PhysicalReg::FA1,
    RISCVRegister::PhysicalReg::FA2, RISCVRegister::PhysicalReg::FA3,
    RISCVRegister::PhysicalReg::FA4, RISCVRegister::PhysicalReg::FA5,
    RISCVRegister::PhysicalReg::FA6, RISCVRegister::PhysicalReg::FA7};

// RISC-V 临时寄存器映射常量
const vector<RISCVRegister::PhysicalReg> INT_TEMP_REGS = {
    RISCVRegister::PhysicalReg::T0, RISCVRegister::PhysicalReg::T1,
    RISCVRegister::PhysicalReg::T2, RISCVRegister::PhysicalReg::T3,
    RISCVRegister::PhysicalReg::T4, RISCVRegister::PhysicalReg::T5,
    RISCVRegister::PhysicalReg::T6};

const vector<RISCVRegister::PhysicalReg> FLOAT_TEMP_REGS = {
    RISCVRegister::PhysicalReg::FT0, RISCVRegister::PhysicalReg::FT1,
    RISCVRegister::PhysicalReg::FT2, RISCVRegister::PhysicalReg::FT3,
    RISCVRegister::PhysicalReg::FT4, RISCVRegister::PhysicalReg::FT5,
    RISCVRegister::PhysicalReg::FT6, RISCVRegister::PhysicalReg::FT7,
    RISCVRegister::PhysicalReg::FT8, RISCVRegister::PhysicalReg::FT9,
    RISCVRegister::PhysicalReg::FT10, RISCVRegister::PhysicalReg::FT11};

namespace
{
    Value *stripValueWrappers(Value *value)
    {
        while (value)
        {
            if (auto *copy = dynamic_cast<CopyInst *>(value))
            {
                value = copy->getSource();
                continue;
            }
            break;
        }
        return value;
    }

    bool isZeroStoreValue(Value *value)
    {
        value = stripValueWrappers(value);
        if (auto *intValue = dynamic_cast<ConstantInt *>(value))
        {
            return intValue->Value == 0;
        }
        if (auto *longValue = dynamic_cast<ConstantLong *>(value))
        {
            return longValue->Value == 0;
        }
        return false;
    }

    bool isBooleanCompareCondition(Value *cond)
    {
        cond = stripValueWrappers(cond);
        if (!cond)
        {
            return false;
        }
        auto *inst = dynamic_cast<Instruction *>(cond);
        if (!inst)
        {
            return false;
        }
        return inst->getOpcode() == Opcode::ICmp || inst->getOpcode() == Opcode::FCmp;
    }

    bool isConstantIntZero(Value *value)
    {
        if (auto *c = dynamic_cast<ConstantInt *>(value))
        {
            return c->Value == 0;
        }
        return false;
    }

    struct MagicDivSignFixAdd
    {
        CastInst *trunc;
        ICmpInst *signIcmp;
        Value *sradResult;
        Value *dividend;
    };

    bool tryMatchMagicDivSignFixAdd(BinaryOperator *add, MagicDivSignFixAdd &out)
    {
        if (!add || add->Op != Opcode::Add)
        {
            return false;
        }

        CastInst *trunc = nullptr;
        ICmpInst *signIcmp = nullptr;

        if (auto *t = dynamic_cast<CastInst *>(add->getLHS()))
        {
            if (t->Op == Opcode::Trunc)
            {
                trunc = t;
            }
        }
        if (auto *i = dynamic_cast<ICmpInst *>(add->getLHS()))
        {
            signIcmp = i;
        }
        if (auto *t = dynamic_cast<CastInst *>(add->getRHS()))
        {
            if (t->Op == Opcode::Trunc)
            {
                trunc = t;
            }
        }
        if (auto *i = dynamic_cast<ICmpInst *>(add->getRHS()))
        {
            signIcmp = i;
        }

        if (!trunc || !signIcmp || signIcmp->Pred != ICmpInst::ICMP_SLT ||
            !isConstantIntZero(signIcmp->getRHS()))
        {
            return false;
        }

        Value *operand = trunc->getOperand();
        if (!operand || !operand->getType()->isLongTy() || trunc->getType()->isLongTy())
        {
            return false;
        }

        if (trunc->getUsers().size() != 1 || signIcmp->getUsers().size() != 1)
        {
            return false;
        }

        out.trunc = trunc;
        out.signIcmp = signIcmp;
        out.sradResult = operand;
        out.dividend = signIcmp->getLHS();
        return true;
    }

    bool isTruncFusableWithMagicDivSignFix(CastInst *trunc)
    {
        if (!trunc || trunc->Op != Opcode::Trunc || trunc->getUsers().size() != 1)
        {
            return false;
        }
        auto *add = dynamic_cast<BinaryOperator *>(trunc->getUsers()[0]);
        if (!add)
        {
            return false;
        }
        MagicDivSignFixAdd fusion;
        return tryMatchMagicDivSignFixAdd(add, fusion) && fusion.trunc == trunc;
    }

    bool isIcmpFusableWithMagicDivSignFix(ICmpInst *icmp)
    {
        if (!icmp || icmp->getUsers().size() != 1)
        {
            return false;
        }
        auto *add = dynamic_cast<BinaryOperator *>(icmp->getUsers()[0]);
        if (!add)
        {
            return false;
        }
        MagicDivSignFixAdd fusion;
        return tryMatchMagicDivSignFixAdd(add, fusion) && fusion.signIcmp == icmp;
    }
}

void InstructionSelector::selectInstructions(shared_ptr<RISCVFunction> func, Function *irFunc)
{
    currentFunc = func;
    irFunction = irFunc;

    // 创建虚拟寄存器映射表
    registerMap.clear();
    tempRegisters.clear();
    globalVarMap.clear();
    MoveArgMap.clear();
    pendingAllocaInits.clear();
    pendingAllocaInitBB = nullptr;
    allocaExtraByteOffset.clear();

    buildControlFlowGraph();

    currentBB = func->getBasicBlocks().front(); // 从第一个基本块开始处理
    DealArgumentsInStart();

    // 处理函数体
    for (size_t i = 0; i < irFunc->BasicBlocks.size(); ++i)
    {
        auto irBB = irFunc->BasicBlocks[i].get();
        auto riscvBB = func->getBasicBlock(irBB->getName());
        currentBB = riscvBB;

        // 遍历基本块中的所有指令
        for (auto &irInstr : irBB->Instructions)
        {
            visitInstruction(irInstr.get());
        }
        flushPendingAllocaInits();
    }
}

// 当基本块中使用alloca指令访问函数参数时，我应该将该块空间与寄存器联合起来
void InstructionSelector::visitInstruction(Instruction *inst)
{
    if (!pendingAllocaInits.empty() && pendingAllocaInitBB == currentBB &&
        inst->getOpcode() != Opcode::Alloca)
    {
        flushPendingAllocaInits();
    }

    switch (inst->Op)
    {
    case Opcode::Add:
    case Opcode::Sub:
    case Opcode::Mul:
    case Opcode::SDiv:
    case Opcode::SRem:
    case Opcode::FAdd:
    case Opcode::FSub:
    case Opcode::FMul:
    case Opcode::FDiv:
    case Opcode::Sll:
    case Opcode::Sra:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Xor:
    case Opcode::Muld:
    case Opcode::Mulhd:
    case Opcode::Addd:
    case Opcode::Slld:
    case Opcode::Srad:
        if (auto binOp = dynamic_cast<BinaryOperator *>(inst))
        {
            visitBinaryOp(binOp);
        }
        break;
    case Opcode::Load:
        if (auto loadInst = dynamic_cast<LoadInst *>(inst))
        {
            visitLoadInst(loadInst);
        }
        break;
    case Opcode::Store:
    case Opcode::Stored:
        if (auto storeInst = dynamic_cast<StoreInst *>(inst))
        {
            visitStoreInst(storeInst);
        }
        break;
    case Opcode::PackI64:
        if (auto packInst = dynamic_cast<PackI64Inst *>(inst))
        {
            visitPackI64Inst(packInst);
        }
        break;
    case Opcode::Call:
        if (auto callInst = dynamic_cast<CallInst *>(inst))
        {
            visitCallInst(callInst);
        }
        break;
    case Opcode::Ret:
        if (auto retInst = dynamic_cast<ReturnInst *>(inst))
        {
            visitReturnInst(retInst);
        }
        break;
    case Opcode::Br:
        if (auto brInst = dynamic_cast<BranchInst *>(inst))
        {
            visitBranchInst(brInst);
        }
        break;
    case Opcode::Alloca:
        if (auto allocaInst = dynamic_cast<AllocaInst *>(inst))
        {
            visitAllocaInst(allocaInst);
        }
        break;
    case Opcode::GetElementPtr:
        if (auto gepInst = dynamic_cast<GetElementPtrInst *>(inst))
        {
            visitElementPtrInst(gepInst);
        }
        break;
    case Opcode::ICmp:
        if (auto icmpInst = dynamic_cast<ICmpInst *>(inst))
        {
            visitICmpInst(icmpInst);
        }
        break;
    case Opcode::FCmp:
        if (auto fcmpInst = dynamic_cast<FCmpInst *>(inst))
        {
            visitFCmpInst(fcmpInst);
        }
        break;
    case Opcode::SIToFP:
        if (auto castInst = dynamic_cast<CastInst *>(inst))
        {
            visitSIToFPInst(castInst);
        }
        break;
    case Opcode::FPToSI:
        if (auto castInst = dynamic_cast<CastInst *>(inst))
        {
            visitFPToSIInst(castInst);
        }
        break;

    case Opcode::Copy:
        if (auto copyInst = dynamic_cast<CopyInst *>(inst))
        {
            visitCopyInst(copyInst);
        }
        break;
    case Opcode::BitCast:
        if (auto bitCastInst = dynamic_cast<CastInst *>(inst))
        {
            visitBitCastInst(bitCastInst);
        }
        break;
    case Opcode::Sext:
        if (auto castInst = dynamic_cast<CastInst *>(inst))
        {
            visitSExtInst(castInst);
        }
        break;
    case Opcode::Trunc:
        if (auto castInst = dynamic_cast<CastInst *>(inst))
        {
            visitTruncInst(castInst);
        }
        break;
    case Opcode::Xnor:
        if (auto binOp = dynamic_cast<BinaryOperator *>(inst))
        {
            visitXnorInst(binOp);
        }
        break;
    case Opcode::Select:
        if (auto selectInst = dynamic_cast<SelectInst *>(inst))
        {
            visitSelectInst(selectInst);
        }
        break;
    case Opcode::VecSetVl:
        if (auto *vs = dynamic_cast<VecSetVlInst *>(inst))
            visitVecSetVlInst(vs);
        break;
    case Opcode::VecLoad:
        if (auto *vl = dynamic_cast<VecLoadInst *>(inst))
            visitVecLoadInst(vl);
        break;
    case Opcode::VecStore:
        if (auto *vs = dynamic_cast<VecStoreInst *>(inst))
            visitVecStoreInst(vs);
        break;
    case Opcode::VecStridedLoad:
        if (auto *vs = dynamic_cast<VecStridedLoadInst *>(inst))
            visitVecStridedLoadInst(vs);
        break;
    case Opcode::VecStridedStore:
        if (auto *vs = dynamic_cast<VecStridedStoreInst *>(inst))
            visitVecStridedStoreInst(vs);
        break;
    case Opcode::VecSplat:
        if (auto *vs = dynamic_cast<VecSplatInst *>(inst))
            visitVecSplatInst(vs);
        break;
    case Opcode::VecAdd:
    case Opcode::VecSub:
    case Opcode::VecMul:
    case Opcode::VecSll:
    case Opcode::VecSrl:
    case Opcode::VecSra:
    case Opcode::VecMax:
    case Opcode::VecMin:
    case Opcode::VecDiv:
    case Opcode::VecRem:
        if (auto *vb = dynamic_cast<VecBinaryInst *>(inst))
            visitVecBinaryInst(vb);
        break;
    case Opcode::VecVid:
        if (auto *vv = dynamic_cast<VecVidInst *>(inst))
            visitVecVidInst(vv);
        break;
    case Opcode::VecReduceAdd:
        if (auto *vr = dynamic_cast<VecReduceAddInst *>(inst))
            visitVecReduceAddInst(vr);
        break;
    case Opcode::VecReduceMax:
        if (auto *vr = dynamic_cast<VecReduceMaxInst *>(inst))
            visitVecReduceMaxInst(vr);
        break;
    case Opcode::VecReduceMin:
        if (auto *vr = dynamic_cast<VecReduceMinInst *>(inst))
            visitVecReduceMinInst(vr);
        break;
    default:
        // 其他指令暂时忽略
        break;
    }
}

bool InstructionSelector::isValidImmediate(int64_t value, Opcode opcode)
{
    switch (opcode)
    {
    case Opcode::Add:
    case Opcode::Addd:
    case Opcode::Sub:
    case Opcode::And:
    case Opcode::Or:
    case Opcode::Xor:
        return value >= -2048 && value <= 2047;

    case Opcode::Sll:
    case Opcode::Sra:
        return value >= 0 && value <= 31; // 32位移位

    case Opcode::Slld:
    case Opcode::Srad:
        return value >= 0 && value <= 63; // 64位移位

    default:
        return false;
    }
}

void InstructionSelector::visitBinaryOp(BinaryOperator *inst)
{
    auto *lhsConst = dynamic_cast<ConstantInt *>(inst->getLHS());
    auto *rhsConst = dynamic_cast<ConstantInt *>(inst->getRHS());

    auto destReg = getOrCreateVirtualReg(inst->getDest());

    // trunc i64 + icmp slt(dividend,0) + add → slti + addw（magic div 符号修正）
    if (inst->Op == Opcode::Add)
    {
        MagicDivSignFixAdd fusion;
        if (tryMatchMagicDivSignFixAdd(inst, fusion))
        {
            auto sradReg = getOrCreateVirtualReg(fusion.sradResult);
            auto dividendReg = getOrCreateVirtualReg(fusion.dividend);
            auto signReg = getTempReg();
            currentBB->addInstruction(
                RISCVInstruction::createIType(RISCVOpcode::SLTI, signReg, dividendReg, 0));
            currentBB->addInstruction(
                RISCVInstruction::createRType(RISCVOpcode::ADDW, destReg, sradReg, signReg));
            return;
        }
    }

    // 尝试使用立即数形式
    // 1. 处理可交换的运算 (Add, Addd, And, Or, Xor, ...)
    int64_t immVal = 0;
    Value *varOperand = nullptr;
    bool hasImmOperand = false;
    if (rhsConst)
    {
        immVal = rhsConst->Value;
        varOperand = inst->getLHS();
        hasImmOperand = true;
    }
    else if (lhsConst)
    {
        immVal = lhsConst->Value;
        varOperand = inst->getRHS();
        hasImmOperand = true;
    }
    else if (auto *rhsLong = dynamic_cast<ConstantLong *>(inst->getRHS()))
    {
        immVal = rhsLong->Value;
        varOperand = inst->getLHS();
        hasImmOperand = true;
    }
    else if (auto *lhsLong = dynamic_cast<ConstantLong *>(inst->getLHS()))
    {
        immVal = lhsLong->Value;
        varOperand = inst->getRHS();
        hasImmOperand = true;
    }

    if ((inst->Op == Opcode::Add || inst->Op == Opcode::Addd || inst->Op == Opcode::And ||
         inst->Op == Opcode::Or || inst->Op == Opcode::Xor ||
         inst->Op == Opcode::Mulhd || inst->Op == Opcode::Muld ||
         inst->Op == Opcode::Mul) &&
        hasImmOperand)
    {
        if (isValidImmediate(immVal, inst->Op))
        {
            auto varReg = getOrCreateVirtualReg(varOperand);
            RISCVOpcode opcode;
            switch (inst->Op)
            {
            case Opcode::Add:
                opcode = RISCVOpcode::ADDIW;
                break;
            case Opcode::Addd:
                opcode = RISCVOpcode::ADDI;
                break;
            case Opcode::And:
                opcode = RISCVOpcode::ANDI;
                break;
            case Opcode::Or:
                opcode = RISCVOpcode::ORI;
                break;
            case Opcode::Xor:
                opcode = RISCVOpcode::XORI;
                break;
            case Opcode::Mulhd:
                opcode = RISCVOpcode::MULDH;
                break;
            case Opcode::Muld:
                opcode = RISCVOpcode::MUL;
                break;
            case Opcode::Mul:
                opcode = RISCVOpcode::MULW;
                break;
            default:
                break;
            }
            auto immInst = RISCVInstruction::createIType(opcode, destReg, varReg, immVal);
            currentBB->addInstruction(immInst);
            return;
        }
    }

    // 2. 处理减法 (只能处理 x - C 的形式)
    if (inst->Op == Opcode::Sub && rhsConst && isValidImmediate(-rhsConst->Value, inst->Op))
    {
        auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
        auto immInst = RISCVInstruction::createIType(RISCVOpcode::ADDIW, destReg, lhsReg, -rhsConst->Value);
        currentBB->addInstruction(immInst);
        return;
    }

    // 3. 处理移位 (只能是右操作数为常量；slld/srad 来自 SRFixed 时常为 ConstantLong)
    if (inst->Op == Opcode::Sll || inst->Op == Opcode::Sra || inst->Op == Opcode::Slld ||
        inst->Op == Opcode::Srad)
    {
        int64_t shiftAmt = 0;
        bool hasShiftConst = false;
        if (rhsConst)
        {
            shiftAmt = rhsConst->Value;
            hasShiftConst = true;
        }
        else if (auto *rhsLong = dynamic_cast<ConstantLong *>(inst->getRHS()))
        {
            shiftAmt = rhsLong->Value;
            hasShiftConst = true;
        }

        if (hasShiftConst && isValidImmediate(shiftAmt, inst->Op))
        {
            Value *shiftSrc = inst->getLHS();
            CastInst *fusedSext = nullptr;
            if (inst->Op == Opcode::Slld)
            {
                if (auto *sext = dynamic_cast<CastInst *>(shiftSrc))
                {
                    if (sext->Op == Opcode::Sext && sext->getUsers().size() == 1)
                    {
                        fusedSext = sext;
                        shiftSrc = sext->getOperand();
                    }
                }
            }

            auto lhsReg = getOrCreateVirtualReg(shiftSrc);
            RISCVOpcode opcode;
            switch (inst->Op)
            {
            case Opcode::Sll:
                opcode = RISCVOpcode::SLLIW;
                break;
            case Opcode::Sra:
                opcode = RISCVOpcode::SRAI;
                break; // 没有 SRAIW，使用 SRAI
            case Opcode::Slld:
                opcode = RISCVOpcode::SLLI;
                break;
            case Opcode::Srad:
                opcode = RISCVOpcode::SRAI;
                break;
            default:
                break;
            }
            auto immInst = RISCVInstruction::createIType(opcode, destReg, lhsReg, shiftAmt);
            currentBB->addInstruction(immInst);
            if (fusedSext)
            {
                registerMap[fusedSext->getName()] = destReg;
            }
            return;
        }
    }

    // 4. 回退到 R 型指令
    auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
    auto rhsReg = getOrCreateVirtualReg(inst->getRHS());

    RISCVOpcode opcode;
    switch (inst->Op)
    {
    case Opcode::Add:
        opcode = RISCVOpcode::ADDW;
        break;
    case Opcode::Addd:
        opcode = RISCVOpcode::ADD;
        break;
    case Opcode::Sub:
        opcode = RISCVOpcode::SUBW;
        break;
    case Opcode::Mul:
        opcode = RISCVOpcode::MULW;
        break;
    case Opcode::SDiv:
        opcode = inst->getType()->isLongTy() ? RISCVOpcode::DIV : RISCVOpcode::DIVW;
        break;
    case Opcode::SRem:
        opcode = inst->getType()->isLongTy() ? RISCVOpcode::REM : RISCVOpcode::REMW;
        break;
    case Opcode::FAdd:
        opcode = RISCVOpcode::FADD_S;
        break;
    case Opcode::FSub:
        opcode = RISCVOpcode::FSUB_S;
        break;
    case Opcode::FMul:
        opcode = RISCVOpcode::FMUL_S;
        break;
    case Opcode::FDiv:
        opcode = RISCVOpcode::FDIV_S;
        break;
    case Opcode::Sll:
        opcode = RISCVOpcode::SLLW;
        break;
    case Opcode::Sra:
        opcode = RISCVOpcode::SRAW;
        break;
    case Opcode::And:
        opcode = RISCVOpcode::AND;
        break;
    case Opcode::Or:
        opcode = RISCVOpcode::OR;
        break;
    case Opcode::Xor:
        opcode = RISCVOpcode::XOR;
        break;
    case Opcode::Muld:
        opcode = RISCVOpcode::MUL;
        break;
    case Opcode::Mulhd:
        opcode = RISCVOpcode::MULDH;
        break;
    case Opcode::Slld:
        opcode = RISCVOpcode::SLL;
        break;
    case Opcode::Srad:
        opcode = RISCVOpcode::SRA;
        break;
    default:
        return;
    }

    auto riscvInst = RISCVInstruction::createRType(opcode, destReg, lhsReg, rhsReg);
    currentBB->addInstruction(riscvInst);
}

void InstructionSelector::visitLoadInst(LoadInst *inst)
{
    auto ptrReg = materializeAllocaBase(inst->getPointer());
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    // 根据数据类型选择合适的加载指令
    RISCVOpcode loadOpcode = RISCVOpcode::LW;
    if (inst->getType()->isFloatTy())
    {
        loadOpcode = RISCVOpcode::FLW; // 浮点数加载
    }
    else if (inst->getType()->isPointerTy())
    {
        loadOpcode = RISCVOpcode::LD; // 指针加载
    }

    auto loadInst = RISCVInstruction::createIType(loadOpcode, destReg, ptrReg, 0);
    currentBB->addInstruction(loadInst);
}

shared_ptr<RISCVRegister> InstructionSelector::packI64FromHalves(Value *hi, Value *lo, bool isPhysical)
{
    (void)isPhysical;
    // 仅用虚拟临时寄存器，避免与外层归纳变量（常分配到 t0）同色后被覆盖
    auto materializeHalf = [this](Value *v) -> shared_ptr<RISCVRegister> {
        if (auto *c = dynamic_cast<ConstantInt *>(v))
            return LiInt(c->Value, false);
        return getOrCreateVirtualReg(v, false);
    };

    Value *hiStripped = stripValueWrappers(hi);
    Value *loStripped = stripValueWrappers(lo);
    const bool sameHalf = hiStripped && loStripped && hiStripped == loStripped;

    auto hiRaw = materializeHalf(hi);
    auto hiShifted = getTempReg();
    currentBB->addInstruction(
        RISCVInstruction::createIType(RISCVOpcode::SLLI, hiShifted, hiRaw, 32));

    auto loRaw = sameHalf ? hiRaw : materializeHalf(lo);
    // 0x00000000FFFFFFFF：低位与掩码做零扩展，掩码 li 可由后端 CSE 复用
    auto maskReg = LiLong(static_cast<int64_t>(0xFFFFFFFFu));
    auto loZext = getTempReg();
    currentBB->addInstruction(
        RISCVInstruction::createRType(RISCVOpcode::AND, loZext, loRaw, maskReg));

    auto destReg = getTempReg();
    currentBB->addInstruction(
        RISCVInstruction::createRType(RISCVOpcode::OR, destReg, hiShifted, loZext));
    return destReg;
}

void InstructionSelector::visitPackI64Inst(PackI64Inst *inst)
{
    auto destReg = getOrCreateVirtualReg(inst);
    if (isZeroStoreValue(inst->getHigh()) && isZeroStoreValue(inst->getLow()))
    {
        auto zeroReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO);
        auto copyInst = RISCVInstruction::createRType(RISCVOpcode::ADD, destReg, zeroReg, zeroReg);
        currentBB->addInstruction(copyInst);
        return;
    }
    auto valueReg = packI64FromHalves(inst->getHigh(), inst->getLow());
    if (valueReg != destReg)
    {
        auto moveInst = RISCVInstruction::createRType(RISCVOpcode::ADD, destReg, valueReg,
                                                      make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO));
        currentBB->addInstruction(moveInst);
    }
}

void InstructionSelector::visitStoreInst(StoreInst *inst)
{
    const bool isZero = isZeroStoreValue(inst->getValueToStore());

    shared_ptr<RISCVRegister> valueReg;
    if (!isZero)
    {
        valueReg = getOrCreateVirtualReg(inst->getValueToStore());
    }

    auto ptrReg = materializeAllocaBase(inst->getPointer());

    // 根据要存储的数据类型选择合适的存储指令
    RISCVOpcode storeOpcode = RISCVOpcode::SW;
    if (inst->getValueToStore()->getType()->isFloatTy())
    {
        storeOpcode = inst->getOpcode() == Opcode::Stored ? RISCVOpcode::FSD : RISCVOpcode::FSW;
    }
    else if (inst->getValueToStore()->getType()->isPointerTy() || inst->getOpcode() == Opcode::Stored)
    {
        storeOpcode = RISCVOpcode::SD;
    }

    if (isZero)
    {
        auto zeroReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO);
        auto storeInst = RISCVInstruction::createSType(storeOpcode, ptrReg, zeroReg, 0);
        currentBB->addInstruction(storeInst);
    }
    else
    {
        auto storeInst = RISCVInstruction::createSType(storeOpcode, ptrReg, valueReg, 0);
        currentBB->addInstruction(storeInst);
    }
}

shared_ptr<RISCVRegister> InstructionSelector::materializeAllocaBase(Value *ptr)
{
    auto baseAddr = getOrCreateVirtualReg(ptr);
    auto it = allocaExtraByteOffset.find(ptr->getName());
    if (it == allocaExtraByteOffset.end() || it->second == 0)
        return baseAddr;

    const int extra = it->second;
    auto adjusted = getTempReg(true);
    if (isValidImmediate(extra, Opcode::Add))
    {
        currentBB->addInstruction(RISCVInstruction::createIType(
            RISCVOpcode::ADDI, adjusted, baseAddr, extra));
    }
    else
    {
        auto offReg = LiInt(extra);
        currentBB->addInstruction(RISCVInstruction::createRType(
            RISCVOpcode::ADD, adjusted, baseAddr, offReg));
    }
    return adjusted;
}

shared_ptr<RISCVRegister> InstructionSelector::materializeCallArg(Value *arg)
{
    if (arg->getType()->isPointerTy())
        return materializeAllocaBase(arg);
    return getOrCreateVirtualReg(arg);
}

void InstructionSelector::visitAllocaInst(AllocaInst *inst)
{
    auto &stack = currentFunc->getStackFrame();
    stack.allocateValueSpace(inst->getName(), inst->getAllocatedSize()); // 分配空间
    int imm = stack.getValueOffset(inst->getName());

    const bool fuseWithPending = inst->getIsInitialized() && pendingAllocaInitBB == currentBB &&
                                 !pendingAllocaInits.empty();
    shared_ptr<RISCVRegister> addrReg;

    if (fuseWithPending)
    {
        const int relOff = imm - pendingAllocaInits[0].stackOffset;
        // 非主 alloca 的偏移已在 addrReg 中物化，勿再记入 map（避免 call/load 重复 +offset）
        allocaExtraByteOffset[inst->getName()] = 0;
        auto primaryBase = pendingAllocaInits[0].baseReg;
        if (relOff == 0)
        {
            addrReg = primaryBase;
        }
        else
        {
            // 必须用 sp+绝对 imm，以便 reallocOffsetForInstructions 按 alloca 名修正；
            // primaryBase+relOff 在 maxArgStackSize>0 时会少加传参区偏移。
            auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);
            addrReg = getOrCreateVirtualReg(inst->getDest());
            if (isValidImmediate(imm, Opcode::Add))
            {
                auto addInst = RISCVInstruction::createIType(
                    RISCVOpcode::ADDI, addrReg, spReg, imm);
                currentBB->addInstruction(addInst);
                currentFunc->addInstructionNeedReGetOffset(inst->getName(), addInst);
            }
            else
            {
                auto immReg = LiInt(imm, true);
                currentFunc->addInstructionNeedReGetOffset(inst->getName(), currentLiInstruction);
                auto addInst = RISCVInstruction::createRType(
                    RISCVOpcode::ADD, addrReg, spReg, immReg);
                currentBB->addInstruction(addInst);
            }
        }
        registerMap[inst->getName()] = addrReg;
        if (currentFunc)
            currentFunc->addIRValueMapping(inst->getName(), addrReg);
    }
    else
    {
        allocaExtraByteOffset[inst->getName()] = 0;
        auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);
        addrReg = getOrCreateVirtualReg(inst->getDest());
        if (imm == 0 || isValidImmediate(imm, Opcode::Add))
        {
            auto addInst =
                RISCVInstruction::createIType(RISCVOpcode::ADDI, addrReg, spReg, imm);
            currentBB->addInstruction(addInst);
            currentFunc->addInstructionNeedReGetOffset(inst->getName(), addInst);
        }
        else
        {
            auto immReg = LiInt(imm, true);
            currentFunc->addInstructionNeedReGetOffset(inst->getName(), currentLiInstruction);

            auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, addrReg, spReg, immReg);
            currentBB->addInstruction(addInst);
        }
        registerMap[inst->getName()] = addrReg;
        if (currentFunc)
            currentFunc->addIRValueMapping(inst->getName(), addrReg);
    }

    if (inst->getIsInitialized())
    {
        auto initBase = fuseWithPending ? pendingAllocaInits[0].baseReg : addrReg;
        enqueueAllocaInit(inst->getAllocatedSize(), imm, initBase);
    }
}

void InstructionSelector::enqueueAllocaInit(int size, int stackOffset,
                                            shared_ptr<RISCVRegister> baseReg)
{
    if (pendingAllocaInitBB && pendingAllocaInitBB != currentBB)
        flushPendingAllocaInits();
    pendingAllocaInitBB = currentBB;
    pendingAllocaInits.push_back({size, stackOffset, baseReg});
}

void InstructionSelector::flushPendingAllocaInits()
{
    if (pendingAllocaInits.empty() || !pendingAllocaInitBB)
        return;

    auto setupBB = pendingAllocaInitBB;
    if (setupBB->getSuccessors().empty())
    {
        pendingAllocaInits.clear();
        pendingAllocaInitBB = nullptr;
        return;
    }

    auto loopBB = setupBB->getSuccessors()[0];
    auto tailBB = loopBB->getSuccessors().empty() ? loopBB : loopBB->getSuccessors()[0];

    std::map<int, vector<PendingAllocaInit>> bySize;
    for (const auto &item : pendingAllocaInits)
        bySize[item.size].push_back(item);

    vector<vector<PendingAllocaInit>> groups;
    groups.reserve(bySize.size());
    for (auto &kv : bySize)
    {
        auto &group = kv.second;
        std::sort(group.begin(), group.end(),
                  [](const PendingAllocaInit &a, const PendingAllocaInit &b)
                  { return a.stackOffset < b.stackOffset; });
        groups.push_back(std::move(group));
    }

    auto prevBB = currentBB;
    currentBB = setupBB;
    shared_ptr<RISCVBasicBlock> curLoopBB = loopBB;
    for (size_t gi = 0; gi < groups.size(); ++gi)
    {
        if (gi > 0)
        {
            auto newLoopBB = make_shared<RISCVBasicBlock>(
                setupBB->getLabel() + "_alloca_init_" + std::to_string(gi), currentFunc);
            currentFunc->addBasicBlock(newLoopBB);
            curLoopBB->removeSuccessor(tailBB);
            curLoopBB->addSuccessor(newLoopBB);
            newLoopBB->addPredecessor(curLoopBB);
            curLoopBB = newLoopBB;
        }
        const bool isLast = (gi + 1 == groups.size());
        emitFusedAllocaZeroInit(groups[gi], setupBB, curLoopBB, isLast ? tailBB : nullptr);
    }
    if (curLoopBB != tailBB)
    {
        bool linked = false;
        for (const auto &succ : curLoopBB->getSuccessors())
        {
            if (succ == tailBB)
            {
                linked = true;
                break;
            }
        }
        if (!linked)
        {
            curLoopBB->addSuccessor(tailBB);
            tailBB->addPredecessor(curLoopBB);
        }
    }
    currentBB = prevBB;

    pendingAllocaInits.clear();
    pendingAllocaInitBB = nullptr;
}

void InstructionSelector::emitRvvAllocaZeroInit(const vector<PendingAllocaInit> &group,
                                                shared_ptr<RISCVBasicBlock> setupBB,
                                                shared_ptr<RISCVBasicBlock> loopBB,
                                                shared_ptr<RISCVBasicBlock> tailBB)
{
    if (group.empty() || !setupBB || !loopBB)
        return;

    const int byteSize = group[0].size;
    const int tailBytes = byteSize & 7;
    const int fullBytes = byteSize - tailBytes;
    const int elemCount = fullBytes / 4;
    if (elemCount <= 0)
        return;

    const int baseStackOffset = group[0].stackOffset;
    auto zeroReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO);
    auto zeroVec = make_shared<RISCVRegister>(RegisterType::VECTOR);
    auto primaryBase = group[0].baseReg;
    auto prevSelBB = currentBB;

    auto addLi = [&](shared_ptr<RISCVBasicBlock> bb, int imm)
    {
        auto reg = getTempReg(true);
        bb->addInstruction(RISCVInstruction::createPseudoLI(reg, imm));
        return reg;
    };

    auto emitPtrForItem = [&](shared_ptr<RISCVBasicBlock> bb, const PendingAllocaInit &item)
    {
        const int relOff = item.stackOffset - baseStackOffset;
        auto ptrReg = getTempReg(true);
        if (relOff == 0)
        {
            bb->addInstruction(
                RISCVInstruction::createPseudo(RISCVOpcode::MV, ptrReg, primaryBase));
        }
        else if (isValidImmediate(relOff, Opcode::Add))
        {
            bb->addInstruction(RISCVInstruction::createIType(
                RISCVOpcode::ADDI, ptrReg, primaryBase, relOff));
        }
        else
        {
            auto offReg = addLi(bb, relOff);
            bb->addInstruction(RISCVInstruction::createRType(
                RISCVOpcode::ADD, ptrReg, primaryBase, offReg));
        }
        return ptrReg;
    };

    auto countReg = addLi(setupBB, elemCount);
    vector<shared_ptr<RISCVRegister>> ptrRegs;
    ptrRegs.reserve(group.size());
    for (const auto &item : group)
        ptrRegs.push_back(emitPtrForItem(setupBB, item));

    auto vlReg = getTempReg(true);
    loopBB->addInstruction(RISCVInstruction::createVectorSetVl(vlReg, countReg));
    loopBB->addInstruction(RISCVInstruction::createVectorSplat(zeroVec, zeroReg));
    for (const auto &ptrReg : ptrRegs)
    {
        loopBB->addInstruction(
            RISCVInstruction::createVectorMemory(RISCVOpcode::VSE32_V, zeroVec, ptrReg));
    }
    loopBB->addInstruction(RISCVInstruction::createRType(
        RISCVOpcode::SUB, countReg, countReg, vlReg));
    auto advReg = getTempReg(true);
    loopBB->addInstruction(RISCVInstruction::createIType(
        RISCVOpcode::SLLI, advReg, vlReg, 2));
    for (const auto &ptrReg : ptrRegs)
    {
        loopBB->addInstruction(RISCVInstruction::createRType(
            RISCVOpcode::ADD, ptrReg, ptrReg, advReg));
    }
    loopBB->addInstruction(RISCVInstruction::createBType(
        RISCVOpcode::BNE, countReg, zeroReg, loopBB->getLabel()));

    if (tailBytes >= 4)
    {
        auto tailTarget = tailBB ? tailBB : loopBB;
        for (const auto &ptrReg : ptrRegs)
        {
            tailTarget->addInstruction(
                RISCVInstruction::createSType(RISCVOpcode::SW, ptrReg, zeroReg, 0));
        }
    }

    currentBB = prevSelBB;
}

void InstructionSelector::emitFusedAllocaZeroInit(const vector<PendingAllocaInit> &group,
                                                  shared_ptr<RISCVBasicBlock> setupBB,
                                                  shared_ptr<RISCVBasicBlock> loopBB,
                                                  shared_ptr<RISCVBasicBlock> tailBB)
{
    if (group.empty() || !setupBB || !loopBB)
        return;

    const int byteSize = group[0].size;
    const int tailBytes = byteSize & 7;
    const int fullBytes = byteSize - tailBytes;
    if (CompilerConfig::enableRVV && byteSize >= kRvvAllocaZeroMinBytes && fullBytes >= 16)
    {
        emitRvvAllocaZeroInit(group, setupBB, loopBB, tailBB);
        return;
    }

    auto zeroReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO);

    const int loopLimitDelta = byteSize - (tailBytes ? 8 : 0);

    if (fullBytes > 0)
    {
        const int baseStackOffset = group[0].stackOffset;
        auto walkReg = getTempReg(true);
        setupBB->addInstruction(
            RISCVInstruction::createPseudo(RISCVOpcode::MV, walkReg, group[0].baseReg));

        auto limitReg = getTempReg(true);
        if (isValidImmediate(loopLimitDelta, Opcode::Add))
        {
            setupBB->addInstruction(RISCVInstruction::createIType(
                RISCVOpcode::ADDI, limitReg, walkReg, loopLimitDelta));
        }
        else
        {
            auto deltaReg = LiInt(loopLimitDelta);
            setupBB->addInstruction(RISCVInstruction::createRType(
                RISCVOpcode::ADD, limitReg, walkReg, deltaReg));
        }

        for (const auto &item : group)
        {
            const int relOff = item.stackOffset - baseStackOffset;
            loopBB->addInstruction(
                RISCVInstruction::createSType(RISCVOpcode::SD, walkReg, zeroReg, relOff));
        }

        loopBB->addInstruction(RISCVInstruction::createIType(
            RISCVOpcode::ADDI, walkReg, walkReg, 8));

        loopBB->addInstruction(RISCVInstruction::createBType(
            RISCVOpcode::BLT, walkReg, limitReg, loopBB->getLabel()));

        if (tailBytes != 0 && tailBB)
        {
            for (const auto &item : group)
            {
                const int relOff = item.stackOffset - baseStackOffset;
                tailBB->addInstruction(
                    RISCVInstruction::createSType(RISCVOpcode::SW, walkReg, zeroReg, relOff));
            }
        }
    }
    else if (tailBytes >= 4 && tailBB)
    {
        const int baseStackOffset = group[0].stackOffset;
        auto walkReg = getTempReg(true);
        setupBB->addInstruction(
            RISCVInstruction::createPseudo(RISCVOpcode::MV, walkReg, group[0].baseReg));
        for (const auto &item : group)
        {
            const int relOff = item.stackOffset - baseStackOffset;
            tailBB->addInstruction(
                RISCVInstruction::createSType(RISCVOpcode::SW, walkReg, zeroReg, relOff));
        }
    }
}

void InstructionSelector::visitElementPtrInst(GetElementPtrInst *inst)
{
    auto baseAddr = materializeAllocaBase(inst->getPointerOperand());
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    auto indices = inst->getIndices();
    auto stridePtr = inst->getArrayStride();

    // 检查是否所有索引除了第一个都是0
    // 如果是，则可以直接使用第一个索引的值作为偏移量
    bool allIndicesExceptFirstAreZero = true;
    for (size_t i = 1; i < indices.size(); ++i)
    {
        if (auto constInt = dynamic_cast<ConstantInt *>(indices[i]))
        {
            if (constInt->Value != 0)
            {
                allIndicesExceptFirstAreZero = false;
                break;
            }
        }
        else
        {
            // 如果不是常量，无法静态判断，需运行时判断
            allIndicesExceptFirstAreZero = false;
            break;
        }
    }

    if (allIndicesExceptFirstAreZero)
    {
        if (dynamic_cast<ConstantInt *>(indices[0]))
        {
            // 直接使用第一个索引的值乘步长乘以4作为偏移量
            int offset = dynamic_cast<ConstantInt *>(indices[0])->Value;
            if (offset == 0)
            {
                // 如果第一个索引也是0，则直接返回baseAddr
                auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, destReg, baseAddr);
                currentBB->addInstruction(mvInst);
                return;
            }
            else
            {
                if (stridePtr)
                {
                    for (size_t i = 0; i < stridePtr->size(); ++i)
                    {
                        if ((*stridePtr)[i] != 1)
                        {
                            // 如果步长不为1，则需要计算偏移量
                            offset *= (*stridePtr)[i];
                        }
                    }
                }
                offset *= 4; // 每个元素占4字节

                if (isValidImmediate(offset, Opcode::Add))
                {
                    // 如果偏移量是合法的立即数，直接使用ADDI指令
                    auto liOffsetInst = RISCVInstruction::createIType(RISCVOpcode::ADDI, destReg, baseAddr, offset);
                    currentBB->addInstruction(liOffsetInst);
                }
                else
                {
                    auto totalOffsetReg = LiInt(offset);
                    auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, destReg, baseAddr, totalOffsetReg);
                    currentBB->addInstruction(addInst);
                }
            }

            return;
        }
        else
        {
            // 如果第一个索引不是常量，则需要计算偏移量
            auto indexReg = getOrCreateVirtualReg(indices[0], false);
            int offset = 1; // 初始偏移量为1
            if (stridePtr)
            {
                for (size_t i = 0; i < stridePtr->size(); ++i)
                {
                    if ((*stridePtr)[i] != 1)
                    {
                        offset *= (*stridePtr)[i];
                    }
                }
            }

            offset *= 4; // 每个元素占4字节
            auto scaleReg = LiInt(offset);
            auto offsetMulReg = getTempReg();
            auto mulInst =
                RISCVInstruction::createRType(RISCVOpcode::MUL, offsetMulReg, indexReg, scaleReg);
            currentBB->addInstruction(mulInst);

            auto addInst =
                RISCVInstruction::createRType(RISCVOpcode::ADD, destReg, baseAddr, offsetMulReg);
            currentBB->addInstruction(addInst);

            return;
        }
    }
    else
    {
        auto totalOffsetReg = LiInt(0);
        auto offsetReg = LiInt(1);
        auto tmpReg = getTempReg();
        auto strideReg = getTempReg();

        // 处理每个维度的索引
        for (int i = static_cast<int>(indices.size()) - 1; i >= 0; --i)
        {
            auto indexReg = getOrCreateVirtualReg(indices[i], false);

            // totalOffset += offset * index * 4
            auto mulInst = RISCVInstruction::createRType(RISCVOpcode::MUL, tmpReg, indexReg, offsetReg);
            currentBB->addInstruction(mulInst);
            auto shiftInst = RISCVInstruction::createIType(RISCVOpcode::SLLI, tmpReg, tmpReg, 2); // 左移2位，相当于乘以4
            currentBB->addInstruction(shiftInst);
            auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, totalOffsetReg, totalOffsetReg, tmpReg);
            currentBB->addInstruction(addInst);

            // offset *= stride
            if (i == 0)
            {
                // 最后一个维度的偏移量不需要乘以stride
                break;
            }

            int stride = (*stridePtr)[i - 1];
            if (stride != 1)
            {
                auto liStrideInst = RISCVInstruction::createPseudoLI(strideReg, stride);
                currentBB->addInstruction(liStrideInst);
                auto newOffsetReg = getTempReg();
                auto mulStrideInst =
                    RISCVInstruction::createRType(RISCVOpcode::MUL, newOffsetReg, offsetReg, strideReg);
                currentBB->addInstruction(mulStrideInst);
                offsetReg = newOffsetReg;
            }
        }

        auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, destReg, baseAddr, totalOffsetReg);
        currentBB->addInstruction(addInst);
    }
}

void InstructionSelector::visitCallInst(CallInst *inst)
{
    // 1. 使用两阶段参数传递解耦方法处理参数
    const auto &callerArgsVec = irFunction->getArguments();
    vector<Argument *> callerArgs;
    for (const auto &argPtr : callerArgsVec)
    {
        callerArgs.push_back(argPtr.get());
    }

    unordered_map<string, shared_ptr<RISCVRegister>> tempMoveArgMap;
    if (!callerArgs.empty())
    {
        // 如果有参数，先处理参数传递
        tempMoveArgMap = moveCallerArgsTwoPhase(inst->getCalledFunction()->getName());
    }

    // 2. 处理栈参数传递（超过8个寄存器参数的情况）
    auto arguments = inst->getArguments();
    int intArgIndex = 0;
    int floatArgIndex = 0;
    int argNum = 0;
    auto spReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP);
    auto &stack = currentFunc->getStackFrame();
    stack.allocateRaSpace(8);

    // 处理超出寄存器范围的参数（通过栈传递）
    for (auto arg : arguments)
    {
        bool isFloat = arg->getType()->isFloatTy();
        bool needStackPass = false;

        if (isFloat)
        {
            if (floatArgIndex >= 8)
            {
                needStackPass = true;
            }
            else if (tempMoveArgMap.find(arg->getName()) != tempMoveArgMap.end())
            {
                // 如果是两阶段传递的参数，直接使用临时寄存器
                auto tempReg = tempMoveArgMap[arg->getName()];
                auto destReg = make_shared<RISCVRegister>(FLOAT_PARAM_REGS[floatArgIndex]);
                auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_S, destReg, tempReg);
                currentBB->addInstruction(mvInst);
            }
            else
            {
                // 使用寄存器传递参数
                auto argReg = materializeCallArg(arg);
                auto destReg = make_shared<RISCVRegister>(FLOAT_PARAM_REGS[floatArgIndex]);
                auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_S, destReg, argReg);
                currentBB->addInstruction(mvInst);
            }
            floatArgIndex++;
        }
        else
        {
            if (intArgIndex >= 8)
            {
                needStackPass = true;
            }
            else if (tempMoveArgMap.find(arg->getName()) != tempMoveArgMap.end())
            {
                // 如果是两阶段传递的参数，直接使用临时寄存器
                auto tempReg = tempMoveArgMap[arg->getName()];
                auto destReg = make_shared<RISCVRegister>(INT_PARAM_REGS[intArgIndex]);
                auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, destReg, tempReg);
                currentBB->addInstruction(mvInst);
            }
            else
            {
                // 使用寄存器传递参数
                auto argReg = materializeCallArg(arg);
                auto destReg = make_shared<RISCVRegister>(INT_PARAM_REGS[intArgIndex]);
                auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, destReg, argReg);
                currentBB->addInstruction(mvInst);
            }
            intArgIndex++;
        }

        if (needStackPass)
        {
            shared_ptr<RISCVRegister> argReg = materializeCallArg(arg);
            if (tempMoveArgMap.find(arg->getName()) != tempMoveArgMap.end())
            {
                // 如果是两阶段传递的参数，直接使用临时寄存器
                argReg = tempMoveArgMap[arg->getName()];
            }

            if (isFloat)
            {
                stack.allocateCalleeArgSpace(inst->getName(), argNum);
                int offset = stack.getCalleeArgOffset(inst->getName(), argNum);

                auto tempReg = getTempReg();
                auto liInst = RISCVInstruction::createPseudoLI(tempReg, offset);
                currentBB->addInstruction(liInst);
                auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, spReg, tempReg);
                currentBB->addInstruction(addInst);
                auto storeInst = RISCVInstruction::createSType(RISCVOpcode::FSW, tempReg, argReg, 0);
                currentBB->addInstruction(storeInst);
            }
            else
            {
                bool isPtr = arg->getType()->isPointerTy();
                stack.allocateCalleeArgSpace(inst->getName(), argNum, isPtr ? 8 : 4);
                int offset = stack.getCalleeArgOffset(inst->getName(), argNum);

                auto tempReg = getTempReg();
                auto liInst = RISCVInstruction::createPseudoLI(tempReg, offset);
                currentBB->addInstruction(liInst);
                auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, spReg, tempReg);
                currentBB->addInstruction(addInst);
                auto storeInst = RISCVInstruction::createSType(isPtr ? RISCVOpcode::SD : RISCVOpcode::SW, tempReg, argReg, 0);
                currentBB->addInstruction(storeInst);
            }
        }
        argNum++;
    }

    // 3. 生成函数调用指令
    auto callInst = RISCVInstruction::createPseudoCALL(inst->getCalledFunction()->getName());
    currentBB->addInstruction(callInst);

    // 4. 处理返回值
    if (inst->hasReturnValue())
    {
        auto destReg = getOrCreateVirtualReg(inst->getDest());
        if (inst->getType()->isFloatTy())
        {
            auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_S, destReg, std::make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA0));
            currentBB->addInstruction(mvInst);
        }
        else
        {
            auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, destReg, std::make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A0));
            currentBB->addInstruction(mvInst);
        }
    }

    // 5. 回复caller参数寄存器
    move2RestoreArgs(tempMoveArgMap, inst->getCalledFunction()->getName());
}

void InstructionSelector::visitReturnInst(ReturnInst *inst)
{
    if (inst->getReturnValue())
    {
        // 有返回值，将值移动到返回寄存器
        auto valueReg = getOrCreateVirtualReg(inst->getReturnValue());
        auto returnReg = make_shared<RISCVRegister>(
            inst->getReturnValue()->getType()->isFloatTy() ? RISCVRegister::PhysicalReg::FA0 : RISCVRegister::PhysicalReg::A0);

        // 根据类型选择正确的移动指令
        RISCVOpcode moveOpcode = inst->getReturnValue()->getType()->isFloatTy() ? RISCVOpcode::FMV_S : RISCVOpcode::MV;
        auto moveInst = RISCVInstruction::createPseudo(moveOpcode, returnReg, valueReg);
        currentBB->addInstruction(moveInst);
    }

    // 对于其他函数，直接返回
    auto retInst = RISCVInstruction::createPseudoRET();
    currentBB->addInstruction(retInst);
}

namespace
{
    bool icmpOnlyUsedByBranch(ICmpInst *icmp)
    {
        const auto &users = icmp->getUsers();
        if (users.size() != 1)
        {
            return false;
        }
        return dynamic_cast<BranchInst *>(users[0]) != nullptr;
    }

    bool icmpComparesZero(ICmpInst *icmp, Value *&nonZeroSide)
    {
        if (auto *rhsConst = dynamic_cast<ConstantInt *>(icmp->getRHS()); rhsConst && rhsConst->Value == 0)
        {
            nonZeroSide = icmp->getLHS();
            return true;
        }
        if (auto *lhsConst = dynamic_cast<ConstantInt *>(icmp->getLHS()); lhsConst && lhsConst->Value == 0)
        {
            nonZeroSide = icmp->getRHS();
            return true;
        }
        return false;
    }
}

void InstructionSelector::visitBranchInst(BranchInst *inst)
{
    auto zeroReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO);
    auto emitCondFalseAndJalTrue = [&](RISCVOpcode brOp, shared_ptr<RISCVRegister> condReg)
    {
        currentBB->addInstruction(RISCVInstruction::createBType(
            brOp, condReg, zeroReg, inst->FalseBlock->getName()));
        currentBB->addInstruction(RISCVInstruction::createJType(
            RISCVOpcode::JAL, zeroReg, inst->TrueBlock->getName()));
    };
    auto emitRegPairFalseAndJalTrue = [&](RISCVOpcode brOp,
                                          shared_ptr<RISCVRegister> lhsReg,
                                          shared_ptr<RISCVRegister> rhsReg)
    {
        currentBB->addInstruction(RISCVInstruction::createBType(
            brOp, lhsReg, rhsReg, inst->FalseBlock->getName()));
        currentBB->addInstruction(RISCVInstruction::createJType(
            RISCVOpcode::JAL, zeroReg, inst->TrueBlock->getName()));
    };

    if (inst->getCondition())
    {
        // icmp (eq|ne) 直接分支，避免 xor/sltu + beq 序列
        if (auto *icmp = dynamic_cast<ICmpInst *>(inst->getCondition()))
        {
            if (icmpOnlyUsedByBranch(icmp) &&
                (icmp->getPredicate() == ICmpInst::ICMP_NE || icmp->getPredicate() == ICmpInst::ICMP_EQ) &&
                inst->FalseBlock)
            {
                Value *zeroSide = nullptr;
                if (icmpComparesZero(icmp, zeroSide))
                {
                    auto vReg = getOrCreateVirtualReg(zeroSide);
                    if (vReg->getType() == RegisterType::FLOAT)
                    {
                        auto intCondReg = getTempReg();
                        auto ftoiInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_X_W, intCondReg, vReg);
                        currentBB->addInstruction(ftoiInst);
                        vReg = intCondReg;
                    }
                    RISCVOpcode brOp = icmp->getPredicate() == ICmpInst::ICMP_NE ? RISCVOpcode::BEQ
                                                                                 : RISCVOpcode::BNE;
                    emitCondFalseAndJalTrue(brOp, vReg);
                    return;
                }

                auto lhsReg = getOrCreateVirtualReg(icmp->getLHS());
                auto rhsReg = getOrCreateVirtualReg(icmp->getRHS());
                if (lhsReg->getType() == RegisterType::FLOAT || rhsReg->getType() == RegisterType::FLOAT)
                {
                    // 浮点 eq/ne 仍走 i1 物化路径
                }
                else
                {
                    RISCVOpcode falseBr = icmp->getPredicate() == ICmpInst::ICMP_NE ? RISCVOpcode::BEQ
                                                                                    : RISCVOpcode::BNE;
                    emitRegPairFalseAndJalTrue(falseBr, lhsReg, rhsReg);
                    return;
                }
            }
        }

        // 条件分支
        auto condReg = getOrCreateVirtualReg(inst->getCondition());

        if (condReg->getType() == RegisterType::FLOAT)
        {
            auto intCondReg = getTempReg();
            auto ftoiInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_X_W, intCondReg, condReg);
            currentBB->addInstruction(ftoiInst);
            condReg = intCondReg;
        }

        if (inst->FalseBlock)
        {
            emitCondFalseAndJalTrue(RISCVOpcode::BEQ, condReg);
        }
    }
    else
    {
        // 无条件分支
        auto jumpInst = RISCVInstruction::createJType(RISCVOpcode::JAL,
                                                      make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO),
                                                      inst->TrueBlock->getName());
        currentBB->addInstruction(jumpInst);
    }
}

void InstructionSelector::visitICmpInst(ICmpInst *inst)
{
    if (isIcmpFusableWithMagicDivSignFix(inst))
    {
        return;
    }

    if (icmpOnlyUsedByBranch(inst) &&
        (inst->getPredicate() == ICmpInst::ICMP_NE || inst->getPredicate() == ICmpInst::ICMP_EQ))
    {
        Value *zeroSide = nullptr;
        if (icmpComparesZero(inst, zeroSide))
        {
            return;
        }
        const bool lhsFloat = inst->getLHS() && inst->getLHS()->getType()->isFloatTy();
        const bool rhsFloat = inst->getRHS() && inst->getRHS()->getType()->isFloatTy();
        if (!lhsFloat && !rhsFloat)
        {
            return;
        }
    }

    switch (inst->Pred)
    {
    case ICmpInst::ICMP_EQ:
    {
        auto destReg = getOrCreateVirtualReg(inst->getDest());
        if (dynamic_cast<ConstantInt *>(inst->getRHS()))
        {
            auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
            auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, lhsReg, dynamic_cast<ConstantInt *>(inst->getRHS())->Value);
            currentBB->addInstruction(xoriInst);
            auto seqzInst = RISCVInstruction::createIType(RISCVOpcode::SLTIU, destReg, destReg, 1);
            currentBB->addInstruction(seqzInst);
        }
        else if (dynamic_cast<ConstantInt *>(inst->getLHS()))
        {
            auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
            auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, rhsReg, dynamic_cast<ConstantInt *>(inst->getLHS())->Value);
            currentBB->addInstruction(xoriInst);
            auto seqzInst = RISCVInstruction::createIType(RISCVOpcode::SLTIU, destReg, destReg, 1);
            currentBB->addInstruction(seqzInst);
        }
        else
        {
            auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
            auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
            auto xorInst = RISCVInstruction::createRType(RISCVOpcode::XOR, destReg, lhsReg, rhsReg);
            currentBB->addInstruction(xorInst);
            auto seqzInst = RISCVInstruction::createIType(RISCVOpcode::SLTIU, destReg, destReg, 1);
            currentBB->addInstruction(seqzInst);
        }
    }
    break;
    case ICmpInst::ICMP_NE:
    {
        auto destReg = getOrCreateVirtualReg(inst->getDest());
        if (dynamic_cast<ConstantInt *>(inst->getRHS()) && dynamic_cast<ConstantInt *>(inst->getRHS())->Value <= 2047 && dynamic_cast<ConstantInt *>(inst->getRHS())->Value >= -2048)
        {
            auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
            auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, lhsReg, dynamic_cast<ConstantInt *>(inst->getRHS())->Value);
            currentBB->addInstruction(xoriInst);
            auto snezInst = RISCVInstruction::createRType(RISCVOpcode::SLTU, destReg,
                                                          make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO),
                                                          destReg);
            currentBB->addInstruction(snezInst);
        }
        else if (dynamic_cast<ConstantInt *>(inst->getLHS()) && dynamic_cast<ConstantInt *>(inst->getLHS())->Value <= 2047 && dynamic_cast<ConstantInt *>(inst->getLHS())->Value >= -2048)
        {
            auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
            auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, rhsReg, dynamic_cast<ConstantInt *>(inst->getLHS())->Value);
            currentBB->addInstruction(xoriInst);
            auto snezInst = RISCVInstruction::createRType(RISCVOpcode::SLTU, destReg,
                                                          make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO),
                                                          destReg);
            currentBB->addInstruction(snezInst);
        }
        else
        {
            auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
            auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
            auto xorInst = RISCVInstruction::createRType(RISCVOpcode::XOR, destReg, lhsReg, rhsReg);
            currentBB->addInstruction(xorInst);
            auto snezInst = RISCVInstruction::createRType(RISCVOpcode::SLTU, destReg,
                                                          make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO),
                                                          destReg);
            currentBB->addInstruction(snezInst);
        }
    }
    break;
    case ICmpInst::ICMP_SLT:
    {
        auto destReg = getOrCreateVirtualReg(inst->getDest());
        if (auto *rhsConst = dynamic_cast<ConstantInt *>(inst->getRHS()); rhsConst && rhsConst->Value == 0)
        {
            auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
            auto cmpInst = RISCVInstruction::createIType(RISCVOpcode::SLTI, destReg, lhsReg, 0);
            currentBB->addInstruction(cmpInst);
        }
        else if (auto *lhsConst = dynamic_cast<ConstantInt *>(inst->getLHS()); lhsConst && lhsConst->Value == 0)
        {
            auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
            auto zeroReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO);
            auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::SLT, destReg, zeroReg, rhsReg);
            currentBB->addInstruction(cmpInst);
        }
        else
        {
            auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
            auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
            auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::SLT, destReg, lhsReg, rhsReg);
            currentBB->addInstruction(cmpInst);
        }
    }
    break;
    case ICmpInst::ICMP_SLE:
    {
        auto destReg = getOrCreateVirtualReg(inst->getDest());
        if (auto *lhsConst = dynamic_cast<ConstantInt *>(inst->getLHS()); lhsConst && lhsConst->Value == 0)
        {
            auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
            auto sltiInst = RISCVInstruction::createIType(RISCVOpcode::SLTI, destReg, rhsReg, 0);
            currentBB->addInstruction(sltiInst);
            auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, destReg, 1);
            currentBB->addInstruction(xoriInst);
        }
        else if (auto *rhsConst = dynamic_cast<ConstantInt *>(inst->getRHS()); rhsConst && rhsConst->Value == 0)
        {
            auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
            auto sltiInst = RISCVInstruction::createIType(RISCVOpcode::SLTI, destReg, lhsReg, 1);
            currentBB->addInstruction(sltiInst);
        }
        else
        {
            auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
            auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
            auto sltInst = RISCVInstruction::createRType(RISCVOpcode::SLT, destReg, rhsReg, lhsReg);
            currentBB->addInstruction(sltInst);
            auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, destReg, 1);
            currentBB->addInstruction(xoriInst);
        }
    }
    break;
    case ICmpInst::ICMP_SGT:
    {
        auto destReg = getOrCreateVirtualReg(inst->getDest());
        if (auto *rhsConst = dynamic_cast<ConstantInt *>(inst->getRHS()); rhsConst && rhsConst->Value == 0)
        {
            auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
            auto sltiInst = RISCVInstruction::createIType(RISCVOpcode::SLTI, destReg, lhsReg, 1);
            currentBB->addInstruction(sltiInst);
            auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, destReg, 1);
            currentBB->addInstruction(xoriInst);
        }
        else if (auto *lhsConst = dynamic_cast<ConstantInt *>(inst->getLHS()); lhsConst && lhsConst->Value == 0)
        {
            auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
            auto sltiInst = RISCVInstruction::createIType(RISCVOpcode::SLTI, destReg, rhsReg, 0);
            currentBB->addInstruction(sltiInst);
        }
        else
        {
            auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
            auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
            auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::SLT, destReg, rhsReg, lhsReg);
            currentBB->addInstruction(cmpInst);
        }
    }
    break;
    case ICmpInst::ICMP_SGE:
    {
        auto destReg = getOrCreateVirtualReg(inst->getDest());
        if (auto *rhsConst = dynamic_cast<ConstantInt *>(inst->getRHS()); rhsConst && rhsConst->Value == 0)
        {
            auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
            auto sltiInst = RISCVInstruction::createIType(RISCVOpcode::SLTI, destReg, lhsReg, 0);
            currentBB->addInstruction(sltiInst);
            auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, destReg, 1);
            currentBB->addInstruction(xoriInst);
        }
        else if (auto *lhsConst = dynamic_cast<ConstantInt *>(inst->getLHS()); lhsConst && lhsConst->Value == 0)
        {
            auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
            auto sltiInst = RISCVInstruction::createIType(RISCVOpcode::SLTI, destReg, rhsReg, 1);
            currentBB->addInstruction(sltiInst);
        }
        else
        {
            auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
            auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
            auto sltInst = RISCVInstruction::createRType(RISCVOpcode::SLT, destReg, lhsReg, rhsReg);
            currentBB->addInstruction(sltInst);
            auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, destReg, 1);
            currentBB->addInstruction(xoriInst);
        }
    }
    break;
    default:
        return;
    }
}

void InstructionSelector::visitFCmpInst(FCmpInst *inst)
{
    // 修改：使用浮点寄存器类型
    auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
    auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    switch (inst->Pred)
    {
    case FCmpInst::FCMP_OEQ:
    {
        auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::FEQ_S, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(cmpInst);
    }
    break;
    case FCmpInst::FCMP_OLT:
    {
        auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::FLT_S, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(cmpInst);
    }
    break;
    case FCmpInst::FCMP_OLE:
    {
        auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::FLE_S, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(cmpInst);
    }
    break;
    case FCmpInst::FCMP_OGT:
    {
        auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::FLT_S, destReg, rhsReg, lhsReg);
        currentBB->addInstruction(cmpInst);
    }
    break;
    case FCmpInst::FCMP_OGE:
    {
        auto cmpInst = RISCVInstruction::createRType(RISCVOpcode::FLE_S, destReg, rhsReg, lhsReg);
        currentBB->addInstruction(cmpInst);
    }
    break;
    case FCmpInst::FCMP_ONE:
    {
        auto feqInst = RISCVInstruction::createRType(RISCVOpcode::FEQ_S, destReg, lhsReg, rhsReg);
        currentBB->addInstruction(feqInst);
        auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, destReg, 1);
        currentBB->addInstruction(xoriInst);
    }
    break;
    default:
        return;
    }
}

void InstructionSelector::visitSIToFPInst(CastInst *inst)
{
    // 处理有符号整数到浮点数的转换指令
    auto srcReg = getOrCreateVirtualReg(inst->getOperand());

    // 创建目标浮点寄存器 - 使用临时寄存器管理
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    // 生成 RISC-V 的 fcvt.s.w 指令（整数到单精度浮点）
    auto fcvtInst = RISCVInstruction::createPseudo(RISCVOpcode::FCVT_S_W, destReg, srcReg);
    currentBB->addInstruction(fcvtInst);
}

void InstructionSelector::visitFPToSIInst(CastInst *inst)
{
    // 处理浮点数到有符号整数的转换指令
    auto srcReg = getOrCreateVirtualReg(inst->getOperand());

    // 创建目标整数寄存器 - 使用临时寄存器管理
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    // 生成 RISC-V 的 fcvt.w.s 指令（单精度浮点到整数）
    // 使用RTZ（Round toward Zero）舍入模式，这是C语言标准的行为
    auto fcvtInst = RISCVInstruction::createPseudo(RISCVOpcode::FCVT_W_S, destReg, srcReg);
    currentBB->addInstruction(fcvtInst);
}

void InstructionSelector::visitCopyInst(CopyInst *inst)
{
    // Copy指令用于将源值复制到目标位置
    // 在我们的"所有变量溢出到栈上"策略中，这实际上是从源位置加载值，然后存储到目标位置

    // copy 源为常数时单独物化 li 并打 copyInit 标记（归纳/循环初始化），避免 LICM/CSE 与只读 li 合并
    shared_ptr<RISCVRegister> srcReg;
    Value *src = inst->getSource();
    if (auto *c = dynamic_cast<ConstantInt *>(src))
        srcReg = LiInt(c->Value, false, true);
    else if (auto *cl = dynamic_cast<ConstantLong *>(src))
        srcReg = LiLong(cl->Value, false, true);
    else if (auto *cf = dynamic_cast<ConstantFloat *>(src))
        srcReg = LiFloat(cf->Value, false, true);
    else
        srcReg = getOrCreateVirtualReg(src);

    // 创建临时寄存器进行复制操作
    auto destReg = getOrCreateVirtualReg(inst->getDest());
    RegisterType regType = inst->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::INT;
    int intIndex = 0;
    int floatIndex = 0;
    for (auto &arg : irFunction->getArguments())
    {
        if (arg->getName() == inst->getDest()->getName())
        {
            if (arg->getType()->isFloatTy())
            {
                if (floatIndex < FLOAT_PARAM_REGS.size() - 1)
                    destReg = make_shared<RISCVRegister>(FLOAT_PARAM_REGS[floatIndex]);
            }
            else
            {
                if (intIndex < INT_PARAM_REGS.size())
                    destReg = make_shared<RISCVRegister>(INT_PARAM_REGS[intIndex]);
            }

            break;
        }
        regType == RegisterType::FLOAT ? floatIndex++ : intIndex++;
    }

    // 生成移动指令
    RISCVOpcode moveOpcode;
    if (inst->getType()->isFloatTy())
    {
        // 浮点数使用 fmv.s 指令
        moveOpcode = RISCVOpcode::FMV_S;
    }
    else
    {
        // 整数/指针使用 mv 伪指令（实际上是 addi rd, rs1, 0）
        moveOpcode = RISCVOpcode::MV;
    }

    auto moveInst = RISCVInstruction::createPseudo(moveOpcode, destReg, srcReg);
    currentBB->addInstruction(moveInst);
}

void InstructionSelector::visitBitCastInst(CastInst *inst)
{
    // BitCast指令用于类型转换，但不改变数据的位模式
    // 在RISC-V中，我们可以直接使用mv指令进行转换

    // 获取源值的寄存器
    auto srcReg = getOrCreateVirtualReg(inst->getOperand());

    // 创建目标寄存器
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    // 生成移动指令
    auto moveInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, destReg, srcReg);
    currentBB->addInstruction(moveInst);
}

void InstructionSelector::visitSExtInst(CastInst *inst)
{
    // sext 后紧跟 slld(常量) 时，由 visitBinaryOp 融合为单条 slli
    const auto &users = inst->getUsers();
    if (users.size() == 1)
    {
        if (auto *slld = dynamic_cast<BinaryOperator *>(users[0]))
        {
            if (slld->Op == Opcode::Slld)
            {
                int64_t shiftAmt = 0;
                bool hasShiftConst = false;
                if (auto *rhsConst = dynamic_cast<ConstantInt *>(slld->getRHS()))
                {
                    shiftAmt = rhsConst->Value;
                    hasShiftConst = true;
                }
                else if (auto *rhsLong = dynamic_cast<ConstantLong *>(slld->getRHS()))
                {
                    shiftAmt = rhsLong->Value;
                    hasShiftConst = true;
                }
                if (hasShiftConst && isValidImmediate(shiftAmt, Opcode::Slld))
                {
                    return;
                }
            }
        }
    }

    auto srcReg = getOrCreateVirtualReg(inst->getOperand());
    auto destReg = getOrCreateVirtualReg(inst->getDest());
    auto sextInst = RISCVInstruction::createIType(RISCVOpcode::ADDIW, destReg, srcReg, 0);
    currentBB->addInstruction(sextInst);
}

void InstructionSelector::visitTruncInst(CastInst *inst)
{
    // trunc + divsign + add 时由 visitBinaryOp 融合为 slti + addw
    if (isTruncFusableWithMagicDivSignFix(inst))
    {
        return;
    }

    auto srcReg = getOrCreateVirtualReg(inst->getOperand());
    auto destReg = getOrCreateVirtualReg(inst->getDest());
    auto truncInst = RISCVInstruction::createRType(
        RISCVOpcode::ADDW, destReg, srcReg,
        std::make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO));
    currentBB->addInstruction(truncInst);
}

void InstructionSelector::visitXnorInst(BinaryOperator *inst)
{
    // XNOR操作可以通过先进行XOR操作，然后取反来实现
    auto lhsReg = getOrCreateVirtualReg(inst->getLHS());
    auto rhsReg = getOrCreateVirtualReg(inst->getRHS());
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    // 生成XOR指令
    auto xorInst = RISCVInstruction::createRType(RISCVOpcode::XOR, destReg, lhsReg, rhsReg);
    currentBB->addInstruction(xorInst);

    // 生成NOT指令（使用XORI指令将结果取反）
    auto notInst = RISCVInstruction::createIType(RISCVOpcode::XORI, destReg, destReg, -1);
    currentBB->addInstruction(notInst);
}

void InstructionSelector::visitSelectInst(SelectInst *inst)
{
    auto condReg = getOrCreateVirtualReg(inst->getCondition());
    auto trueReg = getOrCreateVirtualReg(inst->getTrueValue());
    auto falseReg = getOrCreateVirtualReg(inst->getFalseValue());
    auto destReg = getOrCreateVirtualReg(inst->getDest());

    bool isFloat = inst->getType()->isFloatTy();
    bool isBoolCond = isBooleanCompareCondition(inst->getCondition());
    auto zeroReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO);

    if (!isFloat && isBoolCond)
    {
        // cond 来自 ICmp/FCmp，值为 0/1：dest = false ^ ((true ^ false) & (-cond))
        auto maskReg = getTempReg(true);
        auto negInst = RISCVInstruction::createRType(RISCVOpcode::SUB, maskReg, zeroReg, condReg);
        currentBB->addInstruction(negInst);

        auto diffReg = getTempReg(true);
        auto xorDiffInst = RISCVInstruction::createRType(RISCVOpcode::XOR, diffReg, trueReg, falseReg);
        currentBB->addInstruction(xorDiffInst);

        auto maskedReg = getTempReg(true);
        auto andInst = RISCVInstruction::createRType(RISCVOpcode::AND, maskedReg, diffReg, maskReg);
        currentBB->addInstruction(andInst);

        auto xorDestInst = RISCVInstruction::createRType(RISCVOpcode::XOR, destReg, falseReg, maskedReg);
        currentBB->addInstruction(xorDestInst);
        return;
    }

    auto fullMaskReg = getTempReg(true);
    if (isBoolCond)
    {
        auto negInst = RISCVInstruction::createRType(RISCVOpcode::SUB, fullMaskReg, zeroReg, condReg);
        currentBB->addInstruction(negInst);
    }
    else
    {
        auto maskReg = getTempReg(true);
        auto snezInst = RISCVInstruction::createRType(RISCVOpcode::SLTU, maskReg, zeroReg, condReg);
        currentBB->addInstruction(snezInst);
        auto negInst = RISCVInstruction::createRType(RISCVOpcode::SUB, fullMaskReg, zeroReg, maskReg);
        currentBB->addInstruction(negInst);
    }

    auto tvalReg = getTempReg(true);
    auto invMaskReg = getTempReg(true);
    auto fvalReg = getTempReg(true);

    if (isFloat)
    {
        // 浮点数处理：转换为整数进行位运算
        auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_X_W, tvalReg, trueReg);
        currentBB->addInstruction(mvInst);
        auto andInst1 = RISCVInstruction::createRType(RISCVOpcode::AND, tvalReg, tvalReg, fullMaskReg);
        currentBB->addInstruction(andInst1);

        auto mvInst2 = RISCVInstruction::createPseudo(RISCVOpcode::FMV_X_W, fvalReg, falseReg);
        currentBB->addInstruction(mvInst2);
        auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, invMaskReg, fullMaskReg, -1);
        currentBB->addInstruction(xoriInst);
        auto andInst2 = RISCVInstruction::createRType(RISCVOpcode::AND, fvalReg, fvalReg, invMaskReg);
        currentBB->addInstruction(andInst2);

        auto tempReg = getTempReg(true);
        auto orInst = RISCVInstruction::createRType(RISCVOpcode::OR, tempReg, tvalReg, fvalReg);
        currentBB->addInstruction(orInst);

        auto mvInst3 = RISCVInstruction::createPseudo(RISCVOpcode::FMV_W_X, destReg, tempReg);
        currentBB->addInstruction(mvInst3);
    }
    else
    {
        // 整数处理：直接使用全位掩码
        auto andInst1 = RISCVInstruction::createRType(RISCVOpcode::AND, tvalReg, trueReg, fullMaskReg);
        currentBB->addInstruction(andInst1);

        auto xoriInst = RISCVInstruction::createIType(RISCVOpcode::XORI, invMaskReg, fullMaskReg, -1);
        currentBB->addInstruction(xoriInst);

        auto andInst2 = RISCVInstruction::createRType(RISCVOpcode::AND, fvalReg, falseReg, invMaskReg);
        currentBB->addInstruction(andInst2);

        auto orInst = RISCVInstruction::createRType(RISCVOpcode::OR, destReg, tvalReg, fvalReg);
        currentBB->addInstruction(orInst);
    }
}
void InstructionSelector::DealArgumentsInStart()
{
    const auto &args = irFunction->getArguments();
    vector<Argument *> argsVec;
    for (const auto &argPtr : args)
    {
        argsVec.push_back(argPtr.get());
    }

    if (argsVec.empty())
    {
        return; // 无参数函数，直接返回
    }

    auto intArgIndex = 0;
    auto floatArgIndex = 0;
    auto argIndex = 0;
    vector<size_t> NoneUsedRegsIndex = irFunction->getIndexOfNotUsedArguments();
    for (auto arg : argsVec)
    {
        bool isFloat = arg->getType()->isFloatTy();
        bool isPointer = arg->getType()->isPointerTy();
        if (isFloat)
        {
            if (floatArgIndex < 8)
            {
                floatArgIndex++;
                continue;
            }

            currentFunc->getStackFrame().allocateCallerArgSpace(arg->getName(), 4);
        }
        else
        {
            if (intArgIndex < 8)
            {
                intArgIndex++;
                continue;
            }

            currentFunc->getStackFrame().allocateCallerArgSpace(arg->getName(), isPointer ? 8 : 4);
        }
    }

    floatArgIndex = 0;
    intArgIndex = 0;
    for (auto arg : argsVec)
    {
        bool isFloat = arg->getType()->isFloatTy();
        if (find(NoneUsedRegsIndex.begin(), NoneUsedRegsIndex.end(), argIndex) != NoneUsedRegsIndex.end())
        {
            argIndex++;
            if (isFloat)
            {
                floatArgIndex++;
            }
            else
            {
                intArgIndex++;
            }

            continue; // 跳过未使用的参数
        }

        if (isFloat)
        {
            getCallerArgReg(arg, floatArgIndex);
            floatArgIndex++;
        }
        else
        {
            getCallerArgReg(arg, intArgIndex);
            intArgIndex++;
        }

        argIndex++;
    }
}

unordered_map<string, shared_ptr<RISCVRegister>> InstructionSelector::moveCallerArgsTwoPhase(const string &calleeName)
{
    const auto &callerArgsVec = irFunction->getArguments();
    vector<Argument *> callerArgs;
    for (const auto &argPtr : callerArgsVec)
    {
        callerArgs.push_back(argPtr.get());
    }

    if (callerArgs.empty())
    {
        return {}; // 无参数函数，直接返回
    }

    // 创建临时寄存器数组，用于存储参数
    vector<shared_ptr<RISCVRegister>> tempRegs;
    unordered_map<string, shared_ptr<RISCVRegister>> tempMoveArgMap;

    // 为每个参数创建一个临时寄存器
    for (size_t i = 0; i < callerArgs.size(); i++)
    {
        if (callerArgs[i]->getType()->isFloatTy())
        {
            // 浮点参数使用浮点临时寄存器
            tempRegs.push_back(getTempFloatReg());
        }
        else
        {
            // 整数/指针参数使用整数临时寄存器
            tempRegs.push_back(getTempReg());
        }
    }

    // 将参数移动到临时寄存器
    for (size_t i = 0; i < callerArgs.size(); i++)
    {
        auto argReg = getOrCreateVirtualReg(callerArgs[i]);
        RISCVOpcode moveOpcode = callerArgs[i]->getType()->isFloatTy() ? RISCVOpcode::FMV_S : RISCVOpcode::MV;
        auto moveInst = RISCVInstruction::createPseudo(moveOpcode, tempRegs[i], argReg);
        currentBB->addInstruction(moveInst);
        currentFunc->addMoveInstructionBeforeCall(calleeName, moveInst);
        // 更新临时寄存器映射
        tempMoveArgMap[callerArgs[i]->getName()] = tempRegs[i];
    }

    return tempMoveArgMap; // 返回临时寄存器映射
}
void InstructionSelector::move2RestoreArgs(unordered_map<string, shared_ptr<RISCVRegister>> &registerMap, const string &funcName)
{
    // 1. 解析 IR Function 的形参列表
    const auto &arguments = irFunction->getArguments();
    if (arguments.empty())
    {
        return; // 无参数函数，直接返回
    }

    int intArgIndex = 0;
    int floatArgIndex = 0;

    // 2. 为每个形参创建虚拟寄存器 xi 并生成 mv xi, ai 指令
    for (const auto &arg : arguments)
    {
        // 确定参数类型和对应的参数寄存器
        bool isFloat = arg->getType()->isFloatTy();
        shared_ptr<RISCVRegister> paramReg;

        if (isFloat)
        {
            if (floatArgIndex < 8)
            {
                auto tempReg = registerMap[arg->getName()];
                paramReg = make_shared<RISCVRegister>(FLOAT_PARAM_REGS[floatArgIndex]);
                auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_S, paramReg, tempReg);
                currentFunc->addMoveInstructionAfterCall(funcName, mvInst);
                currentBB->addInstruction(mvInst);
                floatArgIndex++;
            }
        }
        else
        {
            if (intArgIndex < 8)
            {
                auto tempReg = registerMap[arg->getName()];
                paramReg = make_shared<RISCVRegister>(INT_PARAM_REGS[intArgIndex]);
                auto mvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, paramReg, tempReg);
                currentFunc->addMoveInstructionAfterCall(funcName, mvInst);
                currentBB->addInstruction(mvInst);
                intArgIndex++;
            }
        }
    }
}

shared_ptr<RISCVRegister> InstructionSelector::getCallerArgReg(Argument *arg, size_t index)
{
    if (arg->getType()->isFloatTy())
    {
        if (index < FLOAT_PARAM_REGS.size())
        {
            auto sourceReg = make_shared<RISCVRegister>(FLOAT_PARAM_REGS[index]);
            auto reg = getArgReg(arg->getName(), RegisterType::FLOAT);
            auto FmvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_S, reg, sourceReg);
            currentBB->addInstruction(FmvInst);
            return reg;
        }
        else
        {
            // 超出范围，从栈上获取参数
            auto offset = currentFunc->getStackFrame().getCallerArgOffset(arg->getName());
            auto tempReg = LiInt(offset, true);
            currentFunc->addInstructionNeedReGetOffset(arg->getName(), currentLiInstruction);
            auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP), tempReg);
            currentBB->addInstruction(addInst);
            // 从栈上加载浮点数参数
            auto reg = getArgReg(arg->getName(), RegisterType::FLOAT);
            auto loadInst = RISCVInstruction::createIType(RISCVOpcode::FLW, reg, tempReg, 0);
            currentBB->addInstruction(loadInst);
            return tempReg;
        }
    }
    else
    {
        if (index < INT_PARAM_REGS.size())
        {
            auto sourceReg = make_shared<RISCVRegister>(INT_PARAM_REGS[index]);
            auto reg = getArgReg(arg->getName(), RegisterType::INT);
            auto MvInst = RISCVInstruction::createPseudo(RISCVOpcode::MV, reg, sourceReg);
            currentBB->addInstruction(MvInst);
            return reg;
        }
        else
        {
            // 超出范围，从栈上获取参数
            RISCVOpcode op = arg->getType()->isPointerTy() ? RISCVOpcode::LD : RISCVOpcode::LW;
            auto offset = currentFunc->getStackFrame().getCallerArgOffset(arg->getName());
            auto tempReg = LiInt(offset, true);
            currentFunc->addInstructionNeedReGetOffset(arg->getName(), currentLiInstruction);
            auto addInst = RISCVInstruction::createRType(RISCVOpcode::ADD, tempReg, make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP), tempReg);
            currentBB->addInstruction(addInst);
            auto reg = getArgReg(arg->getName(), RegisterType::INT);
            auto loadInst = RISCVInstruction::createIType(op, reg, tempReg, 0);
            currentBB->addInstruction(loadInst);
            return tempReg;
        }
    }
}

shared_ptr<RISCVRegister> InstructionSelector::getOrCreateVirtualReg(Value *value, bool isPhysical)
{
    // 立即数
    if (auto constantIntValue = dynamic_cast<ConstantInt *>(value))
    {
        return LiInt(constantIntValue->Value, isPhysical);
    }
    else if (auto constantFloatValue = dynamic_cast<ConstantFloat *>(value))
    {
        return LiFloat(constantFloatValue->Value, isPhysical);
    }
    else if (auto constantLong = dynamic_cast<ConstantLong *>(value))
    {
        return LiLong(constantLong->Value, isPhysical);
    }
    // 全局变量
    else if (auto globlVar = dynamic_cast<GlobalVariable *>(value))
    {
        return LaGlobl(globlVar);
    }
    // 函数参数
    else if (auto arg = dynamic_cast<Argument *>(value))
    {
        RegisterType regType = arg->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::INT;
        return getArgReg(arg->getName(), regType);
    }

    // 变量
    auto valueName = value->getName();
    if (registerMap.find(valueName) != registerMap.end())
    {
        return registerMap[valueName];
    }
    else
    {
        auto dataType = value->getType()->isFloatTy() ? RegisterType::FLOAT : RegisterType::INT;
        auto virtualReg = make_shared<RISCVRegister>(dataType);
        registerMap[valueName] = virtualReg;
        // 将 IR value 名称映射记录到后端函数，供后续 peephole 使用
        if (currentFunc)
        {
            currentFunc->addIRValueMapping(valueName, virtualReg);
        }
        return virtualReg;
    }

    return nullptr;
}

shared_ptr<RISCVRegister> InstructionSelector::getOrCreateVectorReg(Value *value, bool isPhysical)
{
    // 向量值（<N x i32>）：创建 RegisterType::VECTOR 虚拟寄存器
    auto valueName = value->getName();
    if (registerMap.find(valueName) != registerMap.end())
    {
        auto existing = registerMap[valueName];
        if (existing->getType() == RegisterType::VECTOR)
            return existing;
    }

    if (isPhysical)
    {
        // 向量暂不使用物理寄存器（由分配器从 v0-v31 中分配）
        auto reg = make_shared<RISCVRegister>(RegisterType::VECTOR);
        registerMap[valueName] = reg;
        if (currentFunc)
            currentFunc->addIRValueMapping(valueName, reg);
        return reg;
    }

    auto virtualReg = make_shared<RISCVRegister>(RegisterType::VECTOR);
    registerMap[valueName] = virtualReg;
    if (currentFunc)
    {
        currentFunc->addIRValueMapping(valueName, virtualReg);
    }
    return virtualReg;
}

shared_ptr<RISCVRegister> InstructionSelector::getArgReg(const string &argName, RegisterType regType)
{
    // 获取当前函数的参数寄存器
    if (MoveArgMap.find(argName) != MoveArgMap.end())
    {
        return MoveArgMap[argName];
    }
    else
    {
        auto tempReg = regType == RegisterType::INT ? getTempReg() : getTempFloatReg();
        MoveArgMap[argName] = tempReg; // 临时寄存器到参数寄存器的映射
        return tempReg;
    }
}

shared_ptr<RISCVRegister> InstructionSelector::getTempReg(bool isPhysical)
{
    if (isPhysical)
    {
        return getTempPhysicalReg();
    }

    auto tempReg = make_shared<RISCVRegister>(RegisterType::INT);
    return tempReg;
}

shared_ptr<RISCVRegister> InstructionSelector::getTempFloatReg(bool isPhysical)
{
    if (isPhysical)
    {
        return getTempPhysicalFloatReg();
    }

    auto tempReg = make_shared<RISCVRegister>(RegisterType::FLOAT);
    return tempReg;
}

shared_ptr<RISCVRegister> InstructionSelector::getTempPhysicalReg()
{
    auto tempReg = make_shared<RISCVRegister>(INT_TEMP_REGS[tempRegCount++ % INT_TEMP_REGS.size()]);
    tempRegisters.push_back(tempReg);
    return tempReg;
}

shared_ptr<RISCVRegister> InstructionSelector::getTempPhysicalFloatReg()
{
    auto tempReg = make_shared<RISCVRegister>(FLOAT_TEMP_REGS[tempFloatRegCount++ % FLOAT_TEMP_REGS.size()]);
    tempRegisters.push_back(tempReg);
    return tempReg;
}

shared_ptr<RISCVRegister> InstructionSelector::LaGlobl(GlobalVariable *globlvar)
{
    // 每次使用点单独 la + 虚拟寄存器，避免跨大段循环/调用复用同一 vr 后被分配器压到 caller-saved 并踩坏
    auto globReg = getTempReg();
    auto laInst = RISCVInstruction::createPseudoLA(globReg, globlvar->getName());
    currentBB->addInstruction(laInst);
    return globReg;
}

shared_ptr<RISCVRegister> InstructionSelector::LiInt(int value, bool isPhysical, bool copyInit)
{
    auto destReg = getTempReg(isPhysical);
    auto LiInst = RISCVInstruction::createPseudoLI(destReg, value, copyInit);
    currentLiInstruction = LiInst; // 保存当前的立即数指令
    currentBB->addInstruction(LiInst);

    return destReg;
}

shared_ptr<RISCVRegister> InstructionSelector::LiFloat(float floatValue, bool isPhysical, bool copyInit)
{
    auto tmpReg = getTempReg(isPhysical);
    uint32_t hexValue;
    memcpy(&hexValue, &floatValue, sizeof(floatValue));
    auto LiInst = RISCVInstruction::createPseudoLI(tmpReg, hexValue, copyInit);
    currentBB->addInstruction(LiInst);

    auto destReg = getTempFloatReg(isPhysical);
    auto FmvInst = RISCVInstruction::createPseudo(RISCVOpcode::FMV_W_X, destReg, tmpReg);
    currentBB->addInstruction(FmvInst);

    return destReg;
}

shared_ptr<RISCVRegister> InstructionSelector::LiLong(long longValue, bool isPhysical, bool copyInit)
{
    auto destReg = getTempReg(isPhysical);
    auto LiInst = RISCVInstruction::createPseudoLI(destReg, longValue, copyInit);
    currentLiInstruction = LiInst; // 保存当前的立即数指令
    currentBB->addInstruction(LiInst);

    return destReg;
}

void InstructionSelector::visitVecSetVlInst(VecSetVlInst *inst)
{
    // %vl = vecsetvl %count, sew —— vsetvli rd, rs1, e32, m1, ta, ma
    auto count = getOrCreateVirtualReg(inst->getCount());
    auto vlReg = getTempReg(); // rd 是 i32 虚拟寄存器（保存 vl）
    currentBB->addInstruction(RISCVInstruction::createVectorSetVl(vlReg, count));
    registerMap[inst->getName()] = vlReg;
    if (currentFunc)
        currentFunc->addIRValueMapping(inst->getName(), vlReg);
}

void InstructionSelector::visitVecLoadInst(VecLoadInst *inst)
{
    // %v = vecload %ptr, %vl —— vle32.v vd, (ptr)
    auto ptr = getOrCreateVirtualReg(inst->getPointerOperand());
    (void)inst->getVl(); // vl 由 vsetvli 隐含设置，无需单独传参
    auto vd = getOrCreateVectorReg(inst);
    currentBB->addInstruction(
        RISCVInstruction::createVectorMemory(RISCVOpcode::VLE32_V, vd, ptr));
}

void InstructionSelector::visitVecStoreInst(VecStoreInst *inst)
{
    // vecstore %v, %ptr, %vl —— vse32.v vs2, (ptr)
    auto val = getOrCreateVectorReg(inst->getValue());
    auto ptr = getOrCreateVirtualReg(inst->getPointerOperand());
    (void)inst->getVl();
    currentBB->addInstruction(
        RISCVInstruction::createVectorMemory(RISCVOpcode::VSE32_V, val, ptr));
}

void InstructionSelector::visitVecStridedLoadInst(VecStridedLoadInst *inst)
{
    // vecstridedload %ptr, %strideBytes, %vl —— vlse32.v vd, (ptr), stride
    auto ptr = getOrCreateVirtualReg(inst->getPointerOperand());
    auto stride = getOrCreateVirtualReg(inst->getStride());
    (void)inst->getVl();
    auto vd = getOrCreateVectorReg(inst);
    currentBB->addInstruction(RISCVInstruction::createVectorStridedMemory(
        RISCVOpcode::VLESE32_V, vd, ptr, stride));
}

void InstructionSelector::visitVecStridedStoreInst(VecStridedStoreInst *inst)
{
    // vecstridedstore %v, %ptr, %strideBytes, %vl —— vsse32.v vs3, (ptr), stride
    auto val = getOrCreateVectorReg(inst->getValue());
    auto ptr = getOrCreateVirtualReg(inst->getPointerOperand());
    auto stride = getOrCreateVirtualReg(inst->getStride());
    (void)inst->getVl();
    currentBB->addInstruction(RISCVInstruction::createVectorStridedMemory(
        RISCVOpcode::VSSE32_V, val, ptr, stride));
}

void InstructionSelector::visitVecBinaryInst(VecBinaryInst *inst)
{
    auto lhs = getOrCreateVectorReg(inst->getLHS());
    auto rhs = getOrCreateVectorReg(inst->getRHS());
    auto vd = getOrCreateVectorReg(inst);
    bool isFloat = false;
    if (auto *vt = dynamic_cast<VectorType *>(inst->getType()))
        isFloat = vt->getElementType() && vt->getElementType()->isFloatTy();
    RISCVOpcode op;
    switch (inst->getOpcode())
    {
    case Opcode::VecAdd:
        op = isFloat ? RISCVOpcode::VFADD_VV : RISCVOpcode::VADD_VV;
        break;
    case Opcode::VecSub:
        op = isFloat ? RISCVOpcode::VFSUB_VV : RISCVOpcode::VSUB_VV;
        break;
    case Opcode::VecMul:
        op = isFloat ? RISCVOpcode::VFMUL_VV : RISCVOpcode::VMUL_VV;
        break;
    case Opcode::VecDiv:
        op = isFloat ? RISCVOpcode::VFDIV_VV : RISCVOpcode::VDIV_VV;
        break;
    case Opcode::VecSll:
        op = RISCVOpcode::VSLL_VV;
        break;
    case Opcode::VecSrl:
        op = RISCVOpcode::VSRL_VV;
        break;
    case Opcode::VecSra:
        op = RISCVOpcode::VSRA_VV;
        break;
    case Opcode::VecMax:
        op = RISCVOpcode::VMAX_VV;
        break;
    case Opcode::VecMin:
        op = RISCVOpcode::VMIN_VV;
        break;
    case Opcode::VecRem:
        op = RISCVOpcode::VREM_VV;
        break;
    default:
        op = isFloat ? RISCVOpcode::VFMUL_VV : RISCVOpcode::VMUL_VV;
        break;
    }
    currentBB->addInstruction(RISCVInstruction::createVectorBinary(op, vd, lhs, rhs));
}

void InstructionSelector::visitVecSplatInst(VecSplatInst *inst)
{
    // %v = vecsplat %x —— int: vmv.v.x；float: vfmv.v.f
    // 注意：splat 只写当前 VL 范围内的活动元素，须紧跟循环内 vsetvli。
    auto scalar = getOrCreateVirtualReg(inst->getScalar());
    auto vd = getOrCreateVectorReg(inst);
    bool isFloat = false;
    if (auto *vt = dynamic_cast<VectorType *>(inst->getType()))
        isFloat = vt->getElementType() && vt->getElementType()->isFloatTy();
    else if (inst->getScalar() && inst->getScalar()->getType())
        isFloat = inst->getScalar()->getType()->isFloatTy();
    if (isFloat)
        currentBB->addInstruction(RISCVInstruction::createVectorFloatSplat(vd, scalar));
    else
        currentBB->addInstruction(RISCVInstruction::createVectorSplat(vd, scalar));
}

void InstructionSelector::visitVecVidInst(VecVidInst *inst)
{
    // %v = vecvid %vl —— vid.v vd（生成 0..vl-1）
    auto vd = getOrCreateVectorReg(inst);
    (void)inst->getVl();
    currentBB->addInstruction(RISCVInstruction::createVectorVid(vd));
}

void InstructionSelector::visitVecReduceAddInst(VecReduceAddInst *inst)
{
    // int:   vredsum.vs + vmv.x.s
    // float: vfredosum.vs + vfmv.f.s（有序归约，贴近标量从左到右累加）
    auto vec = getOrCreateVectorReg(inst->getVector());
    auto rd = getOrCreateVirtualReg(inst);
    auto vd = make_shared<RISCVRegister>(RegisterType::VECTOR);
    bool isFloat = inst->getType() && inst->getType()->isFloatTy();
    if (isFloat)
    {
        auto zeroF = getOrCreateVirtualReg(new ConstantFloat(FloatType::getInstance(), 0.0f));
        currentBB->addInstruction(RISCVInstruction::createVectorFloatSplat(vd, zeroF));
        currentBB->addInstruction(
            RISCVInstruction::createVectorBinary(RISCVOpcode::VFREDOSUM_VS, vd, vec, vd));
        currentBB->addInstruction(RISCVInstruction::createVectorFloatExtract(rd, vd));
    }
    else
    {
        auto zeroReg = make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::ZERO);
        currentBB->addInstruction(RISCVInstruction::createVectorSplat(vd, zeroReg));
        currentBB->addInstruction(
            RISCVInstruction::createVectorBinary(RISCVOpcode::VREDSUM_VS, vd, vec, vd));
        currentBB->addInstruction(RISCVInstruction::createVectorExtract(rd, vd));
    }
}

void InstructionSelector::visitVecReduceMaxInst(VecReduceMaxInst *inst)
{
    auto vec = getOrCreateVectorReg(inst->getVector());
    auto rd = getOrCreateVirtualReg(inst);
    auto vd = make_shared<RISCVRegister>(RegisterType::VECTOR);
    auto seed = getOrCreateVirtualReg(new ConstantInt(IntegerType::getInstance(), -2147483648));
    currentBB->addInstruction(RISCVInstruction::createVectorSplat(vd, seed));
    currentBB->addInstruction(
        RISCVInstruction::createVectorBinary(RISCVOpcode::VREDMAX_VS, vd, vec, vd));
    currentBB->addInstruction(RISCVInstruction::createVectorExtract(rd, vd));
}

void InstructionSelector::visitVecReduceMinInst(VecReduceMinInst *inst)
{
    auto vec = getOrCreateVectorReg(inst->getVector());
    auto rd = getOrCreateVirtualReg(inst);
    auto vd = make_shared<RISCVRegister>(RegisterType::VECTOR);
    auto seed = getOrCreateVirtualReg(new ConstantInt(IntegerType::getInstance(), 2147483647));
    currentBB->addInstruction(RISCVInstruction::createVectorSplat(vd, seed));
    currentBB->addInstruction(
        RISCVInstruction::createVectorBinary(RISCVOpcode::VREDMIN_VS, vd, vec, vd));
    currentBB->addInstruction(RISCVInstruction::createVectorExtract(rd, vd));
}

