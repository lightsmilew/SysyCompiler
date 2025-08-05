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
    // 支持三种模式，参数顺序和位置均可变
    bool debugMode = false;
    bool infoMode = false;
    bool optMode = false;
    string input_file, output_file;
    optimization::OptimizationLevel opt_level = optimization::OptimizationLevel::O0;
    bool emit_riscv = false;

    // 参数解析
    if (argc >= 2 && strcmp(argv[1], "-debug") == 0)
    {
        debugMode = true;
        if (argc < 4)
        {
            cerr << "Usage: compiler -debug <input.sy> <output_prefix> [-info|-O0|-O1|...]" << endl;
            return 1;
        }
        input_file = argv[2];
        output_file = argv[3];
        for (int i = 4; i < argc; ++i)
        {
            if (strcmp(argv[i], "-info") == 0)
                infoMode = true;
            else if (strncmp(argv[i], "-O", 2) == 0)
            {
                string optstr = argv[i];
                if (optstr == "-O0")
                    opt_level = optimization::OptimizationLevel::O0;
                else if (optstr == "-O1")
                    opt_level = optimization::OptimizationLevel::O1;
                else if (optstr == "-O2")
                    opt_level = optimization::OptimizationLevel::O2;
                else if (optstr == "-O15")
                    opt_level = optimization::OptimizationLevel::O15;
                else if (optstr == "-O16")
                    opt_level = optimization::OptimizationLevel::O16;
                else
                {
                    cerr << "Unknown optimization level: " << argv[i] << endl;
                    return 1;
                }
                optMode = true;
            }
        }
    }
    else
    {
        // 自动识别输入/输出文件和参数
        for (int i = 1; i < argc; ++i)
        {
            if (strcmp(argv[i], "-S") == 0)
                emit_riscv = true;
            else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc)
                output_file = argv[++i];
            else if (strncmp(argv[i], "-O", 2) == 0)
            {
                string optstr = argv[i];
                if (optstr == "-O0")
                    opt_level = optimization::OptimizationLevel::O0;
                else if (optstr == "-O1")
                    opt_level = optimization::OptimizationLevel::O1;
                else if (optstr == "-O2")
                    opt_level = optimization::OptimizationLevel::O2;
                else if (optstr == "-O15")
                    opt_level = optimization::OptimizationLevel::O15;
                else if (optstr == "-O16")
                    opt_level = optimization::OptimizationLevel::O16;
                else
                {
                    cerr << "Unknown optimization level: " << argv[i] << endl;
                    return 1;
                }
            }
            // 源文件识别：以.sy结尾
            else if (strlen(argv[i]) > 3 && strcmp(argv[i] + strlen(argv[i]) - 3, ".sy") == 0)
                input_file = argv[i];
            // 汇编文件识别：以.s结尾
            else if (strlen(argv[i]) > 2 && strcmp(argv[i] + strlen(argv[i]) - 2, ".s") == 0)
                output_file = argv[i];
        }
        if (!emit_riscv || input_file.empty() || output_file.empty())
        {
            cerr << "Usage:\n"
                 << "  参数顺序可变，示例：\n"
                 << "    compiler -S -o <output.s> <input.sy> [-O0|-O1|-O2|...]\n"
                 << "    compiler <input.sy> -S -o <output.s> [-O0|-O1|-O2|...]\n"
                 << "    compiler -o <output.s> -S <input.sy> [-O0|-O1|-O2|...]\n"
                 << "    compiler -debug <input.sy> <output_prefix> [-info|-O0|-O1|...]\n";
            return 1;
        }
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
        string before_ir_file = output_file + ".ir";
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
        case optimization::OptimizationLevel::O15:
            opt_str = "O15";
            break;
        case optimization::OptimizationLevel::O16:
            opt_str = "O16";
            break;
        default:
            opt_str = "O0";
        }
        string after_ir_file = output_file + ".ir.opt" + opt_str;

        if (!optMode)
        {
            ofstream fout_before(before_ir_file);
            if (!fout_before)
            {
                cerr << "Cannot open output file: " << before_ir_file << endl;
                return 1;
            }
            fout_before << ir_module->toString() << endl;
            if (infoMode)
            {
                fout_before << ir_module->getBasicBlockInfo() << endl;
                fout_before << irbuilder.getValueTableInEveryBlock() << endl;
            }
            fout_before.close();
        }
        else
        {
            pass_manager->setVerbose(true);
            pass_manager->runOnModule(ir_module.get());
            ofstream fout_after(after_ir_file);
            if (!fout_after)
            {
                cerr << "Cannot open output file: " << after_ir_file << endl;
                return 1;
            }
            fout_after << ir_module->toString() << endl;
            if (infoMode)
            {
                // 输出优化Pass调试信息
                fout_after << ir_module->getBasicBlockInfo() << endl;
                fout_after << irbuilder.getDebugOutput();
                fout_after << pass_manager->toString() << endl;
            }
            fout_after.close();
        }
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