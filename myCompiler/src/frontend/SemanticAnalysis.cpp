#include "SemanticAnalysis.h"
#include <memory>
#include <string>
#include <stdexcept>
#include "../common/Common.h"

using std::dynamic_pointer_cast;
using std::to_string;

shared_ptr<Symbol> SymbolTable::lookup(const string &name)
{
  auto it = table.find(name);
  if (it != table.end())
  {
    return it->second;
  }
  // 如果在当前作用域未找到，继续在父作用域中查找
  if (parent)
  {
    return parent->lookup(name);
  }
  return nullptr;
}

void SymbolTable::insert(const string &name, shared_ptr<Symbol> symbol)
{
  // 检查当前作用域中是否已存在同名符号
  if (table.find(name) != table.end())
  {
    throw std::runtime_error("Symbol '" + name +
                             "' already declared in this scope.");
  }
  table[name] = symbol;
}

void SemanticAnalyzer::enterScope()
{
  currentScope = make_shared<SymbolTable>(currentScope);
}

void SemanticAnalyzer::exitScope()
{
  currentScope = currentScope->parent;
}

void SemanticAnalyzer::declareVariable(const std::string &name,
                                       const std::shared_ptr<Symbol> &symbol)
{
  currentScope->insert(name, symbol);
}

shared_ptr<Symbol> SemanticAnalyzer::resolveVariable(const std::string &name)
{
  return currentScope->lookup(name);
}

void SemanticAnalyzer::declareFunction(const std::string &name,
                                       const std::shared_ptr<Symbol> &symbol)
{
  functionTable->insert(name, symbol);
}

shared_ptr<Symbol> SemanticAnalyzer::resolveFunction(const std::string &name)
{
  return functionTable->lookup(name);
}

// 辅助方法实现
void TypeCheckerVisitor::addError(const string &message)
{
  errors.push_back(message);
}
bool TypeCheckerVisitor::checkArrayInit(const shared_ptr<ExprNode> &init, const DataType &declaredType, int dim, bool isConstArray)
{
  if (dim == declaredType.arrayDimensionCount())
  {
    DataType elemType = getExpressionType(init, ExprContext::EXPRESSION);
    if (!isTypeCompatible(elemType, declaredType.baseType))
      return false;
    // 常量数组要求初始化元素必须是常量表达式
    if (isConstArray && !init->isConst)
    {
      addError("Initializer for constant array must be a constant expression");
      return false;
    }
    return true;
  }

  auto list = dynamic_pointer_cast<InitExprNode>(init)->multiInitVal;
  if (!list.empty())
  {
    int maxCount = declaredType._arraySizes[dim];
    int count = 0;
    for (auto &elem : list)
    {
      if (!checkArrayInit(elem, declaredType, dim + 1, isConstArray))
        return false;
      ++count;
      if (count > maxCount)
      {
        addError("Too many initializers for array dimension " + std::to_string(dim));
        return false;
      }
    }
    return true;
  }

  // 平铺递归
  return checkArrayInit(init, declaredType, dim + 1, isConstArray);
}
DataType TypeCheckerVisitor::getExpressionType(shared_ptr<ExprNode> expr, ExprContext context)
{
  if (auto intLiteral = dynamic_pointer_cast<IntLiteralExprNode>(expr))
  {
    return DataType(PrimaryDataType::INT);
  }
  else if (auto floatLiteral = dynamic_pointer_cast<FloatLiteralExprNode>(expr))
  {
    return DataType(PrimaryDataType::FLOAT);
  }
  else if (auto lvalue = dynamic_pointer_cast<LValueExprNode>(expr))
  {
    auto symbol = analyzer.resolveVariable(lvalue->identifier);
    if (symbol)
    {
      // 处理多维数组其中一维传参
      DataType type = symbol->type;
      // 对每个下标，降一维
      for (auto &idx : lvalue->indices)
      {
        if (type.isArray() && !type.arraySizes().empty())
        {
          // 复制一份修改 const不可直接修改
          Vector<int> dims = type.arraySizes();
          dims.erase(dims.begin());
          type._arraySizes = dims;
        }
        else
        {
          // 非法下标访问
          addError("Too many indices for array '" + lvalue->identifier + "'");
          return DataType(PrimaryDataType::VOID);
        }
      }
      return type;
    }
    // 未定义报错
    addError("Variable '" + lvalue->identifier + "' not declared");
    return DataType(PrimaryDataType::VOID); // 错误情况
  }
  else if (auto binary = dynamic_pointer_cast<BinaryExprNode>(expr))
  {
    DataType leftType = getExpressionType(binary->left, context);
    DataType rightType = getExpressionType(binary->right, context);

    // 根据上下文检查是否允许逻辑和比较操作
    if (context == ExprContext::EXPRESSION || context == ExprContext::ARRAY_INDEX)
    {
      // 在普通表达式或数组索引中不能存在与或非和大小比较
      if (binary->op == BinaryOp::And || binary->op == BinaryOp::Or ||
          binary->op == BinaryOp::Lt || binary->op == BinaryOp::Gt ||
          binary->op == BinaryOp::Le || binary->op == BinaryOp::Ge ||
          binary->op == BinaryOp::Eq || binary->op == BinaryOp::Ne)
      {
        string contextStr = (context == ExprContext::EXPRESSION) ? "expression" : "array index";
        addError("Logical or comparison operations are not allowed in " + contextStr);
        return DataType(PrimaryDataType::VOID);
      }
    }

    // 对于比较和逻辑操作，返回类型应该是布尔型（在condition中）
    if (binary->op == BinaryOp::Lt || binary->op == BinaryOp::Gt ||
        binary->op == BinaryOp::Le || binary->op == BinaryOp::Ge ||
        binary->op == BinaryOp::Eq || binary->op == BinaryOp::Ne ||
        binary->op == BinaryOp::And || binary->op == BinaryOp::Or)
    {
      return DataType(PrimaryDataType::INT); // SysY中布尔值用int表示
    }

    // 类型提升规则：int + float → float
    if (leftType.baseType == PrimaryDataType::FLOAT || rightType.baseType == PrimaryDataType::FLOAT)
    {
      return DataType(PrimaryDataType::FLOAT);
    }
    return DataType(PrimaryDataType::INT);
  }
  //+或者-
  else if (auto unary = dynamic_pointer_cast<UnaryExprNode>(expr))
  {
    // 根据上下文检查一元操作符中的Not操作
    if (unary->op == UnaryOp::Not &&
        (context == ExprContext::EXPRESSION || context == ExprContext::ARRAY_INDEX))
    {
      string contextStr = (context == ExprContext::EXPRESSION) ? "expression" : "array index";
      addError("Logical NOT operation is not allowed in " + contextStr);
      return DataType(PrimaryDataType::VOID);
    }

    DataType operandType = getExpressionType(unary->operand, context);

    // NOT操作返回布尔值（int）
    if (unary->op == UnaryOp::Not)
    {
      return DataType(PrimaryDataType::INT);
    }

    return operandType;
  }
  else if (auto call = dynamic_pointer_cast<CallExprNode>(expr))
  {

    // auto it = functionTable.find(call->callee);
    // if (it != functionTable.end())
    // {
    //   return it->second->returnType;
    // }
    auto it = analyzer.resolveVariable(call->callee);
    if (it)
    {
      return it->type;
    }
    addError("Callee Function is not defined");
    // 此处函数未声明应该报错
    return DataType(PrimaryDataType::VOID);
  }
  return DataType(PrimaryDataType::VOID);
}

