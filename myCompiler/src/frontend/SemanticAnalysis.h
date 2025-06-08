#include <ASTNodeVisitor.h>

using namespace std;

class SymbolTable
{
public:
    std::unordered_map<std::string, std::shared_ptr<Symbol>> table;
    std::shared_ptr<SymbolTable> parent; // 指向父作用域的符号表

    SymbolTable(std::shared_ptr<SymbolTable> parent = nullptr)
        : parent(parent) {}

    // 在当前作用域查找符号
    std::shared_ptr<Symbol> lookup(const std::string &name)
    {
        // ···
    }

    // 向符号表中插入新的符号
    void insert(const std::string &name, std::shared_ptr<Symbol> symbol)
    {
        // ···
    }
};

class SemanticAnalyzer
{
public:
    std::shared_ptr<SymbolTable> currentScope;

    void enterScope()
    {
        currentScope = std::make_shared<SymbolTable>(currentScope);
    }

    void exitScope()
    {
        currentScope = currentScope->parent;
    }

    void declareVariable(const std::string &name, const std::shared_ptr<Symbol> &symbol)
    {
        currentScope->insert(name, symbol);
    }

    std::shared_ptr<Symbol> resolveVariable(const std::string &name)
    {
        return currentScope->lookup(name);
    }
};

class TypeCheckerVisitor : public ASTVisitor
{
public:
    std::shared_ptr<Type> visitBinaryExpr(BinaryExprNode *node) override
    {
        auto leftType = visit(node->left);   // 检查左操作数的类型
        auto rightType = visit(node->right); // 检查右操作数的类型

        // 检查操作数的类型是否匹配
        if (!leftType->equals(rightType))
        {
            throw std::runtime_error("Type mismatch in binary expression.");
        }

        // 返回表达式的类型
        return leftType;
    }

    std::shared_ptr<Type> visitVariableDecl(VariableDeclNode *node) override
    {
        // 检查变量声明的类型是否正确
        auto varType = node->type;
        if (!isValidType(varType))
        {
            throw std::runtime_error("Invalid type for variable.");
        }

        return varType;
    }

    // 其他类型检查逻辑...
};

class TypeCheckerVisitor : public ASTVisitor
{
public:
    std::shared_ptr<Type> visitBinaryExpr(BinaryExprNode *node) override
    {
        auto leftType = visit(node->left);
        auto rightType = visit(node->right);

        // 进行隐式类型转换
        if (leftType->isInteger() && rightType->isFloat())
        {
            leftType = floatType(); // 将整数提升为浮点数
        }
        else if (leftType->isFloat() && rightType->isInteger())
        {
            rightType = floatType();
        }

        if (!leftType->equals(rightType))
        {
            throw std::runtime_error("Type mismatch in binary expression.");
        }

        return leftType;
    }
};