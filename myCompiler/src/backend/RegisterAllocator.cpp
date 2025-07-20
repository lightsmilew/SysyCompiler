#include "RegisterAllocator.h"
#include <iostream>
#include <algorithm>
using namespace RISCV;

// 可用的物理寄存器定义 - 包含参数寄存器
const vector<shared_ptr<RISCVRegister>> RegisterAllocator::availableGeneralRegs = {
    // 临时寄存器 (caller-saved) - 优先使用
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T0, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T1, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T2, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T3, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T4, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T5, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::T6, RegisterType::GENERAL),

    // 参数寄存器 (caller-saved) - 在参数解耦后可用于分配
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A0, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A1, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A2, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A3, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A4, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A5, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A6, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::A7, RegisterType::GENERAL),

    // 保存寄存器 (callee-saved) - 需要保存/恢复，优先级较低
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S0, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S1, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S2, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S3, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S4, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S5, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S6, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S7, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S8, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S9, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S10, RegisterType::GENERAL),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::S11, RegisterType::GENERAL)};

const vector<shared_ptr<RISCVRegister>> RegisterAllocator::availableFloatRegs = {
    // 临时浮点寄存器 (caller-saved) - 优先使用
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT0, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT1, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT2, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT3, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT4, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT5, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT6, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT7, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT8, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT9, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT10, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FT11, RegisterType::FLOAT),

    // 浮点参数寄存器 (caller-saved) - 在参数解耦后可用于分配
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA0, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA1, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA2, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA3, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA4, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA5, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA6, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FA7, RegisterType::FLOAT),

    // 保存浮点寄存器 (callee-saved) - 需要保存/恢复，优先级较低
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS0, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS1, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS2, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS3, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS4, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS5, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS6, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS7, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS8, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS9, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS10, RegisterType::FLOAT),
    make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::FS11, RegisterType::FLOAT)};

// 主要接口：为函数分配寄存器
void RegisterAllocator::allocateRegisters(shared_ptr<RISCVFunction> func)
{
    currentFunc = func;

    // 清空所有数据结构
    adjList.clear();
    adjSet.clear();
    degree.clear();
    moveList.clear();
    moveRelated.clear();

    simplifyWorklist.clear();
    freezeWorklist.clear();
    spillWorklist.clear();
    spilledNodes.clear();
    coalescedNodes.clear();
    coloredNodes.clear();
    while (!selectStack.empty())
        selectStack.pop();

    coalescedMoves.clear();
    constrainedMoves.clear();
    frozenMoves.clear();
    worklistMoves.clear();
    activeMoves.clear();

    alias.clear();
    color.clear();
    precolored.clear();
    initial.clear();

    // 执行图染色算法的主循环
    do
    {
        build();
        makeWorklist();

        do
        {
            if (!simplifyWorklist.empty())
            {
                simplify();
            }
            else if (!worklistMoves.empty())
            {
                coalesce();
            }
            else if (!freezeWorklist.empty())
            {
                freeze();
            }
            else if (!spillWorklist.empty())
            {
                selectSpill();
            }
        } while (!simplifyWorklist.empty() || !worklistMoves.empty() ||
                 !freezeWorklist.empty() || !spillWorklist.empty());

        assignColors();

        if (!spilledNodes.empty())
        {
            rewriteProgram();
        }
    } while (!spilledNodes.empty());

    printStatistics();
}
// ============================================================================
// 算法主要阶段实现
// ============================================================================

// Build阶段：构建冲突图和收集移动指令
void RegisterAllocator::build()
{
    // 初始化预着色寄存器
    initializePrecoloredRegisters();

    // 收集所有寄存器
    collectAllRegisters();

    // 收集移动指令
    collectMoveInstructions();

    // 构建冲突图
    const auto &livenessInfo = currentFunc->getLivenessInfo();

    // 遍历所有寄存器对，检查是否冲突
    for (auto reg1 : initial)
    {
        for (auto reg2 : initial)
        {
            if (reg1 != reg2 && livenessInfo.interferes(reg1, reg2))
            {
                addEdge(reg1, reg2);
            }
        }
    }

    // 预着色寄存器之间也可能有冲突
    for (auto reg1 : precolored)
    {
        for (auto reg2 : precolored)
        {
            if (reg1 != reg2 && reg1->getType() == reg2->getType())
            {
                addEdge(reg1, reg2);
            }
        }
    }
}

