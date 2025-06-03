// src/main.cpp
#include "antlr4-runtime.h"
#include "frontend/SysYBaseVisitor.h"
#include "frontend/SysYLexer.h"
#include "frontend/SysYParser.h"
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
    SysYBaseVisitor parse_visitor;
    parse_visitor.visit(tree);
    cout << tree->toStringTree(&parser, true) << endl;
    return 0;
}