#include "SemanticAnalysis.h"

using namespace ast;

shared_ptr<Symbol> Symbol::lookup(const string &name)
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

void Symbol::insert(const string &name, shared_ptr<Symbol> symbol)
{
    // 检查当前作用域中是否已存在同名符号
    if (table.find(name) != table.end())
    {
        throw std::runtime_error("Symbol '" + name + "' already declared in this scope.");
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

void SemanticAnalyzer::declareVariable(const std::string &name, const std::shared_ptr<Symbol> &symbol)
{
    currentScope->insert(name, symbol);
}

shared_ptr<Symbol> SemanticAnalyzer::resolveVariable(const std::string &name)
{
    return currentScope->lookup(name);
}
