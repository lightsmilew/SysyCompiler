#include "SemanticAnalysis.h"
#include <memory>
#include <string>
#include <stdexcept>
#include <algorithm>
#include <iostream>

using std::dynamic_pointer_cast;
using std::to_string;

string convertPrimaryDataType2String(PrimaryDataType type)
{
  switch (type)
  {
  case PrimaryDataType::INT:
    return "int";
  case PrimaryDataType::FLOAT:
    return "float";
  case PrimaryDataType::STRING:
    return "string";
  case PrimaryDataType::VOID:
    return "void";
  default:
    throw std::invalid_argument("Unknown PrimaryDataType");
  }
}

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

bool SymbolTable::insert(const string &name, shared_ptr<Symbol> symbol)
{
  // 检查当前作用域中是否已存在同名符号
  if (table.find(name) != table.end())
  {
    return false; // 返回false表示插入失败（重复声明）
  }
  table[name] = symbol;
  return true; // 插入成功
}

void SemanticAnalyzer::enterScope()
{
  currentScope = make_shared<SymbolTable>(currentScope);
}

void SemanticAnalyzer::exitScope()
{
  currentScope = currentScope->parent;
}

bool SemanticAnalyzer::declareVariable(const std::string &name,
                                       const std::shared_ptr<Symbol> &symbol)
{
  return currentScope->insert(name, symbol);
}

shared_ptr<Symbol> SemanticAnalyzer::resolveVariable(const std::string &name)
{
  return currentScope->lookup(name);
}