bool TypeCheckerVisitor::isTypeCompatible(DataType from, DataType to)
{
  // 完全相同的类型（包括数组维度和各维大小）
  if (from.baseType == to.baseType && from.arrayDimensionCount() == to.arrayDimensionCount())
  {
    // 检查各维度的大小是否相同
    if (from.arrayDimensionCount() > 0)
    {
      const auto &fromSizes = from.arraySizes();
      const auto &toSizes = to.arraySizes();
      for (size_t i = 0; i < fromSizes.size(); ++i)
      {
        if (fromSizes[i] != toSizes[i])
        {
          return false; // 数组大小不匹配
        }
      }
    }
    return true;
  }

  // 隐式类型转换：只有当表达式结果为基本类型时才允许转换
  // 注意：这里检查的是表达式计算后的类型，不是变量的声明类型
  // 例如：int a[3]; float b = a[0]; // a[0]的结果类型是int（基本类型），可以转换为float
  if (from.arrayDimensionCount() == 0 && to.arrayDimensionCount() == 0) // 都是基本类型
  {
    // int可以隐式转换为float float可以隐式转换为int
    if (from.baseType == PrimaryDataType::INT && to.baseType == PrimaryDataType::FLOAT)
    {
      return true;
    }
    if (from.baseType == PrimaryDataType::FLOAT && to.baseType == PrimaryDataType::INT)
    {
      return true;
    }

    // 可以添加更多的隐式转换规则，如：
    // bool可以转换为int (true -> 1, false -> 0)
    // if (from.baseType == PrimaryDataType::BOOL && to.baseType == PrimaryDataType::INT)
    // {
    //   return true;
    // }
  }

  return false;
}

bool TypeCheckerVisitor::isValidArrayAccess(shared_ptr<LValueExprNode> lvalue)
{
  auto symbol = analyzer.resolveVariable(lvalue->identifier);
  if (!symbol)
    return false;

  // 检查维度匹配
  if (lvalue->indices.size() > symbol->type.arrayDimensionCount())
  {
    addError("Array access dimension exceeds array dimension count");
    return false;
  }
  // 检查索引为整数
  for (auto &index : lvalue->indices)
  {
    DataType indexType = getExpressionType(index, ExprContext::ARRAY_INDEX);
    if (indexType.baseType != PrimaryDataType::INT || indexType.isArray())
    {
      addError("Array index must be integer type");
      return false;
    }
  }

  return true;
}

void TypeCheckerVisitor::checkFunctionCall(shared_ptr<CallExprNode> call)
{
  // auto it = functionTable.find(call->callee);
  // if (it == functionTable.end())
  // {
  //   addError("Function '" + call->callee + "' not declared");
  //   return;
  // }
  auto it = analyzer.resolveFunction(call->callee);
  if (!it)
  {
    addError("Callee Function is not defined");
    return;
  }
  auto func = it->functionNode;

  // 检查参数个数
  if (call->args.size() != func->params.size())
  {
    addError("Function '" + call->callee + "' expects " +
             to_string(func->params.size()) + " arguments, got " +
             to_string(call->args.size()));
    return;
  }

  // 检查参数类型匹配
  for (size_t i = 0; i < call->args.size(); ++i)
  {
    DataType argType = getExpressionType(call->args[i], ExprContext::EXPRESSION);
    DataType paramType = func->params[i]->type;

    if (!isTypeCompatible(argType, paramType))
    {
      addError("Argument " + to_string(i + 1) + " of function '" + call->callee +
               "' has incompatible type");
    }
  }
}