// MakeWorklist阶段：将节点分类到不同的工作列表
void RegisterAllocator::makeWorklist()
{
    for (auto n : initial)
    {
        if (degree[n] >= getK(n))
        {
            spillWorklist.insert(n);
        }
        else if (isMoveRelated(n))
        {
            freezeWorklist.insert(n);
        }
        else
        {
            simplifyWorklist.insert(n);
        }
    }
    initial.clear();
}

// Simplify阶段：移除非移动相关的低度数节点
void RegisterAllocator::simplify()
{
    auto n = *simplifyWorklist.begin();
    simplifyWorklist.erase(n);
    selectStack.push(n);

    for (auto m : getAdjacent(n))
    {
        decrementDegree(m);
    }
}

// Coalesce阶段：保守合并移动指令
void RegisterAllocator::coalesce()
{
    auto moveIt = worklistMoves.begin();
    auto movePair = *moveIt;
    worklistMoves.erase(moveIt);

    auto x = getAlias(movePair.first);
    auto y = getAlias(movePair.second);

    shared_ptr<RISCVRegister> u, v;
    if (isPrecolored(y))
    {
        u = y;
        v = x;
    }
    else
    {
        u = x;
        v = y;
    }

    if (u == v)
    {
        // 移动指令是冗余的
        coalescedMoves.insert(movePair);
        addWorkList(u);
    }
    else if (isPrecolored(v) || adjSet.count({u, v}))
    {
        // 移动指令是受约束的
        constrainedMoves.insert(movePair);
        addWorkList(u);
        addWorkList(v);
    }
    else if ((isPrecolored(u) && ok(v, u)) ||
             (!isPrecolored(u) && conservative(u, v)))
    {
        // 可以安全合并
        coalescedMoves.insert(movePair);
        combine(u, v);
        addWorkList(u);
    }
    else
    {
        // 移动指令变为活跃状态
        activeMoves.insert(movePair);
    }
}

// Freeze阶段：冻结移动指令
void RegisterAllocator::freeze()
{
    auto u = *freezeWorklist.begin();
    freezeWorklist.erase(u);
    simplifyWorklist.insert(u);
    freezeMoves(u);
}

// SelectSpill阶段：选择溢出节点
void RegisterAllocator::selectSpill()
{
    // 简单的溢出选择策略：选择度数最高的节点
    shared_ptr<RISCVRegister> m = nullptr;
    int maxDegree = -1;

    for (auto n : spillWorklist)
    {
        if (degree[n] > maxDegree)
        {
            maxDegree = degree[n];
            m = n;
        }
    }

    if (m)
    {
        spillWorklist.erase(m);
        simplifyWorklist.insert(m);
        freezeMoves(m);
    }
}

// AssignColors阶段：为节点分配颜色
void RegisterAllocator::assignColors()
{
    while (!selectStack.empty())
    {
        auto n = selectStack.top();
        selectStack.pop();

        unordered_set<shared_ptr<RISCVRegister>> okColors;

        // 获取可用颜色
        if (n->getType() == RegisterType::GENERAL)
        {
            for (auto reg : availableGeneralRegs)
            {
                okColors.insert(reg);
            }
        }
        else if (n->getType() == RegisterType::FLOAT)
        {
            for (auto reg : availableFloatRegs)
            {
                okColors.insert(reg);
            }
        }

        // 移除邻居已使用的颜色
        for (auto w : adjList[n])
        {
            auto aliasW = getAlias(w);
            if (coloredNodes.count(aliasW) || precolored.count(aliasW))
            {
                if (color.count(aliasW))
                {
                    okColors.erase(color[aliasW]);
                }
                else if (precolored.count(aliasW))
                {
                    okColors.erase(aliasW);
                }
            }
        }

        if (okColors.empty())
        {
            spilledNodes.insert(n);
        }
        else
        {
            coloredNodes.insert(n);
            color[n] = *okColors.begin();
        }
    }

    // 为合并的节点分配颜色
    for (auto n : coalescedNodes)
    {
        color[n] = color[getAlias(n)];
    }
}

