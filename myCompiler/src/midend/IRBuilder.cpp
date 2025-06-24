#include "IRBuilder.h"
#include <stdexcept>
#include <sstream>

using namespace ir_builder;
using namespace ast;

// ===== 库函数初始化函数 =====
void IRBuilder::initializeLibraryFunctions()
{
    // int getint();
    {
        FunctionType* funcType = new FunctionType(IntegerType::getInstance(), {});
        module->addFunction(funcType, "getint");
    }
    // int getch();
    {
        FunctionType* funcType = new FunctionType(IntegerType::getInstance(), {});
        module->addFunction(funcType, "getch");
    }
    // float getfloat();
    {
        FunctionType* funcType = new FunctionType(FloatType::getInstance(), {});
        module->addFunction(funcType, "getfloat");
    }
    // int getarray(int a[]);
    {
        std::vector<Type*> params = { new PointerType(IntegerType::getInstance()) };
        FunctionType* funcType = new FunctionType(IntegerType::getInstance(), params);
        module->addFunction(funcType, "getarray");
    }
    // int getfarray(float a[]);
    {
        std::vector<Type*> params = { new PointerType(FloatType::getInstance()) };
        FunctionType* funcType = new FunctionType(IntegerType::getInstance(), params);
        module->addFunction(funcType, "getfarray");
    }
    // void putint(int a);
    {
        std::vector<Type*> params = { IntegerType::getInstance() };
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putint");
    }
    // void putch(int a);
    {
        std::vector<Type*> params = { IntegerType::getInstance() };
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putch");
    }
    // void putfloat(float a);
    {
        std::vector<Type*> params = { FloatType::getInstance() };
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putfloat");
    }
    // void putarray(int n, int a[]);
    {
        std::vector<Type*> params = { IntegerType::getInstance(), new PointerType(IntegerType::getInstance()) };
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putarray");
    }
    // void putfarray(int n, float a[]);
    {
        std::vector<Type*> params = { IntegerType::getInstance(), new PointerType(FloatType::getInstance()) };
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putfarray");
    }
    // void putf(string a); 这里假设 string 用 i8* 表示
    {
        std::vector<Type*> params = { StringType::getInstance() }; 
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putf");
    }
    // void starttime();
    {
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), {});
        module->addFunction(funcType, "starttime");
    }
    // void stoptime();
    {
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), {});
        module->addFunction(funcType, "stoptime");
    }
}
// ===== 主入口：构建整个模块 =====
std::unique_ptr<Module> IRBuilder::buildModule(std::shared_ptr<ast::CompUnitNode> compUnit)
{
    visitCompUnit(compUnit);
    return std::move(module);
}
// ===== AST 节点访问实现 =====
void IRBuilder::visitCompUnit(std::shared_ptr<ast::CompUnitNode> node)
{
    for (auto &child : node->children)
    {
        if (auto funcNode = std::dynamic_pointer_cast<ast::FuncNode>(child))
        {
            visitFunction(funcNode);
        }
        else if (auto declNode = std::dynamic_pointer_cast<ast::DeclStmtNode>(child))
        {
            // 全局变量声明
            visitDeclStmt(declNode);
        }
    }
}

void IRBuilder::visitFunction(std::shared_ptr<ast::FuncNode> node)
{
    // 创建函数类型
    Type *retType = convertASTTypeToIRType(node->returnType,false);
    std::vector<Type *> paramTypes;
    for (auto &param : node->params)
    {
        paramTypes.push_back(convertASTTypeToIRType(param->type,true));
    }
    FunctionType *funcType = new FunctionType(retType, paramTypes);
    Function *func = module->addFunction(funcType, node->identifier);
    currentFunction = func;

    // 创建入口基本块
    BasicBlock *entryBlock = createBasicBlock("entry");
    setCurrentBlock(entryBlock);

    // 添加参数并为每个参数创建 alloca
    for (size_t i = 0; i < node->params.size(); i++)
    {
        Argument *arg = func->addArgument(paramTypes[i], node->params[i]->identifier);
        // 如果是指针类型参数（如退化后的数组参数），直接用参数本身
        if (paramTypes[i]->isPointerTy()) 
        {
            // 直接使用参数，不需要额外的 alloca
        varToValue[node->params[i]->identifier] = arg;
        } 
        else
        {
        Value *alloca = createAlloca(paramTypes[i], node->params[i]->identifier+".addr");
        createStore(arg, alloca);
        varToValue[node->params[i]->identifier] = alloca;
        }
    }

    // 进入新的作用域
    varToValueStack.push(varToValue);

    // 访问函数体
    visitBlock(node->body);

    // 如果函数没有显式返回，添加默认返回
    if (!currentBlock->hasTerminator())
    {
        if (retType->isVoidTy())
        {
            createReturn();
        }
        else
        {
            // 非 void 函数必须有返回值
            throw std::runtime_error("Non-void function must return a value");
        }
    }

    // 退出作用域
    varToValue = varToValueStack.top();
    varToValueStack.pop();
    currentFunction = nullptr;
}

void IRBuilder::visitBlock(std::shared_ptr<ast::BlockStmtNode> node)
{
    // 进入新的作用域
    varToValueStack.push(varToValue);

    for (auto &stmt : node->stmts)
    {
        visitStatement(stmt);
        // 如果当前块已经有终结指令，跳过后续语句
        if (currentBlock->hasTerminator())
        {
            break;
        }
    }

    // 退出作用域
    varToValue = varToValueStack.top();
    varToValueStack.pop();
}
//写一个获取expnode值的函数

