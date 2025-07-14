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
#include "midend/irbuild/IRBuilder.h"
#include "midend/pass/OptimizationPasses.h"
#include "backend/RISCVBuilder.h"

using namespace antlr4;
using namespace tree;
using namespace std;
using namespace ir_builder;

int main(int argc, const char *argv[])
{
    ifstream f_stream;
    f_stream.open(argv[1]);
    ANTLRInputStream input(f_stream);
    SysYLexer lexer(&input);
    CommonTokenStream tokens(&lexer);
    SysYParser parser(&tokens);
    ParseTree *tree = parser.compUnit();

    // 抽象语法树生成部分
    ASTNodeVisitor ast_visitor;
    auto ast_root = AS(ast_visitor.visit(tree), Ptr<ast::CompUnitNode>);

    // 语义分析部分
    TypeCheckerVisitor type_checker;
    try
    {
        type_checker.checkSemantic(ast_root);
        if (!type_checker.getErrors().empty())
        {
            cerr << "Semantic errors found:" << endl;
            for (const auto &error : type_checker.getErrors())
            {
                cerr << error << endl;
            }
            return 1; // 返回错误代码
        }
    }
    catch (const std::exception &e)
    {
        cerr << "Semantic analysis failed: " << e.what() << endl;
        return 1; // 返回错误代码
    }

    // 中间代码生成部分
    IRBuilder irbuilder;
    auto ir_module = irbuilder.buildModule(ast_root);

    // 中间代码优化部分
    unique_ptr<optimization::PassManager> pass_manager;
    if (argc > 4 && strcmp(argv[3], "-opt") == 0)
    {
        optimization::OptimizationLevel opt_level = optimization::OptimizationLevel::O2; // 默认O2级别
        if (strcmp(argv[4], "O0") == 0)
            opt_level = optimization::OptimizationLevel::O0;
        else if (strcmp(argv[4], "O1") == 0)
            opt_level = optimization::OptimizationLevel::O1;
        else if (strcmp(argv[4], "O2") == 0)
            opt_level = optimization::OptimizationLevel::O2;
        else if (strcmp(argv[4], "O10") == 0)
            opt_level = optimization::OptimizationLevel::O10; // 调试级别O10
        else if (strcmp(argv[4], "O11") == 0)
            opt_level = optimization::OptimizationLevel::O11; // 调试级别O11
        else if (strcmp(argv[4], "O12") == 0)   
            opt_level = optimization::OptimizationLevel::O12; // 调试级别O12
        else if (strcmp(argv[4], "O13") == 0)
            opt_level = optimization::OptimizationLevel::O13; // 调试级别O13
        else if (strcmp(argv[4], "O14") == 0)
            opt_level = optimization::OptimizationLevel::O14; // 调试级别O14
        else if (strcmp(argv[4], "O15") == 0)
            opt_level = optimization::OptimizationLevel::O15; // 调试级别O15
        else if (strcmp(argv[4], "O16") == 0)
            opt_level = optimization::OptimizationLevel::O16; // 调试级别O16
        else
            throw std::invalid_argument("Unknown optimization level: " + string(argv[4]));
        // 不开启优化日志
        pass_manager = optimization::createOptimizationPipeline(opt_level, false);
        pass_manager->runOnModule(ir_module.get());
    }
    // 输出结果
    if (argc > 2 && strcmp(argv[2], "-ir") == 0)
    {
        // 输出IR中间代码
        cout << ir_module->toString() << endl;
        // 调试
        if(argc > 3 && strcmp(argv[3], "-debug") == 0)
         {
            cout << "Debugging IR Module:" << endl;
            ir_module->printBasicBlockInfo();
            irbuilder.printValueTableInEveryBlock();
         }
    }
    else if (argc > 2 && strcmp(argv[2], "-riscv") == 0)
    {
        // 消除phi指令
        optimization::OptimizationLevel opt_level = optimization::OptimizationLevel::O0;
        auto pass_manager = optimization::createOptimizationPipeline(opt_level, false);
        pass_manager->runOnModule(ir_module.get());
        // 输出RISC-V代码
        RISCV::RISCVBuilder riscv_builder;
        auto riscv_module = riscv_builder.generateRISCVCode(std::shared_ptr<Module>(std::move(ir_module)));
        string assembly_code = riscv_builder.generateAssembly(riscv_module);
        cout << assembly_code << endl;
    }
    else if (argc > 2 && strcmp(argv[2], "-ast") == 0)
    {
        // 输出AST
        ast_root->print(cout, 0);
    }

    return 0;
}