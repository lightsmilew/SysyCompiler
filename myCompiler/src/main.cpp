// src/main.cpp
#include "antlr4-runtime.h"
#include "frontend/generate/SysYBaseVisitor.h"
#include "frontend/generate/SysYLexer.h"
#include "frontend/generate/SysYParser.h"
#include "frontend/ASTNodeVisitor.h"
#include "frontend/ASTNode.h"
#include <fstream>
#include <iostream>

using namespace antlr4;
using namespace tree;
using namespace std;

int main(int argc, const char *argv[])
{
    ifstream f_stream;
    f_stream.open(argv[1]);
    ANTLRInputStream input(f_stream);
    SysYLexer lexer(&input);
    CommonTokenStream tokens(&lexer);
    SysYParser parser(&tokens);
    ParseTree *tree = parser.compUnit();
    cout << "parse tree: " << endl;
    // SysYBaseVisitor parse_visitor;
    // parse_visitor.visit(tree);
    // cout << tree->toStringTree(&parser, true) << endl;
    ASTNodeVisitor ast_visitor;
    cout << "AST visitor finished." << endl;
    auto ast_root =AS(ast_visitor.visit(tree),Ptr<ast::CompUnitNode>);
    cout << "AST: " << endl;
    ast_root->print(cout, 0);
    cout << "Compilation finished successfully." << endl;
    return 0;
}