void IRBuilder::visitStatement(std::shared_ptr<ast::StmtNode> node)
{
    if (auto declStmt = std::dynamic_pointer_cast<ast::DeclStmtNode>(node))
    {
        visitDeclStmt(declStmt);
    }
    else if (auto assignStmt = std::dynamic_pointer_cast<ast::AssignStmtNode>(node))
    {
        visitAssignStmt(assignStmt);
    }
    else if (auto exprStmt = std::dynamic_pointer_cast<ast::ExprStmtNode>(node))
    {
        visitExprStmt(exprStmt);
    }
    else if (auto ifStmt = std::dynamic_pointer_cast<ast::IfElseStmtNode>(node))
    {
        visitIfElseStmt(ifStmt);
    }
    else if (auto whileStmt = std::dynamic_pointer_cast<ast::WhileStmtNode>(node))
    {
        visitWhileStmt(whileStmt);
    }
    else if (auto breakStmt = std::dynamic_pointer_cast<ast::BreakStmtNode>(node))
    {
        visitBreakStmt(breakStmt);
    }
    else if (auto continueStmt = std::dynamic_pointer_cast<ast::ContinueStmtNode>(node))
    {
        visitContinueStmt(continueStmt);
    }
    else if (auto returnStmt = std::dynamic_pointer_cast<ast::ReturnStmtNode>(node))
    {
        visitReturnStmt(returnStmt);
    }
    else if (auto blockStmt = std::dynamic_pointer_cast<ast::BlockStmtNode>(node))
    {
        visitBlock(blockStmt);
    }
}

void IRBuilder::visitDeclStmt(std::shared_ptr<ast::DeclStmtNode> node)
{
    Type *varType = convertASTTypeToIRType(node->type,false);

    if (currentFunction == nullptr)
    {
        // 全局变量
        Constant *initializer = nullptr;
        if (node->initializer)
        {
            if (varType->isArrayTy()) 
            {
            initializer = evaluateConstantArray(node->initializer, static_cast<ArrayType*>(varType));
            } 
            else 
            {
            initializer = evaluateConstantExpr(node->initializer->singleInitVal);
            }
        }
        GlobalVariable *globalVar = module->addGlobalVariable(varType, node->identifier, initializer, node->type.isConst());
        varToValue[node->identifier] = globalVar;
        // const变量加入常量表
        if(node->type.isConst())
        {
             constVarInitValues[node->identifier] = initializer;
        }
    }
    else
    {
        // 局部变量
        Value *alloca = createAlloca(varType, node->identifier);
        varToValue[node->identifier] = alloca;

        if (node->initializer)
        {   
            if(!node->type.isConst())
            {
                if (varType->isArrayTy()) 
                {
                    // 直接初始化 alloca 指向的空间
                    visitInitExpr(node->initializer, varType, alloca);
                } 
                else 
                {
                    Value *initValue = visitInitExpr(node->initializer, varType);
                    createStore(initValue, alloca);
                }
            }
            else
            {
                Constant *initializer = nullptr;
                // 如果是常量变量，记录初始值
                if(varType->isArrayTy()) 
                {
                    initializer = evaluateConstantArray(node->initializer, static_cast<ArrayType*>(varType));
                }
                else 
                {
                    initializer = evaluateConstantExpr(node->initializer->singleInitVal);
                }
                    constVarInitValues[node->identifier] = initializer;
            }
        }  
    }
}

void IRBuilder::visitAssignStmt(std::shared_ptr<ast::AssignStmtNode> node)
{
    Value *lvalue = visitLValueExpr(node->lvalue);
    Value *rvalue = visitExpression(node->rvalue);

    // 类型转换（如果需要）
    Type *targetType = static_cast<PointerType *>(lvalue->getType())->ElementType;
    if (rvalue->getType() != targetType)
    {
        rvalue = createCast(rvalue, targetType);
    }

    createStore(rvalue, lvalue);
}

void IRBuilder::visitExprStmt(std::shared_ptr<ast::ExprStmtNode> node)
{
    visitExpression(node->expr);
}

void IRBuilder::visitIfElseStmt(std::shared_ptr<ast::IfElseStmtNode> node)
{
    Value *condition = visitExpression(node->condition);

    BasicBlock *thenBlock = createBasicBlock("if.then");
    BasicBlock *elseBlock = node->else_body ? createBasicBlock("if.else") : nullptr;
    BasicBlock *mergeBlock = createBasicBlock("if.end");

    // 记录分支前变量状态
    auto varToValueBefore = varToValue;

    // 条件跳转
    createCondBranch(condition, thenBlock, elseBlock ? elseBlock : mergeBlock);

    // then 分支
    setCurrentBlock(thenBlock);
    visitStatement(node->then_body);
    auto varToValueThen = varToValue; // then分支后的变量状态
    if (!currentBlock->hasTerminator())
    {
        createBranch(mergeBlock);
    }

    // else 分支
    std::unordered_map<std::string, Value*> varToValueElse = varToValueBefore;
    if (elseBlock)
    {
        setCurrentBlock(elseBlock);
        varToValue = varToValueBefore; // else分支变量初始状态与if前一致
        visitStatement(node->else_body);
        varToValueElse = varToValue; // else分支后的变量状态
        if (!currentBlock->hasTerminator())
        {
            createBranch(mergeBlock);
        }
    }

    // 合流块
    setCurrentBlock(mergeBlock);

    // 插入PHI，只为被赋值的变量插入
    for (const auto& [name, valThen] : varToValueThen) {
        auto itElse = varToValueElse.find(name);
        if (itElse != varToValueElse.end() && (valThen != itElse->second)) {
            PHINode* phi = createPhi(valThen->getType(), getNextTempName());
            phi->IncomingValues.push_back({valThen, thenBlock});
            phi->IncomingValues.push_back({itElse->second, elseBlock ? elseBlock : mergeBlock});
            varToValue[name] = phi;
        } else if (itElse != varToValueElse.end()) {
            // 两分支一致，直接用任意一个
            varToValue[name] = valThen;
        }
    }
    // 处理只在else分支被赋值的变量
    for (const auto& [name, valElse] : varToValueElse) {
        if (varToValueThen.find(name) == varToValueThen.end()) {
            PHINode* phi = createPhi(valElse->getType(), getNextTempName());
            phi->IncomingValues.push_back({varToValueBefore[name], thenBlock});
            phi->IncomingValues.push_back({valElse, elseBlock ? elseBlock : mergeBlock});
            varToValue[name] = phi;
        }
    }
}

