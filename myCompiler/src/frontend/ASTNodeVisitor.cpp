#include "ASTNodeVisitor.h"
#include "Common.h"
#include <memory>
#include <string>
#include <typeinfo>

using namespace ast;
// 字符串类型名转化为基础数据类型的枚举名
PrimaryDataType convertToPrimaryDataType(const std::string &typeStr)
{
    if (typeStr == "int")
    {
        return PrimaryDataType::INT;
    }
    else if (typeStr == "float")
    {
        return PrimaryDataType::FLOAT;
    }
    else if (typeStr == "void")
    {
        return PrimaryDataType::VOID;
    }
    else
    {
        throw std::invalid_argument("Unknown type: " + typeStr);
    }
}
[[nodiscard]] Ptr<ast::CompUnitNode> ASTNodeVisitor::compileUnit()
{
    return compUnit;
}
antlrcpp::Any ASTNodeVisitor::visitCompUnit(SysYParser::CompUnitContext *const ctx)
{
    // 符号表
    std::vector<Ptr<ast::ASTNode>> children;
    // 访问每一个变量声明
    for (auto declCtx : ctx->decl())
    {
        auto any = declCtx->accept(this);
        try
        {
            auto decls = AS(any, std::vector<Ptr<ast::DeclStmtNode>>);
            for (auto d : decls)
            {
                children.emplace_back(d);
            }
        }
        catch (const std::bad_cast &e)
        {
            std::cerr << "Bad cast: " << e.what() << std::endl;
            std::cerr << "Actual type: " << typeid(any).name() << std::endl;
        }
    }
    // 访问每一个函数定义
    for (auto funcDefCtx : ctx->funcDef())
    {
        // 这里进入的是func的作用域，在visitFuncDef中会进入新的作用域
        auto func = AS(funcDefCtx->accept(this), Ptr<ast::FuncNode>);

        // 将函数添加到该作用域，函数的形参也添加进去
        handleFunctionDef(func);
        // 里面的block是新的作用域，main作用域的子作用域指向func作用域，func作用域的父作用域指向main作用域
        children.emplace_back(func);
    }

    // 构建 CompUnit 节点
    auto compUnitNode = std::make_shared<ast::CompUnitNode>(std::move(children));
    return compUnitNode;
}
antlrcpp::Any ASTNodeVisitor::visitConstDeclaration(SysYParser::ConstDeclarationContext *const ctx)
{
    return visit(ctx->constDecl()); // 直接访问 constDecl
}
antlrcpp::Any ASTNodeVisitor::visitVariableDeclaration(SysYParser::VariableDeclarationContext *const ctx)
{
    return visit(ctx->varDecl()); // 直接访问 varDecl
}
antlrcpp::Any ASTNodeVisitor::visitConstDecl(SysYParser::ConstDeclContext *const ctx)
{
    Vector<Ptr<ast::DeclStmtNode>> decls;
    for (auto defctx : ctx->constDef())
    {
        String identifier = defctx->ident()->getText();
        // 可能为多维数组或者一般变量
        Vector<Ptr<ast::ExprNode>> arrayIndices;
        for (auto expctx : defctx->constExp())
        {
            arrayIndices.emplace_back(AS(expctx->accept(this), Ptr<ast::ExprNode>));
        }
        // 这里实现部分功能
        DataType type = convertToPrimaryDataType(ctx->bType()->getText());
        type._isConst = true; // 设置为常量类型
        // 解析初始化
        auto initVal = defctx->constInitVal()->accept(this);
        Ptr<ast::InitExprNode> initExprPtr;
        if (initVal.IS(Vector<Ptr<ast::InitExprNode>>))
        {
            // 构造指针
            Vector<Ptr<ast::InitExprNode>> initVals = AS(initVal, Vector<Ptr<ast::InitExprNode>>);
            initExprPtr = makePtr<ast::InitExprNode>(initVals);
        }
        else
        {
            Ptr<ast::ExprNode> singleInitVal = AS(initVal, Ptr<ast::InitExprNode>);
            initExprPtr = makePtr<ast::InitExprNode>(singleInitVal);
        }
        auto declptr = makePtr<ast::DeclStmtNode>(type, identifier, initExprPtr, true, false);
        declptr->indices = std::move(arrayIndices); // 设置数组下标
        decls.emplace_back(declptr);
    }
    return decls;
}
// 单一表达式
antlrcpp::Any ASTNodeVisitor::visitConstInitExpr(SysYParser::ConstInitExprContext *const ctx)
{
    auto constExpPtr = AS(ctx->constExp()->accept(this), Ptr<ast::ExprNode>);
    // 返回一个 InitExprNode，表示单一的初始化表达式
    return makePtr<ast::InitExprNode>(constExpPtr);
}
// 多维数组初始化列表
antlrcpp::Any ASTNodeVisitor::visitConstInitList(SysYParser::ConstInitListContext *const ctx)
{
    Vector<Ptr<ast::InitExprNode>> initVals;
    for (auto initValCtx : ctx->constInitVal())
    {
        // 访问每个 ConstInitValContext，获取初始化值
        auto initVal = visit(initValCtx);
        if (initVal.IS(Ptr<ast::InitExprNode>))
        {
            initVals.emplace_back(AS(initVal, Ptr<ast::InitExprNode>));
        }
        else if (initVal.IS(Vector<Ptr<ast::InitExprNode>>))
        {
            // 多维数组嵌套，创建新的InitExprNode封装
            auto vals = AS(initVal, Vector<Ptr<ast::InitExprNode>>);
            initVals.emplace_back(makePtr<ast::InitExprNode>(vals));
        }
    }
    return initVals; // 返回一个包含所有初始化值的向量
}

