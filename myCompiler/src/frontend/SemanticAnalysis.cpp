#include "SemanticAnalysis.h"

using namespace ast;

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

// Implementation of visitCompUnit
void TypeCheckerVisitor::visitCompUnit(shared_ptr<CompUnitNode> node)
{
  // Add the logic for visiting a CompUnitNode here
}
void TypeCheckerVisitor::visitFuncNode(shared_ptr<FuncNode> node)
{
}
void TypeCheckerVisitor::visitBlockStmt(shared_ptr<BlockStmtNode> node)
{
}
void TypeCheckerVisitor::visitDeclStmt(shared_ptr<DeclStmtNode> node)
{
}
void TypeCheckerVisitor::visitExprStmt(shared_ptr<ExprStmtNode> node)
{
}
void TypeCheckerVisitor::visitAssignStmt(shared_ptr<AssignStmtNode> node)
{
}
void TypeCheckerVisitor::visitIfElseStmt(shared_ptr<IfElseStmtNode> node)
{
}
void TypeCheckerVisitor::visitWhileStmt(shared_ptr<WhileStmtNode> node)
{
}
void TypeCheckerVisitor::visitBreakStmt(shared_ptr<BreakStmtNode> node)
{
}
void TypeCheckerVisitor::visitContinueStmt(shared_ptr<ContinueStmtNode> node)
{
}
void TypeCheckerVisitor::visitReturnStmt(shared_ptr<ReturnStmtNode> node)
{
}
void TypeCheckerVisitor::visitLValueExpr(shared_ptr<LValueExprNode> node)
{
}
void TypeCheckerVisitor::visitInitExpr(shared_ptr<InitExprNode> node)
{
}
void TypeCheckerVisitor::visitCallExpr(shared_ptr<CallExprNode> node)
{
}
void TypeCheckerVisitor::visitBinaryExpr(shared_ptr<BinaryExprNode> node)
{
}
void TypeCheckerVisitor::visitUnaryExpr(shared_ptr<UnaryExprNode> node)
{
}
void TypeCheckerVisitor::visitLiteralExpr(shared_ptr<LiteralExprNode> node)
{
}
void TypeCheckerVisitor::visitIntLiteralExpr(shared_ptr<IntLiteralExprNode> node)
{
}
void TypeCheckerVisitor::visitFloatLiteralExpr(shared_ptr<FloatLiteralExprNode> node)
{
}