void IRBuilder::visitWhileStmt(std::shared_ptr<ast::WhileStmtNode> node)
{
    // 1. 创建基本块
    BasicBlock *condBlock = createBasicBlock("while.cond");
    BasicBlock *bodyBlock = createBasicBlock("while.body");
    BasicBlock *exitBlock = createBasicBlock("while.end");

    // 2. 记录循环前变量SSA状态
    auto varToValueBefore = varToValue;

    // 3. 跳转到条件判断块
    createBranch(condBlock);

    // 4. 设置当前块为条件判断块
    setCurrentBlock(condBlock);

    // 5. 生成条件表达式的 IR
    Value *condition = visitExpression(node->condition);
    createCondBranch(condition, bodyBlock, exitBlock);

    // 6. 进入循环体
    setCurrentBlock(bodyBlock);
    loopStack.push(LoopContext(condBlock, exitBlock));
    auto varToValueBodyEntry = varToValue; // 进入循环体前的变量状态
    visitStatement(node->body);
    loopStack.pop();

    // 7. 记录循环体后变量状态
    auto varToValueAfter = varToValue;

    // 8. 如果循环体没有提前 return/break，循环体结尾跳回条件判断块
    if (!currentBlock->hasTerminator())
    {
        createBranch(condBlock);
    }

    // 9. 回到 condBlock，插入 PHI，只为被赋值的变量插入
    setCurrentBlock(condBlock); // 确保在 condBlock 插入
    //用于记录每个变量名对应的 PHI 节点指针，便于后续查找和管理
    std::unordered_map<std::string, PHINode*> phiNodes;
    for (const auto& [name, valueBefore] : varToValueBefore) {
        auto it = varToValueAfter.find(name);
        // 只为循环体内被赋值的变量插入PHI
        if (it != varToValueAfter.end() && it->second != valueBefore) {
            PHINode* phi = createPhi(valueBefore->getType(), getNextTempName());
            // 循环前的输入
            phi->IncomingValues.push_back({valueBefore, condBlock->Predecessors.front()});
            // 循环体的输入
            phi->IncomingValues.push_back({it->second, bodyBlock});
            //记录该变量的 PHI 节点
            phiNodes[name] = phi;
            //更新 condBlock 作用域下该变量的 SSA 值为 PHI 节点，后续 IR 取变量都用 PHI
            varToValue[name] = phi;
        }
    }

    // 10. 设置当前块为循环结束块
    setCurrentBlock(exitBlock);
}
void IRBuilder::visitBreakStmt(std::shared_ptr<ast::BreakStmtNode> node)
{
    if (loopStack.empty())
    {
        throw std::runtime_error("Break statement outside of loop,line: " + std::to_string(node->line));
    }
    createBranch(loopStack.top().breakBlock);
}

void IRBuilder::visitContinueStmt(std::shared_ptr<ast::ContinueStmtNode> node)
{
    if (loopStack.empty())
    {
        throw std::runtime_error("Continue statement outside of loop,line: " + std::to_string(node->line));
    }
    createBranch(loopStack.top().continueBlock);
}

void IRBuilder::visitReturnStmt(std::shared_ptr<ast::ReturnStmtNode> node)
{
    if (node->ret_expr)
    {
        Value *retValue = visitExpression(node->ret_expr);
        Type *expectedType = currentFunction->getFunctionType()->ReturnType;
        if (retValue->getType() != expectedType)
        {
            retValue = createCast(retValue, expectedType);
        }
        createReturn(retValue);
    }
    else
    {
        createReturn();
    }
}

// ===== 表达式访问实现 =====  这里需要检测int和float是否越界，左值表达式需要检测数组下标是否合法(非负以及不超过范围)
Value *IRBuilder::visitExpression(std::shared_ptr<ast::ExprNode> node)
{
    if (auto binaryExpr = std::dynamic_pointer_cast<ast::BinaryExprNode>(node))
    {   
        auto value = visitBinaryExpr(binaryExpr);
        return value;
    }
    else if (auto unaryExpr = std::dynamic_pointer_cast<ast::UnaryExprNode>(node))
    {
        return visitUnaryExpr(unaryExpr);
    }
    else if (auto lvalueExpr = std::dynamic_pointer_cast<ast::LValueExprNode>(node))    
    {
        Value *ptr = visitLValueExpr(lvalueExpr);
         // 如果是数组退化为指针（如 int a[] 作为参数），直接返回指针，不 load
        Type *ptrType = ptr->getType();
        if (ptrType->isPointerTy()) {
            Type *elemType = static_cast<PointerType*>(ptrType)->ElementType;
            if (elemType->isArrayTy()) 
            {
            return ptr;
            }
        }
        auto value = createLoad(ptr);
        return value;
    }
    else if (auto callExpr = std::dynamic_pointer_cast<ast::CallExprNode>(node))
    {
        return visitCallExpr(callExpr);
    }
    else if (auto intLiteral = std::dynamic_pointer_cast<ast::IntLiteralExprNode>(node))
    {
        return visitIntLiteralExpr(intLiteral);
    }
    else if (auto floatLiteral = std::dynamic_pointer_cast<ast::FloatLiteralExprNode>(node))
    {
        return visitFloatLiteralExpr(floatLiteral);
    }
    else if (auto stringLiteral = std::dynamic_pointer_cast<ast::StringLiteralExprNode>(node))
    {
        return visitStringLiteralExpr(stringLiteral);
    }

    throw std::runtime_error("Unknown expression type ,line: " + std::to_string(node->line));
}