// Implementation of visitCompUnit
void TypeCheckerVisitor::visitCompUnitForCheck(shared_ptr<CompUnitNode> node)
{
  hasMainFunction = false;

  // 遍历所有子节点
  for (auto &child : node->children)
  {
    if (auto func = dynamic_pointer_cast<FuncNode>(child))
    {
      visitFuncNode(func);
    }
    else if (auto decl = dynamic_pointer_cast<DeclStmtNode>(child))
    {
      visitDeclStmt(decl);
    }
  }

  // 检查是否有且只有一个main函数
  if (!hasMainFunction)
  {
    addError("No main function found");
  }

  analyzer.exitScope(); // 退出全局作用域
}

void TypeCheckerVisitor::visitFuncNode(shared_ptr<FuncNode> node)
{
  currentFunction = node;

  // 检查main函数
  if (node->identifier == "main")
  {
    if (hasMainFunction)
    {
      addError("Multiple main functions declared");
    }
    hasMainFunction = true;

    // main函数必须返回int且无参数
    if (node->returnType.baseType != PrimaryDataType::INT)
    {
      addError("Main function must return int");
    }
    if (!node->params.empty())
    {
      addError("Main function cannot have parameters");
    }
  }

  // 注册函数到符号表
  auto funcSymbol = make_shared<Symbol>(currentFunction);
  analyzer.declareFunction(node->identifier, funcSymbol);

  // 进入函数作用域
  analyzer.enterScope();

  // 声明参数
  for (auto &param : node->params)
  {
    auto symbol = make_shared<Symbol>(param->type, true); // 参数默认已初始化
    analyzer.declareVariable(param->identifier, symbol);
  }

  // 访问函数体
  if (node->body)
  {
    visitBlockStmt(node->body);
  }

  analyzer.exitScope();
  currentFunction = nullptr;
}

void TypeCheckerVisitor::visitBlockStmt(shared_ptr<BlockStmtNode> node)
{
  analyzer.enterScope();

  for (auto &stmt : node->stmts)
  {
    if (auto declStmt = dynamic_pointer_cast<DeclStmtNode>(stmt))
    {
      visitDeclStmt(declStmt);
    }
    else if (auto exprStmt = dynamic_pointer_cast<ExprStmtNode>(stmt))
    {
      visitExprStmt(exprStmt);
    }
    else if (auto assignStmt = dynamic_pointer_cast<AssignStmtNode>(stmt))
    {
      visitAssignStmt(assignStmt);
    }
    else if (auto ifStmt = dynamic_pointer_cast<IfElseStmtNode>(stmt))
    {
      visitIfElseStmt(ifStmt);
    }
    else if (auto whileStmt = dynamic_pointer_cast<WhileStmtNode>(stmt))
    {
      visitWhileStmt(whileStmt);
    }
    else if (auto breakStmt = dynamic_pointer_cast<BreakStmtNode>(stmt))
    {
      visitBreakStmt(breakStmt);
    }
    else if (auto continueStmt = dynamic_pointer_cast<ContinueStmtNode>(stmt))
    {
      visitContinueStmt(continueStmt);
    }
    else if (auto returnStmt = dynamic_pointer_cast<ReturnStmtNode>(stmt))
    {
      visitReturnStmt(returnStmt);
    }
    else if (auto blockStmt = dynamic_pointer_cast<BlockStmtNode>(stmt))
    {
      visitBlockStmt(blockStmt);
    }
  }

  analyzer.exitScope();
}

