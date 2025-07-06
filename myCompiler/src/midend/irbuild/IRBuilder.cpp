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
        Vector<Type*> params = { new PointerType(IntegerType::getInstance()) };
        FunctionType* funcType = new FunctionType(IntegerType::getInstance(), params);
        module->addFunction(funcType, "getarray");
    }
    // int getfarray(float a[]);
    {
        Vector<Type*> params = { new PointerType(FloatType::getInstance()) };
        FunctionType* funcType = new FunctionType(IntegerType::getInstance(), params);
        module->addFunction(funcType, "getfarray");
    }
    // void putint(int a);
    {
        Vector<Type*> params = { IntegerType::getInstance() };
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putint");
    }
    // void putch(int a);
    {
        Vector<Type*> params = { IntegerType::getInstance() };
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putch");
    }
    // void putfloat(float a);
    {
        Vector<Type*> params = { FloatType::getInstance() };
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putfloat");
    }
    // void putarray(int n, int a[]);
    {
        Vector<Type*> params = { IntegerType::getInstance(), new PointerType(IntegerType::getInstance()) };
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putarray");
    }
    // void putfarray(int n, float a[]);
    {
        Vector<Type*> params = { IntegerType::getInstance(), new PointerType(FloatType::getInstance()) };
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putfarray");
    }
    // void putf(string a); 这里假设 string 用 i8* 表示
    {
        Vector<Type*> params = { StringType::getInstance() }; 
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putf");
    }
    // void starttime();
    {
        //传入行号
        Vector<Type*> params = { IntegerType::getInstance() };
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "_sysy_starttime");
    }
    // void stoptime();
    {
        Vector<Type*> params = { IntegerType::getInstance() };
        FunctionType* funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "_sysy_stoptime");
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
    Vector<Type *> paramTypes;
    for (auto &param : node->params)
    {
        paramTypes.push_back(convertASTTypeToIRType(param->type,true));
    }
    FunctionType *funcType = new FunctionType(retType, paramTypes);
    Function *func = module->addFunction(funcType, node->identifier);
    currentFunction = func;

    // 创建入口基本块
    //entry
    BasicBlock *entryBlock = createBasicBlock();
    setCurrentBlock(entryBlock);

    // 进入新的作用域 访问参数之前调用，防止形参实参之间干扰或者不同函数形参名相同产生干扰
    varToValueStack.push(varToValue);

    // 添加参数并为每个参数创建 alloca
    for (size_t i = 0; i < node->params.size(); i++)
    {
        Argument *arg = func->addArgument(paramTypes[i], node->params[i]->identifier);
        varToValue[node->params[i]->identifier] = arg;
        basicBlockVarToValue[currentBlock][node->params[i]->identifier] = arg;
    }

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
            if(!hasTerminatorInst(currentBlock))
            {
                throw std::runtime_error("Non-void function must return a value");    
            }

        }
    }

    // 退出作用域
    varToValue = varToValueStack.top();
    varToValueStack.pop();
    currentFunction = nullptr;
}
void IRBuilder::visitBlock(std::shared_ptr<ast::BlockStmtNode> node, bool isRestore)
{
    // 1. 记录外层变量名
    std::unordered_set<String> outerVars;
    for (auto& [name, _] : varToValue) outerVars.insert(name);

    // 2. 进入新作用域
    varToValueStack.push(varToValue);

    // 3. 访问 block 内所有语句
    for (auto &stmt : node->stmts) 
    {
        visitStatement(stmt);
        if (currentBlock->hasTerminator()) break;
    }
    // 恢复作用域
    auto innerVarToValue = varToValue;
    varToValue = varToValueStack.top();
    varToValueStack.pop();
    // 只在 isRestore 为真时写回外层变量
    if (isRestore) {
        for (const auto& name : outerVars) 
        {
            if (innerVarToValue.count(name) && !isBlockNewDeclaredVar(name)) 
            {
                varToValue[name] = innerVarToValue[name];
                //basicBlockVarToValue[currentBlock][name] = innerVarToValue[name];
            }
        }
    }
    blockNewDeclaredVars.clear(); // 清空当前块新声明的变量列表
}

