#include "ASTNodeVisitor.h"
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
int ASTNodeVisitor::handleFunctionDef(Ptr<ast::FuncNode> func)
{
    return 0;
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
        String identifier = defctx->Ident()->getText();
        // 可能为多维数组或者一般变量
        Vector<Ptr<ast::ExprNode>> arrayIndices;
        for (auto expctx : defctx->constExp())
        {
            arrayIndices.emplace_back(AS(expctx->accept(this), Ptr<ast::ExprNode>));
        }

        // 创建带有数组维度信息的DataType
        DataType type = DataType(convertToPrimaryDataType(ctx->bType()->getText()), arrayIndices, true);
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
            // 直接使用返回的InitExprNode，不要重新创建
            initExprPtr = AS(initVal, Ptr<ast::InitExprNode>);
        }
        auto declptr = makePtr<ast::DeclStmtNode>(type, identifier, initExprPtr);
        declptr->line= defctx->getStart()->getLine(); // 设置行号
        decls.emplace_back(declptr);
    }
    return decls;
}
// 单一表达式
antlrcpp::Any ASTNodeVisitor::visitConstInitExpr(SysYParser::ConstInitExprContext *const ctx)
{
    auto constExpPtr = AS(ctx->constExp()->accept(this), Ptr<ast::ExprNode>);
    // 返回一个 InitExprNode，表示单一的初始化表达式
    auto initExpr = makePtr<ast::InitExprNode>(constExpPtr);
    initExpr->line = ctx->getStart()->getLine(); // 设置行号
    return initExpr;
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
antlrcpp::Any ASTNodeVisitor::visitVarDecl(SysYParser::VarDeclContext *ctx)
{
    // 获取变量的类型
    PrimaryDataType type = convertToPrimaryDataType(ctx->bType()->getText());
    // 存储所有变量的声明
    auto decls = Vector<Ptr<ast::DeclStmtNode>>();
    for (auto varDefs : ctx->varDef())
    {
        String identifier;
        Vector<Ptr<ast::ExprNode>> arrayIndices;
        Ptr<ast::InitExprNode> initExprPtr = nullptr; // 初始化为 nullptr
        // 如果有初始化列表
        if (varDefs->initVal())
        {
            identifier = varDefs->Ident()->getText();
            // 每一维的大小
            for (auto constExpCtx : varDefs->constExp())
            {
                arrayIndices.emplace_back(AS(visit(constExpCtx), Ptr<ast::ExprNode>));
            }
            auto initVal = varDefs->initVal()->accept(this);
            if (initVal.IS(Ptr<ast::InitExprNode>))
            {
                initExprPtr = AS(initVal, Ptr<ast::InitExprNode>);
            }
            else if (initVal.IS(Vector<Ptr<ast::InitExprNode>>))
            {
                // 多维数组的初始化
                auto initVals = AS(initVal, Vector<Ptr<ast::InitExprNode>>);
                initExprPtr = makePtr<ast::InitExprNode>(initVals);
            }
        }
        else
        {
            identifier = varDefs->Ident()->getText();
            for (auto constExpCtx : varDefs->constExp())
            {
                arrayIndices.emplace_back(AS(visit(constExpCtx), Ptr<ast::ExprNode>));
            }
            initExprPtr = nullptr;
        }

        DataType varType(type, arrayIndices, false); // 创建DataType，最后一个参数表示是否为常量

        // 创建VarDecl 最后两个参数默认false
        auto decl = makePtr<ast::DeclStmtNode>(varType, identifier, initExprPtr);
        decl->line = varDefs->getStart()->getLine(); // 设置行号
        decls.emplace_back(decl);
    }
    return decls; // 返回所有变量声明的向量
}
antlrcpp::Any ASTNodeVisitor::visitInitExpr(SysYParser::InitExprContext *ctx)
{
    auto initVal = AS(ctx->exp()->accept(this), Ptr<ast::ExprNode>);
    // 创建InitExprNode并设置常量标志
    auto initExpr = makePtr<ast::InitExprNode>(initVal);
    initExpr->line = ctx->getStart()->getLine(); // 设置行号
    return initExpr;
}
antlrcpp::Any ASTNodeVisitor::visitInitList(SysYParser::InitListContext *ctx)
{
    Vector<Ptr<ast::InitExprNode>> initVals;
    for (auto initValCtx : ctx->initVal())
    {
        // 访问每个 InitValContext，获取初始化值
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
antlrcpp::Any ASTNodeVisitor::visitFuncDef(SysYParser::FuncDefContext *ctx)
{
    PrimaryDataType funcType = convertToPrimaryDataType(ctx->funcType()->getText());
    DataType returnType(funcType);
    // 获取函数名
    String funcName = ctx->Ident()->getText();
    // 获取函数参数列表
    Vector<Ptr<ast::DeclStmtNode>> params;
    if (ctx->funcFParams())
    {
        for (auto paramCtx : ctx->funcFParams()->funcFParam())
        {
            auto param = AS(paramCtx->accept(this), Ptr<ast::DeclStmtNode>);
            params.emplace_back(param);
        }
    }
    auto bodyptr = AS(visit(ctx->block()), Ptr<ast::BlockStmtNode>);
    // 创建函数节点
    auto funcNode = makePtr<ast::FuncNode>(returnType, funcName, params, bodyptr);
    return funcNode; // 返回函数节点
}
antlrcpp::Any ASTNodeVisitor::visitTypeVoid(SysYParser::TypeVoidContext *ctx)
{
    return PrimaryDataType::VOID; // 返回 PrimaryDataType::VOID
}
antlrcpp::Any ASTNodeVisitor::visitTypeBType(SysYParser::TypeBTypeContext *ctx)
{
    return PrimaryDataType::INT; // 返回 PrimaryDataType::INT
}
// 形参列表
antlrcpp::Any ASTNodeVisitor::visitFuncFParams(SysYParser::FuncFParamsContext *ctx)
{
    Vector<Ptr<ast::DeclStmtNode>> params;
    for (auto paramCtx : ctx->funcFParam())
    {
        auto param = AS(paramCtx->accept(this), Ptr<ast::DeclStmtNode>);
        param->isFuncParam = true; // 设置为函数参数
        params.emplace_back(param);
    }
    return params; // 返回函数参数列表
}
// 处理实参列表
antlrcpp::Any ASTNodeVisitor::visitFuncRParams(SysYParser::FuncRParamsContext *ctx)
{
    Vector<Ptr<ast::ExprNode>> params;
    for (auto paramCtx : ctx->exp())
    {
        auto param = std::any_cast<Ptr<ast::ExprNode>>(visit(paramCtx));
        params.emplace_back(param);
    }
    return params; // 返回函数参数列表
}

// 处理函数参数(非数组)
antlrcpp::Any ASTNodeVisitor::visitScalarParam(SysYParser::ScalarParamContext *ctx)
{
    String identifier = ctx->Ident()->getText();
    PrimaryDataType type = convertToPrimaryDataType(ctx->bType()->getText());
    DataType dataType(type);
    // 创建函数参数节点
    auto paramNode = makePtr<ast::DeclStmtNode>(dataType, identifier);
    paramNode->line = ctx->getStart()->getLine(); // 设置行号
    return paramNode; // 返回函数参数节点
}
// 处理函数参数(数组 省略第一维度)
antlrcpp::Any ASTNodeVisitor::visitArrayParamNoSize(SysYParser::ArrayParamNoSizeContext *ctx)
{
    String identifier = ctx->Ident()->getText();
    PrimaryDataType type = convertToPrimaryDataType(ctx->bType()->getText());

    Vector<Ptr<ast::ExprNode>> arrayIndices;
    auto minusOne = makePtr<ast::IntLiteralExprNode>(-1); // 数组维度为0，表示省略
    arrayIndices.emplace_back(minusOne);                  // 数组维度为0，表示省略
    for (auto expCtx : ctx->constExp())
    {
        // 处理数组的其他维度
        auto exp = AS(expCtx->accept(this), Ptr<ast::ExprNode>);
        arrayIndices.emplace_back(exp);
    }
    DataType dataType(type, arrayIndices); // 创建包含数组信息的DataType
    // 创建函数参数节点
    auto paramNode = makePtr<ast::DeclStmtNode>(dataType, identifier);
    paramNode->line = ctx->getStart()->getLine(); // 设置行号
    return paramNode; // 返回函数参数节点
}
// 处理函数参数(数组 有第一维度)
antlrcpp::Any ASTNodeVisitor::visitArrayParamWithSize(SysYParser::ArrayParamWithSizeContext *ctx)
{
    String identifier = ctx->Ident()->getText();
    PrimaryDataType type = convertToPrimaryDataType(ctx->bType()->getText());

    Vector<Ptr<ast::ExprNode>> arrayIndices;
    for (auto expCtx : ctx->constExp())
    {
        auto exp = AS(expCtx->accept(this), Ptr<ast::ExprNode>);
        arrayIndices.emplace_back(exp);
    }

    // 创建包含数组信息的DataType
    DataType dataType(type, arrayIndices);

    // 创建函数参数节点
    auto paramNode = makePtr<ast::DeclStmtNode>(dataType, identifier);
    paramNode->line = ctx->getStart()->getLine(); // 设置行号
    return paramNode; // 返回函数参数节点
}
// 语句块
antlrcpp::Any ASTNodeVisitor::visitBlock(SysYParser::BlockContext *ctx)
{
    Vector<Ptr<ast::StmtNode>> blockItems;
    for (auto itemCtx : ctx->blockItem())
    {
        auto item = AS(itemCtx->accept(this), Vector<Ptr<ast::StmtNode>>);
        blockItems.insert(blockItems.end(), item.begin(), item.end());
    }
    auto block = makePtr<ast::BlockStmtNode>(Move(blockItems));
    block->line = ctx->getStart()->getLine(); // 设置行号
    return block;// 返回一个 BlockStmtNode
}
// 变量声明
antlrcpp::Any ASTNodeVisitor::visitItemDecl(SysYParser::ItemDeclContext *ctx)
{
    auto decl = AS(ctx->decl()->accept(this), Vector<Ptr<ast::DeclStmtNode>>);
    Vector<Ptr<ast::StmtNode>> stmts;
    for (const auto &d : decl)
    {
        // 将每个声明转换为语句
        stmts.emplace_back(Scast<Ptr<ast::StmtNode>>(d));
    }
    return stmts; // 返回一个包含所有声明的向量
}
// 语句 封装单一语句返回数组
antlrcpp::Any ASTNodeVisitor::visitItemStmt(SysYParser::ItemStmtContext *ctx)
{
    auto stmt = AS(ctx->stmt()->accept(this), Ptr<ast::StmtNode>);
    Vector<Ptr<ast::StmtNode>> stmts;
    stmts.emplace_back(stmt); // 将单个语句添加到向量中
    return stmts;             // 返回一个包含单个语句的向量
}
// 赋值语句
antlrcpp::Any ASTNodeVisitor::visitAssignStmt(SysYParser::AssignStmtContext *ctx)
{
    // 访问左值
    auto lval = AS(ctx->lVal()->accept(this), Ptr<ast::LValueExprNode>);
    // 访问表达式
    auto exp = AS(ctx->exp()->accept(this), Ptr<ast::ExprNode>);
    auto ret = makePtr<ast::AssignStmtNode>(Move(lval), Move(exp));
    ret->line = ctx->getStart()->getLine(); // 设置行号
    // 创建赋值语句节点
    return Scast<Ptr<ast::StmtNode>>(Move(ret)); // 统一返回基类stmtNode
}
// 表达式语句
antlrcpp::Any ASTNodeVisitor::visitExprStmt(SysYParser::ExprStmtContext *ctx)
{
    if (ctx->exp())
    {
        // 访问表达式
        auto exp = AS(ctx->exp()->accept(this), Ptr<ast::ExprNode>);
        Ptr<ast::ExprStmtNode> exprStmtNode = makePtr<ast::ExprStmtNode>(Move(exp));
        exprStmtNode->line = ctx->getStart()->getLine();      // 设置行号
        return Scast<Ptr<ast::StmtNode>>(Move(exprStmtNode)); // 返回表达式语句节点
    }
    else
    {
        // 没有表达式直接返回
        Ptr<ast::ExprStmtNode> exprStmtNode = makePtr<ast::ExprStmtNode>(nullptr);
        exprStmtNode->line = ctx->getStart()->getLine();      // 设置行号
        return Scast<Ptr<ast::StmtNode>>(Move(exprStmtNode)); // 返回表达式语句节点
    }
}
// 语句块
antlrcpp::Any ASTNodeVisitor::visitBlockStmt(SysYParser::BlockStmtContext *ctx)
{
    auto blockItems = AS(ctx->block()->accept(this), Ptr<ast::BlockStmtNode>);
    return Scast<Ptr<ast::StmtNode>>(Move(blockItems)); // 返回块语句节点
}
// if语句
antlrcpp::Any ASTNodeVisitor::visitIfStmt(SysYParser::IfStmtContext *ctx)
{
    // 访问条件表达式
    auto cond = AS(ctx->cond()->accept(this), Ptr<ast::ExprNode>);
    // 访问 if 语句块
    auto thenBlock = AS(ctx->stmt(0)->accept(this), Ptr<ast::StmtNode>);
    Ptr<ast::StmtNode> elseBlock = nullptr;
    if (ctx->ELSE())
    {
        // 如果有 else 分支，访问 else 语句块
        elseBlock = AS(ctx->stmt(1)->accept(this), Ptr<ast::StmtNode>);
    }
    Ptr<ast::IfElseStmtNode> ifStmtNode = makePtr<ast::IfElseStmtNode>(Move(cond), Move(thenBlock), Move(elseBlock));
    ifStmtNode->line = ctx->getStart()->getLine();      // 设置行号
    return Scast<Ptr<ast::StmtNode>>(Move(ifStmtNode)); // 返回 if 语句节点
}
antlrcpp::Any ASTNodeVisitor::visitWhileStmt(SysYParser::WhileStmtContext *ctx)
{
    // 访问条件表达式
    auto cond = AS(ctx->cond()->accept(this), Ptr<ast::ExprNode>);
    // 访问 while 语句块
    auto body = AS(ctx->stmt()->accept(this), Ptr<ast::StmtNode>);
    auto whileStmtNode = makePtr<ast::WhileStmtNode>(Move(cond), Move(body));
    whileStmtNode->line = ctx->getStart()->getLine();      // 设置行号
    return Scast<Ptr<ast::StmtNode>>(Move(whileStmtNode)); // 返回 while 语句节点
}
antlrcpp::Any ASTNodeVisitor::visitBreakStmt(SysYParser::BreakStmtContext *ctx)
{
    // 创建 Break 语句节点
    auto breakStmtNode = makePtr<ast::BreakStmtNode>();
    // 行号debug信息
    breakStmtNode->line = ctx->getStart()->getLine();      // 设置行号
    return Scast<Ptr<ast::StmtNode>>(Move(breakStmtNode)); // 返回 Break 语句节点
}
// continue语句
antlrcpp::Any ASTNodeVisitor::visitContinueStmt(SysYParser::ContinueStmtContext *ctx)
{
    // 创建 Continue 语句节点
    auto continueStmtNode = makePtr<ast::ContinueStmtNode>();
    // 行号debug信息
    continueStmtNode->line = ctx->getStart()->getLine();      // 设置行号
    return Scast<Ptr<ast::StmtNode>>(Move(continueStmtNode)); // 返回 Continue 语句节点
}
// return语句
antlrcpp::Any ASTNodeVisitor::visitReturnStmt(SysYParser::ReturnStmtContext *ctx)
{
    Ptr<ast::ExprNode> retValue = nullptr; // 默认没有返回值
    if (ctx->exp())
    {
        // 如果有返回值，访问表达式
        retValue = AS(ctx->exp()->accept(this), Ptr<ast::ExprNode>);
    }
    auto returnStmtNode = makePtr<ast::ReturnStmtNode>(Move(retValue));
    // 行号debug信息
    returnStmtNode->line = ctx->getStart()->getLine();      // 设置行号
    return Scast<Ptr<ast::StmtNode>>(Move(returnStmtNode)); // 返回 Return 语句节点
}
// 表达式 exp
antlrcpp::Any ASTNodeVisitor::visitExp(SysYParser::ExpContext *ctx)
{
    return visit(ctx->lOrExp());
}
antlrcpp::Any ASTNodeVisitor::visitCond(SysYParser::CondContext *ctx)
{
    return visit(ctx->lOrExp());
}
antlrcpp::Any ASTNodeVisitor::visitLVal(SysYParser::LValContext *ctx)
{
    string identifier = ctx->Ident()->getText();
    vector<shared_ptr<ExprNode>> exps;
    for (auto idx : ctx->exp())
    {
        // 使用 any_cast 进行类型转换
        auto result = visit(idx);
        exps.push_back(std::any_cast<shared_ptr<ExprNode>>(result));
    }
    auto lvalNode = make_shared<LValueExprNode>(identifier, exps);
    lvalNode->line = ctx->getStart()->getLine(); // 设置行号
    return lvalNode; // 返回 LValueExprNode
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

antlrcpp::Any ASTNodeVisitor::visitStringLiteralExp(SysYParser::StringLiteralExpContext *ctx)
{
    // 处理字符串字面量
    std::string strValue = ctx->STRING_LITERAL()->getText();
    // 去掉引号
    strValue = strValue.substr(1, strValue.size() - 2);
    auto strLiteral = make_shared<StringLiteralExprNode>(strValue);
    strLiteral->line = ctx->getStart()->getLine(); // 设置行号
    // 返回字符串字面量表达式节点
    return static_cast<shared_ptr<ExprNode>>(strLiteral);
}

antlrcpp::Any ASTNodeVisitor::visitIntNum(SysYParser::IntNumContext *ctx)
{
    std::string valueText = ctx->IntConst()->getText();
    std::int32_t intValue = 0;

    if (valueText.find("0x") == 0 || valueText.find("0X") == 0)
    {
        // 十六进制
        intValue = std::stoi(valueText, nullptr, 16);
    }
    else if (valueText.find("0") == 0 && valueText.size() > 1 &&
             valueText.find_first_not_of("01234567", 1) == std::string::npos)
    {
        // 八进制
        intValue = std::stoi(valueText, nullptr, 8);
    }
    else
    {
        // 十进制
        intValue = std::stoi(valueText, nullptr, 10);
    }

    auto intLiteral = make_shared<IntLiteralExprNode>(intValue);
    intLiteral->line = ctx->getStart()->getLine(); // 设置行号
    return static_cast<shared_ptr<NumberLiteralExprNode>>(intLiteral);
}

antlrcpp::Any ASTNodeVisitor::visitFloatNum(SysYParser::FloatNumContext *ctx)
{
    std::string text = ctx->FloatConst()->getText();
    float floatValue = std::stof(text);
    auto floatLiteral = make_shared<FloatLiteralExprNode>(floatValue);
    floatLiteral->line = ctx->getStart()->getLine(); // 设置行号
    return static_cast<shared_ptr<NumberLiteralExprNode>>(floatLiteral);
}
// unaryExp
antlrcpp::Any ASTNodeVisitor::visitToPrimaryExp(SysYParser::ToPrimaryExpContext *ctx)
{
    return visit(ctx->primaryExp());
}
antlrcpp::Any ASTNodeVisitor::visitCallExp(SysYParser::CallExpContext *ctx)
{
    string callee = ctx->Ident()->getText();
    vector<shared_ptr<ExprNode>> args;
    if (ctx->funcRParams())
    {
        auto params = std::any_cast<vector<shared_ptr<ExprNode>>>(visit(ctx->funcRParams()));
        args.insert(args.end(), params.begin(), params.end());
    }
    auto callexpnode = make_shared<CallExprNode>(callee, args);
    callexpnode->line = ctx->getStart()->getLine(); // 设置行号
    return static_cast<shared_ptr<ExprNode>>(callexpnode);
}
antlrcpp::Any ASTNodeVisitor::visitOpUnaryExp(SysYParser::OpUnaryExpContext *ctx)
{
    // 处理一元操作符
    // operator是字符串
    string op = ctx->unaryOp()->getText();
    UnaryOp unaryOp;
    if (op == "+")
    {
        unaryOp = UnaryOp::Plus;
    }
    else if (op == "-")
    {
        unaryOp = UnaryOp::Minus;
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
    auto unaryExprNode = make_shared<UnaryExprNode>(exp, unaryOp);
    unaryExprNode->line = ctx->getStart()->getLine(); // 设置行号
    return static_cast<shared_ptr<ExprNode>>(unaryExprNode);
}
// unaryOp
antlrcpp::Any ASTNodeVisitor::visitOpPlus(SysYParser::OpPlusContext *ctx)
{
    return UnaryOp::Plus; // 返回一元加操作符
}
antlrcpp::Any ASTNodeVisitor::visitOpMinus(SysYParser::OpMinusContext *ctx)
{
    return UnaryOp::Minus; // 返回一元减操作符
}
antlrcpp::Any ASTNodeVisitor::visitOpNot(SysYParser::OpNotContext *ctx)
{
    return UnaryOp::Not; // 返回逻辑非操作符
}
// mulExp
antlrcpp::Any ASTNodeVisitor::visitToUnaryExp_mul(SysYParser::ToUnaryExp_mulContext *ctx)
{
    return visit(ctx->unaryExp());
}
antlrcpp::Any ASTNodeVisitor::visitMulDivModExp(SysYParser::MulDivModExpContext *ctx)
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
    auto binaryExprNode = make_shared<BinaryExprNode>(mulExp, unaryExp, binaryOp);
    binaryExprNode->line = ctx->getStart()->getLine(); // 设置行号
    return static_cast<shared_ptr<ExprNode>>(binaryExprNode);
}
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
    auto binaryExprNode = make_shared<BinaryExprNode>(addExp, mulExp, binaryOp);
    binaryExprNode->line = ctx->getStart()->getLine(); // 设置行号
    return static_cast<shared_ptr<ExprNode>>(binaryExprNode);
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
    auto binaryExprNode = make_shared<BinaryExprNode>(relExp, addExp, binaryOp);
    binaryExprNode->line = ctx->getStart()->getLine(); // 设置行号
    return static_cast<shared_ptr<ExprNode>>(binaryExprNode);
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
    auto eqExpNode = make_shared<BinaryExprNode>(eqExp, relExp, binaryOp);
    eqExpNode->line = ctx->getStart()->getLine(); // 设置行号
    // 返回等式表达式节点
    return static_cast<shared_ptr<ExprNode>>(eqExpNode);
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
    auto lAndExpNode = make_shared<BinaryExprNode>(lAndExp, eqExp, BinaryOp::And);
    lAndExpNode->line = ctx->getStart()->getLine(); // 设置行号
    // 返回逻辑与表达式节点
    return static_cast<shared_ptr<ExprNode>>(lAndExpNode);
}
// lOrExp
antlrcpp::Any ASTNodeVisitor::visitLorOpExp(SysYParser::LorOpExpContext *ctx)
{
    auto lOrExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->lOrExp()));
    auto lAndExp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->lAndExp()));
    string op = ctx->children[1]->getText(); // 获取操作符
    if (op != "||")
    {
        throw std::invalid_argument("Unknown logical operator: " + op);
    }
    auto lOrExpNode = make_shared<BinaryExprNode>(lOrExp, lAndExp, BinaryOp::Or);
    lOrExpNode->line = ctx->getStart()->getLine(); // 设置行号
    // 返回逻辑或表达式节点
    return static_cast<shared_ptr<ExprNode>>(lOrExpNode);
}

antlrcpp::Any ASTNodeVisitor::visitConstExp(SysYParser::ConstExpContext *ctx)
{
    // 访问常量表达式
    auto constexp = std::any_cast<shared_ptr<ExprNode>>(visit(ctx->addExp()));
    constexp->isConstExp = true; // 标记为常量表达式
    return constexp;             // 返回常量表达式
}

antlrcpp::Any ASTNodeVisitor::visitToEqExp_land(SysYParser::ToEqExp_landContext *ctx)
{
    return visit(ctx->eqExp());
}