void TypeCheckerVisitor::visitDeclStmt(shared_ptr<DeclStmtNode> node)
{
  // 检查变量是否已声明（在当前作用域）
  if (analyzer.currentScope->table.find(node->identifier) != analyzer.currentScope->table.end())
  {
    addError("Variable '" + node->identifier + "' already declared in this scope");
    return;
  }
  // 检查全局变量是否用常量初始化
  bool isGlobal = (analyzer.currentScope->parent == nullptr);
  if (isGlobal && node->initializer)
  {
    // 普通变量
    if (node->indices.empty())
    {
      if (!node->initializer->isConst)
      {
        addError("Global variable '" + node->identifier + "' must be initialized with a constant expression");
      }
    }
    // 数组
    else
    {
      if (!checkArrayInit(node->initializer, node->type, 0, /*isConstArray=*/true))
      {
        addError("Global array '" + node->identifier + "' must be initialized with constant expressions");
      }
    }
  }
  // 检查类型是否为void或者数组维度是否合法
  if (!node->indices.empty())
  {
    // 检查数组声明 不能为void类型
    if (node->type.baseType == PrimaryDataType::VOID)
    {
      addError("Array '" + node->identifier + "' must have a valid base type");
      return;
    }
    // 检查数组维度
    for (const auto &size : node->indices)
    {
      DataType indexType = getExpressionType(size, ExprContext::ARRAY_INDEX);
      // 非整型 是数组 或者不是常量
      if (indexType.baseType != PrimaryDataType::INT || indexType.isArray() || !size->isConst)
      {
        addError("Array index must be integer type constant");
        return;
      }
    }
  }
  // 创建符号并声明
  auto symbol = make_shared<Symbol>(node->type, node->initializer != nullptr, node->isConst);
  // 如果是数组,保存下标信息,不是则为空
  symbol->indices = node->indices;
  // 如果有初始化表达式，检查类型匹配
  if (node->initializer)
  {
    visitInitExpr(node->initializer);
    // 这里可以进一步检查初始化表达式的类型是否与变量类型兼容
    DataType initType = getExpressionType(node->initializer, ExprContext::EXPRESSION);

    // 普通变量
    if (node->indices.empty())
    {
      // 类型兼容性检查
      if (!isTypeCompatible(initType, node->type))
      {
        addError("Initializer type does not match variable type for '" + node->identifier + "'");
      }
      // 如果是const变量，检查初始化表达式是否为常量
      if (node->isConst && !node->initializer->isConst)
      {
        addError("Const variable '" + node->identifier + "' must be initialized with a constant expression");
      }
    }
    // 数组
    else
    {
      // 检查初始化表达式是否为数组初始化
      if (!initType.isArray())
      {
        addError("Array '" + node->identifier + "' must be initialized with an array initializer");
      }
      else
      {
        // 检查维度和元素类型（可递归实现）
        if (!checkArrayInit(node->initializer, node->type, 0, node->isConst))
        {
          addError("Array initializer type or dimension does not match for '" + node->identifier + "'");
        }
      }
    }
  }

  analyzer.declareVariable(node->identifier, symbol);
}

void TypeCheckerVisitor::visitExprStmt(shared_ptr<ExprStmtNode> node)
{
  if (node->expr)
  {
    // 获取表达式类型（上下文为EXPRESSION）
    DataType exprStmt = getExpressionType(node->expr, ExprContext::EXPRESSION);

    // 根据表达式类型进行相应的访问
    if (auto binary = dynamic_pointer_cast<BinaryExprNode>(node->expr))
    {
      visitBinaryExpr(binary);
    }
    else if (auto unary = dynamic_pointer_cast<UnaryExprNode>(node->expr))
    {
      visitUnaryExpr(unary);
    }
    else if (auto lvalue = dynamic_pointer_cast<LValueExprNode>(node->expr))
    {
      visitLValueExpr(lvalue);
    }
    else if (auto call = dynamic_pointer_cast<CallExprNode>(node->expr))
    {
      visitCallExpr(call);
    }
    else if (auto intLiteral = dynamic_pointer_cast<IntLiteralExprNode>(node->expr))
    {
      visitIntLiteralExpr(intLiteral);
    }
    else if (auto floatLiteral = dynamic_pointer_cast<FloatLiteralExprNode>(node->expr))
    {
      visitFloatLiteralExpr(floatLiteral);
    }
    else if (auto stringLiteral = dynamic_pointer_cast<StringLiteralExprNode>(node->expr))
    {
      visitStringLiteralExpr(stringLiteral);
    }
  }
}

void TypeCheckerVisitor::visitAssignStmt(shared_ptr<AssignStmtNode> node)
{
  // 检查左值
  visitLValueExpr(node->lvalue);

  // 检查左值是否已声明
  auto symbol = analyzer.resolveVariable(node->lvalue->identifier);
  if (!symbol)
  {
    addError("Variable '" + node->lvalue->identifier + "' not declared");
    return;
  }

  // 检查const正确性
  if (symbol->type.isConst())
  {
    addError("Cannot modify const variable '" + node->lvalue->identifier + "'");
    return;
  }

  // 检查数组访问
  if (!node->lvalue->indices.empty())
  {
    isValidArrayAccess(node->lvalue);
    // 错误已在isValidArrayAccess中报告
  }

  // 检查右值表达式
  if (auto binary = dynamic_pointer_cast<BinaryExprNode>(node->rvalue))
  {
    visitBinaryExpr(binary);
  }
  else if (auto unary = dynamic_pointer_cast<UnaryExprNode>(node->rvalue))
  {
    visitUnaryExpr(unary);
  }
  else if (auto lvalue = dynamic_pointer_cast<LValueExprNode>(node->rvalue))
  {
    visitLValueExpr(lvalue);
  }
  else if (auto call = dynamic_pointer_cast<CallExprNode>(node->rvalue))
  {
    visitCallExpr(call);
  }
  else if (auto intLiteral = dynamic_pointer_cast<IntLiteralExprNode>(node->rvalue))
  {
    visitIntLiteralExpr(intLiteral);
  }
  else if (auto floatLiteral = dynamic_pointer_cast<FloatLiteralExprNode>(node->rvalue))
  {
    visitFloatLiteralExpr(floatLiteral);
  }
  else if (auto stringLiteral = dynamic_pointer_cast<StringLiteralExprNode>(node->rvalue))
  {
    visitStringLiteralExpr(stringLiteral);
  }

  // 检查类型兼容性
  DataType lvalueType = symbol->type;
  // 如果是数组访问，需要获取元素类型
  if (!node->lvalue->indices.empty())
  {
    // 简化处理：假设访问后是基本类型
    lvalueType = DataType(lvalueType.baseType);
  }

  DataType rvalueType = getExpressionType(node->rvalue, ExprContext::EXPRESSION);
  if (!isTypeCompatible(rvalueType, lvalueType))
  {
    addError("Type mismatch in assignment");
  }

  // 标记变量为已初始化
  symbol->isInitialized = true;
}

