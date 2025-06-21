#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <unordered_map>

namespace ast
{
  using std::hash;
  using std::make_pair;
  using std::make_shared;
  using std::make_unique;
  using std::move;
  using std::ostream;
  using std::pair;
  using std::shared_ptr;
  using std::string;
  using std::to_string;
  using std::unique_ptr;
  using std::unordered_map;
  using std::vector;

  // 定义操作符枚举
  enum class UnaryOp
  {
    Plus,  // +
    Minus, // -
    Not    // !
  };

  enum class BinaryOp
  {
    Add, // +
    Sub, // -
    Mul, // *
    Div, // /
    Mod, // %
    Lt,  // <
    Gt,  // >
    Le,  // <=
    Ge,  // >=
    Eq,  // ==
    Ne,  // !=
    And, // &&
    Or   // ||
  };

  // 基础数据类型枚举
  enum class PrimaryDataType
  {
    INT,    // 整数类型
    FLOAT,  // 浮点数类型
    STRING, // 字符串类型
    VOID    // 空类型
  };

  // 完整数据类型结构体（支持数组）
  struct DataType
  {
    PrimaryDataType baseType;
    vector<int> _arraySizes;
    bool _isConstVariable = false;

    // 构造函数
    DataType(PrimaryDataType type = PrimaryDataType::VOID)
        : baseType(type), _arraySizes(), _isConstVariable(false) {}

    DataType(PrimaryDataType type, const vector<int> &arraySizes, bool isConstVariable = false)
        : baseType(type), _arraySizes(arraySizes), _isConstVariable(isConstVariable) {}

    // 数组相关方法
    int arrayDimensionCount() const { return _arraySizes.size(); } // 获取数组维度数量
    const vector<int> &arraySizes() const { return _arraySizes; }  // 获取数组大小列表
    bool isArray() const { return !_arraySizes.empty(); }          // 是否为数组类型
    bool isConst() const { return _isConstVariable; }                      // 是否为常量类型

    // 方便的比较操作
    bool operator==(PrimaryDataType other) const { return baseType == other && !isArray(); }
    bool operator!=(PrimaryDataType other) const { return !(*this == other); }
  };

  // 前向声明
  class ASTNode;
  class ExprNode;
  class InitExprNode;
  class LValueExprNode;
  class BinaryExprNode;
  class UnaryExprNode;
  class LiteralExprNode;
  class IntLiteralExprNode;
  class FloatLiteralExprNode;
  class StringLiteralExprNode;
  class CallExprNode;
  class StmtNode;
  class BlockStmtNode;
  class ExprStmtNode;
  class DeclStmtNode;
  class AssignStmtNode;
  class IfElseStmtNode;
  class WhileStmtNode;
  class BreakStmtNode;
  class ContinueStmtNode;
  class ReturnStmtNode;
  class FuncNode;
  class CompUnitNode;
  class CastExpNode;

  class ASTNode
  {
  public:
    virtual ~ASTNode() = default; // 虚析构函数，确保子类析构函数正确调用

    // 纯虚函数，用于返回节点的字符串表示
    virtual string toString() const = 0;

    // 虚函数，用于打印节点
    virtual void print(ostream &out, unsigned indent = 0) const
    {
      out << string(indent, ' ') << toString() << "\n";
    }
  };

  //--- ExprNode ---//
  class ExprNode : public ASTNode
  {
  public:
    unsigned line = 0;    // 表达式所在的行号，用于调试和错误报告
    bool isConstExp = false; // 是否是常量表达式

    ExprNode() {} // 默认构造函数

    string toString() const override = 0;
  };

  // //--- CastExpNode ---//
  // // 类型转换表达式节点，表示将一个表达式转换为另一种数据类型
  // class CastExpNode : public ExprNode
  // {
  // public:
  //   shared_ptr<ExprNode> sourceExpr; // 被转换的表达式
  //   DataType targetType;             // 目标数据类型

  //   CastExpNode(shared_ptr<ExprNode> op, DataType type)
  //       : sourceExpr{move(op)}, targetType{move(type)} {}

  //   string toString() const override;
  // };

  // 初始化表达式节点
  // int a = 1; // 单一初始值
  // int b[2][3] = { {1, 2, 3}, {4, 5, 6} }; // 复合初始值
  class InitExprNode : public ExprNode
  {
  public:
    shared_ptr<ExprNode> singleInitVal;            // 用于单一初始值
    vector<shared_ptr<InitExprNode>> multiInitVal; // 用于复合初始值
    DataType targetDataType;                       // 因为要赋值给一个变量