// 表达式 exp
antlrcpp::Any ASTNodeVisitor::visitExp(SysYParser::ExpContext *ctx)
{
    return visit(ctx->addExp());
}
antlrcpp::Any ASTNodeVisitor::visitCond(SysYParser::CondContext *ctx)
{
    return visit(ctx->lOrExp());
}
antlrcpp::Any ASTNodeVisitor::visitLVal(SysYParser::LValContext *ctx)
{
    string identifier = ctx->ident()->getText();
    vector<shared_ptr<ExprNode>> exps;
    for (auto idx : ctx->exp())
    {
        // 使用 any_cast 进行类型转换
        auto result = visit(idx);
        exps.push_back(std::any_cast<shared_ptr<ExprNode>>(result));
    }
    return make_shared<LValueExprNode>(identifier, exps);
}
// primaryExp
antlrcpp::Any ASTNodeVisitor::visitParenExp(SysYParser::ParenExpContext *ctx)
{
    return visit(ctx->exp());
}
antlrcpp::Any ASTNodeVisitor::visitLValExp(SysYParser::LValExpContext *ctx)
{
    auto lval = std::any_cast<shared_ptr<LValueExprNode>>(visit(ctx->lVal()));
    return static_cast<shared_ptr<ExprNode>>(lval);
}
antlrcpp::Any ASTNodeVisitor::visitNumberExp(SysYParser::NumberExpContext *ctx)
{
    auto number = std::any_cast<shared_ptr<NumberLiteralExprNode>>(visit(ctx->number()));
    return static_cast<shared_ptr<ExprNode>>(number);
}
// number
antlrcpp::Any ASTNodeVisitor::visitIntNum(SysYParser::IntNumContext *ctx)
{
    auto intValue = std::stoi(ctx->getText());
    if (intValue.find("0x") == 0 || intValue.find("0X") == 0)
    {
        // 十六进制
        intValue = std::stoi(ctx->getText(), nullptr, 16);
    }
    else if (intValue.find("0") == 0 && intValue.size() > 1)
    {
        // 八进制
        intValue = std::stoi(ctx->getText(), nullptr, 8);
    }
    else
    {
        // 十进制
        intValue = std::stoi(ctx->getText(), nullptr, 10);
    }
    return static_cast<shared_ptr<NumberLiteralExprNode>>(make_shared<IntLiteralExprNode>(intValue));
}
antlrcpp::Any ASTNodeVisitor::visitFloatNum(SysYParser::FloatNumContext *ctx)
{
    auto floatValue = std::stof(ctx->getText());
    return static_cast<shared_ptr<NumberLiteralExprNode>>(make_shared<FloatLiteralExprNode>(floatValue));
}
// unaryExp
antlrcpp::Any ASTNodeVisitor::visitToPrimaryExp(SysYParser::ToPrimaryExpContext *ctx)
{
    return visit(ctx->primaryExp());
}
antlrcpp::Any ASTNodeVisitor::visitCallExp(SysYParser::CallExpContext *ctx)
{
    string callee = ctx->ident()->getText();
    vector<shared_ptr<ExprNode>> args;
    if (ctx->funcRParams())
    {
        auto params = std::any_cast<vector<shared_ptr<ExprNode>>>(visit(ctx->funcRParams()));
        args.insert(args.end(), params.begin(), params.end());
    }
    return static_cast<shared_ptr<ExprNode>>(make_shared<CallExprNode>(callee, args));
}
antlrcpp::Any ASTNodeVisitor::visitOpUnaryExp(SysYParser::OpUnaryExpContext *ctx)
{
    // 处理一元操作符
    // operator是字符串
    string op = ctx->unaryOp()->getText();
    UnaryOp unaryOp;
    if (op == "+")
    {
        unaryOp = UnaryOp::Add;
    }
    else if (op == "-")
    {
        unaryOp = UnaryOp::Sub;
    }
    else if (op == "!")
    {
        unaryOp = UnaryOp::Not;
    }
    else
    {
        throw std::invalid_argument("Unknown unary operator: " + op);
    }

    auto exp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->unaryExp()));

    return static_cast<shared_ptr<ExprNode>>(make_shared<UnaryExprNode>(unaryOp, exp));
}
// unaryOp
antlrcpp::Any ASTNodeVisitor::visitOpPlus(SysYParser::OpPlusContext *ctx)
{
    return UnaryOp::Add; // 返回一元加操作符
}
antlrcpp::Any ASTNodeVisitor::visitOpMinus(SysYParser::OpMinusContext *ctx)
{
    return UnaryOp::Sub; // 返回一元减操作符
}
antlrcpp::Any ASTNodeVisitor::visitOpNot(SysYParser::OpNotContext *ctx)
{
    return UnaryOp::Not; // 返回逻辑非操作符
}
// funcRParams
antlrcpp::Any ASTNodeVisitor::visitFuncRParams(SysYParser::FuncRParamsContext *ctx)
{
    vector<shared_ptr<ExprNode>> params;
    for (auto expCtx : ctx->exp())
    {
        auto param = std::any_cast<shared_ptr<ExprNode>>(visit(expCtx));
        params.push_back(param);
    }
    return params; // 返回参数列表
    // mulExp
    antlrcpp::Any ASTNodeVisitor::visitToUnaryExp_mul(SysYParser::ToUnaryExp_mulContext * ctx)
    {
        return visit(ctx->unaryExp());
    }
    antlrcpp::Any ASTNodeVisitor::visitMulDivModExp(SysYParser::MulDivModExpContext * ctx)
    {
        auto mulExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->mulExp()));
        auto unaryExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->unaryExp()));
        string op = ctx->children[1]->getText(); // 获取操作符
        BinaryOp binaryOp;
        if (op == "*")
        {
            binaryOp = BinaryOp::Mul;
        }
        else if (op == "/")
        {
            binaryOp = BinaryOp::Div;
        }
        else if (op == "%")
        {
            binaryOp = BinaryOp::Mod;
        }
        else
        {
            throw std::invalid_argument("Unknown binary operator: " + op);
        }
        return static_cast<shared_ptr<ExprNode>>(make_shared<BinaryExprNode>(mulExp, unaryExp, binaryOp));
    }
};
// addExp
antlrcpp::Any ASTNodeVisitor::visitToMulExp_add(SysYParser::ToMulExp_addContext *ctx)
{
    return visit(ctx->mulExp());
}
antlrcpp::Any ASTNodeVisitor::visitAddSubExp(SysYParser::AddSubExpContext *ctx)
{
    auto addExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->addExp()));
    auto mulExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->mulExp()));
    string op = ctx->children[1]->getText(); // 获取操作符
    BinaryOp binaryOp;
    if (op == "+")
    {
        binaryOp = BinaryOp::Add;
    }
    else if (op == "-")
    {
        binaryOp = BinaryOp::Sub;
    }
    else
    {
        throw std::invalid_argument("Unknown binary operator: " + op);
    }
    return static_cast<shared_ptr<ExprNode>>(make_shared<BinaryExprNode>(addExp, mulExp, binaryOp));
}
// relExp
antlrcpp::Any ASTNodeVisitor::visitToAddExp_rel(SysYParser::ToAddExp_relContext *ctx)
{
    return visit(ctx->addExp());
}
antlrcpp::Any ASTNodeVisitor::visitRelOpExp(SysYParser::RelOpExpContext *ctx)
{
    auto relExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->relExp()));
    auto addExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->addExp()));
    string op = ctx->children[1]->getText(); // 获取操作符
    BinaryOp binaryOp;
    if (op == "<")
    {
        binaryOp = BinaryOp::Lt;
    }
    else if (op == ">")
    {
        binaryOp = BinaryOp::Gt;
    }
    else if (op == "<=")
    {
        binaryOp = BinaryOp::Le;
    }
    else if (op == ">=")
    {
        binaryOp = BinaryOp::Ge;
    }
    else
    {
        throw std::invalid_argument("Unknown relational operator: " + op);
    }
    return static_cast<shared_ptr<ExprNode>>(make_shared<BinaryExprNode>(relExp, addExp, binaryOp));
}
// eqExp
antlrcpp::Any ASTNodeVisitor::visitToRelExp_eq(SysYParser::ToRelExp_eqContext *ctx)
{
    return visit(ctx->relExp());
}
antlrcpp::Any ASTNodeVisitor::visitEqOpExp(SysYParser::EqOpExpContext *ctx)
{
    auto eqExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->eqExp()));
    auto relExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->relExp()));
    string op = ctx->children[1]->getText(); // 获取操作符
    BinaryOp binaryOp;
    if (op == "==")
    {
        binaryOp = BinaryOp::Eq;
    }
    else if (op == "!=")
    {
        binaryOp = BinaryOp::Ne;
    }
    else
    {
        throw std::invalid_argument("Unknown equality operator: " + op);
    }
    return static_cast<shared_ptr<ExprNode>>(make_shared<BinaryExprNode>(eqExp, relExp, binaryOp));
}
// lAndExp
antlrcpp::Any ASTNodeVisitor::visitToLAndExp_lor(SysYParser::ToLAndExp_lorContext *ctx)
{
    return visit(ctx->lAndExp());
}
antlrcpp::Any ASTNodeVisitor::visitLandOpExp(SysYParser::LandOpExpContext *ctx)
{
    auto lAndExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->lAndExp()));
    auto eqExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->eqExp()));
    string op = ctx->children[1]->getText(); // 获取操作符
    if (op != "&&")
    {
        throw std::invalid_argument("Unknown logical operator: " + op);
    }
    return static_cast<shared_ptr<ExprNode>>(make_shared<BinaryExprNode>(lAndExp, eqExp, BinaryOp::And));
}
// lOrExp
antlrcpp::Any ASTNodeVisitor::visitToLAndExp_lor(SysYParser::ToLAndExp_lorContext *ctx)
{
    return visit(ctx->lAndExp());
}
antlrcpp::Any ASTNodeVisitor::visitLorOpExp(SysYParser::LorOpExpContext *ctx)
{
    auto lOrExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->lOrExp()));
    auto lAndExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->lAndExp()));
    string op = ctx->children[1]->getText(); // 获取操作符
    if (op != "||")
    {
        throw std::invalid_argument("Unknown logical operator: " + op);
    }
    return static_cast<shared_ptr<ExprNode>>(make_shared<BinaryExprNode>(lOrExp, lAndExp, BinaryOp::Or));
}