void TypeCheckerVisitor::visitIfElseStmt(shared_ptr<IfElseStmtNode> node)
{
  // 检查条件表达式
  if (node->condition)
  {
    DataType condType = getExpressionType(node->condition, ExprContext::CONDITION);
    // 条件必须是可转换为bool的类型
    if (condType.baseType == PrimaryDataType::VOID)
    {
      addError("Invalid condition type in if statement");
    }

    // 递归检查条件表达式
    if (auto binary = dynamic_pointer_cast<BinaryExprNode>(node->condition))
    {
      visitBinaryExpr(binary);
    }
    else if (auto unary = dynamic_pointer_cast<UnaryExprNode>(node->condition))
    {
      visitUnaryExpr(unary);
    }
    else if (auto lvalue = dynamic_pointer_cast<LValueExprNode>(node->condition))
    {
      visitLValueExpr(lvalue);
    }
    else if (auto call = dynamic_pointer_cast<CallExprNode>(node->condition))
    {
      visitCallExpr(call);
    }
  }

  // 检查then分支
  if (node->then_body)
  {
    if (auto blockStmt = dynamic_pointer_cast<BlockStmtNode>(node->then_body))
    {
      visitBlockStmt(blockStmt);
    }
    else if (auto assignStmt = dynamic_pointer_cast<AssignStmtNode>(node->then_body))
    {
      visitAssignStmt(assignStmt);
    }
    else if (auto exprStmt = dynamic_pointer_cast<ExprStmtNode>(node->then_body))
    {
      visitExprStmt(exprStmt);
    }
    else if (auto ifStmt = dynamic_pointer_cast<IfElseStmtNode>(node->then_body))
    {
      visitIfElseStmt(ifStmt);
    }
    else if (auto whileStmt = dynamic_pointer_cast<WhileStmtNode>(node->then_body))
    {
      visitWhileStmt(whileStmt);
    }
    else if (auto breakStmt = dynamic_pointer_cast<BreakStmtNode>(node->then_body))
    {
      visitBreakStmt(breakStmt);
    }
    else if (auto continueStmt = dynamic_pointer_cast<ContinueStmtNode>(node->then_body))
    {
      visitContinueStmt(continueStmt);
    }
    else if (auto returnStmt = dynamic_pointer_cast<ReturnStmtNode>(node->then_body))
    {
      visitReturnStmt(returnStmt);
    }
  }

  // 检查else分支
  if (node->else_body)
  {
    if (auto blockStmt = dynamic_pointer_cast<BlockStmtNode>(node->else_body))
    {
      visitBlockStmt(blockStmt);
    }
    else if (auto assignStmt = dynamic_pointer_cast<AssignStmtNode>(node->else_body))
    {
      visitAssignStmt(assignStmt);
    }
    else if (auto exprStmt = dynamic_pointer_cast<ExprStmtNode>(node->else_body))
    {
      visitExprStmt(exprStmt);
    }
    else if (auto ifStmt = dynamic_pointer_cast<IfElseStmtNode>(node->else_body))
    {
      visitIfElseStmt(ifStmt);
    }
    else if (auto whileStmt = dynamic_pointer_cast<WhileStmtNode>(node->else_body))
    {
      visitWhileStmt(whileStmt);
    }
    else if (auto breakStmt = dynamic_pointer_cast<BreakStmtNode>(node->else_body))
    {
      visitBreakStmt(breakStmt);
    }
    else if (auto continueStmt = dynamic_pointer_cast<ContinueStmtNode>(node->else_body))
    {
      visitContinueStmt(continueStmt);
    }
    else if (auto returnStmt = dynamic_pointer_cast<ReturnStmtNode>(node->else_body))
    {
      visitReturnStmt(returnStmt);
    }
  }
}

void TypeCheckerVisitor::visitWhileStmt(shared_ptr<WhileStmtNode> node)
{
  // 检查条件表达式
  if (node->condition)
  {
    DataType condType = getExpressionType(node->condition, ExprContext::CONDITION);
    if (condType.baseType == PrimaryDataType::VOID)
    {
      addError("Invalid condition type in while statement");
    }

    // 递归检查条件表达式
    if (auto binary = dynamic_pointer_cast<BinaryExprNode>(node->condition))
    {
      visitBinaryExpr(binary);
    }
    else if (auto unary = dynamic_pointer_cast<UnaryExprNode>(node->condition))
    {
      visitUnaryExpr(unary);
    }
    else if (auto lvalue = dynamic_pointer_cast<LValueExprNode>(node->condition))
    {
      visitLValueExpr(lvalue);
    }
    else if (auto call = dynamic_pointer_cast<CallExprNode>(node->condition))
    {
      visitCallExpr(call);
    }
    else if (auto stringLiteral = dynamic_pointer_cast<StringLiteralExprNode>(node->condition))
    {
      visitStringLiteralExpr(stringLiteral);
    }
  }

  // 检查循环体
  if (node->body)
  {
    if (auto blockStmt = dynamic_pointer_cast<BlockStmtNode>(node->body))
    {
      visitBlockStmt(blockStmt);
    }
    else if (auto assignStmt = dynamic_pointer_cast<AssignStmtNode>(node->body))
    {
      visitAssignStmt(assignStmt);
    }
    else if (auto exprStmt = dynamic_pointer_cast<ExprStmtNode>(node->body))
    {
      visitExprStmt(exprStmt);
    }
    else if (auto ifStmt = dynamic_pointer_cast<IfElseStmtNode>(node->body))
    {
      visitIfElseStmt(ifStmt);
    }
    else if (auto whileStmt = dynamic_pointer_cast<WhileStmtNode>(node->body))
    {
      visitWhileStmt(whileStmt);
    }
    else if (auto breakStmt = dynamic_pointer_cast<BreakStmtNode>(node->body))
    {
      visitBreakStmt(breakStmt);
    }
    else if (auto continueStmt = dynamic_pointer_cast<ContinueStmtNode>(node->body))
    {
      visitContinueStmt(continueStmt);
    }
    else if (auto returnStmt = dynamic_pointer_cast<ReturnStmtNode>(node->body))
    {
      visitReturnStmt(returnStmt);
    }
    else if (auto stringLiteral = dynamic_pointer_cast<StringLiteralExprNode>(node->body))
    {
      visitStringLiteralExpr(stringLiteral);
    }
  }
}