Value *IRBuilder::visitBinaryExpr(std::shared_ptr<ast::BinaryExprNode> node)
{
    // 处理逻辑运算符的短路求值
    if (node->op == BinaryOp::And || node->op == BinaryOp::Or)
    {
        return visitLogicalExpr(node);
    }

    Value *lhs = visitExpression(node->left);
    Value *rhs = visitExpression(node->right);

    // 类型统一
    if (lhs->getType() != rhs->getType())
    {
        if (lhs->getType()->isIntegerTy() && rhs->getType()->isFloatTy())
        {
            lhs = createCast(lhs, FloatType::getInstance());
        }
        else if (lhs->getType()->isFloatTy() && rhs->getType()->isIntegerTy())
        {
            rhs = createCast(rhs, FloatType::getInstance());
        }
    }

    // 判断是比较操作还是算术操作
    switch (node->op)
    {
    case BinaryOp::Lt:
    case BinaryOp::Gt:
    case BinaryOp::Le:
    case BinaryOp::Ge:
    case BinaryOp::Eq:
    case BinaryOp::Ne:
        return createComparison(node->op, lhs, rhs);
    default:
        return createBinaryOp(node->op, lhs, rhs);
    }
}

Value *IRBuilder::visitLogicalExpr(std::shared_ptr<ast::BinaryExprNode> node)
{
    // 短路求值的逻辑表达式处理
    if (node->op == BinaryOp::And)
    {
        // a && b: 如果 a 为 false，直接返回 false，否则计算 b
        BasicBlock *rhsBlock = createBasicBlock("logical.rhs");
        BasicBlock *mergeBlock = createBasicBlock("logical.end");
        BasicBlock *lhsBlock = currentBlock;

        Value *lhs = visitExpression(node->left);

        // 将 lhs 转换为布尔值
        Value *lhsCond = convertToBool(lhs);
        createCondBranch(lhsCond, rhsBlock, mergeBlock);

        // RHS 块
        setCurrentBlock(rhsBlock);
        Value *rhs = visitExpression(node->right);
        Value *rhsCond = convertToBool(rhs);
        createBranch(mergeBlock);
        BasicBlock *rhsEndBlock = currentBlock;

        // 合并块
        setCurrentBlock(mergeBlock);
        PHINode *phi = createPhi(BooleanType::getInstance(), getNextTempName());
        phi->IncomingValues.push_back({new ConstantInt(IntegerType::getInstance(), 0), lhsBlock}); // false from lhs
        phi->IncomingValues.push_back({rhsCond, rhsEndBlock});                                     // result from rhs

        return phi;
    }
    else if (node->op == BinaryOp::Or)
    {
        // a || b: 如果 a 为 true，直接返回 true，否则计算 b
        BasicBlock *rhsBlock = createBasicBlock("logical.rhs");
        BasicBlock *mergeBlock = createBasicBlock("logical.end");
        BasicBlock *lhsBlock = currentBlock;

        Value *lhs = visitExpression(node->left);

        // 将 lhs 转换为布尔值
        Value *lhsCond = convertToBool(lhs);
        createCondBranch(lhsCond, mergeBlock, rhsBlock);

        // RHS 块
        setCurrentBlock(rhsBlock);
        Value *rhs = visitExpression(node->right);
        Value *rhsCond = convertToBool(rhs);
        createBranch(mergeBlock);
        BasicBlock *rhsEndBlock = currentBlock;

        // 合并块
        setCurrentBlock(mergeBlock);
        PHINode *phi = createPhi(BooleanType::getInstance(), getNextTempName());
        phi->IncomingValues.push_back({new ConstantInt(IntegerType::getInstance(), 1), lhsBlock}); // true from lhs
        phi->IncomingValues.push_back({rhsCond, rhsEndBlock});                                     // result from rhs

        return phi;
    }

    throw std::runtime_error("Invalid logical operator,line: " + std::to_string(node->line));
}

Value *IRBuilder::visitUnaryExpr(std::shared_ptr<ast::UnaryExprNode> node)
{
    Value *operand = visitExpression(node->operand);
    return createUnaryOp(node->op, operand);
}

Value *IRBuilder::visitLValueExpr(std::shared_ptr<ast::LValueExprNode> node)
{
    auto it = varToValue.find(node->identifier);
    if (it == varToValue.end())
    {
        throw std::runtime_error("Undefined variable: " + node->identifier + ",line:" + std::to_string(node->line));
    }

    Value *ptr = it->second;

    // 处理数组索引
    if (!node->indices.empty())
    {
        std::vector<Value *> indices;

        // 判断是否需要加第一个0
        // 如果 ptr 是指向数组的指针（如 alloca 或全局变量），加0
        // 如果 ptr 是参数指针（如 int arr[]），不加0
        if (ptr->getType()->isPointerTy()) {
            Type *elemType = static_cast<PointerType*>(ptr->getType())->ElementType;
            if (elemType->isArrayTy()) {
                indices.push_back(new ConstantInt(IntegerType::getInstance(), 0));
            }
        }

        for (auto &indexExpr : node->indices)
        {
            Value *index = visitExpression(indexExpr);
            if (index->getType()->isFloatTy())
            {
                index = createCast(index, IntegerType::getInstance());
            }
            indices.push_back(index);
        }

        auto gepInst = std::make_unique<GetElementPtrInst>(ptr, indices, getNextTempName());
        Value *result = gepInst.get();
        currentBlock->addInstruction(std::move(gepInst));
        return result;
    }
    // 如果是数组类型，自动退化为指针（GEP 0,0）
    if (ptr->getType()->isArrayTy()) {
        std::vector<Value *> indices;
        indices.push_back(new ConstantInt(IntegerType::getInstance(), 0));
        indices.push_back(new ConstantInt(IntegerType::getInstance(), 0));
        auto gepInst = std::make_unique<GetElementPtrInst>(ptr, indices, getNextTempName());
        Value *result = gepInst.get();
        currentBlock->addInstruction(std::move(gepInst));
        return result;
    }
    // 无下标且不是数组，直接返回指针
    return ptr;
}