    InitExprNode(shared_ptr<ExprNode> expr) : singleInitVal{move(expr)} {}
    InitExprNode(vector<shared_ptr<InitExprNode>> initVals) : multiInitVal{move(initVals)} {}

    string toString() const override;
  };

  // 左值表达式节点，表示变量或数组元素等可以被赋值的表达式
  // 例如：a, a[0], a[1][2] 等
  class LValueExprNode : public ExprNode
  {
  public:
    string identifier;                    // 标识符名称
    vector<shared_ptr<ExprNode>> indices; // 索引列表，用于数组或多维数据结构

    LValueExprNode(string id, vector<shared_ptr<ExprNode>> idxs = {})
        : identifier{move(id)}, indices{move(idxs)} {}
    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // 二元表达式节点，表示两个操作数之间的二元操作
  // 例如：a + b, a - b, a * b 等
  class BinaryExprNode : public ExprNode
  {
  public:
    shared_ptr<ExprNode> left;  // 左操作数
    shared_ptr<ExprNode> right; // 右操作数
    BinaryOp op;                // 操作符

    BinaryExprNode(shared_ptr<ExprNode> l, shared_ptr<ExprNode> r, BinaryOp operator_)
        : left{move(l)}, right{move(r)}, op{move(operator_)} {}
    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // 一元表达式节点，表示单个操作数的操作
  // 例如：-a, +b 等
  class UnaryExprNode : public ExprNode
  {
  public:
    shared_ptr<ExprNode> operand; // 操作数
    UnaryOp op;                   // 操作符

    UnaryExprNode(shared_ptr<ExprNode> opnd, UnaryOp operator_)
        : operand{move(opnd)}, op{move(operator_)} {}
    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // 字面量表达式节点，表示常量值
  // 例如：整数、浮点数等
  class LiteralExprNode : public ExprNode
  {
  public:
    string toString() const override = 0;
  };

  // 数字字面量表达式节点，表示整数或浮点数等数字类型的常量
  class NumberLiteralExprNode : public LiteralExprNode
  {
  public:
    // 虚析构函数，确保子类析构函数正确调用
    virtual ~NumberLiteralExprNode() = default;
  };

  // 整数字面量表达式节点，表示整数类型的常量
  // 例如：1, -42 等
  class IntLiteralExprNode : public NumberLiteralExprNode
  {
    using Value = std::int32_t;

  public:
    Value value;

    IntLiteralExprNode(Value val) : value{val} {}

    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // 浮点数字面量表达式节点，表示浮点数类型的常量
  // 例如：3.14, -0.001 等
  class FloatLiteralExprNode : public NumberLiteralExprNode
  {
    using Value = float;

  public:
    Value value;