// RewriteProgram阶段：重写程序处理溢出
void RegisterAllocator::rewriteProgram()
{
    // 为溢出的寄存器在栈上分配空间
    for (auto spilledReg : spilledNodes)
    {
        string spillName = "spill_" + spilledReg->toString();
        currentFunc->getStackFrame().allocateValueSpace(spillName, 4);
    }

    // 重写指令，在使用前加载，在定义后存储
    for (auto &bb : currentFunc->getBasicBlocks())
    {
        vector<shared_ptr<RISCVInstruction>> newInstructions;

        for (auto instr : bb->getInstructions())
        {
            vector<shared_ptr<RISCVInstruction>> beforeInstr;
            vector<shared_ptr<RISCVInstruction>> afterInstr;

            // 处理使用的溢出寄存器
            for (auto useReg : instr->getUseRegisters())
            {
                if (spilledNodes.count(useReg))
                {
                    // 创建临时寄存器并从栈加载
                    auto tempReg = make_shared<RISCVRegister>(useReg->getType());
                    string spillName = "spill_" + useReg->toString();
                    int offset = currentFunc->getStackFrame().getValueOffset(spillName);

                    // 加载指令
                    auto loadInstr = RISCVInstruction::createIType(
                        RISCVOpcode::LW, tempReg,
                        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP, RegisterType::GENERAL),
                        offset);
                    beforeInstr.push_back(loadInstr);

                    // 替换指令中的寄存器引用
                    // 这里需要修改指令的操作数，但当前的指令结构不支持直接修改
                    // 在实际实现中需要重新构造指令
                }
            }

            // 处理定义的溢出寄存器
            for (auto defReg : instr->getDefRegisters())
            {
                if (spilledNodes.count(defReg))
                {
                    // 创建临时寄存器并存储到栈
                    auto tempReg = make_shared<RISCVRegister>(defReg->getType());
                    string spillName = "spill_" + defReg->toString();
                    int offset = currentFunc->getStackFrame().getValueOffset(spillName);

                    // 存储指令
                    auto storeInstr = RISCVInstruction::createSType(
                        RISCVOpcode::SW, tempReg,
                        make_shared<RISCVRegister>(RISCVRegister::PhysicalReg::SP, RegisterType::GENERAL),
                        offset);
                    afterInstr.push_back(storeInstr);
                }
            }

            // 添加指令到新的指令列表
            for (auto beforeI : beforeInstr)
            {
                newInstructions.push_back(beforeI);
            }
            newInstructions.push_back(instr);
            for (auto afterI : afterInstr)
            {
                newInstructions.push_back(afterI);
            }
        }

        // 替换基本块的指令列表
        // 注意：这里需要修改RISCVBasicBlock类来支持替换整个指令列表
        // 当前实现只是示意
    }

    // 清空溢出节点，准备下一轮
    spilledNodes.clear();
}
// ============================================================================
// 辅助函数实现
// ============================================================================

// 添加冲突边
void RegisterAllocator::addEdge(shared_ptr<RISCVRegister> u, shared_ptr<RISCVRegister> v)
{
    if (!adjSet.count({u, v}) && u != v)
    {
        adjSet.insert({u, v});
        adjSet.insert({v, u});

        if (!isPrecolored(u))
        {
            adjList[u].insert(v);
            degree[u]++;
        }
        if (!isPrecolored(v))
        {
            adjList[v].insert(u);
            degree[v]++;
        }
    }
}

// 检查两个节点是否相邻
bool RegisterAllocator::adjacent(shared_ptr<RISCVRegister> n, shared_ptr<RISCVRegister> m)
{
    return adjSet.count({n, m}) > 0;
}

// 获取节点的相邻节点（排除已合并和已选择的节点）
unordered_set<shared_ptr<RISCVRegister>> RegisterAllocator::getAdjacent(shared_ptr<RISCVRegister> n)
{
    unordered_set<shared_ptr<RISCVRegister>> result;
    for (auto m : adjList[n])
    {
        if (!coalescedNodes.count(m) && !coloredNodes.count(m))
        {
            result.insert(m);
        }
    }
    return result;
}

// 获取节点相关的移动指令
unordered_set<pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>>, RegisterAllocator::RegisterPairHash>
RegisterAllocator::getNodeMoves(shared_ptr<RISCVRegister> n)
{
    unordered_set<pair<shared_ptr<RISCVRegister>, shared_ptr<RISCVRegister>>, RegisterPairHash> result;

    for (auto move : moveList)
    {
        if (move.first == n || move.second == n)
        {
            if (activeMoves.count(move) || worklistMoves.count(move))
            {
                result.insert(move);
            }
        }
    }
    return result;
}

