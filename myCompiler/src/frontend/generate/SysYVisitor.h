
// Generated from SysY.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"
#include "SysYParser.h"



/**
 * This class defines an abstract visitor for a parse tree
 * produced by SysYParser.
 */
class  SysYVisitor : public antlr4::tree::AbstractParseTreeVisitor {
public:

  /**
   * Visit parse trees produced by SysYParser.
   */
    virtual std::any visitCompUnit(SysYParser::CompUnitContext *context) = 0;

    virtual std::any visitConstDeclaration(SysYParser::ConstDeclarationContext *context) = 0;

    virtual std::any visitVariableDeclaration(SysYParser::VariableDeclarationContext *context) = 0;

    virtual std::any visitConstDecl(SysYParser::ConstDeclContext *context) = 0;

    virtual std::any visitTypeTensor(SysYParser::TypeTensorContext *context) = 0;

    virtual std::any visitTypeInt(SysYParser::TypeIntContext *context) = 0;

    virtual std::any visitTypeFloat(SysYParser::TypeFloatContext *context) = 0;

    virtual std::any visitConstDef(SysYParser::ConstDefContext *context) = 0;

    virtual std::any visitConstInitExpr(SysYParser::ConstInitExprContext *context) = 0;

    virtual std::any visitConstInitList(SysYParser::ConstInitListContext *context) = 0;

    virtual std::any visitVarDecl(SysYParser::VarDeclContext *context) = 0;

    virtual std::any visitVarDef(SysYParser::VarDefContext *context) = 0;

    virtual std::any visitInitExpr(SysYParser::InitExprContext *context) = 0;

    virtual std::any visitInitList(SysYParser::InitListContext *context) = 0;

    virtual std::any visitFuncDef(SysYParser::FuncDefContext *context) = 0;

    virtual std::any visitTypeVoid(SysYParser::TypeVoidContext *context) = 0;

    virtual std::any visitTypeBType(SysYParser::TypeBTypeContext *context) = 0;

    virtual std::any visitFuncFParams(SysYParser::FuncFParamsContext *context) = 0;

    virtual std::any visitScalarParam(SysYParser::ScalarParamContext *context) = 0;

    virtual std::any visitArrayParamNoSize(SysYParser::ArrayParamNoSizeContext *context) = 0;

    virtual std::any visitArrayParamWithSize(SysYParser::ArrayParamWithSizeContext *context) = 0;

    virtual std::any visitBlock(SysYParser::BlockContext *context) = 0;

    virtual std::any visitItemDecl(SysYParser::ItemDeclContext *context) = 0;

    virtual std::any visitItemStmt(SysYParser::ItemStmtContext *context) = 0;

    virtual std::any visitAssignStmt(SysYParser::AssignStmtContext *context) = 0;

    virtual std::any visitExprStmt(SysYParser::ExprStmtContext *context) = 0;

    virtual std::any visitBlockStmt(SysYParser::BlockStmtContext *context) = 0;

    virtual std::any visitIfStmt(SysYParser::IfStmtContext *context) = 0;

    virtual std::any visitWhileStmt(SysYParser::WhileStmtContext *context) = 0;

    virtual std::any visitBreakStmt(SysYParser::BreakStmtContext *context) = 0;

    virtual std::any visitContinueStmt(SysYParser::ContinueStmtContext *context) = 0;

    virtual std::any visitReturnStmt(SysYParser::ReturnStmtContext *context) = 0;

    virtual std::any visitExp(SysYParser::ExpContext *context) = 0;

    virtual std::any visitCond(SysYParser::CondContext *context) = 0;

    virtual std::any visitLVal(SysYParser::LValContext *context) = 0;

    virtual std::any visitParenExp(SysYParser::ParenExpContext *context) = 0;

    virtual std::any visitLValExp(SysYParser::LValExpContext *context) = 0;

    virtual std::any visitNumberExp(SysYParser::NumberExpContext *context) = 0;

    virtual std::any visitStringLiteralExp(SysYParser::StringLiteralExpContext *context) = 0;

    virtual std::any visitIntNum(SysYParser::IntNumContext *context) = 0;

    virtual std::any visitFloatNum(SysYParser::FloatNumContext *context) = 0;

    virtual std::any visitToPrimaryExp(SysYParser::ToPrimaryExpContext *context) = 0;

    virtual std::any visitCallExp(SysYParser::CallExpContext *context) = 0;

    virtual std::any visitOpUnaryExp(SysYParser::OpUnaryExpContext *context) = 0;

    virtual std::any visitOpPlus(SysYParser::OpPlusContext *context) = 0;

    virtual std::any visitOpMinus(SysYParser::OpMinusContext *context) = 0;

    virtual std::any visitOpNot(SysYParser::OpNotContext *context) = 0;

    virtual std::any visitFuncRParams(SysYParser::FuncRParamsContext *context) = 0;

    virtual std::any visitMulDivModExp(SysYParser::MulDivModExpContext *context) = 0;

    virtual std::any visitToUnaryExp_mul(SysYParser::ToUnaryExp_mulContext *context) = 0;

    virtual std::any visitToMulExp_add(SysYParser::ToMulExp_addContext *context) = 0;

    virtual std::any visitAddSubExp(SysYParser::AddSubExpContext *context) = 0;

    virtual std::any visitRelOpExp(SysYParser::RelOpExpContext *context) = 0;

    virtual std::any visitToAddExp_rel(SysYParser::ToAddExp_relContext *context) = 0;

    virtual std::any visitToRelExp_eq(SysYParser::ToRelExp_eqContext *context) = 0;

    virtual std::any visitEqOpExp(SysYParser::EqOpExpContext *context) = 0;

    virtual std::any visitLandOpExp(SysYParser::LandOpExpContext *context) = 0;

    virtual std::any visitToEqExp_land(SysYParser::ToEqExp_landContext *context) = 0;

    virtual std::any visitLorOpExp(SysYParser::LorOpExpContext *context) = 0;

    virtual std::any visitToLAndExp_lor(SysYParser::ToLAndExp_lorContext *context) = 0;

    virtual std::any visitConstExp(SysYParser::ConstExpContext *context) = 0;


};

