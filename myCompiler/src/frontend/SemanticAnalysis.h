#pragma once
#include "ASTNode.h"
#include <stdexcept>

using namespace ast;

// 符号类，存储变量和函数的信息
class Symbol
{
public:
    DataType type;      // 符号的数据类型
    bool isInitialized; // 符号是否已初始化

    Symbol(DataType type, bool isInitialized = false)
        : type(type), isInitialized(isInitialized) {}
};

// 符号表类，用于存储符号信息
class SymbolTable
{
public:
    unordered_map<string, shared_ptr<Symbol>> table;
    shared_ptr<SymbolTable> parent; // 指向父作用域的符号表

    SymbolTable(shared_ptr<SymbolTable> parent = nullptr)
        : parent(parent) {}

    // 在当前作用域查找符号
    shared_ptr<Symbol> lookup(const string &name);
    // 向符号表中插入新的符号
    void insert(const string &name, shared_ptr<Symbol> symbol);
};

class SemanticAnalyzer
{
public:
    shared_ptr<SymbolTable> currentScope;

    void enterScope();
    void exitScope();
    void declareVariable(const std::string &name, const std::shared_ptr<Symbol> &symbol);
    shared_ptr<Symbol> resolveVariable(const std::string &name);
};

// 有且只有一个main函数
// 符号解析 - 变量/函数是否已声明
// 类型匹配 - 赋值、参数传递的类型兼容性
// 类型提升 - int + float → float
// const 正确性 - 不能修改 const 变量
// 数组边界 - 维度匹配，索引为整数
// 函数签名 - 参数个数和类型匹配
// 作用域规则 - 变量可见性和生命周期
// 初始化检查 - 变量使用前是否已初始化
// expression中不能存在与或非和大小比较
class TypeCheckerVisitor
{
    // 语义分析器，负责检查 AST 的语义正确性
    // 自上而下对 AST 进行遍历，将所有错误收集到 errors 列表中
private:
    SemanticAnalyzer analyzer; // 引用语义分析器
    vector<string> errors;     // 错误列表

    // 新增状态跟踪变量
    shared_ptr<FuncNode> currentFunction;                      // 当前正在检查的函数
    bool inLoop;                                               // 是否在循环中
    bool hasMainFunction;                                      // 是否已声明main函数
    unordered_map<string, shared_ptr<FuncNode>> functionTable; // 函数表

public:
    bool checkSemantic(shared_ptr<CompUnitNode> astRoot)
    {
        // 进入全局作用域
        analyzer.enterScope();
        visitCompUnit(astRoot);
        return errors.empty();
    }

private:
    void visitCompUnit(shared_ptr<CompUnitNode> node);
    void visitFuncNode(shared_ptr<FuncNode> node);
    void visitBlockStmt(shared_ptr<BlockStmtNode> node);
    void visitDeclStmt(shared_ptr<DeclStmtNode> node);
    void visitExprStmt(shared_ptr<ExprStmtNode> node);
    void visitAssignStmt(shared_ptr<AssignStmtNode> node);
    void visitIfElseStmt(shared_ptr<IfElseStmtNode> node);
    void visitWhileStmt(shared_ptr<WhileStmtNode> node);
    void visitBreakStmt(shared_ptr<BreakStmtNode> node);
    void visitContinueStmt(shared_ptr<ContinueStmtNode> node);
    void visitReturnStmt(shared_ptr<ReturnStmtNode> node);
    void visitLValueExpr(shared_ptr<LValueExprNode> node);
    void visitInitExpr(shared_ptr<InitExprNode> node);
    void visitCallExpr(shared_ptr<CallExprNode> node);
    void visitBinaryExpr(shared_ptr<BinaryExprNode> node);
    void visitUnaryExpr(shared_ptr<UnaryExprNode> node);
    void visitLiteralExpr(shared_ptr<LiteralExprNode> node);
    void visitIntLiteralExpr(shared_ptr<IntLiteralExprNode> node);
    void visitFloatLiteralExpr(shared_ptr<FloatLiteralExprNode> node);

    // 新增辅助方法
    void addError(const string &message);
    DataType getExpressionType(shared_ptr<ExprNode> expr);
    bool isTypeCompatible(DataType from, DataType to);
    bool isValidArrayAccess(shared_ptr<LValueExprNode> lvalue);
    void checkFunctionCall(shared_ptr<CallExprNode> call);
    vector<string> getErrors() const { return errors; }
};
