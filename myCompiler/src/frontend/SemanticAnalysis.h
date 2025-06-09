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
public:
};
