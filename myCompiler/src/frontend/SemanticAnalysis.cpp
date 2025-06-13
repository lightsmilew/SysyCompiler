#include "SemanticAnalysis.h"
#include <memory>
#include <string>
#include <stdexcept>

using namespace ast;
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

// 辅助方法实现
void TypeCheckerVisitor::addError(const string &message)
{
  errors.push_back(message);
}

DataType TypeCheckerVisitor::getExpressionType(shared_ptr<ExprNode> expr)
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
      return symbol->type;
    }
    return DataType(PrimaryDataType::VOID); // 错误情况
  }
  else if (auto binary = dynamic_pointer_cast<BinaryExprNode>(expr))
  {
    DataType leftType = getExpressionType(binary->left);
    DataType rightType = getExpressionType(binary->right);

    // 检查expression中不能存在与或非和大小比较
    if (binary->op == BinaryOp::And || binary->op == BinaryOp::Or ||
        binary->op == BinaryOp::Lt || binary->op == BinaryOp::Gt ||
        binary->op == BinaryOp::Le || binary->op == BinaryOp::Ge ||
        binary->op == BinaryOp::Eq || binary->op == BinaryOp::Ne)
    {
      addError("Expression cannot contain logical or comparison operations");
      return DataType(PrimaryDataType::VOID);
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
    // 检查一元操作符中的Not操作
    if (unary->op == UnaryOp::Not)
    {
      addError("Expression cannot contain logical NOT operation");
      return DataType(PrimaryDataType::VOID);
    }
    return getExpressionType(unary->operand);
  }
  else if (auto call = dynamic_pointer_cast<CallExprNode>(expr))
  {

    // auto it = functionTable.find(call->callee);
    // if (it != functionTable.end())
    // {
    //   return it->second->returnType;
    // }
    auto it=analyzer.resolveVariable(call->callee);
    if(it){
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
  // 完全相同的类型
  if (from.baseType == to.baseType && from.arrayDimensionCount() == to.arrayDimensionCount())
  {
    return true;
  }

  // int可以隐式转换为float（仅在非数组情况下）
  if (!from.isArray() && !to.isArray() &&
      from.baseType == PrimaryDataType::INT && to.baseType == PrimaryDataType::FLOAT)
  {
    return true;
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
    DataType indexType = getExpressionType(index);
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
    auto it=analyzer.resolveVariable(call->callee);
    if(!it){
        addError("Callee Function is not defined");
        return ;
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
    DataType argType = getExpressionType(call->args[i]);
    DataType paramType = func->params[i]->type;

    if (!isTypeCompatible(argType, paramType))
    {
      addError("Argument " + to_string(i + 1) + " of function '" + call->callee +
               "' has incompatible type");
    }
  }
}

// Implementation of visitCompUnit
void TypeCheckerVisitor::visitCompUnit(shared_ptr<CompUnitNode> node)
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
  analyzer.declareVariable(node->identifier, funcSymbol);

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

  // 创建符号并声明
  auto symbol = make_shared<Symbol>(node->type, node->initializer != nullptr);

  // 如果有初始化表达式，检查类型匹配
  if (node->initializer)
  {
    visitInitExpr(node->initializer);
    // 这里可以进一步检查初始化表达式的类型是否与变量类型兼容
  }

  analyzer.declareVariable(node->identifier, symbol);
}

void TypeCheckerVisitor::visitExprStmt(shared_ptr<ExprStmtNode> node)
{
  if (node->expr)
  {
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
    if (!isValidArrayAccess(node->lvalue))
    {
      return; // 错误已在isValidArrayAccess中报告
    }
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

  // 检查类型兼容性
  DataType lvalueType = symbol->type;
  // 如果是数组访问，需要获取元素类型
  if (!node->lvalue->indices.empty())
  {
    // 简化处理：假设访问后是基本类型
    lvalueType = DataType(lvalueType.baseType);
  }

  DataType rvalueType = getExpressionType(node->rvalue);
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
    DataType condType = getExpressionType(node->condition);
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
    DataType condType = getExpressionType(node->condition);
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
  }

  // 进入循环上下文
  bool wasInLoop = inLoop;
  inLoop = true;

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
  }

  // 恢复循环上下文
  inLoop = wasInLoop;
}

void TypeCheckerVisitor::visitBreakStmt(shared_ptr<BreakStmtNode> node)
{
  if (!inLoop)
  {
    addError("Break statement not in loop");
  }
}

void TypeCheckerVisitor::visitContinueStmt(shared_ptr<ContinueStmtNode> node)
{
  if (!inLoop)
  {
    addError("Continue statement not in loop");
  }
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
    DataType actualReturnType = getExpressionType(node->ret_expr);

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
    }
  }
}

void TypeCheckerVisitor::visitInitExpr(shared_ptr<InitExprNode> node)
{
  if (node->singleInitVal)
  {
    // 单一初始值
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
  }
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

  // 检查操作数类型
  DataType leftType = getExpressionType(node->left);
  DataType rightType = getExpressionType(node->right);

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

  // 检查操作数类型
  DataType operandType = getExpressionType(node->operand);

  // 如果是数组，不能直接应用一元操作
  if (operandType.isArray())
  {
    addError("Cannot apply unary operator to array");
  }
}

void TypeCheckerVisitor::visitLiteralExpr(shared_ptr<LiteralExprNode> node)
{
  // 字面量表达式通常不需要特殊检查
  // 可以在子类中进行具体的检查
}

void TypeCheckerVisitor::visitIntLiteralExpr(shared_ptr<IntLiteralExprNode> node)
{
  // 整数字面量通常不需要特殊检查
  // 可以检查值的范围等
}

void TypeCheckerVisitor::visitFloatLiteralExpr(shared_ptr<FloatLiteralExprNode> node)
{
  // 浮点数字面量通常不需要特殊检查
  // 可以检查值的范围等
}