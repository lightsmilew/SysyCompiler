#include "ASTNodeVisitor.h"
#include "Common.h"
#include <memory>
#include <string>
#include <typeinfo>

using namespace ast;

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
    // addExp
    antlrcpp::Any ASTNodeVisitor::visitToMulExp_add(SysYParser::ToMulExp_addContext * ctx)
    {
        return visit(ctx->mulExp());
    }
    antlrcpp::Any ASTNodeVisitor::visitAddSubExp(SysYParser::AddSubExpContext * ctx)
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
    antlrcpp::Any ASTNodeVisitor::visitToAddExp_rel(SysYParser::ToAddExp_relContext * ctx)
    {
        return visit(ctx->addExp());
    }
    antlrcpp::Any ASTNodeVisitor::visitRelOpExp(SysYParser::RelOpExpContext * ctx)
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
    antlrcpp::Any ASTNodeVisitor::visitToRelExp_eq(SysYParser::ToRelExp_eqContext * ctx)
    {
        return visit(ctx->relExp());
    }
    antlrcpp::Any ASTNodeVisitor::visitEqOpExp(SysYParser::EqOpExpContext * ctx)
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
    antlrcpp::Any ASTNodeVisitor::visitToLAndExp_lor(SysYParser::ToLAndExp_lorContext * ctx)
    {
        return visit(ctx->lAndExp());
    }
    antlrcpp::Any ASTNodeVisitor::visitLandOpExp(SysYParser::LandOpExpContext * ctx)
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
    antlrcpp::Any ASTNodeVisitor::visitToLAndExp_lor(SysYParser::ToLAndExp_lorContext * ctx)
    {
        return visit(ctx->lAndExp());
    }
    antlrcpp::Any ASTNodeVisitor::visitLorOpExp(SysYParser::LorOpExpContext * ctx)
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