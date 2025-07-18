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
    // 支持两种模式：
    // 1. compiler -S -o testcase.s testcase.sy [-O1]
    // 2. compiler -debug testcase.sy
    bool debugMode = false;
    string input_file, output_file;
    optimization::OptimizationLevel opt_level = optimization::OptimizationLevel::O0;
    bool emit_riscv = false;

    // 参数解析
    if (argc >= 2 && strcmp(argv[1], "-debug") == 0)
    {
        debugMode = true;
        if (argc < 3)
        {
            cerr << "Usage: compiler -debug <input.sy>" << endl;
            return 1;
        }
        input_file = argv[2];
    }
    else if (argc >= 5 && strcmp(argv[1], "-S") == 0 && strcmp(argv[2], "-o") == 0)
    {
        emit_riscv = true;
        output_file = argv[3];
        input_file = argv[4];
        if (argc > 5)
        {
            if (strcmp(argv[5], "-O0") == 0)
                opt_level = optimization::OptimizationLevel::O0;
            else if (strcmp(argv[5], "-O1") == 0)
                opt_level = optimization::OptimizationLevel::O1;
            else if (strcmp(argv[5], "-O2") == 0)
                opt_level = optimization::OptimizationLevel::O2;
            else if (strcmp(argv[5], "-O10") == 0)
                opt_level = optimization::OptimizationLevel::O10;
            else if (strcmp(argv[5], "-O11") == 0)
                opt_level = optimization::OptimizationLevel::O11;
            else if (strcmp(argv[5], "-O12") == 0)
                opt_level = optimization::OptimizationLevel::O12;
            else if (strcmp(argv[5], "-O13") == 0)
                opt_level = optimization::OptimizationLevel::O13;
            else if (strcmp(argv[5], "-O14") == 0)
                opt_level = optimization::OptimizationLevel::O14;
            else if (strcmp(argv[5], "-O15") == 0)
                opt_level = optimization::OptimizationLevel::O15;
            else if (strcmp(argv[5], "-O16") == 0)
                opt_level = optimization::OptimizationLevel::O16;
            else
            {
                cerr << "Unknown optimization level: " << argv[5] << endl;
                return 1;
            }
        }
    }
    else
    {
        cerr << "Usage:\n"
             << "  compiler -S -o <output.s> <input.sy> [-O0|-O1|-O2|...]\n"
             << "  compiler -debug <input.sy>\n";
        return 1;
    }

    ifstream f_stream(input_file);
    if (!f_stream)
    {
        cerr << "Cannot open input file: " << input_file << endl;
        return 1;
    }
    ANTLRInputStream input(f_stream);
    SysYLexer lexer(&input);
    CommonTokenStream tokens(&lexer);
    SysYParser parser(&tokens);
    ParseTree *tree = parser.compUnit();

    // AST生成
    ASTNodeVisitor ast_visitor;
    auto ast_root = AS(ast_visitor.visit(tree), Ptr<ast::CompUnitNode>);

    // 语义分析
    TypeCheckerVisitor type_checker;
    try
    {
        type_checker.checkSemantic(ast_root);
        if (!type_checker.getErrors().empty())
        {
            cerr << "Semantic errors found:" << endl;
            for (const auto &error : type_checker.getErrors())
                cerr << error << endl;
            return 1;
        }
    }
    catch (const std::exception &e)
    {
        cerr << "Semantic analysis failed: " << e.what() << endl;
        return 1;
    }

    IRBuilder irbuilder(debugMode);
    auto ir_module = irbuilder.buildModule(ast_root);

    // 优化
    unique_ptr<optimization::PassManager> pass_manager;
    pass_manager = optimization::createOptimizationPipeline(opt_level, false);

    if (debugMode)
    {
        // 生成输出文件名
        string before_ir_file = input_file + ".ir";
        // 优化级别字符串
        string opt_str;
        switch (opt_level)
        {
        case optimization::OptimizationLevel::O0:
            opt_str = "O0";
            break;
        case optimization::OptimizationLevel::O1:
            opt_str = "O1";
            break;
        case optimization::OptimizationLevel::O2:
            opt_str = "O2";
            break;
        case optimization::OptimizationLevel::O10:
            opt_str = "O10";
            break;
        case optimization::OptimizationLevel::O11:
            opt_str = "O11";
            break;
        case optimization::OptimizationLevel::O12:
            opt_str = "O12";
            break;
        case optimization::OptimizationLevel::O13:
            opt_str = "O13";
            break;
        case optimization::OptimizationLevel::O14:
            opt_str = "O14";
            break;
        case optimization::OptimizationLevel::O15:
            opt_str = "O15";
            break;
        case optimization::OptimizationLevel::O16:
            opt_str = "O16";
            break;
        default:
            opt_str = "O0";
        }
        string after_ir_file = input_file + ".ir.opt" + opt_str;

        // 优化前IR
        ofstream fout_before(before_ir_file);
        if (!fout_before)
        {
            cerr << "Cannot open output file: " << before_ir_file << endl;
            return 1;
        }
        fout_before << ir_module->toString() << endl;
        fout_before.close();

        // 优化
        pass_manager->runOnModule(ir_module.get());

        // 优化后IR
        ofstream fout_after(after_ir_file);
        if (!fout_after)
        {
            cerr << "Cannot open output file: " << after_ir_file << endl;
            return 1;
        }
        fout_after << ir_module->toString() << endl;
        fout_after.close();

        return 0;
    }

    // 非debug模式，输出RISC-V汇编到文件
    pass_manager->runOnModule(ir_module.get());
    ofstream fout(output_file);
    if (!fout)
    {
        cerr << "Cannot open output file: " << output_file << endl;
        return 1;
    }
    RISCV::RISCVBuilder riscv_builder;
    std::shared_ptr<Module> shared_module(ir_module.release());
    auto riscv_module = riscv_builder.generateRISCVCode(shared_module);
    string assembly_code = riscv_builder.generateAssembly(riscv_module);
    fout << assembly_code << endl;

    return 0;
}