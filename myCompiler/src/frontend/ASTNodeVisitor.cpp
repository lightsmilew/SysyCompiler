#include "ASTNodeVisitor.h"
#include "Common.h"
#include <memory>
#include <string>
#include <typeinfo>


using namespace ast;
//字符串类型名转化为基础数据类型的枚举名
PrimaryDataType convertToPrimaryDataType(const std::string &typeStr) {
    if (typeStr == "int") {
        return PrimaryDataType::INT;
    } else if (typeStr == "float") {
        return PrimaryDataType::FLOAT;
    } else if (typeStr == "void") {
        return PrimaryDataType::VOID;
    } else {
        throw std::invalid_argument("Unknown type: " + typeStr);
    }
}
[[nodiscard]]Ptr<ast::CompUnitNode> ASTNodeVisitor::compileUnit(){
    return compUnit;
}
antlrcpp::Any ASTNodeVisitor::visitCompUnit(SysYParser::CompUnitContext *const ctx){
// 符号表
    std::vector<Ptr<ast::ASTNode>> children;
   //访问每一个变量声明
    for (auto declCtx : ctx->decl()) {
        auto any = declCtx->accept(this);
        try {
            auto decls = AS(any, std::vector<Ptr<ast::DeclStmtNode>>);
            for (auto d : decls) {
                children.emplace_back(d);
            }
        } catch (const std::bad_cast &e) {
            std::cerr << "Bad cast: " << e.what() << std::endl;
            std::cerr << "Actual type: " << typeid(any).name() << std::endl;
        }
    }
    //访问每一个函数定义
    for (auto funcDefCtx : ctx->funcDef()) {
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
antlrcpp::Any ASTNodeVisitor::visitConstDeclaration(SysYParser::ConstDeclarationContext *const ctx){
  return visit(ctx->constDecl()); // 直接访问 constDecl
}
antlrcpp::Any ASTNodeVisitor::visitVariableDeclaration(SysYParser::VariableDeclarationContext *const ctx){
  return visit(ctx->varDecl()); // 直接访问 varDecl
}
antlrcpp::Any ASTNodeVisitor::visitConstDecl(SysYParser::ConstDeclContext *const ctx) {
    Vector<Ptr<ast::DeclStmtNode>> decls;
    for(auto defctx:ctx->constDef()) {
        String identifier = defctx->ident()->getText();
        //可能为多维数组或者一般变量
        Vector<Ptr<ast::ExprNode>>arrayIndices;
        for(auto expctx:defctx->constExp()) {
             arrayIndices.emplace_back(AS(expctx->accept(this), Ptr<ast::ExprNode>));  
        }
    //这里实现部分功能
         DataType type = convertToPrimaryDataType(ctx->bType()->getText());
         type._isConst=true; // 设置为常量类型
         //解析初始化
         auto initVal = defctx->constInitVal()->accept(this);
         Ptr<ast::InitExprNode> initExprPtr;
         if(initVal.IS(Vector<Ptr<ast::InitExprNode>>)){
            //构造指针
            Vector<Ptr<ast::InitExprNode>> initVals = AS(initVal, Vector<Ptr<ast::InitExprNode>>);
            initExprPtr=makePtr<ast::InitExprNode>(initVals);
         }
         else{
            Ptr<ast::ExprNode> singleInitVal = AS(initVal, Ptr<ast::InitExprNode>);
            initExprPtr = makePtr<ast::InitExprNode>(singleInitVal);
         }
         auto declptr = makePtr<ast::DeclStmtNode>(type, identifier, initExprPtr, true, false);
         declptr->indices = std::move(arrayIndices); // 设置数组下标
         decls.emplace_back(declptr);
    }
    return decls;
}
//单一表达式
antlrcpp::Any ASTNodeVisitor::visitConstInitExpr(SysYParser::ConstInitExprContext *const ctx){
  auto constExpPtr = AS(ctx->constExp()->accept(this), Ptr<ast::ExprNode>);
  // 返回一个 InitExprNode，表示单一的初始化表达式
  return makePtr<ast::InitExprNode>(constExpPtr);
}
//多维数组初始化列表
antlrcpp::Any ASTNodeVisitor::visitConstInitList(SysYParser::ConstInitListContext *const ctx){
   Vector<Ptr<ast::InitExprNode>> initVals;
    for (auto initValCtx : ctx->constInitVal()) {
        // 访问每个 ConstInitValContext，获取初始化值
        auto initVal = visit(initValCtx);
        if (initVal.IS(Ptr<ast::InitExprNode>)) {
            initVals.emplace_back(AS(initVal, Ptr<ast::InitExprNode>));
        } else if (initVal.IS(Vector<Ptr<ast::InitExprNode>>)) {
            //多维数组嵌套，创建新的InitExprNode封装
            auto vals = AS(initVal, Vector<Ptr<ast::InitExprNode>>);
            initVals.emplace_back(makePtr<ast::InitExprNode>(vals));
        }
    }
    return initVals; // 返回一个包含所有初始化值的向量
}