// 检查节点是否与移动指令相关
bool RegisterAllocator::isMoveRelated(shared_ptr<RISCVRegister> n)
{
    return !getNodeMoves(n).empty();
}

// 减少节点度数
void RegisterAllocator::decrementDegree(shared_ptr<RISCVRegister> m)
{
    int d = degree[m];
    degree[m] = d - 1;

    if (d == getK(m))
    {
        // 启用与m相关的移动
        unordered_set<shared_ptr<RISCVRegister>> nodes = {m};
        for (auto n : getAdjacent(m))
        {
            nodes.insert(n);
        }
        enableMoves(nodes);

        spillWorklist.erase(m);
        if (isMoveRelated(m))
        {
            freezeWorklist.insert(m);
        }
        else
        {
            simplifyWorklist.insert(m);
        }
    }
}

// 启用移动指令
void RegisterAllocator::enableMoves(const unordered_set<shared_ptr<RISCVRegister>> &nodes)
{
    for (auto n : nodes)
    {
        for (auto move : getNodeMoves(n))
        {
            if (activeMoves.count(move))
            {
                activeMoves.erase(move);
                worklistMoves.insert(move);
            }
        }
    }
}

// 添加到工作列表
void RegisterAllocator::addWorkList(shared_ptr<RISCVRegister> u)
{
    if (!isPrecolored(u) && !isMoveRelated(u) && degree[u] < getK(u))
    {
        freezeWorklist.erase(u);
        simplifyWorklist.insert(u);
    }
}

// OK测试：检查合并是否安全
bool RegisterAllocator::ok(shared_ptr<RISCVRegister> t, shared_ptr<RISCVRegister> r)
{
    return degree[t] < getK(t) || isPrecolored(t) || adjSet.count({t, r});
}

// 保守测试：检查合并后是否仍可着色
bool RegisterAllocator::conservative(shared_ptr<RISCVRegister> u, shared_ptr<RISCVRegister> v)
{
    unordered_set<shared_ptr<RISCVRegister>> nodes;
    for (auto n : getAdjacent(u))
    {
        nodes.insert(n);
    }
    for (auto n : getAdjacent(v))
    {
        nodes.insert(n);
    }

    int k = 0;
    for (auto n : nodes)
    {
        if (degree[n] >= getK(n))
        {
            k++;
        }
    }

    return k < getK(u);
}

// 获取节点的别名（处理合并）
shared_ptr<RISCVRegister> RegisterAllocator::getAlias(shared_ptr<RISCVRegister> n)
{
    if (coalescedNodes.count(n))
    {
        return getAlias(alias[n]);
    }
    else
    {
        return n;
    }
}

// 合并两个节点
void RegisterAllocator::combine(shared_ptr<RISCVRegister> u, shared_ptr<RISCVRegister> v)
{
    if (freezeWorklist.count(v))
    {
        freezeWorklist.erase(v);
    }
    else
    {
        spillWorklist.erase(v);
    }

    coalescedNodes.insert(v);
    alias[v] = u;

    // 合并移动列表
    for (auto move : getNodeMoves(v))
    {
        if (move.first == v)
        {
            moveList.insert({u, move.second});
        }
        else
        {
            moveList.insert({move.first, u});
        }
    }

    enableMoves({v});

    for (auto t : getAdjacent(v))
    {
        addEdge(t, u);
        decrementDegree(t);
    }

    if (degree[u] >= getK(u) && freezeWorklist.count(u))
    {
        freezeWorklist.erase(u);
        spillWorklist.insert(u);
    }
}

// 冻结移动指令
void RegisterAllocator::freezeMoves(shared_ptr<RISCVRegister> u)
{
    for (auto move : getNodeMoves(u))
    {
        auto x = move.first;
        auto y = move.second;

        shared_ptr<RISCVRegister> v;
        if (getAlias(y) == getAlias(u))
        {
            v = getAlias(x);
        }
        else
        {
            v = getAlias(y);
        }

        activeMoves.erase(move);
        frozenMoves.insert(move);

        if (!isMoveRelated(v) && degree[v] < getK(v))
        {
            freezeWorklist.erase(v);
            simplifyWorklist.insert(v);
        }
    }
}
//== == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == == ==
// 工具函数实现
// ============================================================================

