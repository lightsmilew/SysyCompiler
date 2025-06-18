# SysY 编译器中间代码优化集成指南

## 概述

本文档介绍如何将中间代码优化 Pass 集成到您的 SysY 编译器项目中。基于对 BUAA-2022-SysYCompiler 和 T202410486202978-1811 两个项目的分析，我们提供了一套完整的优化框架。

## 优化策略分析

### 1. BUAA 项目的优化特点

- **简洁而有效**：实现了核心的 SSA 优化（Mem2Reg）
- **GVN 优化**：通过哈希值进行公共子表达式消除
- **基本块合并**：减少跳转指令
- **优化流程清晰**：O1 级别的优化流水线

### 2. T202410486 项目的优化特点

- **完整的 Pass 系统**：模块化的优化 Pass 设计
- **迭代优化**：使用 FixedPoint 直到收敛
- **数据流分析**：支持活跃变量分析、可用表达式分析
- **高级优化**：循环优化、函数内联等

## 集成到 SysY 项目的步骤

### 第一步：基础架构搭建

1. **创建优化 Pass 基类**

```cpp
class Pass {
public:
    virtual bool runOnFunction(Function* func) = 0;
    virtual std::string getName() const = 0;
};
```

2. **实现 Pass 管理器**

```cpp
class PassManager {
    std::vector<std::unique_ptr<Pass>> passes;
public:
    void addPass(std::unique_ptr<Pass> pass);
    bool runOnModule(Module* module);
};
```

### 第二步：实现基础优化 Pass

#### 1. 死代码消除 (Dead Code Elimination)

- **原理**：删除不会影响程序输出的指令
- **实现要点**：
  - 标记关键指令（return、store、call 等）
  - 反向传播活跃性
  - 删除未标记的指令

#### 2. 常量折叠 (Constant Folding)

- **原理**：在编译时计算常量表达式
- **实现要点**：
  - 识别常量操作数
  - 执行编译时计算
  - 替换指令为常量

#### 3. 公共子表达式消除 (CSE)

- **原理**：避免重复计算相同的表达式
- **实现要点**：
  - 为表达式生成哈希键值
  - 维护表达式到值的映射
  - 替换重复计算

#### 4. 复制传播 (Copy Propagation)

- **原理**：将复制操作的源直接传播到使用点
- **实现要点**：
  - 识别复制指令
  - 跟踪复制链
  - 更新使用点

### 第三步：实现优化管道

```cpp
std::unique_ptr<PassManager> createOptimizationPipeline(OptimizationLevel level) {
    auto manager = std::make_unique<PassManager>();

    switch (level) {
        case O1:
            manager->addPass(std::make_unique<ConstantFoldingPass>());
            manager->addPass(std::make_unique<CopyPropagationPass>());
            manager->addPass(std::make_unique<DeadCodeEliminationPass>());
            break;
        case O2:
            // 添加更多优化Pass
            break;
    }

    return manager;
}
```

### 第四步：集成到编译流程

在您的编译器主流程中，在 IR 生成后、代码生成前添加优化阶段：

```cpp
bool SysYCompiler::compile(const std::string& sourceFile) {
    // 1. 解析和生成IR
    parseAndGenerateIR(sourceFile);

    // 2. 优化IR
    auto passManager = createOptimizationPipeline(optLevel);
    passManager->runOnModule(module.get());

    // 3. 生成目标代码
    generateTargetCode();
}
```

## 具体实现建议

### 1. 适配您的 IR 结构

您需要根据 IRDataStructure.h 中的定义，调整优化 Pass 中的接口：

```cpp
// 示例：根据您的Instruction类调整
class DeadCodeEliminationPass : public Pass {
    bool isInstructionCritical(Instruction* inst) {
        // 根据您的指令类型系统调整
        return inst->isTerminator() || inst->mayHaveSideEffects();
    }
};
```

### 2. 数据流分析支持

对于更高级的优化，您可能需要实现数据流分析：

```cpp
class DataFlowAnalysis {
public:
    virtual void runAnalysis(Function* func) = 0;
    virtual bool isLiveAtExit(Value* val, BasicBlock* bb) = 0;
};
```

### 3. 调试和验证

添加验证 Pass 来确保优化的正确性：

```cpp
class VerifierPass : public Pass {
public:
    bool runOnFunction(Function* func) override {
        // 验证IR的一致性
        return verifyFunction(func);
    }
};
```

## 优化效果预期

### O1 级别优化预期效果：

- **代码大小减少**：10-20%
- **执行效率提升**：5-15%
- **编译时间增加**：20-50%

### O2 级别优化预期效果：

- **代码大小减少**：20-40%
- **执行效率提升**：15-30%
- **编译时间增加**：50-100%

## 调试建议

1. **添加详细日志**：

```cpp
if (verbose) {
    std::cout << "Running " << pass->getName() << std::endl;
    std::cout << "  Before: " << countInstructions(func) << " instructions" << std::endl;
    // 运行优化
    std::cout << "  After: " << countInstructions(func) << " instructions" << std::endl;
}
```

2. **输出中间结果**：

```cpp
func->dump("before_opt.ll");
passManager->runOnModule(module);
func->dump("after_opt.ll");
```

3. **单独测试 Pass**：

```cpp
// 单独测试死代码消除
auto dcePass = std::make_unique<DeadCodeEliminationPass>();
bool changed = dcePass->runOnFunction(testFunc);
```

## 扩展建议

### 未来可以添加的高级优化：

1. **循环优化**：

   - 循环不变代码外提 (LICM)
   - 循环展开 (Loop Unrolling)
   - 循环强度削减 (Strength Reduction)

2. **SSA 优化**：

   - Mem2Reg (内存到寄存器)
   - Phi 节点消除
   - SCCP (稀疏条件常量传播)

3. **函数级优化**：
   - 函数内联 (Inlining)
   - 尾递归消除 (Tail Recursion Elimination)
   - 参数提升 (Argument Promotion)

## 总结

通过分析两个成功的编译器项目，我们为您的 SysY 编译器提供了一套完整的中间代码优化方案。建议您：

1. **循序渐进**：先实现基础优化，再添加高级优化
2. **充分测试**：每个优化 Pass 都要经过充分的测试
3. **性能监控**：监控优化前后的性能变化
4. **模块化设计**：保持 Pass 之间的独立性，便于调试和维护

这套优化框架将显著提升您的编译器生成代码的质量和执行效率。