void IRBuilder::visitStatement(std::shared_ptr<ast::StmtNode> node,bool isRestore)
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
        visitBlock(blockStmt,isRestore);
    }
}

void IRBuilder::visitDeclStmt(std::shared_ptr<ast::DeclStmtNode> node)
{
    Type *varType = convertASTTypeToIRType(node->type,false);
    //如果当前已经定义了同名变量，则记录，退出作用域时不将该变量的ssa值写出
    if(varToValue.find(node->identifier) != varToValue.end())
    {
        blockNewDeclaredVars.push_back(node->identifier);
    }
    if (currentFunction == nullptr)
    {
        // 全局变量
        Constant *initializer = nullptr;
        // 检查数组维度是否合法
        if(varType->isArrayTy())
        {
            for(auto it:node->type.arraySizes())
            {
                int indice=getExpressionConstantValue(it);
                if(indice<=0)throw std::runtime_error("Array indices is not allowed to be less than zero");
            }
        }
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
        if(varType->isArrayTy())
        {
            //数组用内存模型
            Value *alloca = createAlloca(varType);
            //转回原类型
            LoadInst* loadinst=new LoadInst(alloca,alloca->getName());
            varToValue[node->identifier] = loadinst;
            for(auto it:node->type.arraySizes())
            {
                int indice=getExpressionConstantValue(it);
                if(indice<=0)throw std::runtime_error("Array indices is not allowed to be less than zero");
            }
            if (node->initializer && !node->type.isConst())
            {
                visitInitExpr(node->initializer, varType, alloca);
            }
            else if (node->initializer && node->type.isConst())
            {
                Constant *initializer = evaluateConstantArray(node->initializer, static_cast<ArrayType*>(varType));
                constVarInitValues[node->identifier] = initializer;
            }            
        }
        else
        {
            // 标量变量直接用SSA
            Value *initValue = nullptr;
            if (node->initializer && !node->type.isConst())
            {
                initValue = visitInitExpr(node->initializer, varType);
            }
            else if (node->type.isConst() && node->initializer)
            {
                Constant *initializer = evaluateConstantExpr(node->initializer->singleInitVal);
                initValue = initializer;
                constVarInitValues[node->identifier] = initializer;
            }
            else
            {
                // 默认初值
                if (varType->isIntegerTy())
                    initValue = new ConstantInt(IntegerType::getInstance(), 0);
                else if (varType->isFloatTy())
                    initValue = new ConstantFloat(FloatType::getInstance(), 0.0f);
            }       
            // 将初始值存储到 varToValue 中
            varToValue[node->identifier] = initValue;
        }
    }
}

void IRBuilder::visitAssignStmt(std::shared_ptr<ast::AssignStmtNode> node)
{
    Value *lvalue = visitLValueExpr(node->lvalue);
    Value *rvalue = visitExpression(node->rvalue);
    
    if (rvalue->getType() != lvalue->getType())
    {
        rvalue = createCast(rvalue, lvalue->getType());
    }
    AllocaInst *allocaInst =new AllocaInst(lvalue->getType(),lvalue->getName());
    if(lvalue->getType()->isPointerTy()||(node->lvalue->indices.size()>0))
    {
        //如果是指针类型使用store
        createStore(rvalue, allocaInst);
    }
    else
    {
        varToValue[node->lvalue->identifier] = rvalue;
        basicBlockVarToValue[currentBlock][node->lvalue->identifier] = rvalue;
        //std::cout<<currentBlock->getName()<<":"<<node->lvalue->identifier<<" assign "<<rvalue->getName()<<std::endl;
        // 如果是标量变量，直接更新SSA值
    }
}

void IRBuilder::visitExprStmt(std::shared_ptr<ast::ExprStmtNode> node)
{
    visitExpression(node->expr);
}