bool SemanticAnalyzer::declareFunction(const std::string &name,
                                       const std::shared_ptr<Symbol> &symbol)
{
  return functionTable->insert(name, symbol);
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

PrimaryDataType TypeCheckerVisitor::getExpressionType(shared_ptr<ExprNode> expr)
{
  if (auto intnum = dynamic_pointer_cast<IntLiteralExprNode>(expr))
  {
    return PrimaryDataType::INT;
  }
  else if (auto floatnum = dynamic_pointer_cast<FloatLiteralExprNode>(expr))
  {
    return PrimaryDataType::FLOAT;
  }
  else if (auto str = dynamic_pointer_cast<StringLiteralExprNode>(expr))
  {
    visitStringLiteralExpr(str);
    return PrimaryDataType::STRING;
  }
  else if (auto binaryExp = dynamic_pointer_cast<BinaryExprNode>(expr))
  {
    PrimaryDataType lexp = getExpressionType(binaryExp->left);
    PrimaryDataType rexp = getExpressionType(binaryExp->right);

    if (lexp == PrimaryDataType::VOID || rexp == PrimaryDataType::VOID)
    {
      return PrimaryDataType::VOID;
    }
    else if (lexp == PrimaryDataType::FLOAT || rexp == PrimaryDataType::FLOAT)
    {
      return PrimaryDataType::FLOAT;
    }
    else if (lexp == PrimaryDataType::INT && rexp == PrimaryDataType::INT)
    {
      return PrimaryDataType::INT;
    }
  }
  else if (auto unaryExp = dynamic_pointer_cast<UnaryExprNode>(expr))
  {
    return getExpressionType(unaryExp->operand);
  }
  else if (auto callexp = dynamic_pointer_cast<CallExprNode>(expr))
  {
    visitCallExpr(callexp);
    auto call = analyzer.resolveFunction(callexp->callee);
    if (!call)
    {
      return PrimaryDataType::VOID;
    }

    return call->functionNode->returnType.baseType;
  }
  else if (auto lval = dynamic_pointer_cast<LValueExprNode>(expr))
  {
    auto symbol = analyzer.resolveVariable(lval->identifier);
    if (!symbol)
    {
      return PrimaryDataType::VOID;
    }
    if (!symbol->isInitialized)
    {
      addError("Variable '" + lval->identifier + "' is not initialized before use");
    }
    visitLValueExpr(lval);
    // 复用已经查找到的symbol，避免重复查找
    return symbol->type.baseType;
  }
  else if (auto initExpr = dynamic_pointer_cast<InitExprNode>(expr))
  {
    // 如果是初始化表达式，获取其主数据类型
    if (initExpr->singleInitVal)
    {
      return getExpressionType(initExpr->singleInitVal);
    }
    else
    {
      vector<PrimaryDataType> types;
      for (auto &initVal : initExpr->multiInitVal)
      {
        // 递归获取多维数组的初始值类型
        PrimaryDataType type = getExpressionType(initVal);
        types.push_back(type);
      }

      if (std::find(types.begin(), types.end(), PrimaryDataType::VOID) != types.end())
      {
        addError("Array initializer contains void type");
        return PrimaryDataType::VOID; // 如果有一个是void，直接返回
      }
      else if (std::find(types.begin(), types.end(), PrimaryDataType::FLOAT) != types.end())
      {
        return PrimaryDataType::FLOAT; // 如果有一个是float，直接返回
      }
      return PrimaryDataType::INT; // 如果所有都是int，返回int
    }
  }
  return PrimaryDataType::VOID; // 如果没有匹配到任何类型，返回void
}

bool TypeCheckerVisitor::exprChecker(shared_ptr<ExprNode> expr, bool isConst)
{
  if (auto binaryExp = dynamic_pointer_cast<BinaryExprNode>(expr))
  {
    if (binaryExp->op == BinaryOp::And || binaryExp->op == BinaryOp::Or ||
        binaryExp->op == BinaryOp::Eq || binaryExp->op == BinaryOp::Ne ||
        binaryExp->op == BinaryOp::Lt || binaryExp->op == BinaryOp::Gt ||
        binaryExp->op == BinaryOp::Le || binaryExp->op == BinaryOp::Ge)
    {
      addError("Logical and comparison operations are not allowed in expressions");
      return false;
    }
    return exprChecker(binaryExp->left, isConst) && exprChecker(binaryExp->right, isConst);
  }
  else if (auto unaryExp = dynamic_pointer_cast<UnaryExprNode>(expr))
  {
    if (unaryExp->op == UnaryOp::Not)
    {
      addError("Logical NOT operations are not allowed in expressions");
      return false;
    }
    return exprChecker(unaryExp->operand, isConst);
  }
  else if (auto callexp = dynamic_pointer_cast<CallExprNode>(expr))
  {
    visitCallExpr(callexp);
    // 函数调用不允许在常量表达式中出现
    return !isConst;
  }
  else if (auto lval = dynamic_pointer_cast<LValueExprNode>(expr))
  {
    auto symbol = analyzer.resolveVariable(lval->identifier);
    visitLValueExpr(lval);
    if (!symbol)
    {
      return false; // 如果变量未找到，返回false
    }

    if (!symbol->isInitialized && !inFunctionCall && !symbol->type.isArray())
    {
      addError("Variable '" + lval->identifier + "' is not initialized before use");
      return false; // 如果变量未初始化，直接返回false}
    }

    // 复用已经查找到的symbol，避免重复查找
    // 变量不允许在常量表达式中出现
    return symbol->type.isConst() || !isConst;
  }
  else if (auto intnum = dynamic_pointer_cast<IntLiteralExprNode>(expr))
  {
    return true; // 整数字面量是常量表达式
  }
  else if (auto floatnum = dynamic_pointer_cast<FloatLiteralExprNode>(expr))
  {
    return true; // 浮点数字面量是常量表达式
  }
  else if (auto str = dynamic_pointer_cast<StringLiteralExprNode>(expr))
  {
    return false; // 字符串字面量不是常量表达式
  }
  else if (auto initExpr = dynamic_pointer_cast<InitExprNode>(expr))
  {
    if (initExpr->singleInitVal)
    {
      // 单一初始值
      return exprChecker(initExpr->singleInitVal, isConst);
    }
    else
    {
      // 复合初始值
      for (const auto &initVal : initExpr->multiInitVal)
      {
        if (!exprChecker(initVal, isConst))
        {
          addError("Array initializer must be a valid expression");
          return false;
        }
      }
      return true; // 复合初始值只要所有元素都是有效表达式即可
    }
  }
  return false; // 如果没有匹配到任何类型，返回false
}

bool TypeCheckerVisitor::checkExprTypeCompatible(string ident, shared_ptr<ExprNode> expr, DataType targetType, ExprContext context, bool isConst)
{
  if (context == ExprContext::ARRAY_INDEX)
  {
    auto initExpr = dynamic_pointer_cast<InitExprNode>(expr);
    // int可以赋给float, float不能赋给int

    // 如果没有初始值，直接返回true
    if (initExpr->multiInitVal.empty() && !initExpr->singleInitVal)
    {
      return true;
    }

    if (targetType.baseType == PrimaryDataType::INT)
    {
      PrimaryDataType exprType = getExpressionType(initExpr);
      if (exprType == PrimaryDataType::FLOAT)
      {
        addError("Array '" + ident + "' index cannot be a float type");
      }
    }
    if (!exprChecker(initExpr, isConst))
    {
      addError("Array '" + ident + "' index must be a valid expression");
    }
  }
  else if (context == ExprContext::CALL)
  {
    if (!exprChecker(expr, false))
    {
      return false;
    }

    // 目标是数组
    if (targetType.arrayDimensionCount() > 0)
    {
      auto lval = dynamic_pointer_cast<LValueExprNode>(expr);
      if (!lval)
      {
        addError("Function '" + ident + "' need an array");
        return false;
      }
      visitLValueExpr(lval);

      auto array = analyzer.resolveVariable(lval->identifier);
      if (!array)
      {
        return false;
      }

      if (array->type.arrayDimensionCount() - lval->indices.size() < targetType.arrayDimensionCount())
      {
        addError("Function '" + ident + "' expects " +
                 to_string(targetType.arrayDimensionCount()) +
                 " array indices, but got " +
                 to_string(array->type.arrayDimensionCount() - lval->indices.size()));
      }
    }
    else
    {
      auto lval = dynamic_pointer_cast<LValueExprNode>(expr);
      if (lval)
      {
        visitLValueExpr(lval);
        auto array = analyzer.resolveVariable(lval->identifier);
        if (!array)
        {
          return false;
        }

        if (array->type.arrayDimensionCount() - lval->indices.size() != 0)
        {
          addError("Function '" + ident + "' expects a variable but got a array");
        }
      }
      else
      {
        PrimaryDataType exprType = getExpressionType(expr);

        if (exprType == PrimaryDataType::VOID)
        {
          addError("Funcrion '" + ident + "' expects " + convertPrimaryDataType2String(targetType.baseType) + " but get void");
          return false;
        }
      }
    }
  }
  else if (context == ExprContext::CONDITION)
  {
    // 条件表达式只能是int或float
    PrimaryDataType exprType = getExpressionType(expr);
  }
  else if (context == ExprContext::EXPRESSION)
  {
    exprChecker(expr, isConst);
    PrimaryDataType exprType = getExpressionType(expr);
    if (exprType == PrimaryDataType::VOID)

    {
      addError("Expression type mismatch: " + ident + " expected '" +
               convertPrimaryDataType2String(targetType.baseType) + "', but got void");
      return false;
    }
  }
  return true;
}

bool TypeCheckerVisitor::isValidArrayIndex(string ident, vector<shared_ptr<ExprNode>> indices)
{
  // 检查索引为整数
  for (auto &index : indices)
  {
    PrimaryDataType indexType = getExpressionType(index);
    if (!(indexType == PrimaryDataType::INT))
    {
      addError("Array '" + ident + "' index must be integer type");
      return false;
    }
  }

  return true;
}

void TypeCheckerVisitor::initializeFunction()
{
  Vector<shared_ptr<Symbol>> symbols;
  // int getint();
  auto getintNode = make_shared<FuncNode>(
      DataType(PrimaryDataType::INT), "getint",
      Vector<shared_ptr<DeclStmtNode>>{}, nullptr);
  symbols.push_back(make_shared<Symbol>(getintNode));
  // int getch();
  auto getchNode = make_shared<FuncNode>(
      DataType(PrimaryDataType::INT), "getch",
      Vector<shared_ptr<DeclStmtNode>>{}, nullptr);
  symbols.push_back(make_shared<Symbol>(getchNode));
  // float getfloat();
  auto getfloatNode = make_shared<FuncNode>(
      DataType(PrimaryDataType::FLOAT), "getfloat",
      Vector<shared_ptr<DeclStmtNode>>{}, nullptr);
  symbols.push_back(make_shared<Symbol>(getfloatNode));
  // int getarray(int a[]);
  auto getarrayNode = make_shared<FuncNode>(
      DataType(PrimaryDataType::INT), "getarray",
      vector<shared_ptr<DeclStmtNode>>{make_shared<DeclStmtNode>(
          DataType(PrimaryDataType::INT, {makePtr<IntLiteralExprNode>(-1)}), "a")},
      nullptr);
  symbols.push_back(make_shared<Symbol>(getarrayNode));
  // int getfarray(float a[]);
  auto getfarrayNode = make_shared<FuncNode>(
      DataType(PrimaryDataType::INT), "getfarray",
      vector<shared_ptr<DeclStmtNode>>{make_shared<DeclStmtNode>(
          DataType(PrimaryDataType::FLOAT, {makePtr<IntLiteralExprNode>(-1)}), "a")},
      nullptr);
  symbols.push_back(make_shared<Symbol>(getfarrayNode));
  // void putint(int a);
  auto putintNode = make_shared<FuncNode>(
      DataType(PrimaryDataType::VOID), "putint",
      vector<shared_ptr<DeclStmtNode>>{make_shared<DeclStmtNode>(
          DataType(PrimaryDataType::INT), "a")},
      nullptr);
  symbols.push_back(make_shared<Symbol>(putintNode));
  // void putch(int a);
  auto putchNode = make_shared<FuncNode>(
      DataType(PrimaryDataType::VOID), "putch",
      vector<shared_ptr<DeclStmtNode>>{make_shared<DeclStmtNode>(
          DataType(PrimaryDataType::INT), "a")},
      nullptr);
  symbols.push_back(make_shared<Symbol>(putchNode));
  // void putfloat(float a);
  auto putfloatNode = make_shared<FuncNode>(
      DataType(PrimaryDataType::VOID), "putfloat",
      vector<shared_ptr<DeclStmtNode>>{make_shared<DeclStmtNode>(
          DataType(PrimaryDataType::FLOAT), "a")},
      nullptr);
  symbols.push_back(make_shared<Symbol>(putfloatNode));
  // void putarray( int n,int a[]);
  auto putarrayNode = make_shared<FuncNode>(
      DataType(PrimaryDataType::VOID), "putarray",
      vector<shared_ptr<DeclStmtNode>>{
          make_shared<DeclStmtNode>(DataType(PrimaryDataType::INT), "n"),
          make_shared<DeclStmtNode>(DataType(PrimaryDataType::INT, {makePtr<IntLiteralExprNode>(-1)}), "a")},
      nullptr);
  symbols.push_back(make_shared<Symbol>(putarrayNode));
  // void putfarray( int n,float a[]);
  auto putfarrayNode = make_shared<FuncNode>(
      DataType(PrimaryDataType::VOID), "putfarray",
      vector<shared_ptr<DeclStmtNode>>{
          make_shared<DeclStmtNode>(DataType(PrimaryDataType::INT), "n"),
          make_shared<DeclStmtNode>(DataType(PrimaryDataType::FLOAT, {makePtr<IntLiteralExprNode>(-1)}), "a")},
      nullptr);
  symbols.push_back(make_shared<Symbol>(putfarrayNode));
  // void putf(string a);
  auto putfNode = make_shared<FuncNode>(
      DataType(PrimaryDataType::VOID), "putf",
      vector<shared_ptr<DeclStmtNode>>{make_shared<DeclStmtNode>(
          DataType(PrimaryDataType::STRING), "a")},
      nullptr);
  symbols.push_back(make_shared<Symbol>(putfNode));
  // void starttime();
  auto starttimeNode = make_shared<FuncNode>(
      DataType(PrimaryDataType::VOID), "starttime",
      Vector<shared_ptr<DeclStmtNode>>{}, nullptr);
  symbols.push_back(make_shared<Symbol>(starttimeNode));
  // void stoptime();
  auto stoptimeNode = make_shared<FuncNode>(
      DataType(PrimaryDataType::VOID), "stoptime",
      Vector<shared_ptr<DeclStmtNode>>{}, nullptr);
  symbols.push_back(make_shared<Symbol>(stoptimeNode));
  for (auto &symbol : symbols)
  {
    // 注册到符号表
    if (!analyzer.declareFunction(symbol->functionNode->identifier, symbol))
    {
      addError("Function '" + symbol->functionNode->identifier + "' already declared");
    }
  }
  // 可以添加更多的库函数初始化
  // 如：int scanf(string format, ...);
  // 或者其他常用的库函数
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

  // 检查是否有一个main函数
  if (!hasMainFunction)
  {
    addError("No main function found");
  }
}

void TypeCheckerVisitor::visitFuncNode(shared_ptr<FuncNode> node)
{
  currentFunction = node;

  // 检查main函数
  if (node->identifier == "main")
  {
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
  if (!analyzer.declareFunction(node->identifier, funcSymbol))
  {
    addError("Multiple " + node->identifier + " functions declared");
  }

  // 进入函数作用域
  analyzer.enterScope();

  // 声明参数
  for (auto &param : node->params)
  {
    auto symbol = make_shared<Symbol>(param->type, true); // 参数默认已初始化

    // 当参数是数组时，检查数组下标
    if (param->type.arrayDimensionCount() > 0)
    {
      // 数组参数，检查数组维度
      if (!isValidArrayIndex(param->identifier, param->type.arraySizes()))
      {
        addError("Invalid array index for parameter '" + param->identifier + "'");
      }

      // 检查数组下标是否为int字面量
      for (auto &size : param->type.arraySizes())
      {
        if (!exprChecker(size, true))
        {
          addError("Array size for parameter '" + param->identifier + "' must be a constant integer expression");
          break;
        }
      }
    }

    if (!analyzer.declareVariable(param->identifier, symbol))
    {
      addError("Parameter '" + param->identifier + "' already declared in this function");
    }
  }

  // 访问函数体
  if (node->body)
  {
    visitBlockStmt(node->body);
  }

  analyzer.exitScope();
  currentFunction = nullptr;
}

void TypeCheckerVisitor::visitStmt(shared_ptr<StmtNode> node)
{
  if (auto declStmt = dynamic_pointer_cast<DeclStmtNode>(node))
  {
    visitDeclStmt(declStmt);
  }
  else if (auto exprStmt = dynamic_pointer_cast<ExprStmtNode>(node))
  {
    visitExprStmt(exprStmt);
  }
  else if (auto assignStmt = dynamic_pointer_cast<AssignStmtNode>(node))
  {
    visitAssignStmt(assignStmt);
  }
  else if (auto ifStmt = dynamic_pointer_cast<IfElseStmtNode>(node))
  {
    analyzer.enterScope(); // 进入if语句作用域
    visitIfElseStmt(ifStmt);
    analyzer.exitScope(); // 退出if语句作用域
  }
  else if (auto whileStmt = dynamic_pointer_cast<WhileStmtNode>(node))
  {
    analyzer.enterScope(); // 进入while语句作用域
    visitWhileStmt(whileStmt);
    analyzer.exitScope(); // 退出while语句作用域
  }
  else if (auto breakStmt = dynamic_pointer_cast<BreakStmtNode>(node))
  {
    visitBreakStmt(breakStmt);
  }
  else if (auto continueStmt = dynamic_pointer_cast<ContinueStmtNode>(node))
  {
    visitContinueStmt(continueStmt);
  }
  else if (auto returnStmt = dynamic_pointer_cast<ReturnStmtNode>(node))
  {
    visitReturnStmt(returnStmt);
  }
  else if (auto blockStmt = dynamic_pointer_cast<BlockStmtNode>(node))
  {
    analyzer.enterScope(); // 进入嵌套块作用域
    for (auto &stmt : blockStmt->stmts)
    {
      visitBlockStmt(stmt);
    }
    analyzer.exitScope(); // 退出嵌套块作用域
  }
}
void TypeCheckerVisitor::visitBlockStmt(shared_ptr<StmtNode> node)
{
  visitStmt(node);
}
void TypeCheckerVisitor::visitDeclStmt(shared_ptr<DeclStmtNode> node)
{
  // 检查变量是否已声明（在当前作用域）
  // 没有循环展开
  if (!analyzer.declareVariable(node->identifier, make_shared<Symbol>(node->type)))
  {
    addError("Variable '" + node->identifier + "' already declared in this scope");
    return;
  }

  bool isGlobal = (analyzer.currentScope->parent == nullptr);
  auto symbol = analyzer.resolveVariable(node->identifier);
  if (isGlobal)
  {
    // 全局变量默认已初始化

    symbol->isInitialized = true;

    // 检查全局变量的初始化
    if (node->type.arrayDimensionCount() > 0)
    {
      for (const auto &size : node->type.arraySizes())
      {
        // 检查数组下标是否为constexp
        if (!exprChecker(size, true))
        {
          addError("Array size for global variable '" + node->identifier + "' must be a constant integer expression");
          return;
        }
      }
      isValidArrayIndex(node->identifier, node->type.arraySizes());
      if (node->initializer)
      {
        // 检查数组初始化
        checkExprTypeCompatible(node->identifier, node->initializer, node->type, ExprContext::ARRAY_INDEX, true);
      }
    }
    else
    {
      if (node->initializer)
      {
        // 检查全局变量初始化
        checkExprTypeCompatible(node->identifier, node->initializer, node->type, ExprContext::EXPRESSION, true);
      }
    }
  }
  else
  {
    // 检查局部变量的初始化
    if (node->type.arrayDimensionCount() > 0)
    {
      for (const auto &size : node->type.arraySizes())
      {
        // 检查数组下标是否为constexp
        if (!exprChecker(size, true))
        {
          addError("Array size for local variable '" + node->identifier + "' must be a constant integer expression");
          return;
        }
      }
      // 检查数组下标是否为整数
      isValidArrayIndex(node->identifier, node->type.arraySizes());
      if (node->initializer)
      {
        symbol->isInitialized = true;
        // 检查数组初始化
        checkExprTypeCompatible(node->identifier, node->initializer, node->type, ExprContext::ARRAY_INDEX, node->type.isConst());
      }
    }
    else if (node->initializer)
    {

      symbol->isInitialized = true;

      checkExprTypeCompatible(node->identifier, node->initializer, node->type, ExprContext::EXPRESSION, node->type.isConst());
    }
  }
}

void TypeCheckerVisitor::visitExprStmt(shared_ptr<ExprStmtNode> node)
{
  if (node->expr)
  {
    exprChecker(node->expr, false);
  }
}

void TypeCheckerVisitor::visitAssignStmt(shared_ptr<AssignStmtNode> node)
{
  // 检查左值
  visitLValueExpr(node->lvalue);
  auto symbol = analyzer.resolveVariable(node->lvalue->identifier);

  if (!symbol)
  {
    return;
  }

  // 检查const正确性
  if (symbol->type.isConst())
  {
    addError("Cannot modify const variable '" + node->lvalue->identifier + "'");
  }

  // 先标记变量为已初始化，避免在检查右值表达式时出现未初始化错误
  symbol->isInitialized = true;

  // 检查右值表达式
  checkExprTypeCompatible(node->lvalue->identifier, node->rvalue, symbol->type, ExprContext::EXPRESSION, false);
}

void TypeCheckerVisitor::visitIfElseStmt(shared_ptr<IfElseStmtNode> node)
{
  // 检查条件表达式
  if (node->condition)
  {
    checkExprTypeCompatible("if condition", node->condition, DataType(PrimaryDataType::VOID), ExprContext::CONDITION, false);
  }
  // 检查then分支
  if (node->then_body)
  {
    visitStmt(node->then_body);
  }
  // 检查else分支
  if (node->else_body)
  {
    visitStmt(node->else_body);
  }
}

void TypeCheckerVisitor::visitWhileStmt(shared_ptr<WhileStmtNode> node)
{
  // 检查条件表达式
  if (node->condition)
  {
    checkExprTypeCompatible("while condition", node->condition, DataType(PrimaryDataType::VOID), ExprContext::CONDITION, false);
  }
  // 检查循环体
  if (node->body)
  {
    visitStmt(node->body);
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
    if (expectedReturnType.baseType == PrimaryDataType::VOID)
    {
      addError("Function '" + currentFunction->identifier + "' should not return a value");
      return;
    }

    checkExprTypeCompatible(currentFunction->identifier, node->ret_expr, expectedReturnType, ExprContext::EXPRESSION, false);
  }
  else
  {
    // 无返回值
    if (expectedReturnType.baseType != PrimaryDataType::VOID)
    {
      addError("Function '" + currentFunction->identifier + "' must return a value");
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

  // 检查数组访问
  if (!node->indices.empty())
  {
    if (!symbol->type.isArray())
    {
      addError("Variable '" + node->identifier + "' is not an array");
    }

    // 当不处于函数调用中时，检查数组维度
    if (!inFunctionCall)
    {
      if (symbol->type.arrayDimensionCount() != node->indices.size())
      {
        addError("Array '" + node->identifier + "' access dimension mismatch");
      }
    }

    for (const auto &index : node->indices)
    {
      exprChecker(index, false);
    }

    // 检查数组访问的索引为整数
    isValidArrayIndex(node->identifier, node->indices);
  }
}

void TypeCheckerVisitor::visitCallExpr(shared_ptr<CallExprNode> node)
{
  // 检查函数名是否已声明
  auto func = analyzer.resolveFunction(node->callee);
  if (!func)
  {
    addError("Callee '" + node->callee + "' is not defined");
    return;
  }

  // 检查函数调用参数数量是否一致
  inFunctionCall = true;
  auto targetParamsType = func->functionNode->params;

  if (targetParamsType.size() != node->args.size())
  {
    addError("Function '" + node->callee + "' expects " +
             to_string(targetParamsType.size()) + " arguments, got " +
             to_string(node->args.size()));
    inFunctionCall = false;
    return;
  }

  // 检查每个参数类型
  for (size_t i = 0; i < targetParamsType.size(); i++)
  {
    if (i < node->args.size())
    {
      checkExprTypeCompatible(node->callee, node->args[i], targetParamsType[i]->type, ExprContext::CALL, true);
    }
  }

  // 函数调用后，处理形参与实参的绑定关系
  // 当形参是数组类型时，函数可能会修改数组内容，需要将对应的实参标记为已初始化
  for (size_t i = 0; i < node->args.size() && i < targetParamsType.size(); i++)
  {
    if (auto lval = dynamic_pointer_cast<LValueExprNode>(node->args[i]))
    {
      auto actualArg = analyzer.resolveVariable(lval->identifier); // 实参
      auto formalParam = targetParamsType[i];                      // 形参

      // 形参与实参的绑定：如果形参是数组类型，说明函数可能会修改数组
      // 因此需要将实参标记为已初始化，建立形参与实参的初始化状态同步
      if (actualArg && formalParam->type.isArray())
      {
        // 无论实参原来是否初始化，经过函数调用后都应该被视为已初始化
        // 这是因为函数内部可能向数组写入数据
        actualArg->isInitialized = true;
      }
    }
  }

  inFunctionCall = false;
}

void TypeCheckerVisitor::visitStringLiteralExpr(shared_ptr<StringLiteralExprNode> node)
{
  if (!inFunctionCall)
  {
    addError("String literal can only be used in runtime function calls");
  }
}