Value *IRBuilder::visitCallExpr(std::shared_ptr<ast::CallExprNode> node)
{
    Function *func = module->getFunction(node->callee);
    if (!func)
    {
        throw std::runtime_error("Undefined function: " + node->callee+",line:"+ std::to_string(node->line));
    }
    std::vector<Value *> args;
    for (size_t i = 0; i < node->args.size(); i++)
    {
        Value *arg = visitExpression(node->args[i]);
        Type *expectedType = func->getFunctionType()->ParamTypes[i];
        // 多维数组退化：只要 expectedType 是指针，arg 是数组指针，且元素类型不一致，就递归GEP(0)
        while (expectedType->isPointerTy() && arg->getType()->isPointerTy()) {
            Type *argElemType = static_cast<PointerType*>(arg->getType())->ElementType;
            Type *expElemType = static_cast<PointerType*>(expectedType)->ElementType;
            if (argElemType->isArrayTy()&& !argElemType->isTypeEqual(argElemType, expElemType)) {
                // 退化一维
                std::vector<Value*> indices;
                indices.push_back(new ConstantInt(IntegerType::getInstance(), 0));
                indices.push_back(new ConstantInt(IntegerType::getInstance(), 0));
                auto gepInst = std::make_unique<GetElementPtrInst>(arg, indices, getNextTempName());
                Value *result = gepInst.get();
                currentBlock->addInstruction(std::move(gepInst));
                arg = result;
            } else {
                break;
            }
        }
        // 如果类型不匹配，进行类型转换 不能直接用！=，否则比较的是指针类型而不是元素类型
        if (!expectedType->isTypeEqual(arg->getType(), expectedType)) 
        {
            arg = createCast(arg, expectedType);
        }
        args.push_back(arg);
    }

    return createCall(func, args);
}

Value *IRBuilder::visitIntLiteralExpr(std::shared_ptr<ast::IntLiteralExprNode> node)
{
    return new ConstantInt(IntegerType::getInstance(), node->value);
}

Value *IRBuilder::visitFloatLiteralExpr(std::shared_ptr<ast::FloatLiteralExprNode> node)
{
    return new ConstantFloat(FloatType::getInstance(), node->value);
}

Value *IRBuilder::visitStringLiteralExpr(std::shared_ptr<ast::StringLiteralExprNode> node)
{    // 假设字符串用 i8* 表示
    return new ConstantString(StringType::getInstance(),node->value);
}

Value *IRBuilder::visitInitExpr(std::shared_ptr<ast::InitExprNode> node, Type *targetType)
{
    if (node->singleInitVal)
    {
        return visitExpression(node->singleInitVal);
    }
    //处理数组初始化
    else if (auto arrayType = dynamic_cast<ArrayType *>(targetType))
    {
        // 分配一块数组空间
        Value *arrayAlloca = createAlloca(targetType);

        // 遍历每个元素，递归初始化
        for (size_t i = 0; i < node->multiInitVal.size(); ++i)
        {
            // 计算当前元素的 GEP
            std::vector<Value *> indices;
            indices.push_back(new ConstantInt(IntegerType::getInstance(), 0)); // 数组变量的第一个索引总是0
            indices.push_back(new ConstantInt(IntegerType::getInstance(), i));

            // 递归初始化子元素
            Value *elemValue = visitInitExpr(node->multiInitVal[i], arrayType->ElementType);

            // 生成 GEP 指令
            auto gepInst = std::make_unique<GetElementPtrInst>(arrayAlloca, indices, getNextTempName());
            Value *elemPtr = gepInst.get();
            currentBlock->addInstruction(std::move(gepInst));

            // 存储元素值
            createStore(elemValue, elemPtr);
        }

        return arrayAlloca;
    }
    else
    {
        throw std::runtime_error("Array initialization: targetType is not array,line: " + std::to_string(node->line));
    }
}
// 新增重载 处理数组初始化表达式
// 支持平铺和嵌套初始化的递归数组初始化
void IRBuilder::visitInitExpr(std::shared_ptr<ast::InitExprNode> node, Type *targetType, Value *targetPtr) {
    std::vector<int> indices;
    size_t flat_idx = 0;
    std::vector<std::shared_ptr<ast::InitExprNode>> flat_inits;
    // 展开所有叶子节点
    flattenInitList(node, flat_inits);
    visitInitExprImpl(targetType, targetPtr, indices, flat_inits, flat_idx);
}

// 展开所有叶子节点到 flat_inits
void IRBuilder::flattenInitList(std::shared_ptr<ast::InitExprNode> node, std::vector<std::shared_ptr<ast::InitExprNode>>& flat_inits) {
    if (!node) return;
    if (node->singleInitVal) {
        flat_inits.push_back(node);
    } else {
        for (auto& child : node->multiInitVal) {
            flattenInitList(child, flat_inits);
        }
    }
}

