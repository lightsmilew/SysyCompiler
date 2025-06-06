#ifndef SYSY_ASTNODEVISITOR_H
#define SYSY_ASTNODEVISITOR_H

#include "SysYBaseVisitor.h"
#include "SysYParser.h"
#include "ASTNode.h"
#include "Common.h"
class ASTNodeVisitor : public SysYBaseVisitor
{
public:
    [[nodiscard]] Ptr<ast::CompUnitNode> compileUnit(); // 返回编译单元根节点

    // 按g4文法顺序定义访问函数
    //  编译单元开始符号
    virtual antlrcpp::Any visitCompUnit(SysYParser::CompUnitContext *ctx) override;
    // 声明 decl
    virtual antlrcpp::Any visitConstDeclaration(SysYParser::ConstDeclarationContext *ctx) override;
    virtual antlrcpp::Any visitVariableDeclaration(SysYParser::VariableDeclarationContext *ctx) override;
    // 常量声明 constDecl
    virtual antlrcpp::Any visitConstDecl(SysYParser::ConstDeclContext *ctx) override;
    // bType
    // virtual antlrcpp::Any visitTypeInt(SysYParser::TypeIntContext *ctx) override;
    // virtual antlrcpp::Any visitTypeFloat(SysYParser::TypeFloatContext *ctx) override;
    // constDef
    // virtual antlrcpp::Any visitConstDef(SysYParser::ConstDefContext *ctx) override;
    // constInitVal
    virtual antlrcpp::Any visitConstInitExpr(SysYParser::ConstInitExprContext *ctx) override;
    virtual antlrcpp::Any visitConstInitList(SysYParser::ConstInitListContext *ctx) override;
    // 变量声明 varDecl
    virtual antlrcpp::Any visitVarDecl(SysYParser::VarDeclContext *ctx) override;
    // varDef
    virtual antlrcpp::Any visitVarDef(SysYParser::VarDefContext *ctx) override;
    // initVal
    virtual antlrcpp::Any visitInitExpr(SysYParser::InitExprContext *ctx) override;
    virtual antlrcpp::Any visitInitList(SysYParser::InitListContext *ctx) override;
    // 函数定义 funcDef
    virtual antlrcpp::Any visitFuncDef(SysYParser::FuncDefContext *ctx) override;
    // funcType
    virtual antlrcpp::Any visitTypeVoid(SysYParser::TypeVoidContext *ctx) override;
    virtual antlrcpp::Any visitTypeBType(SysYParser::TypeBTypeContext *ctx) override;
    // funcFParams
    virtual antlrcpp::Any visitFuncFParams(SysYParser::FuncFParamsContext *ctx) override;
    // funcFParam
    virtual antlrcpp::Any visitScalarParam(SysYParser::ScalarParamContext *ctx) override;
    //arrayParamNoSize
    virtual antlrcpp::Any visitArrayParamNoSize(SysYParser::ArrayParamNoSizeContext *ctx) override;
    //arrayParamWithSize
    virtual antlrcpp::Any visitArrayParamWithSize(SysYParser::ArrayParamWithSizeContext *ctx) override;
    // 语句块 block
    virtual antlrcpp::Any visitBlock(SysYParser::BlockContext *ctx) override;
    // blockItem
    virtual antlrcpp::Any visitItemDecl(SysYParser::ItemDeclContext *ctx) override;
    virtual antlrcpp::Any visitItemStmt(SysYParser::ItemStmtContext *ctx) override;
    // 语句 stmt
    virtual antlrcpp::Any visitAssignStmt(SysYParser::AssignStmtContext *ctx) override;
    virtual antlrcpp::Any visitExprStmt(SysYParser::ExprStmtContext *ctx) override;
    virtual antlrcpp::Any visitBlockStmt(SysYParser::BlockStmtContext *ctx) override;
    virtual antlrcpp::Any visitIfStmt(SysYParser::IfStmtContext *ctx) override;
    virtual antlrcpp::Any visitWhileStmt(SysYParser::WhileStmtContext *ctx) override;
    virtual antlrcpp::Any visitBreakStmt(SysYParser::BreakStmtContext *ctx) override;
    virtual antlrcpp::Any visitContinueStmt(SysYParser::ContinueStmtContext *ctx) override;
    virtual antlrcpp::Any visitReturnStmt(SysYParser::ReturnStmtContext *ctx) override;
    // 表达式 exp
    virtual antlrcpp::Any visitExp(SysYParser::ExpContext *ctx) override;
    virtual antlrcpp::Any visitCond(SysYParser::CondContext *ctx) override;
    virtual antlrcpp::Any visitLVal(SysYParser::LValContext *ctx) override;
    // primaryExp
    virtual antlrcpp::Any visitParenExp(SysYParser::ParenExpContext *ctx) override;
    virtual antlrcpp::Any visitLValExp(SysYParser::LValExpContext *ctx) override;
    virtual antlrcpp::Any visitNumberExp(SysYParser::NumberExpContext *ctx) override;
    // number
    virtual antlrcpp::Any visitIntNum(SysYParser::IntNumContext *ctx) override;
    virtual antlrcpp::Any visitFloatNum(SysYParser::FloatNumContext *ctx) override;
    // unaryExp
    virtual antlrcpp::Any visitToPrimaryExp(SysYParser::ToPrimaryExpContext *ctx) override;
    virtual antlrcpp::Any visitCallExp(SysYParser::CallExpContext *ctx) override;
    virtual antlrcpp::Any visitOpUnaryExp(SysYParser::OpUnaryExpContext *ctx) override;
    // unaryOp
    virtual antlrcpp::Any visitOpPlus(SysYParser::OpPlusContext *ctx) override;
    virtual antlrcpp::Any visitOpMinus(SysYParser::OpMinusContext *ctx) override;
    virtual antlrcpp::Any visitOpNot(SysYParser::OpNotContext *ctx) override;
    // funcRParams
    virtual antlrcpp::Any visitFuncRParams(SysYParser::FuncRParamsContext *ctx) override;
    // mulExp
    virtual antlrcpp::Any visitToUnaryExp_mul(SysYParser::ToUnaryExp_mulContext *ctx) override;
    virtual antlrcpp::Any visitMulDivModExp(SysYParser::MulDivModExpContext *ctx) override;
    // addExp
    virtual antlrcpp::Any visitToMulExp_add(SysYParser::ToMulExp_addContext *ctx) override;
    virtual antlrcpp::Any visitAddSubExp(SysYParser::AddSubExpContext *ctx) override;
    // relExp
    virtual antlrcpp::Any visitToAddExp_rel(SysYParser::ToAddExp_relContext *ctx) override;
    virtual antlrcpp::Any visitRelOpExp(SysYParser::RelOpExpContext *ctx) override;
    // eqExp
    virtual antlrcpp::Any visitToRelExp_eq(SysYParser::ToRelExp_eqContext *ctx) override;
    virtual antlrcpp::Any visitEqOpExp(SysYParser::EqOpExpContext *ctx) override;
    // lAndExp
    virtual antlrcpp::Any visitToLAndExp_lor(SysYParser::ToLAndExp_lorContext *ctx) override;
    virtual antlrcpp::Any visitLandOpExp(SysYParser::LandOpExpContext *ctx) override;
    // lOrExp
    virtual antlrcpp::Any visitToLAndExp_lor(SysYParser::ToLAndExp_lorContext *ctx) override;
    virtual antlrcpp::Any visitLorOpExp(SysYParser::LorOpExpContext *ctx) override;
    virtual antlrcpp::Any visitConstExp(SysYParser::ConstExpContext *ctx) override;
    virtual antlrcpp::Any visitIdent(SysYParser::IdentContext *ctx) override;
    virtual antlrcpp::Any visitIntConst(SysYParser::IntConstContext *ctx) override;
    virtual antlrcpp::Any visitFloatConst(SysYParser::FloatConstContext *ctx) override;

    // 处理符号表
    virtual int handleFunctionDef(Ptr<ast::FuncNode> func);

    ASTNodeVisitor() = default;

    // 报错函数
    void error(std::string str)
    {
        std::cerr << "[Error] " << str << std::endl;
    }

private:
    Ptr<ast::CompUnitNode> compUnit; // 整个编译单元的根节点
};

#endif // SYSY_ASTNODEVISITOR_H