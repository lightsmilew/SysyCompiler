
// Generated from ./myCompiler/src/SysY.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"
#include "SysYVisitor.h"


/**
 * This class provides an empty implementation of SysYVisitor, which can be
 * extended to create a visitor which only needs to handle a subset of the available methods.
 */
class  SysYBaseVisitor : public SysYVisitor {
public:

  virtual std::any visitCompUnit(SysYParser::CompUnitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstDeclaration(SysYParser::ConstDeclarationContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitVariableDeclaration(SysYParser::VariableDeclarationContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstDecl(SysYParser::ConstDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeInt(SysYParser::TypeIntContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeFloat(SysYParser::TypeFloatContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstDef(SysYParser::ConstDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstInitExpr(SysYParser::ConstInitExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstInitList(SysYParser::ConstInitListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitVarDecl(SysYParser::VarDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitVarDef(SysYParser::VarDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitInitExpr(SysYParser::InitExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitInitList(SysYParser::InitListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFuncDef(SysYParser::FuncDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeVoid(SysYParser::TypeVoidContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeBType(SysYParser::TypeBTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFuncFParams(SysYParser::FuncFParamsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitScalarParam(SysYParser::ScalarParamContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitArrayParam(SysYParser::ArrayParamContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBlock(SysYParser::BlockContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitItemDecl(SysYParser::ItemDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitItemStmt(SysYParser::ItemStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAssignStmt(SysYParser::AssignStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExprStmt(SysYParser::ExprStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBlockStmt(SysYParser::BlockStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIfStmt(SysYParser::IfStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWhileStmt(SysYParser::WhileStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBreakStmt(SysYParser::BreakStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitContinueStmt(SysYParser::ContinueStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitReturnStmt(SysYParser::ReturnStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExp(SysYParser::ExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCond(SysYParser::CondContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLVal(SysYParser::LValContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParenExp(SysYParser::ParenExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLValExp(SysYParser::LValExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNumberExp(SysYParser::NumberExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIntNum(SysYParser::IntNumContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFloatNum(SysYParser::FloatNumContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitToPrimaryExp(SysYParser::ToPrimaryExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCallExp(SysYParser::CallExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitOpUnaryExp(SysYParser::OpUnaryExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitOpPlus(SysYParser::OpPlusContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitOpMinus(SysYParser::OpMinusContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitOpNot(SysYParser::OpNotContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFuncRParams(SysYParser::FuncRParamsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMulDivModExp(SysYParser::MulDivModExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitToUnaryExp_mul(SysYParser::ToUnaryExp_mulContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitToMulExp_add(SysYParser::ToMulExp_addContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAddSubExp(SysYParser::AddSubExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRelOpExp(SysYParser::RelOpExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitToAddExp_rel(SysYParser::ToAddExp_relContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitToRelExp_eq(SysYParser::ToRelExp_eqContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEqOpExp(SysYParser::EqOpExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLandOpExp(SysYParser::LandOpExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitToEqExp_land(SysYParser::ToEqExp_landContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLorOpExp(SysYParser::LorOpExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitToLAndExp_lor(SysYParser::ToLAndExp_lorContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstExp(SysYParser::ConstExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIdent(SysYParser::IdentContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIntConst(SysYParser::IntConstContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFloatConst(SysYParser::FloatConstContext *ctx) override {
    return visitChildren(ctx);
  }


};

