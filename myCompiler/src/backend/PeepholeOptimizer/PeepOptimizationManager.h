#pragma once

#include "../RISCVDataStructure.h"
#include <vector>
#include <memory>
#include <string>

using std::shared_ptr;
using std::string;
using std::vector;

// 确保使用RISCV命名空间中的类型
using namespace RISCV;

enum PeepOptiState
{
    DELETE,
    MODIFY,
    KEEP
};

class PeepPass
{
private:
    string name; // 优化Pass的名称
public:
    PeepPass(const string &name) : name(name) {}
    virtual ~PeepPass() = default; // 添加虚析构函数
    const string &getName() const { return name; }
    // 优化函数，接受指令和基本块
    virtual PeepOptiState optimize(shared_ptr<RISCVInstruction> instr, shared_ptr<RISCVBasicBlock> bb) = 0;
};

class PeepOptimizationManager
{
private:
    vector<shared_ptr<PeepPass>> passes;

public:
    PeepOptimizationManager() {}
    ~PeepOptimizationManager() {}

    void addPass(shared_ptr<PeepPass> pass);
    void optimizeFunction(shared_ptr<RISCVFunction> func);
    void optimizeBasicBlock(shared_ptr<RISCVBasicBlock> bb, bool skipAddressFusion = false);
    void optimizeModule(shared_ptr<RISCVModule> module);
};