void IRBuilder::visitIfElseStmt(std::shared_ptr<ast::IfElseStmtNode> node)
{
    Value *condition = visitExpression(node->condition); 
    //if.then
    BasicBlock *thenBlock = createBasicBlock();
    //if.else
    BasicBlock *elseBlock = node->else_body ? createBasicBlock() : nullptr;
    //if.end
    BasicBlock *mergeBlock = createBasicBlock();
    // 记录分支前变量状态
    auto tmp_block=currentBlock;
    // 生成phi占位
    setCurrentBlock(mergeBlock);
    addPhiForVars(); 
    setCurrentBlock(tmp_block);
    // 条件跳转
    createCondBranch(condition, thenBlock, elseBlock ? elseBlock : mergeBlock);
    // then 分支
    setCurrentBlock(thenBlock);
    visitStatement(node->then_body,false);
    blockNewDeclaredVars.clear(); // 清空当前块新声明的变量列表
    if (!currentBlock->hasTerminator())
    {
        createBranch(mergeBlock);
    }
    if (elseBlock)
    {
        setCurrentBlock(elseBlock);
        visitStatement(node->else_body,false);
        if (!currentBlock->hasTerminator())
        {
            createBranch(mergeBlock);
        }
    }
    blockNewDeclaredVars.clear(); // 清空当前块新声明的变量列表
    // 合流块
    setCurrentBlock(mergeBlock);
    //  插入phi输入
    addPhiForVarsIncomings(currentBlock);
}

