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
antlrcpp::Any ASTNodeVisitor::visitVarDecl(SysYParser::VarDeclContext *ctx) {
    //获取变量的类型
    PrimaryDataType type = convertToPrimaryDataType(ctx->bType()->getText());
    //存储所有变量的声明
    auto decls=Vector<Ptr<ast::DeclStmtNode>>();
    for(auto varDefs:ctx->varDef()){
        String identifier;
        Vector<Ptr<ast::ExprNode>> arrayIndices;
        Ptr<ast::InitExprNode> initExprPtr = nullptr; // 初始化为 nullptr
        //如果有初始化列表
    if(varDefs->initVal()){
        identifier = varDefs->ident()->getText();
        //每一维的大小
        for(auto constExpCtx:varDefs->constExp()){
            arrayIndices.emplace_back(AS(visit(constExpCtx), Ptr<ast::ExprNode>));
        }
        auto initVal=varDefs->initVal()->accept(this);
        if(initVal.IS(Ptr<ast::InitExprNode>)){
            initExprPtr = AS(initVal, Ptr<ast::InitExprNode>);
        } else if(initVal.IS(Vector<Ptr<ast::InitExprNode>>)){
            //多维数组的初始化
            auto initVals = AS(initVal, Vector<Ptr<ast::InitExprNode>>);
            initExprPtr = makePtr<ast::InitExprNode>(initVals);
        }
    }else{
        identifier = varDefs->ident()->getText();
        for(auto constExpCtx:varDefs->constExp()){
            arrayIndices.emplace_back(AS(visit(constExpCtx), Ptr<ast::ExprNode>));
        }
        initExprPtr=NULL;
    }
    DataType varType(PrimaryDataType);

    //创建VarDecl 最后两个参数默认false
    auto decl= makePtr<ast::DeclStmtNode>(varType, identifier, initExprPtr);
    decl->indices=arrayIndices;
    decls.emplace_back(decl);
    }
    return decls; // 返回所有变量声明的向量
 }
 antlrcpp::Any ASTNodeVisitor::visitInitExpr(SysYParser::InitExprContext *ctx) {
    auto initVal=AS(ctx->exp()->accept(this), Ptr<ast::ExprNode>);
    // 返回一个 InitExprNode，表示单一的初始化表达式
    return makePtr<ast::InitExprNode>(initVal);
 }
antlrcpp::Any ASTNodeVisitor::visitInitList(SysYParser::InitListContext *ctx) {
    Vector<Ptr<ast::InitExprNode>> initVals;
    for (auto initValCtx : ctx->initVal()) {
        // 访问每个 InitValContext，获取初始化值
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
antlrcpp::Any ASTNodeVisitor::visitFuncDef(SysYParser::FuncDefContext *ctx) {
    PrimaryDataType funcType=convertToPrimaryDataType(ctx->funcType()->getText());
    DataType returnType(funcType);
    // 获取函数名
    String funcName = ctx->ident()->getText();
    // 获取函数参数列表
    Vector<Ptr<ast::DeclStmtNode>> params;
    for (auto paramCtx : ctx->funcFParams()->funcFParam()) {
        auto param = AS(paramCtx->accept(this), Ptr<ast::DeclStmtNode>);
        params.emplace_back(param);
    }
    auto bodyptr=AS(visit(ctx->block()), Ptr<ast::BlockStmtNode>);
    // 创建函数节点
    auto funcNode = makePtr<ast::FuncNode>(returnType,funcName, params, bodyptr);
    return funcNode; // 返回函数节点
}
antlrcpp::Any ASTNodeVisitor::visitTypeVoid(SysYParser::TypeVoidContext *ctx) {
    return PrimaryDataType::VOID; // 返回 PrimaryDataType::VOID
}
antlrcpp::Any ASTNodeVisitor::visitTypeBType(SysYParser::TypeBTypeContext *ctx) {
    return PrimaryDataType::INT; // 返回 PrimaryDataType::INT
}
//形参列表
antlrcpp::Any ASTNodeVisitor::visitFuncFParams(SysYParser::FuncFParamsContext *ctx) {
    Vector<Ptr<ast::DeclStmtNode>> params;
    for (auto paramCtx : ctx->funcFParam()) {
        auto param = AS(paramCtx->accept(this), Ptr<ast::DeclStmtNode>);
        param->isFuncParam = true; // 设置为函数参数
        params.emplace_back(param);
    }
    return params; // 返回函数参数列表
}
//处理实参列表
antlrcpp::Any ASTNodeVisitor::visitFuncRParams(SysYParser::FuncRParamsContext *ctx) {
    Vector<Ptr<ast::DeclStmtNode>> params;
    for (auto paramCtx : ctx->exp()) {
        auto param = AS(paramCtx->accept(this), Ptr<ast::ExprNode>);
        params.emplace_back(param);
    }
    return params; // 返回函数参数列表
}