    FloatLiteralExprNode(Value val) : value{val} {}

    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  class StringLiteralExprNode : public LiteralExprNode
  {
  public:
    string value; // 字符串值

    StringLiteralExprNode(string val) : value{move(val)} {}

    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // 函数调用表达式节点，表示对函数的调用
  // 例如：foo(a, b), bar(1, 2.0) 等
  class CallExprNode : public ExprNode
  {
  public:
    string callee;                     // 被调用的函数名
    vector<shared_ptr<ExprNode>> args; // 函数参数列表

    CallExprNode(string callee, vector<shared_ptr<ExprNode>> args)
        : callee{move(callee)}, args{move(args)} {}

    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  //--- StmtNode ---//
  class StmtNode : public ASTNode
  {
  public:
    unsigned line = 0; // 语句所在的行号，用于调试和错误报告

    StmtNode() {} // 默认构造函数
    string toString() const override = 0;
  };

  // 语句块节点，表示一组语句的集合
  // 例如：{ a = 1; b = 2; } 等
  class BlockStmtNode : public StmtNode
  {
  public:
    vector<shared_ptr<StmtNode>> stmts; // 语句列表

    BlockStmtNode(vector<shared_ptr<StmtNode>> statements)
        : stmts{move(statements)} {}

    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // 表达式语句节点，表示一个表达式作为语句执行
  class ExprStmtNode : public StmtNode
  {
  public:
    shared_ptr<ExprNode> expr; // 表达式节点

    ExprStmtNode(shared_ptr<ExprNode> expr) : expr{move(expr)} {}

    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // 声明语句节点，表示变量或函数的声明
  // 例如：int a; float b[10]; void foo(int x); 等
  class DeclStmtNode : public StmtNode
  {
  public:
    bool isFuncParam;                     // 是否是函数参数
    DataType type;                        // 变量类型
    string identifier;                    // 变量名
    shared_ptr<InitExprNode> initializer; // 初始化表达式，nullptr表示无初始化
    vector<shared_ptr<ExprNode>> indices; // 数组下标，可能为空

    // 构造函数，使用默认参数实现isConst的自动设置
    DeclStmtNode(DataType type, string identifier, shared_ptr<InitExprNode> initializer = nullptr, bool isFuncParam = false)
        :  isFuncParam{isFuncParam}, type{type}, identifier{move(identifier)}, initializer{move(initializer)} {}
    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // 赋值语句节点，表示将一个表达式的值赋给一个左值
  // 例如：a = 1; b[0] = a + 2; c = d[1][2] * 3.14; 等
  class AssignStmtNode : public StmtNode
  {
  public:
    shared_ptr<LValueExprNode> lvalue; // 左值表达式
    shared_ptr<ExprNode> rvalue;       // 右值表达式

    AssignStmtNode(shared_ptr<LValueExprNode> lval, shared_ptr<ExprNode> rval)
        : lvalue{move(lval)}, rvalue{move(rval)} {}

    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // 条件语句节点，表示 if-else 结构
  class IfElseStmtNode : public StmtNode
  {
  public:
    shared_ptr<ExprNode> condition; // 条件表达式
    shared_ptr<StmtNode> then_body; // if 分支体
    shared_ptr<StmtNode> else_body; // else 分支体，nullptr表示无 else 分支

    IfElseStmtNode(shared_ptr<ExprNode> cond, shared_ptr<StmtNode> thenBody, shared_ptr<StmtNode> elseBody = nullptr)
        : condition{move(cond)}, then_body{move(thenBody)}, else_body{move(elseBody)} {}

    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // 循环语句节点，表示 while 循环结构
  class WhileStmtNode : public StmtNode
  {
  public:
    shared_ptr<ExprNode> condition; // 循环条件表达式
    shared_ptr<StmtNode> body;      // 循环体

    WhileStmtNode(shared_ptr<ExprNode> cond, shared_ptr<StmtNode> body)
        : condition{move(cond)}, body{move(body)} {}

    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // 跳出循环或函数的语句节点
  class BreakStmtNode : public StmtNode
  {
  public:
    string toString() const override;
  };

  // 跳过当前循环迭代的语句节点
  class ContinueStmtNode : public StmtNode
  {
  public:
    string toString() const override;
  };

  // 返回语句节点，表示函数的返回操作
  class ReturnStmtNode : public StmtNode
  {
  public:
    shared_ptr<ExprNode> ret_expr; // 返回表达式，nullptr表示无返回值

    ReturnStmtNode(shared_ptr<ExprNode> expr = nullptr) : ret_expr{move(expr)} {}

    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // 函数节点，表示函数的定义
  class FuncNode : public ASTNode
  {
  public:
    DataType returnType;                     // 函数返回类型
    string identifier;                       // 函数名
    vector<shared_ptr<DeclStmtNode>> params; // 函数参数列表
    shared_ptr<BlockStmtNode> body;          // 函数体

    FuncNode(DataType retType, string id, vector<shared_ptr<DeclStmtNode>> params, shared_ptr<BlockStmtNode> body)
        : returnType{retType}, identifier{move(id)}, params{move(params)}, body{move(body)} {}

    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // 复合单位节点，表示整个程序的顶层结构
  class CompUnitNode : public ASTNode
  {
  public:
    vector<shared_ptr<ASTNode>> children; // 子节点列表

    CompUnitNode(vector<shared_ptr<ASTNode>> children) : children{move(children)} {}

    string toString() const override;
    void print(ostream &out, unsigned indent = 0) const override;
  };

  // // 类型转换工具类（不是AST节点）
  // class TypeConverter
  // {
  // public:
  //   // 将DataType转换为字符串
  //   static string dataTypeToString(const DataType &type)
  //   {
  //     switch (type.baseType)
  //     {
  //     case PrimaryDataType::INT:
  //       return "int";
  //     case PrimaryDataType::FLOAT:
  //       return "float";
  //     case PrimaryDataType::VOID:
  //       return "void";
  //     default:
  //       return "unknown";
  //     }
  //   }

  //   // 检查两个类型是否兼容
  //   static bool isCompatible(const DataType &from, const DataType &to)
  //   {
  //     if (from.baseType == to.baseType)
  //       return true;
  //     // int可以隐式转换为float
  //     if (from.baseType == PrimaryDataType::INT && to.baseType == PrimaryDataType::FLOAT)
  //       return true;
  //     return false;
  //   }
  // };
}