void IRBuilder::visitInitExprImpl(Type *targetType, Value *targetPtr, std::vector<int>& indices, const std::vector<std::shared_ptr<ast::InitExprNode>>& flat_inits, size_t& flat_idx) {
    if (auto arrayType = dynamic_cast<ArrayType *>(targetType)) {
        int dim = arrayType->getNumElements();
        Type *elemType = arrayType->ElementType;
        for (int i = 0; i < dim; ++i) {
            indices.push_back(i);
            visitInitExprImpl(elemType, targetPtr, indices, flat_inits, flat_idx);
            indices.pop_back();
        }
    } else {
        // 到达最底层元素
        std::vector<Value *> gep_indices;
        gep_indices.push_back(new ConstantInt(IntegerType::getInstance(), 0));
        for (int idx : indices) {
            gep_indices.push_back(new ConstantInt(IntegerType::getInstance(), idx));
        }
        auto gepInst = std::make_unique<GetElementPtrInst>(targetPtr, gep_indices, getNextTempName());
        Value *elemPtr = gepInst.get();
        currentBlock->addInstruction(std::move(gepInst));
        Value *val;
        if (flat_idx < flat_inits.size() && flat_inits[flat_idx] && flat_inits[flat_idx]->singleInitVal) {
            val = visitExpression(flat_inits[flat_idx]->singleInitVal);
        } else {
            val = new ConstantInt(IntegerType::getInstance(), 0);
        }
        ++flat_idx;
        createStore(val, elemPtr);
    }
}
// 用于数组初始化 递归返回一个ConstantArray
Constant *IRBuilder::evaluateConstantArray(std::shared_ptr<ast::InitExprNode> node, ArrayType *arrayType) {
    std::vector<Constant*> elements;
    int dim = arrayType->getNumElements();
    Type *elemType = arrayType->ElementType;
    size_t i = 0;
    for (; i < node->multiInitVal.size(); ++i) {
        if (elemType->isArrayTy()) {
            elements.push_back(evaluateConstantArray(node->multiInitVal[i], static_cast<ArrayType*>(elemType)));
        } else {
            elements.push_back(evaluateConstantExpr(node->multiInitVal[i]->singleInitVal));
        }
    }
    // 补零
    for (; i < dim; ++i) {
        if (elemType->isArrayTy()) {
            elements.push_back(evaluateConstantArray(nullptr, static_cast<ArrayType*>(elemType)));
        } else if (elemType->isIntegerTy()) {
            elements.push_back(new ConstantInt(IntegerType::getInstance(), 0));
        } else if (elemType->isFloatTy()) {
            elements.push_back(new ConstantFloat(FloatType::getInstance(), 0.0f));
        }
    }
    return new ConstantArray(arrayType, elements);
}
// 这里需要判断int和float范围-->动态检测
Constant *IRBuilder::evaluateConstantExpr(std::shared_ptr<ast::ExprNode> node)
{
    if (!node) 
        throw std::runtime_error("Null expression in constant evaluation");

    // 整型字面量
    if (auto intLiteral = std::dynamic_pointer_cast<ast::IntLiteralExprNode>(node))
        return new ConstantInt(IntegerType::getInstance(), intLiteral->value);

    // 浮点字面量
    if (auto floatLiteral = std::dynamic_pointer_cast<ast::FloatLiteralExprNode>(node))
        return new ConstantFloat(FloatType::getInstance(), floatLiteral->value);

    // 常量二元表达式
    if (auto binExpr = std::dynamic_pointer_cast<ast::BinaryExprNode>(node)) {
        auto lhs = evaluateConstantExpr(binExpr->left);
        auto rhs = evaluateConstantExpr(binExpr->right);
        // 这里只处理 int/float 常量
        if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy()) {
            int l = static_cast<ConstantInt*>(lhs)->Value;
            int r = static_cast<ConstantInt*>(rhs)->Value;
            int res = 0;
            switch (binExpr->op) {
                case ast::BinaryOp::Add: res = l + r; break;
                case ast::BinaryOp::Sub: res = l - r; break;
                case ast::BinaryOp::Mul: res = l * r; break;
                case ast::BinaryOp::Div: res = l / r; break;
                case ast::BinaryOp::Mod: res = l % r; break;
                default: throw std::runtime_error("Unsupported op in const int expr");
            }
            return new ConstantInt(IntegerType::getInstance(), res);
        } else if (lhs->getType()->isFloatTy() || rhs->getType()->isFloatTy()) {
            float l = lhs->getType()->isFloatTy() ? static_cast<ConstantFloat*>(lhs)->Value : static_cast<ConstantInt*>(lhs)->Value;
            float r = rhs->getType()->isFloatTy() ? static_cast<ConstantFloat*>(rhs)->Value : static_cast<ConstantInt*>(rhs)->Value;
            float res = 0;
            switch (binExpr->op) {
                case ast::BinaryOp::Add: res = l + r; break;
                case ast::BinaryOp::Sub: res = l - r; break;
                case ast::BinaryOp::Mul: res = l * r; break;
                case ast::BinaryOp::Div: res = l / r; break;
                default: throw std::runtime_error("Unsupported op in const float expr");
            }
            return new ConstantFloat(FloatType::getInstance(), res);
        }
    }
    // 常量变量引用（只允许 const 变量）
    if (auto lval = std::dynamic_pointer_cast<ast::LValueExprNode>(node)) {
        auto it = constVarInitValues.find(lval->identifier);
        if (it == constVarInitValues.end())
            throw std::runtime_error("Non-constant variable in constant expression: " + lval->identifier);
        if(auto constInt= dynamic_cast<ConstantInt*>(it->second)) {
            return constInt;
        } else if(auto constFloat = dynamic_cast<ConstantFloat*>(it->second)) {
            return constFloat;
        } else if(auto constArray = dynamic_cast<ConstantArray*>(it->second)) {
            auto indices= lval->indices;
            auto indice_size=lval->indices.size();
            auto tmp_array=constArray;
            //获取元素
            for(int i=0;i<indice_size-2;i++){
                auto j=getExpressionConstantValue(indices[i]);
                tmp_array=dynamic_cast<ConstantArray*>(tmp_array->Elements[j]);
                //转换失败:常量计算不允许指针操作
                if(tmp_array==nullptr)
                {
                    throw std::runtime_error("Point is not allowed to appear in constant expression");
                }
            }
            // 最后一维
            return tmp_array->Elements[getExpressionConstantValue(indices[indice_size-1])];
        }
    }

    throw std::runtime_error("Non-constant expression in constant context ,line: " + std::to_string(node->line));
}
int IRBuilder::getExpressionConstantValue(std::shared_ptr<ast::ExprNode> node){
    auto value=evaluateConstantExpr(node);
    if(auto int_value=dynamic_cast<ConstantInt*>(value)){
        return int_value->Value;
    }
    else if(auto float_value=dynamic_cast<ConstantFloat*>(value)){
        return (int)float_value->Value;
    }
    else{
        throw std::runtime_error("Unsupported constant expression type in getExpressionConstantValue");
    }
}
bool IRBuilder::isConstVariable(Value *value){
    auto identifier=value->getName();
    auto it=constVarInitValues.find(identifier);
    if(it==constVarInitValues.end())return false;
    return true;
}
// ===== 基本块管理 =====
BasicBlock *IRBuilder::createBasicBlock(const std::string &name)
{
    std::string actualName = name.empty() ? getNextLabelName() : name;
    return currentFunction->addBasicBlock(actualName);
}