void TypeCheckerVisitor::visitBreakStmt(shared_ptr<BreakStmtNode> node)
{
  // if (!inLoop)
  // {
  //   addError("Break statement not in loop");
  // }
}

void TypeCheckerVisitor::visitContinueStmt(shared_ptr<ContinueStmtNode> node)
{
  // if (!inLoop)
  // {
  //   addError("Continue statement not in loop");
  // }
}

void TypeCheckerVisitor::visitReturnStmt(shared_ptr<ReturnStmtNode> node)
{
  if (!currentFunction)
  {
    addError("Return statement not in function");
    return;
  }

  DataType expectedReturnType = currentFunction->returnType;

  if (node->ret_expr)
  {
    // 有返回值
    DataType actualReturnType = getExpressionType(node->ret_expr, ExprContext::EXPRESSION);

    if (!isTypeCompatible(actualReturnType, expectedReturnType))
    {
      addError("Return type mismatch");
    }

    // 递归检查返回表达式
    if (auto binary = dynamic_pointer_cast<BinaryExprNode>(node->ret_expr))
    {
      visitBinaryExpr(binary);
    }
    else if (auto unary = dynamic_pointer_cast<UnaryExprNode>(node->ret_expr))
    {
      visitUnaryExpr(unary);
    }
    else if (auto lvalue = dynamic_pointer_cast<LValueExprNode>(node->ret_expr))
    {
      visitLValueExpr(lvalue);
    }
    else if (auto call = dynamic_pointer_cast<CallExprNode>(node->ret_expr))
    {
      visitCallExpr(call);
    }
    else if (auto intLiteral = dynamic_pointer_cast<IntLiteralExprNode>(node->ret_expr))
    {
      visitIntLiteralExpr(intLiteral);
    }
    else if (auto floatLiteral = dynamic_pointer_cast<FloatLiteralExprNode>(node->ret_expr))
    {
      visitFloatLiteralExpr(floatLiteral);
    }
    else if (auto stringLiteral = dynamic_pointer_cast<StringLiteralExprNode>(node->ret_expr))
    {
      visitStringLiteralExpr(stringLiteral);
    }
  }
  else
  {
    // 无返回值
    if (expectedReturnType.baseType != PrimaryDataType::VOID)
    {
      addError("Function must return a value");
    }
  }
}

void TypeCheckerVisitor::visitLValueExpr(shared_ptr<LValueExprNode> node)
{
  // 检查变量是否已声明
  auto symbol = analyzer.resolveVariable(node->identifier);
  if (!symbol)
  {
    addError("Variable '" + node->identifier + "' not declared");
    return;
  }

  // 检查变量是否已初始化
  if (!symbol->isInitialized)
  {
    addError("Variable '" + node->identifier + "' used before initialization");
  }

  // 检查数组访问
  if (!node->indices.empty())
  {
    isValidArrayAccess(node);

    // 递归检查数组索引表达式
    for (auto &index : node->indices)
    {
      if (auto binary = dynamic_pointer_cast<BinaryExprNode>(index))
      {
        visitBinaryExpr(binary);
      }
      else if (auto unary = dynamic_pointer_cast<UnaryExprNode>(index))
      {
        visitUnaryExpr(unary);
      }
      else if (auto lvalue = dynamic_pointer_cast<LValueExprNode>(index))
      {
        visitLValueExpr(lvalue);
      }
      else if (auto call = dynamic_pointer_cast<CallExprNode>(index))
      {
        visitCallExpr(call);
      }
      else if (auto intLiteral = dynamic_pointer_cast<IntLiteralExprNode>(index))
      {
        visitIntLiteralExpr(intLiteral);
      }
      else if (auto floatLiteral = dynamic_pointer_cast<FloatLiteralExprNode>(index))
      {
        visitFloatLiteralExpr(floatLiteral);
      }
      else if (auto stringLiteral = dynamic_pointer_cast<StringLiteralExprNode>(index))
      {
        visitStringLiteralExpr(stringLiteral);
      }
    }
  }
}

