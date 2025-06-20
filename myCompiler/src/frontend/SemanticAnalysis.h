#pragma once
#include "ASTNode.h"
#include <stdexcept>

using namespace ast;

enum class SymbolType
{
    VARIABLE,
    FUNCTION
};

// 符号类，存储变量和函数的信息
class Symbol
{
public:
    SymbolType symbolType;                // 符号类型（变量或函数）
    DataType type;                        // 符号的数据类型
    bool isInitialized;                   // 符号是否已初始化
    bool isConst;                         // 是否为常量
    vector<shared_ptr<ExprNode>> indices; // 数组下标，可能为空
    shared_ptr<FuncNode> functionNode;    // 如果是函数，指向函数节点

    Symbol(DataType type, bool isInitialized = false, bool isConst = false)
        : symbolType(SymbolType::VARIABLE), type(type), isInitialized(isInitialized), isConst(isConst), functionNode(nullptr) {}
    Symbol(shared_ptr<FuncNode> funcNode)
        : symbolType(SymbolType::FUNCTION), type(funcNode->returnType), isInitialized(true), isConst(false), functionNode(funcNode) {}
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
    // 向符号表中插入新的符号，返回是否成功（false表示重复声明）
    bool insert(const string &name, shared_ptr<Symbol> symbol);
};

class SemanticAnalyzer
{
public:
    shared_ptr<SymbolTable> currentScope;
    shared_ptr<SymbolTable> functionTable; // 函数符号表

    // 构造函数
    SemanticAnalyzer()
    {
        currentScope = make_shared<SymbolTable>();
        functionTable = make_shared<SymbolTable>();
    }

    void enterScope();
    void exitScope();
    void declareVariable(const std::string &name, const std::shared_ptr<Symbol> &symbol);
    shared_ptr<Symbol> resolveVariable(const std::string &name);
    bool declareFunction(const std::string &name, const std::shared_ptr<Symbol> &symbol);
    shared_ptr<Symbol> resolveFunction(const std::string &name);
};

// 有且只有一个main函数    ✔
// 符号解析 - 变量/函数是否已声明  ✔
// 类型匹配 - 赋值、参数传递的类型兼容性 ✔
// 类型提升 - int + float → float  ✔
// const 正确性 - 不能修改 const 变量 ✔
// 数组边界 - 维度匹配，索引为整数 ✔
// 函数签名 - 参数个数和类型匹配   ✔
// 作用域规则 - 变量可见性和生命周期   ✔
// 初始化检查 - 变量使用前是否已初始化  ✔
// expression中不能存在与或非和大小比较  ×非没有检查 √
// a) 当返回类型为int/float时，函数内所有分支都应当含有带有Exp的return语句。不含有return语句的分支的返回值未定义。(中间代码时解决)
// b) 当返回值类型为void时，函数内只能出现不带返回值的return语句。✔

// 字面量未检查范围 √
// 变量类型隐式转换是否可以在数组下进行 √
// 数组访问越界问题(动态检查先不管)
// 溢出判断需要在动态分析中解决
// 库函数可能因为没有在文件中定义而无法使用
// 库函数需支持字符串类型 √

// 数组长度必须为常量 ✔
// 数组初始化多种形式 ✔全部平铺展开
// 必须用常量初始化常量表达式       ✔
// 全局变量的初值必须为常量表达式   ✔
// 数组初始化要检查初值类型是否匹配 ✔
// 可以把多维数组的一维当作参数传递 ✔
// void visitDeclStmt(shared_ptr<DeclStmtNode> node);处理数组，维度必须非负整数 ✔
class TypeCheckerVisitor
{
    // 语义分析器，负责检查 AST 的语义正确性
    // 自上而下对 AST 进行遍历，将所有错误收集到 errors 列表中
private:
    // 表达式上下文枚举
    enum class ExprContext
    {
        EXPRESSION, // 普通表达式上下文（不允许逻辑和比较操作）
        CONDITION,  // 条件表达式上下文（允许逻辑和比较操作）
        ARRAY_INDEX // 数组索引上下文（不允许逻辑和比较操作）
    };

    SemanticAnalyzer analyzer; // 引用语义分析器
    vector<string> errors;     // 错误列表

    // 状态跟踪变量
    shared_ptr<FuncNode> currentFunction; // 当前正在检查的函数
    bool hasMainFunction;                 // 是否已声明main函数
    bool inFunctionCall;                  // 是否在函数调用上下文中

public:
    // 构造函数
    TypeCheckerVisitor() : hasMainFunction(false), inFunctionCall(false), currentFunction(nullptr) {}

    bool checkSemantic(shared_ptr<CompUnitNode> astRoot)
    {
        // 进入全局作用域
        analyzer.enterScope();
        visitCompUnitForCheck(astRoot);
        return errors.empty();
    }
    vector<string> getErrors() const { return errors; }

private:
    void visitCompUnitForCheck(shared_ptr<CompUnitNode> node);
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
    // void visitLiteralExpr(shared_ptr<LiteralExprNode> node);
    void visitIntLiteralExpr(shared_ptr<IntLiteralExprNode> node);
    void visitFloatLiteralExpr(shared_ptr<FloatLiteralExprNode> node);
    void visitStringLiteralExpr(shared_ptr<StringLiteralExprNode> node);

    // 新增辅助方法
    shared_ptr<ExprNode> castExpression(shared_ptr<ExprNode> expr, DataType targetType);
    void addError(const string &message);
    DataType getExpressionType(shared_ptr<ExprNode> expr, ExprContext context = ExprContext::EXPRESSION);
    bool isTypeCompatible(DataType from, DataType to);
    bool isValidArrayAccess(shared_ptr<LValueExprNode> lvalue);
    bool checkArrayInit(const shared_ptr<ExprNode> &init, const DataType &declaredType, int dim, bool isConst = false);
    void checkFunctionCall(shared_ptr<CallExprNode> call);
};