void IRBuilder::setCurrentBlock(BasicBlock *block)
{
    currentBlock = block;
}

// ===== 指令生成辅助 =====
Value *IRBuilder::createBinaryOp(ast::BinaryOp op, Value *lhs, Value *rhs)
{
    Opcode opcode;
    bool isFloat = lhs->getType()->isFloatTy();

    switch (op)
    {
    case BinaryOp::Add:
        opcode = isFloat ? Opcode::FAdd : Opcode::Add;
        break;
    case BinaryOp::Sub:
        opcode = isFloat ? Opcode::FSub : Opcode::Sub;
        break;
    case BinaryOp::Mul:
        opcode = isFloat ? Opcode::FMul : Opcode::Mul;
        break;
    case BinaryOp::Div:
        opcode = isFloat ? Opcode::FDiv : Opcode::SDiv;
        break;
    case BinaryOp::Mod:
        if (isFloat)
            throw std::runtime_error("Modulo not supported for float");
        opcode = Opcode::SRem;
        break;
    default:
        throw std::runtime_error("Invalid binary operator");
    }

    auto binOp = std::make_unique<BinaryOperator>(opcode, lhs, rhs, getNextTempName());
    Value *result = binOp.get();
    currentBlock->addInstruction(std::move(binOp));
    return result;
}

Value *IRBuilder::createComparison(ast::BinaryOp op, Value *lhs, Value *rhs)
{
    bool isFloat = lhs->getType()->isFloatTy();

    if (isFloat)
    {
        FCmpInst::Predicate pred;
        switch (op)
        {
        case BinaryOp::Lt:
            pred = FCmpInst::FCMP_OLT;
            break;
        case BinaryOp::Gt:
            pred = FCmpInst::FCMP_OGT;
            break;
        case BinaryOp::Le:
            pred = FCmpInst::FCMP_OLE;
            break;
        case BinaryOp::Ge:
            pred = FCmpInst::FCMP_OGE;
            break;
        case BinaryOp::Eq:
            pred = FCmpInst::FCMP_OEQ;
            break;
        case BinaryOp::Ne:
            pred = FCmpInst::FCMP_ONE;
            break;
        default:
            throw std::runtime_error("Invalid comparison operator");
        }

        auto fcmp = std::make_unique<FCmpInst>(pred, lhs, rhs, getNextTempName());
        Value *result = fcmp.get();
        currentBlock->addInstruction(std::move(fcmp));
        return result;
    }
    else
    {
        ICmpInst::Predicate pred;
        switch (op)
        {
        case BinaryOp::Lt:
            pred = ICmpInst::ICMP_SLT;
            break;
        case BinaryOp::Gt:
            pred = ICmpInst::ICMP_SGT;
            break;
        case BinaryOp::Le:
            pred = ICmpInst::ICMP_SLE;
            break;
        case BinaryOp::Ge:
            pred = ICmpInst::ICMP_SGE;
            break;
        case BinaryOp::Eq:
            pred = ICmpInst::ICMP_EQ;
            break;
        case BinaryOp::Ne:
            pred = ICmpInst::ICMP_NE;
            break;
        default:
            throw std::runtime_error("Invalid comparison operator");
        }

        auto icmp = std::make_unique<ICmpInst>(pred, lhs, rhs, getNextTempName());
        Value *result = icmp.get();
        currentBlock->addInstruction(std::move(icmp));
        return result;
    }
}

Value *IRBuilder::createUnaryOp(ast::UnaryOp op, Value *operand)
{
    switch (op)
    {
    case UnaryOp::Plus:
        return operand; // +x 就是 x
    case UnaryOp::Minus:
    {
        // -x 等价于 0 - x
        Value *zero;
        if (operand->getType()->isFloatTy())
        {
            zero = new ConstantFloat(FloatType::getInstance(), 0.0f);
        }
        else
        {
            zero = new ConstantInt(IntegerType::getInstance(), 0);
        }
        return createBinaryOp(BinaryOp::Sub, zero, operand);
    }
    case UnaryOp::Not:
    {
        // !x 等价于 x == 0
        Value *zero;
        if (operand->getType()->isFloatTy())
        {
            zero = new ConstantFloat(FloatType::getInstance(), 0.0f);
        }
        else
        {
            zero = new ConstantInt(IntegerType::getInstance(), 0);
        }
        return createComparison(BinaryOp::Eq, operand, zero);
    }
    default:
        throw std::runtime_error("Invalid unary operator");
    }
}