void TypeCheckerVisitor::visitInitExpr(shared_ptr<InitExprNode> node)
{
  if (node->singleInitVal)
  {
    // 单一初始值 - 检查表达式类型（应该是EXPRESSION上下文）
    DataType initType = getExpressionType(node->singleInitVal, ExprContext::EXPRESSION);

    // 递归访问子表达式
    if (auto binary = dynamic_pointer_cast<BinaryExprNode>(node->singleInitVal))
    {
      visitBinaryExpr(binary);
    }
    else if (auto unary = dynamic_pointer_cast<UnaryExprNode>(node->singleInitVal))
    {
      visitUnaryExpr(unary);
    }
    else if (auto lvalue = dynamic_pointer_cast<LValueExprNode>(node->singleInitVal))
    {
      visitLValueExpr(lvalue);
    }
    else if (auto call = dynamic_pointer_cast<CallExprNode>(node->singleInitVal))
    {
      visitCallExpr(call);
    }
    else if (auto intLiteral = dynamic_pointer_cast<IntLiteralExprNode>(node->singleInitVal))
    {
      visitIntLiteralExpr(intLiteral);
    }
    else if (auto floatLiteral = dynamic_pointer_cast<FloatLiteralExprNode>(node->singleInitVal))
    {
      visitFloatLiteralExpr(floatLiteral);
    }
    else if (auto stringLiteral = dynamic_pointer_cast<StringLiteralExprNode>(node->singleInitVal))
    {
      visitStringLiteralExpr(stringLiteral);
    }
  }
  else
  {
    // 复合初始值
    for (auto &initVal : node->multiInitVal)
    {
      visitInitExpr(initVal);
    }
  }
}

void TypeCheckerVisitor::visitCallExpr(shared_ptr<CallExprNode> node)
{
  checkFunctionCall(node);

  // 递归检查参数表达式
  inFunctionCall = true;
  for (auto &arg : node->args)
  {
    if (auto binary = dynamic_pointer_cast<BinaryExprNode>(arg))
    {
      visitBinaryExpr(binary);
    }
    else if (auto unary = dynamic_pointer_cast<UnaryExprNode>(arg))
    {
      visitUnaryExpr(unary);
    }
    else if (auto lvalue = dynamic_pointer_cast<LValueExprNode>(arg))
    {
      visitLValueExpr(lvalue);
    }
    else if (auto call = dynamic_pointer_cast<CallExprNode>(arg))
    {
      visitCallExpr(call);
    }
    else if (auto intLiteral = dynamic_pointer_cast<IntLiteralExprNode>(arg))
    {
      visitIntLiteralExpr(intLiteral);
    }
    else if (auto floatLiteral = dynamic_pointer_cast<FloatLiteralExprNode>(arg))
    {
      visitFloatLiteralExpr(floatLiteral);
    }
    else if (auto stringLiteral = dynamic_pointer_cast<StringLiteralExprNode>(arg))
    {
      visitStringLiteralExpr(stringLiteral);
    }
  }
  inFunctionCall = false;
}

void TypeCheckerVisitor::visitBinaryExpr(shared_ptr<BinaryExprNode> node)
{
  // 递归检查左右操作数
  if (auto leftBinary = dynamic_pointer_cast<BinaryExprNode>(node->left))
  {
    visitBinaryExpr(leftBinary);
  }
  else if (auto leftUnary = dynamic_pointer_cast<UnaryExprNode>(node->left))
  {
    visitUnaryExpr(leftUnary);
  }
  else if (auto leftLvalue = dynamic_pointer_cast<LValueExprNode>(node->left))
  {
    visitLValueExpr(leftLvalue);
  }
  else if (auto leftCall = dynamic_pointer_cast<CallExprNode>(node->left))
  {
    visitCallExpr(leftCall);
  }
  else if (auto leftIntLiteral = dynamic_pointer_cast<IntLiteralExprNode>(node->left))
  {
    visitIntLiteralExpr(leftIntLiteral);
  }
  else if (auto leftFloatLiteral = dynamic_pointer_cast<FloatLiteralExprNode>(node->left))
  {
    visitFloatLiteralExpr(leftFloatLiteral);
  }
  else if (auto stringLiteral = dynamic_pointer_cast<StringLiteralExprNode>(node->left))
  {
    visitStringLiteralExpr(stringLiteral);
  }

  if (auto rightBinary = dynamic_pointer_cast<BinaryExprNode>(node->right))
  {
    visitBinaryExpr(rightBinary);
  }
  else if (auto rightUnary = dynamic_pointer_cast<UnaryExprNode>(node->right))
  {
    visitUnaryExpr(rightUnary);
  }
  else if (auto rightLvalue = dynamic_pointer_cast<LValueExprNode>(node->right))
  {
    visitLValueExpr(rightLvalue);
  }
  else if (auto rightCall = dynamic_pointer_cast<CallExprNode>(node->right))
  {
    visitCallExpr(rightCall);
  }
  else if (auto rightIntLiteral = dynamic_pointer_cast<IntLiteralExprNode>(node->right))
  {
    visitIntLiteralExpr(rightIntLiteral);
  }
  else if (auto rightFloatLiteral = dynamic_pointer_cast<FloatLiteralExprNode>(node->right))
  {
    visitFloatLiteralExpr(rightFloatLiteral);
  }
  else if (auto stringLiteral = dynamic_pointer_cast<StringLiteralExprNode>(node->right))
  {
    visitStringLiteralExpr(stringLiteral);
  }

  // 检查操作数类型
  DataType leftType = getExpressionType(node->left, ExprContext::EXPRESSION);
  DataType rightType = getExpressionType(node->right, ExprContext::EXPRESSION);

  // 检查除零
  if (node->op == BinaryOp::Div || node->op == BinaryOp::Mod)
  {
    if (auto rightIntLiteral = dynamic_pointer_cast<IntLiteralExprNode>(node->right))
    {
      if (rightIntLiteral->value == 0)
      {
        addError("Division by zero");
      }
    }
    else if (auto rightFloatLiteral = dynamic_pointer_cast<FloatLiteralExprNode>(node->right))
    {
      if (rightFloatLiteral->value == 0.0f)
      {
        addError("Division by zero");
      }
    }
  }

  // 检查模运算只能用于整数
  if (node->op == BinaryOp::Mod)
  {
    if (leftType.baseType != PrimaryDataType::INT || rightType.baseType != PrimaryDataType::INT)
    {
      addError("Modulo operation requires integer operands");
    }
  }
}

