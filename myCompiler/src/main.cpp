// src/main.cpp
#include "antlr4-runtime.h"
#include "frontend/generate/SysYBaseVisitor.h"
#include "frontend/generate/SysYLexer.h"
#include "frontend/generate/SysYParser.h"
#include "frontend/ASTNodeVisitor.h"
#include "frontend/ASTNode.h"
#include "frontend/SemanticAnalysis.h"
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

    // SysYBaseVisitor parse_visitor;
    // parse_visitor.visit(tree);
    // cout << tree->toStringTree(&parser, true) << endl;
    ASTNodeVisitor ast_visitor;

    auto ast_root = AS(ast_visitor.visit(tree), Ptr<ast::CompUnitNode>);

    // TypeCheckerVisitor type_checker;
    // type_checker.checkSemantic(ast_root);
    // if (!type_checker.getErrors().empty())
    // {
    //     cerr << "Semantic errors found:" << endl;
    //     for (const auto &error : type_checker.getErrors())
    //     {
    //         cerr << error << endl;
    //     }
    //     return 1; // 返回错误代码
    // }
    ast_root->print(cout, 0);
    return 0;
}