// 获取寄存器类型对应的K值
int RegisterAllocator::getK(shared_ptr<RISCVRegister> reg)
{
    if (reg->getType() == RegisterType::GENERAL)
    {
        return K_GENERAL;
    }
    else if (reg->getType() == RegisterType::FLOAT)
    {
        return K_FLOAT;
    }
    return 0;
}

// 检查寄存器是否为预着色寄存器
bool RegisterAllocator::isPrecolored(shared_ptr<RISCVRegister> reg)
{
    return precolored.count(reg) > 0;
}

// 初始化预着色寄存器
void RegisterAllocator::initializePrecoloredRegisters()
{
    precolored.clear();

    // 添加通用寄存器
    for (auto reg : availableGeneralRegs)
    {
        precolored.insert(reg);
        color[reg] = reg; // 预着色寄存器的颜色就是自己
    }

    // 添加浮点寄存器
    for (auto reg : availableFloatRegs)
    {
        precolored.insert(reg);
        color[reg] = reg; // 预着色寄存器的颜色就是自己
    }
}

// 收集移动指令
void RegisterAllocator::collectMoveInstructions()
{
    moveList.clear();
    worklistMoves.clear();

    for (auto &bb : currentFunc->getBasicBlocks())
    {
        for (auto instr : bb->getInstructions())
        {
            // 检查是否为移动指令
            if (instr->getOpcode() == RISCVOpcode::MV ||
                instr->getOpcode() == RISCVOpcode::FMV_S)
            {

                auto defRegs = instr->getDefRegisters();
                auto useRegs = instr->getUseRegisters();

                if (!defRegs.empty() && !useRegs.empty())
                {
                    auto dst = defRegs[0];
                    auto src = useRegs[0];

                    // 只处理虚拟寄存器之间的移动
                    if (dst->isVirtual() && src->isVirtual())
                    {
                        moveList.insert({src, dst});
                        worklistMoves.insert({src, dst});
                    }
                }
            }
        }
    }
}

// 收集所有寄存器
void RegisterAllocator::collectAllRegisters()
{
    initial.clear();

    for (auto &bb : currentFunc->getBasicBlocks())
    {
        for (auto instr : bb->getInstructions())
        {
            // 收集定义的寄存器
            for (auto reg : instr->getDefRegisters())
            {
                if (reg->isVirtual())
                {
                    initial.insert(reg);
                    degree[reg] = 0; // 初始化度数
                }
            }

            // 收集使用的寄存器
            for (auto reg : instr->getUseRegisters())
            {
                if (reg->isVirtual())
                {
                    initial.insert(reg);
                    degree[reg] = 0; // 初始化度数
                }
            }
        }
    }
}

// 调试和统计信息
void RegisterAllocator::printStatistics()
{
    std::cout << "=== Register Allocation Statistics ===" << std::endl;
    std::cout << "Function: " << currentFunc->getName() << std::endl;
    std::cout << "Total virtual registers: " << initial.size() << std::endl;
    std::cout << "Colored registers: " << coloredNodes.size() << std::endl;
    std::cout << "Coalesced registers: " << coalescedNodes.size() << std::endl;
    std::cout << "Spilled registers: " << spilledNodes.size() << std::endl;
    std::cout << "Coalesced moves: " << coalescedMoves.size() << std::endl;
    std::cout << "Frozen moves: " << frozenMoves.size() << std::endl;
    std::cout << "=======================================" << std::endl;
}

// 验证分配结果
void RegisterAllocator::validateAllocation()
{
    // 检查所有虚拟寄存器都已分配
    for (auto reg : initial)
    {
        if (!coloredNodes.count(reg) && !coalescedNodes.count(reg) && !spilledNodes.count(reg))
        {
            std::cerr << "Error: Register " << reg->toString() << " not allocated!" << std::endl;
        }
    }

    // 检查冲突图约束
    for (auto &pair : adjSet)
    {
        auto reg1 = pair.first;
        auto reg2 = pair.second;

        auto color1 = color.count(reg1) ? color[reg1] : nullptr;
        auto color2 = color.count(reg2) ? color[reg2] : nullptr;

        if (color1 && color2 && color1 == color2)
        {
            std::cerr << "Error: Adjacent registers " << reg1->toString()
                      << " and " << reg2->toString() << " have same color!" << std::endl;
        }
    }
}