void TypeCheckerVisitor::visitUnaryExpr(shared_ptr<UnaryExprNode> node)
{
  // 递归检查操作数
  if (auto binary = dynamic_pointer_cast<BinaryExprNode>(node->operand))
  {
    visitBinaryExpr(binary);
  }
  else if (auto unary = dynamic_pointer_cast<UnaryExprNode>(node->operand))
  {
    visitUnaryExpr(unary);
  }
  else if (auto lvalue = dynamic_pointer_cast<LValueExprNode>(node->operand))
  {
    visitLValueExpr(lvalue);
  }
  else if (auto call = dynamic_pointer_cast<CallExprNode>(node->operand))
  {
    visitCallExpr(call);
  }
  else if (auto intLiteral = dynamic_pointer_cast<IntLiteralExprNode>(node->operand))
  {
    visitIntLiteralExpr(intLiteral);
  }
  else if (auto floatLiteral = dynamic_pointer_cast<FloatLiteralExprNode>(node->operand))
  {
    visitFloatLiteralExpr(floatLiteral);
  }
  else if (auto stringLiteral = dynamic_pointer_cast<StringLiteralExprNode>(node->operand))
  {
    visitStringLiteralExpr(stringLiteral);
  }

  // 检查操作数类型
  DataType operandType = getExpressionType(node->operand, ExprContext::EXPRESSION);

  // 如果是数组，不能直接应用一元操作
  if (operandType.isArray())
  {
    addError("Cannot apply unary operator to array");
  }
}

// void TypeCheckerVisitor::visitLiteralExpr(shared_ptr<LiteralExprNode> node)
// {
//   // 但可以检查类型是否符合预期
//   if (auto intLiteral = dynamic_pointer_cast<IntLiteralExprNode>(node))
//   {
//     // 整数字面量是32位整数，所以范围在-2147483648到2147483647之间
//     if (intLiteral->value < -2147483648 ||
//         intLiteral->value > 2147483647)
//     {
//       addError("Integer literal out of range: " + to_string(intLiteral->value));
//     }
//   }
//   else if (auto floatLiteral = dynamic_pointer_cast<FloatLiteralExprNode>(node))
//   {
//     // 浮点数字面量通常是32位单精度浮点数
//     // 检查是否在有效范围内
//     if (floatLiteral->value < -3.402823e38f ||
//         floatLiteral->value > 3.402823e38f)
//     {
//       addError("Float literal out of range: " + to_string(floatLiteral->value));
//     }
//   }
//   else if (auto strLiteral = dynamic_pointer_cast<StringLiteralExprNode>(node))
//   {
//     if (!InFunctionCall)
//     {
//       addError("String literal can only be used in function calls");
//     }
//   }
//   else
//   {
//     addError("Unknown literal expression type");
//   }
// }

void TypeCheckerVisitor::visitIntLiteralExpr(shared_ptr<IntLiteralExprNode> node)
{
  auto intLiteral = dynamic_pointer_cast<IntLiteralExprNode>(node);
  if (intLiteral->value < -2147483648 ||
      intLiteral->value > 2147483647)
  {
    addError("Integer literal out of range: " + to_string(intLiteral->value));
  }
}

void TypeCheckerVisitor::visitFloatLiteralExpr(shared_ptr<FloatLiteralExprNode> node)
{
  auto floatLiteral = dynamic_pointer_cast<FloatLiteralExprNode>(node);
  if (floatLiteral->value < -3.402823e38f ||
      floatLiteral->value > 3.402823e38f)
  {
    addError("Float literal out of range: " + to_string(floatLiteral->value));
  }
}

void TypeCheckerVisitor::visitStringLiteralExpr(shared_ptr<StringLiteralExprNode> node)
{
  if (!inFunctionCall)
  {
    addError("String literal can only be used in function calls");
  }
}

// shared_ptr<ExprNode> castExpression(shared_ptr<ExprNode> expr, DataType targetType)
// {
//   if (!needsImplicitConversion(expr->dataType, targetType))
//   {
//     return expr;
//   }

//   // 创建类型转换节点
//   auto castNode = make_shared<CastExpNode>();
//   castNode->sourceExpr = expr;
//   castNode->targetType = targetType;

//   return castNode;
// }