Value *IRBuilder::createLoad(Value *ptr)
{
    auto loadInst = std::make_unique<LoadInst>(ptr, getNextTempName());
    Value *result = loadInst.get();
    currentBlock->addInstruction(std::move(loadInst));
    return result;
}

void IRBuilder::createStore(Value *value, Value *ptr)
{
    auto storeInst = std::make_unique<StoreInst>(value, ptr);
    currentBlock->addInstruction(std::move(storeInst));
}

Value *IRBuilder::createAlloca(Type *type, const std::string &name)
{
    auto allocaInst = std::make_unique<AllocaInst>(type, name.empty() ? getNextTempName() : name);
    Value *result = allocaInst.get();
    currentBlock->addInstruction(std::move(allocaInst));
    return result;
}

Value *IRBuilder::createCall(Function *func, const std::vector<Value *> &args)
{  
    auto callInst = std::make_unique<CallInst>(func, args, getNextTempName());
    Value *result = callInst.get();
    currentBlock->addInstruction(std::move(callInst));
    return result;
}

void IRBuilder::createBranch(BasicBlock *target)
{
    auto brInst = std::make_unique<BranchInst>(target);
    currentBlock->addInstruction(std::move(brInst));

    // 更新 CFG
    currentBlock->addSuccessor(target);
    target->addPredecessor(currentBlock);
}

void IRBuilder::createCondBranch(Value *condition, BasicBlock *trueBlock, BasicBlock *falseBlock)
{
    auto brInst = std::make_unique<BranchInst>(condition, trueBlock, falseBlock);
    currentBlock->addInstruction(std::move(brInst));

    // 更新 CFG
    currentBlock->addSuccessor(trueBlock);
    currentBlock->addSuccessor(falseBlock);
    trueBlock->addPredecessor(currentBlock);
    falseBlock->addPredecessor(currentBlock);
}

void IRBuilder::createReturn(Value *value)
{
    auto retInst = value ? std::make_unique<ReturnInst>(value) : std::make_unique<ReturnInst>();
    currentBlock->addInstruction(std::move(retInst));
}

PHINode *IRBuilder::createPhi(Type *type, const std::string &name)
{
    std::string actualName = name.empty() ? getNextTempName() : name;
    auto phiInst = std::make_unique<PHINode>(type, actualName);
    PHINode *result = phiInst.get();
    currentBlock->addInstruction(std::move(phiInst));
    return result;
}

// ===== 类型转换 ===== 修改该函数以支持int a[][10]这种情况，第一维度默认-1,此时退化为指针
Type *IRBuilder::convertASTTypeToIRType(const ast::DataType &astType,bool isFunctionParam)
{
    switch (astType.baseType)
    {
    case PrimaryDataType::INT:
        if (astType.isArray())
        {
            Type *elemType = IntegerType::getInstance();
            const auto &sizes = astType.arraySizes();
            if(isFunctionParam)
            {
                for (int i = sizes.size() - 1; i >=1; i--)
                {
                    elemType = new ArrayType(elemType, getExpressionConstantValue(sizes[i]));
                }
                return new PointerType(elemType);
            }
            for(int i=sizes.size()-1;i>=0;i--)
            {
                elemType=new ArrayType(elemType,getExpressionConstantValue(sizes[i]));
            }
            return elemType;
        }
        return IntegerType::getInstance();
    case PrimaryDataType::FLOAT:
        if (astType.isArray())
        {
            Type *elemType = FloatType::getInstance();
            const auto &sizes = astType.arraySizes();
            if(isFunctionParam)
            {
                for (int i = sizes.size() - 1; i >=1; i--)
                {
                    elemType = new ArrayType(elemType, getExpressionConstantValue(sizes[i]));
                }
                return new PointerType(elemType);
            }
            for(int i=sizes.size()-1;i>=0;i--)
            {
                elemType=new ArrayType(elemType,getExpressionConstantValue(sizes[i]));
            }
            return elemType;          
        }
        return FloatType::getInstance();
    case PrimaryDataType::VOID:
        return VoidType::getInstance();
    default:
        throw std::runtime_error("Unsupported type in convertASTtoIR" );
    }
}

Value *IRBuilder::createCast(Value *value, Type *targetType)
{
    Type *srcType = value->getType();

    if (srcType == targetType)
    {
        return value;
    }
    Opcode castOp;
    if (srcType->isIntegerTy() && targetType->isFloatTy())
    {
        castOp = Opcode::SIToFP;
    }
    else if (srcType->isFloatTy() && targetType->isIntegerTy())
    {
        castOp = Opcode::FPToSI;
    }
    else if (srcType->isPointerTy() && targetType->isPointerTy()) {
        if (srcType->isTypeEqual(srcType, targetType)) 
        {
            return value; // 指针类型一致，直接返回
        }
        throw std::runtime_error("Unsupported pointer type cast");
    }
    // 不支持的类型转换
    else
    {
        throw std::runtime_error("Unsupported type conversion in creatcast:" + to_string(srcType->getTypeID()) + " to " + to_string(targetType->getTypeID()));
    }

    auto castInst = std::make_unique<CastInst>(castOp, value, targetType, getNextTempName());
    Value *result = castInst.get();
    currentBlock->addInstruction(std::move(castInst));
    return result;
}

Value *IRBuilder::convertToBool(Value *value)
{
    // 将值转换为布尔值（非零为真，零为假）
    Value *zero;
    if (value->getType()->isFloatTy())
    {
        zero = new ConstantFloat(FloatType::getInstance(), 0.0f);
    }
    else if (value->getType()->isIntegerTy())
    {
        zero = new ConstantInt(IntegerType::getInstance(), 0);
    }
    else if (value->getType()->isBooleanTy())
    {
        return value; // 已经是布尔值
    }
    else
    {
        throw std::runtime_error("Cannot convert to bool");
    }

    return createComparison(BinaryOp::Ne, value, zero);
}