void IRBuilder::visitWhileStmt(std::shared_ptr<ast::WhileStmtNode> node)
{
    //while.cond
    BasicBlock *condBlock = createBasicBlock();
    //生成phi占位
    auto tmpblock=currentBlock;
    setCurrentBlock(condBlock); 
    //更新当前块的变量映射,原因为condblock是第一块
    addPhiForVars();
    setCurrentBlock(tmpblock);
    //while.body
    BasicBlock *bodyBlock = createBasicBlock();
    //while.end
    BasicBlock *exitBlock = createBasicBlock(); 
    // 跳转到条件判断块
    createBranch(condBlock);
    // 设置当前块为条件判断块
    setCurrentBlock(condBlock); 
    // 生成条件表达式的 IR
    Value *condition = visitExpression(node->condition);
    createCondBranch(condition, bodyBlock, exitBlock);

    // 进入循环体
    setCurrentBlock(bodyBlock);
    loopStack.push(LoopContext(condBlock, exitBlock));
    visitStatement(node->body,false);
    loopStack.pop(); 
    //  如果循环体没有提前 return/break，循环体结尾跳回条件判断块
    if (!currentBlock->hasTerminator())
    {
        createBranch(condBlock);
    }
    //  回到 condBlock
    setCurrentBlock(condBlock); 
    //  插入phi输入
    addPhiForVarsIncomings(currentBlock);
    //  设置当前块为循环结束块
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

// ===== 表达式访问实现 ===== 
Value *IRBuilder::visitExpression(std::shared_ptr<ast::ExprNode> node)
{
    if(node== nullptr)
    {
        return nullptr; // 如果节点为空，直接返回 nullptr
    }
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
        // 返回指针或int/float
        Value *ptr = visitLValueExpr(lvalueExpr);
        // 如果是数组退化为指针（如 int a[] 作为参数），直接返回指针，不 load
        if (ptr->getType()->isPointerTy()) 
        {
            return ptr;
        }
        // 如果是数组 使用load加载
        if(lvalueExpr->indices.size() > 0)
        {
            AllocaInst *allocaInst =new AllocaInst(ptr->getType(),ptr->getName());
            auto value = createLoad(allocaInst);
            return value;
        }
        // 如果是标量 返回
        return ptr;
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
        BasicBlock *lhsBlock = currentBlock;
        //"logical.rhs"
        BasicBlock *rhsBlock = createBasicBlock();
        //"logical.end"
        BasicBlock *mergeBlock = createBasicBlock();


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
        PhiInst *phi = createPhi(BooleanType::getInstance());
        phi->addIncoming(new ConstantBool(BooleanType::getInstance(),false), lhsBlock); // true from lhs
        phi->addIncoming(rhsCond, rhsEndBlock);                                     // result from rhs

        return phi;
    }
    else if (node->op == BinaryOp::Or)
    {
        // a || b: 如果 a 为 true，直接返回 true，否则计算 b
        BasicBlock *lhsBlock = currentBlock;
        //"logical.rhs"
        BasicBlock *rhsBlock = createBasicBlock();
        //"logical.end"
        BasicBlock *mergeBlock = createBasicBlock();


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
        PhiInst *phi = createPhi(BooleanType::getInstance());
        phi->addIncoming(new ConstantBool(BooleanType::getInstance(),true), lhsBlock); // true from lhs
        phi->addIncoming(rhsCond, rhsEndBlock);                                     // result from rhs
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
        Vector<Value *> indices;
        // 如果原类型为数组，则加0解引用
        if (ptr->getType()->isArrayTy()) 
        {
            indices.push_back(new ConstantInt(IntegerType::getInstance(), 0));
        }
        // 如果是指针则不进行操作
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
    // 如果是数组类型且无下标，自动退化为指针（GEP 0,0)
    if (ptr->getType()->isArrayTy()) {
        Vector<Value *> indices;
        indices.push_back(new ConstantInt(IntegerType::getInstance(), 0));
        //获取到原来维度大小
        auto it=dynamic_cast<ArrayType*>(ptr->getType());
        int indice=it->getNumElements();
        auto gepInst = std::make_unique<GetElementPtrInst>(ptr, indices, getNextTempName());
        Value *result = gepInst.get();
        currentBlock->addInstruction(std::move(gepInst));
        return result;
    }
    // 无下标且不是数组则直接返回指针   
    return ptr;
}

Value *IRBuilder::visitCallExpr(std::shared_ptr<ast::CallExprNode> node)
{
    Function *func = module->getFunction(node->callee);
    if (!func)
    {
        throw std::runtime_error("Undefined function: " + node->callee+",line:"+ std::to_string(node->line));
    }
    Vector<Value *> args;
    // _sysy_starttime 和 _sysy_stoptime 函数单独处理
    if(func->getName()=="_sysy_starttime" || func->getName()=="_sysy_stoptime")
    {
        //传入行号
        args.push_back(new ConstantInt(IntegerType::getInstance(), node->line));
        return createCall(func, args);
    }
    // 其他函数的处理
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
                Vector<Value*> indices;
                indices.push_back(new ConstantInt(IntegerType::getInstance(), 0));
                auto gepInst = std::make_unique<GetElementPtrInst>(arg, indices, getNextTempName());
                Value *result = gepInst.get();
                currentBlock->addInstruction(std::move(gepInst));
                arg = result;
            }
            else
            {
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
            Vector<Value *> indices;
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
    Vector<int> indices;
    size_t flat_idx = 0;
    Vector<std::shared_ptr<ast::InitExprNode>> flat_inits;

    // 展平所有叶子节点，用于底层赋值
    flattenInitList(node, flat_inits);

    // 计算数组总元素个数（支持多维）
    size_t totalElements = getArrayTotalElements(targetType);
    if (flat_inits.size() > totalElements) {
        throw std::runtime_error("Initializer list has more elements than array dimension");
    }

    // 递归处理初始化并检查每一维的初始化项数量
    visitInitExprImpl(targetType, targetPtr, indices, node, flat_inits, flat_idx);
}

// 展开所有叶子节点到 flat_inits
void IRBuilder::flattenInitList(std::shared_ptr<ast::InitExprNode> node, Vector<std::shared_ptr<ast::InitExprNode>>& flat_inits) {
    if (!node) return;
    if (node->singleInitVal) {
        flat_inits.push_back(node);
    } else {
        for (auto& child : node->multiInitVal) {
            flattenInitList(child, flat_inits);
        }
    }
}

void IRBuilder::visitInitExprImpl(Type *targetType, Value *targetPtr,
                                  Vector<int>& indices,
                                  std::shared_ptr<ast::InitExprNode> initNode,
                                  const Vector<std::shared_ptr<ast::InitExprNode>>& flat_inits,
                                  size_t& flat_idx) {
    if (auto arrayType = dynamic_cast<ArrayType *>(targetType)) 
    {
        int dim = arrayType->getNumElements();
        Type *elemType = arrayType->ElementType;

        auto children = getChildrenAtCurrentLevel(initNode);

        for (int i = 0; i < dim; ++i) 
        {
            indices.push_back(i);
            auto childNode = (i < children.size()) ? children[i] : nullptr;
            visitInitExprImpl(elemType, targetPtr, indices, childNode, flat_inits, flat_idx);
            indices.pop_back();
        }
    } 
    else 
    {
        // 到达最底层元素
        Vector<Value *> gep_indices;
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
    Vector<Constant*> elements;
    int dim = arrayType->getNumElements();
    Type *elemType = arrayType->ElementType;
    size_t i = 0;
    if(node)
    {
        for (; i < node->multiInitVal.size(); ++i) {
            if (elemType->isArrayTy()) 
            {
                elements.push_back(evaluateConstantArray(node->multiInitVal[i], static_cast<ArrayType*>(elemType)));
            } 
            else 
            {
                elements.push_back(evaluateConstantExpr(node->multiInitVal[i]->singleInitVal));
            }
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
Constant *IRBuilder::evaluateConstantExpr(std::shared_ptr<ast::ExprNode> node)
{
    if (!node) 
        throw std::runtime_error("Null expression in constant evaluation");

    // 整型字面量
    if (auto intLiteral = std::dynamic_pointer_cast<ast::IntLiteralExprNode>(node))
        return new ConstantInt(IntegerType::getInstance(), intLiteral->value);

    // 浮点字面量
    else if (auto floatLiteral = std::dynamic_pointer_cast<ast::FloatLiteralExprNode>(node))
        return new ConstantFloat(FloatType::getInstance(), floatLiteral->value);

    // 常量二元表达式
    else if (auto binExpr = std::dynamic_pointer_cast<ast::BinaryExprNode>(node)) {
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
    else if(auto uval=std::dynamic_pointer_cast<ast::UnaryExprNode>(node))
    {
        auto operand=evaluateConstantExpr(uval->operand);
        if(operand->getType()->isIntegerTy())
        {
            int v=static_cast<ConstantInt*>(operand)->Value;
            int res=0;
            switch(uval->op)
            {
                case ast::UnaryOp::Plus:res=v;break;
                case ast::UnaryOp::Minus:res=0-v;break;
                default:throw std::runtime_error("Unsupported op in const int expr");
            }
            return  new ConstantInt(IntegerType::getInstance(), res);
        }else if(operand->getType()->isFloatTy())
        {
            float v=static_cast<ConstantFloat*>(operand)->Value;
            float res=0;
            switch(uval->op)
            {
                case ast::UnaryOp::Plus:res=v;break;
                case ast::UnaryOp::Minus:res=0-v;break;
                default:throw std::runtime_error("Unsupported op in const float expr");
            }
            return new ConstantFloat(FloatType::getInstance(), res);
        }
    }
    // 常量变量引用（只允许 const 变量）
    else if (auto lval = std::dynamic_pointer_cast<ast::LValueExprNode>(node)) {
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
bool IRBuilder::isConstVariable(string name){
    auto it=constVarInitValues.find(name);
    if(it==constVarInitValues.end())return false;
    return true;
}
// ===== 基本块管理 =====
BasicBlock *IRBuilder::createBasicBlock(const String &name)
{
    String actualName = (name.empty()||name=="") ? getNextLabelName() : name;
    auto basicblock=currentFunction->addBasicBlock(actualName);
    // 复制符号表
    basicBlockVarToValue[basicblock]=varToValue;
    return basicblock;
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
Value *IRBuilder::createAlloca(Type *type, const String &name)
{
    auto allocaInst = std::make_unique<AllocaInst>(type, name.empty() ? getNextTempName() : name);
    Value *result = allocaInst.get();
    currentBlock->addInstruction(std::move(allocaInst));
    return result;
}

Value *IRBuilder::createCall(Function *func, const Vector<Value *> &args)
{  
    auto callInst = std::make_unique<CallInst>(func, args, getNextTempName());
    Value *result = callInst.get();
    currentBlock->addInstruction(std::move(callInst));
    return result;
}

void IRBuilder::createBranch(BasicBlock *target)
{
    // 先记录当前块结束时的变量SSA
    //basicBlockVarToValue[currentBlock] = varToValue;
    auto brInst = std::make_unique<BranchInst>(target);
    currentBlock->addInstruction(std::move(brInst));

    // 更新 CFG
    currentBlock->addSuccessor(target);
    target->addPredecessor(currentBlock);
}

void IRBuilder::createCondBranch(Value *condition, BasicBlock *trueBlock, BasicBlock *falseBlock)
{
    // 先记录当前块结束时的变量SSA
    //basicBlockVarToValue[currentBlock] = varToValue;
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
    // 记录当前块结束时的变量SSA
    //basicBlockVarToValue[currentBlock] = varToValue;

    auto retInst = value ? std::make_unique<ReturnInst>(value) : std::make_unique<ReturnInst>();
    currentBlock->addInstruction(std::move(retInst));
}

PhiInst *IRBuilder::createPhi(Type *type, const String &name)
{
    std::string actualName = name.empty() ? getNextTempName() : name;
    auto phiInst = std::make_unique<PhiInst>(type, actualName);
    auto *result = phiInst.get();
    currentBlock->addInstruction(std::move(phiInst));
    return result;
}
// ===== 类型转换 ===== 做函数参数数组自动退化为指针
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
        auto srcPtrType=dynamic_cast<PointerType*>(srcType);
        auto targetPtrType=dynamic_cast<PointerType*>(targetType);
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
size_t IRBuilder::getArrayTotalElements(Type* type) {
    if (auto arrayType = dynamic_cast<ArrayType*>(type)) {
        return arrayType->getNumElements() * getArrayTotalElements(arrayType->ElementType);
    } else {
        return 1;
    }
}
Vector<shared_ptr<ast::InitExprNode>> IRBuilder::getChildrenAtCurrentLevel(
    shared_ptr<ast::InitExprNode> node) {
    if (!node) return {};
    if (node->multiInitVal.empty()) {
        return {node}; // 单个值视为一个子项
    } else {
        return node->multiInitVal; // 多个子项
    }
}
bool IRBuilder::hasTerminatorInst(BasicBlock *block){
    if(block->hasTerminator())return true;
    else
    {
        bool result=true;
        for(auto pre:block->Predecessors)
        {
            if(pre->Parent!=currentFunction)return false;
            result=hasTerminatorInst(pre);
            if(!result)return result;
        }
        return result;
    }
}
void IRBuilder::addPhiForVars()
{
    for (const auto& [name, value] : varToValue)
    {
                //普通变量
        if (!(value->getType()->isPointerTy()||value->getType()->isArrayTy()||isConstVariable(name))) 
        {
            PhiInst* phi = createPhi(value->getType());
            varToValue[name] = phi; // 更新 SSA 值为 PHI 节点
            basicBlockVarToValue[currentBlock][name] = phi; // 更新当前块的变量映射
        }
    }   
}
void IRBuilder::addPhiForVarsIncomings(BasicBlock *block)
{
    // 遍历合流块所有变量
    for (const auto& [name, value] : basicBlockVarToValue[block]) {
        // 只处理 phi
        auto phi = dynamic_cast<PhiInst*>(value);
        if (!phi) continue;
        // 遍历所有前驱块
        for (auto pred : block->getPredecessors()) {
            // 如果前驱块有该变量的 SSA 值
            auto it = basicBlockVarToValue[pred].find(name);
            if (it != basicBlockVarToValue[pred].end()&&it->second != value) {
                phi->IncomingValues.push_back({it->second, pred});
            }
            // 如果没有，说明该变量在该前驱块未定义，可以补默认值或报错（视 SSA 设计而定）
        }
    }
}