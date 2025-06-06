#include <iostream>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace ast
{
    using std::move;
    using std::ostream;
    using std::shared_ptr;
    using std::string;
    using std::vector;
    using std::make_shared;


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
        INT,   // 整数类型
        FLOAT, // 浮点数类型
        VOID   // 空类型
    };

    // 完整数据类型结构体（支持数组）
    struct DataType
    {
        PrimaryDataType baseType;
        vector<int> _arraySizes;
        bool _isConst = false;

        // 构造函数
        DataType(PrimaryDataType type = PrimaryDataType::VOID)
            : baseType(type), _arraySizes(), _isConst(false) {}

        DataType(PrimaryDataType type, const vector<int> &arraySizes, bool isConst = false)
            : baseType(type), _arraySizes(arraySizes), _isConst(isConst) {}

        // 数组相关方法
        int arrayDimensionCount() const { return _arraySizes.size(); } // 获取数组维度数量
        const vector<int> &arraySizes() const { return _arraySizes; }  // 获取数组大小列表
        bool isArray() const { return !_arraySizes.empty(); }          // 是否为数组类型
        bool isConst() const { return _isConst; }                      // 是否为常量类型

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

        // // ir代码生成的基类方法
        // virtual void baseCodegen(const Ptr<ir_builder::Builder> &builder) = 0;
    };

    //--- ExprNode ---//
    class ExprNode : public ASTNode
    {
    public:
        unsigned line = 0;    // 表达式所在的行号，用于调试和错误报告
        bool isConst = false; // 是否是常量表达式

        ExprNode() {} // 默认构造函数

        string toString() const override = 0;

        virtual DataType getDataType() const = 0;
    };

    class InitExprNode : public ExprNode
    {
    public:
        shared_ptr<ExprNode> singleInitVal;            // 用于单一初始值
        vector<shared_ptr<InitExprNode>> multiInitVal; // 用于复合初始值
        DataType targetDataType;                       // 因为要赋值给一个变量

        InitExprNode(shared_ptr<ExprNode> expr) : singleInitVal{move(expr)} {}
        InitExprNode(vector<shared_ptr<InitExprNode>> initVals) : multiInitVal{move(initVals)} {}

        string toString() const override;
        void print(ostream &out, unsigned indent = 0) const override;
        DataType getDataType() const override;
    };

    class LValueExprNode : public ExprNode
    {
    public:
        string identifier;                    // 标识符名称
        vector<shared_ptr<ExprNode>> indices; // 索引列表，用于数组或多维数据结构

        LValueExprNode(string id, vector<shared_ptr<ExprNode>> idxs = {})
            : identifier{move(id)}, indices{move(idxs)} {}
        string toString() const override;
        void print(ostream &out, unsigned indent = 0) const override;
        DataType getDataType() const override;
    };

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
        DataType getDataType() const override;
    };

    class UnaryExprNode : public ExprNode
    {
    public:
        shared_ptr<ExprNode> operand; // 操作数
        UnaryOp op;                   // 操作符

        UnaryExprNode(shared_ptr<ExprNode> opnd, UnaryOp operator_)
            : operand{move(opnd)}, op{move(operator_)} {}
        string toString() const override;
        void print(ostream &out, unsigned indent = 0) const override;
        DataType getDataType() const override;
    };

    class LiteralExprNode : public ExprNode
    {
    public:
        string toString() const override = 0;
    };

    class NumberLiteralExprNode : public LiteralExprNode
    {
    public:
        // 虚析构函数，确保子类析构函数正确调用
        virtual ~NumberLiteralExprNode() = default;
    };

    class IntLiteralExprNode : public NumberLiteralExprNode
    {
        using Value = std::int32_t;

    public:
        Value value;

        IntLiteralExprNode(Value val) : value{val} {}

        string toString() const override;
        void print(ostream &out, unsigned indent = 0) const override;
        DataType getDataType() const override;
    };

    class FloatLiteralExprNode : public NumberLiteralExprNode
    {
        using Value = float;

    public:
        Value value;

        FloatLiteralExprNode(Value val) : value{val} {}

        string toString() const override;
        void print(ostream &out, unsigned indent = 0) const override;
        DataType getDataType() const override;
    };

    // class StringLiteralExprNode : public LiteralExprNode
    // {
    // public:
    //     string value; // 字符串值

    //     StringLiteralExprNode(string val) : value{move(val)} {}

    //     string toString() const override;
    //     void print(ostream &out, unsigned indent = 0) const override;
    //     DataType getDataType() const override;
    // };

    class CallExprNode : public ExprNode
    {
    public:
        string callee;                     // 被调用的函数名
        vector<shared_ptr<ExprNode>> args; // 函数参数列表

        CallExprNode(string callee, vector<shared_ptr<ExprNode>> args)
            : callee{move(callee)}, args{move(args)} {}

        string toString() const override;
        void print(ostream &out, unsigned indent = 0) const override;
        DataType getDataType() const override;
    };

    //--- StmtNode ---//
    class StmtNode : public ASTNode
    {
    public:
        unsigned line = 0; // 语句所在的行号，用于调试和错误报告

        StmtNode() {} // 默认构造函数
        string toString() const override = 0;
    };

    class BlockStmtNode : public StmtNode
    {
    public:
        vector<shared_ptr<StmtNode>> stmts; // 语句列表

        BlockStmtNode(vector<shared_ptr<StmtNode>> statements)
            : stmts{move(statements)} {}

        string toString() const override;
        void print(ostream &out, unsigned indent = 0) const override;
    };

    class ExprStmtNode : public StmtNode
    {
    public:
        shared_ptr<ExprNode> expr; // 表达式节点

        ExprStmtNode(shared_ptr<ExprNode> expr) : expr{move(expr)} {}

        string toString() const override;
        void print(ostream &out, unsigned indent = 0) const override;
    };

    class DeclStmtNode : public StmtNode
    {
    public:
        bool isConst;                         // 是否是常量声明
        bool isFuncParam;                     // 是否是函数参数
        DataType type;                        // 变量类型
        string identifier;                    // 变量名
        shared_ptr<InitExprNode> initializer; // 初始化表达式，nullptr表示无初始化
        vector<shared_ptr<ExprNode>> indices; // 数组下标，可能为空

        // 构造函数，使用默认参数实现isConst的自动设置
        DeclStmtNode(DataType type, string identifier, shared_ptr<InitExprNode> initializer = nullptr, bool isConst = false, bool isFuncParam = false)
            : isConst{isConst}, isFuncParam{isFuncParam}, type{type}, identifier{move(identifier)}, initializer{move(initializer)} {}
        string toString() const override;
        void print(ostream &out, unsigned indent = 0) const override;
    };

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

    class BreakStmtNode : public StmtNode
    {
    public:
        string toString() const override;
    };

    class ContinueStmtNode : public StmtNode
    {
    public:
        string toString() const override;
    };

    class ReturnStmtNode : public StmtNode
    {
    public:
        shared_ptr<ExprNode> ret_expr; // 返回表达式，nullptr表示无返回值

        ReturnStmtNode(shared_ptr<ExprNode> expr = nullptr) : ret_expr{move(expr)} {}

        string toString() const override;
        void print(ostream &out, unsigned indent = 0) const override;
    };

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

    class CompUnitNode : public ASTNode
    {
    public:
        vector<shared_ptr<ASTNode>> children; // 子节点列表

        CompUnitNode(vector<shared_ptr<ASTNode>> children) : children{move(children)} {}

        string toString() const override;
        void print(ostream &out, unsigned indent = 0) const override;
    };

    // 类型转换工具类（不是AST节点）
    class TypeConverter
    {
    public:
        // 将DataType转换为字符串
        static string dataTypeToString(const DataType &type)
        {
            switch (type.baseType)
            {
            case PrimaryDataType::INT:
                return "int";
            case PrimaryDataType::FLOAT:
                return "float";
            case PrimaryDataType::VOID:
                return "void";
            default:
                return "unknown";
            }
        }

        // 检查两个类型是否兼容
        static bool isCompatible(const DataType &from, const DataType &to)
        {
            if (from.baseType == to.baseType)
                return true;
            // int可以隐式转换为float
            if (from.baseType == PrimaryDataType::INT && to.baseType == PrimaryDataType::FLOAT)
                return true;
            return false;
        }
    };
}
