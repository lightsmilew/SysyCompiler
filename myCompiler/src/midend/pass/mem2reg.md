# mem2reg 算法简述

## 步骤一：分析可提升的 alloca（仅处理标量局部变量）

- 只提升局部的、非地址逃逸的、非数组的 `alloca`。
- 记录每个 `alloca` 的所有 `store` 和 `load` 指令。

## 步骤二：构建支配树和支配边界

- 用于确定哪些基本块需要插入 `phi` 节点。
- 可以使用现成的算法（如 dominance frontier）。

## 步骤三：插入 phi 节点

- 在需要合流的基本块插入 `phi`。
- `phi` 的每个输入来自前驱块的 SSA 值。

## 步骤四：重命名变量（SSA 重命名）

- 用栈追踪每个变量的当前 SSA 值。
- `store` 时压栈，`load` 时用栈顶，`phi` 时合流。
- 替换所有 `load` 为 SSA 值，`store` 变为赋值，`phi` 变为 SSA 合流。

## 步骤五：删除原有 alloca/store/load

- 只保留 SSA 变量和 `phi` 指令。

---

## 伪代码（简化版）

1. 扫描函数入口，收集所有局部 `alloca`。
2. 对每个 `alloca`：
    - 记录所有 `store`/`load` 的基本块。
    - 计算需要插入 `phi` 的块。
    - 在这些块插入 `phi`。
3. 遍历 CFG，做变量重命名：
    - 每遇到 `store`，更新当前 SSA 值。
    - 每遇到 `load`，替换为当前 SSA 值。
    - 合流点用 `phi` 合并。
4. 删除原有 `alloca`/`store`/`load` 指令。

---

## 推荐实践

- 可以先用 LLVM IR + `opt -mem2reg` 自动完成（如果你用 LLVM）。
- 如果自己实现，建议先只支持简单的 `int`/`float` 局部变量，不处理数组/指针/取地址。

---

## 参考资料

- LLVM mem2reg pass 原理
- mem2reg 算法论文
- LLVM 源码 `PromoteMemoryToRegister.cpp`

---

## 总结

- mem2reg 是 IR 优化 pass，不是 IRBuilder 阶段做的。
- 需要遍历 IR，分析 `alloca`/`store`/`load`，插入 `phi`，做 SSA 重命名。
- 最后删除 `alloca`/`store`/`load`，变量全用 SSA 和 `phi`。

## 示例
```
void IRBuilder::visitAssignStmt(std::shared_ptr<ast::AssignStmtNode> node)
{
    // 判断变量是否可SSA化
    auto lvalNode = std::dynamic_pointer_cast<ast::LValueExprNode>(node->lvalue);
    bool isSSA = isPromotableScalar(lvalNode->identifier);

    Value *rvalue = visitExpression(node->rvalue);

    // 类型转换（如果需要）
    Type *targetType = getVariableType(lvalNode->identifier);
    if (rvalue->getType() != targetType)
    {
        rvalue = createCast(rvalue, targetType);
    }

    if (isSSA) {
        // 直接更新 SSA 映射
        varToValue[lvalNode->identifier] = rvalue;
    } else {
        // 原有 alloca/store 逻辑
        Value *lvalue = visitLValueExpr(node->lvalue);
        createStore(rvalue, lvalue);
    }
}
```