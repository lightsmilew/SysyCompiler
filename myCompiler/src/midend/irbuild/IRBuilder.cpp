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
        //此处已退化数组为指针
        paramTypes.push_back(convertASTTypeToIRType(param->type,true));
    }
    FunctionType *funcType = new FunctionType(retType, paramTypes);
    Function *func = module->addFunction(funcType, node->identifier);
    currentFunction = func;

    // 创建入口基本块
    // entry
    string entryblock_name=debugMode?"entry":"";
    BasicBlock *entryBlock = createBasicBlock(entryblock_name);
    setCurrentBlock(entryBlock);

    // 进入新的作用域 访问参数之前调用，防止形参实参之间干扰或者不同函数形参名相同产生干扰
    varToValueStack.push(varToValue);

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
                throw std::runtime_error("Non-void function must return a value,line: " + std::to_string(node->body->line));    
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
    // 记录外层变量名
    std::unordered_set<String> outerVars;
    for (auto& [name, _] : varToValue) outerVars.insert(name);

    // 进入新作用域
    varToValueStack.push(varToValue);

    // 访问 block 内所有语句
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
            }
        }
    }
    NewDeclaredVarsInBlock.clear(); // 清空当前块新声明的变量列表
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
    // 如果当前已经定义了同名变量，则记录，退出作用域时不将该变量的ssa值写出
    if(varToValue.count(node->identifier))
    {
        NewDeclaredVarsInBlock.push_back(node->identifier);
    }
    // 检查数组维度是否合法
    if(varType->isArrayTy())
    {
        for(auto it:node->type.arraySizes())
        {
            int indice=getExpressionConstantValue(it);
            if(indice<=0)throw std::runtime_error("Array indices is not allowed to be less than zero,line: "+ std::to_string(node->line) );
        }
    }   
    // 全局变量
    if (currentFunction == nullptr)
    {
        Constant *initializer = nullptr;
        // 检查数组维度是否合法
        if (node->initializer)
        {
            if (varType->isArrayTy()) 
            {
                initializer = evaluateConstantArray(node->initializer, static_cast<ArrayType*>(varType));
            } 
            else 
            {
                // 经过静态检查，这里必定是常量，不用判空
                initializer = evaluateConstantExpr(node->initializer->singleInitVal);
            }
        }
        GlobalVariable *globalVar = module->addGlobalVariable(varType, node->identifier, initializer, node->type.isConst());
        varToValue[node->identifier] = globalVar;
        // const变量加入常量表
        if(node->type.isConst())
        {
            constVarToValue[node->identifier] = initializer;
        }
    }
    else
    {
        // 局部变量
        if(varType->isArrayTy())
        {
            // 数组用内存模型
            Value *alloca = createAlloca(varType);
            varToValue[node->identifier] = alloca;
            if (node->initializer && !node->type.isConst())
            {
                visitInitExpr(node->initializer, varType, alloca);
            }
            else if (node->initializer && node->type.isConst())
            {
                visitInitExpr(node->initializer, varType, alloca);
                Constant *initializer = evaluateConstantArray(node->initializer, static_cast<ArrayType*>(varType));
                constVarToValue[node->identifier] = initializer;
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
            else if (node->initializer && node->type.isConst())
            {
                // 经过静态检查，这里必定是常量，不用判空
                Constant *initializer = evaluateConstantExpr(node->initializer->singleInitVal);
                initValue = initializer;
                constVarToValue[node->identifier] = initializer;
            }
            // 无初始值时，使用默认值
            else
            {
                if (varType->isIntegerTy())
                    initValue = new ConstantInt(IntegerType::getInstance(), 0);
                else if (varType->isFloatTy())
                    initValue = new ConstantFloat(FloatType::getInstance(), 0.0f);
            }       
            // 将初始值存储到 varToValue 中
            varToValue[node->identifier] = initValue;
            // 在当前基本块中记录变量的 SSA 值
            basicBlockVarToValue[currentBlock][node->identifier] = initValue;
        }
    }
}

void IRBuilder::visitAssignStmt(std::shared_ptr<ast::AssignStmtNode> node)
{
    Value *lvalue = visitLValueExpr(node->lvalue);
    Value *rvalue = visitExpression(node->rvalue);
    
    if(lvalue->getType()->isPointerTy())
    {
        auto ptrType=dynamic_cast<PointerType*>(lvalue->getType());
        if(ptrType->ElementType!= rvalue->getType())
        {
            // 如果指针类型的元素类型和右值类型不匹配，进行类型转换
            rvalue = createCast(rvalue, ptrType->ElementType,"assign in array");
        }     
        // 指针类型用store
        createStore(rvalue, lvalue);
    }
    else
    {
        if (rvalue->getType() != lvalue->getType())
        {
            rvalue = createCast(rvalue, lvalue->getType(),"assign in scalar");
        }
        // 如果是标量变量，直接更新SSA值
        varToValue[node->lvalue->identifier] = rvalue;
        basicBlockVarToValue[currentBlock][node->lvalue->identifier] = rvalue;
    }
}

void IRBuilder::visitExprStmt(std::shared_ptr<ast::ExprStmtNode> node)
{
    visitExpression(node->expr);
}

void IRBuilder::visitIfElseStmt(std::shared_ptr<ast::IfElseStmtNode> node)
{
    Value *condition = visitExpression(node->condition); 
    // if.then
    string thenblock_name=debugMode?"if.then."+std::to_string(node->line):"";
    BasicBlock *thenBlock = createBasicBlock(thenblock_name);
    // if.else
    string elseblock_name=debugMode?"if.else."+std::to_string(node->line):"";
    BasicBlock *elseBlock = node->else_body ? createBasicBlock(elseblock_name) : nullptr;
    // if.end
    string mergeblock_name=debugMode?"if.merge."+std::to_string(node->line):"";
    BasicBlock *mergeBlock = createBasicBlock(mergeblock_name);
    // 记录分支前变量状态
    auto tmp_block=currentBlock;
    // 条件跳转
    createCondBranch(condition, thenBlock, elseBlock ? elseBlock : mergeBlock);
    // then 分支
    setCurrentBlock(thenBlock);
    visitStatement(node->then_body,false);
    NewDeclaredVarsInBlock.clear(); // 清空当前块新声明的变量列表
    //bool then_hasTerminator = currentBlock->hasTerminator();
    // 如果没有else分支，则该变量为true，符合逻辑
    //bool else_hasTerminator=true;
    if (!currentBlock->hasTerminator())
    {
        createBranch(mergeBlock);
    }
    if (elseBlock)
    {
        setCurrentBlock(elseBlock);
        visitStatement(node->else_body,false);
        //else_hasTerminator = currentBlock->hasTerminator();
        if (!currentBlock->hasTerminator())
        {
            createBranch(mergeBlock);
        }
    }
    // 清空当前块新声明的变量列表
    NewDeclaredVarsInBlock.clear();
    // 合流块
    setCurrentBlock(mergeBlock);
    // // 如果两个分支都有终结指令则不插入phi
    // if (then_hasTerminator && else_hasTerminator)
    // {
    //     return;
    // }
    // 修改判断,如果合流块没有前驱则返回,不生产phi指令
    if (mergeBlock->getPredecessors().empty())
        return;
    // 生成phi占位    
    addPhiForVars(); 
    // 插入phi输入
    addPhiIncomings(currentBlock);
}

void IRBuilder::visitWhileStmt(std::shared_ptr<ast::WhileStmtNode> node)
{
    // while.cond
    string condblock_name=debugMode?"while.cond."+std::to_string(node->line):"";
    BasicBlock *condBlock = createBasicBlock(condblock_name);
    // 生成phi占位
    auto tmpblock=currentBlock;
    setCurrentBlock(condBlock); 
    // 更新当前块的变量映射,原因为condblock是第一块
    addPhiForVars();
    setCurrentBlock(tmpblock);
    // while.body
    string bodyblock_name=debugMode?"while.body."+std::to_string(node->line):"";
    BasicBlock *bodyBlock = createBasicBlock(bodyblock_name);
    // while.end
    string exitblock_name=debugMode?"while.exit."+std::to_string(node->line):"";
    BasicBlock *exitBlock = createBasicBlock(exitblock_name);
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
    // 如果循环体没有提前 return/break，循环体结尾跳回条件判断块
    if (!currentBlock->hasTerminator())
    {
        createBranch(condBlock);
    }
    // 回到 condBlock
    setCurrentBlock(condBlock); 
    // 插入phi输入
    addPhiIncomings(currentBlock);
    // 设置当前块为循环结束块
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
            retValue = createCast(retValue, expectedType,"return");
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
    // 如果节点为空，直接返回 nullptr
    if(node== nullptr)
    {
        return nullptr; 
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
        // 数组返回指针，普通变量返回ssa值
        Value *ptr = visitLValueExpr(lvalueExpr);
        // 如果是标量，直接返回
        if (ptr->getType()->isIntegerTy() || ptr->getType()->isFloatTy()) 
        {
            return ptr;
        }
        else if (ptr->getType()->isPointerTy()) 
        {
            int dims=getArrayDims(lvalueExpr->identifier);
            if(lvalueExpr->indices.size()<dims)
            {
                // 如果是数组，且下标不够，则返回指针
                return ptr;
            }
            // 如果是指针类型，且下标足够，则需要进行 load 操作
            else if(lvalueExpr->indices.size()==dims)
            {
                return createLoad(ptr);
            }
            else
            {
                // 如果下标超过数组维度，抛出异常
                throw std::runtime_error("Array index out of bounds,line: " + std::to_string(node->line));
            }
        }
        else throw std::runtime_error("Invalid LValue expression,line: " + std::to_string(node->line));
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
        auto stringValue = visitStringLiteralExpr(stringLiteral);
        return module->addGlobalVariable(StringType::getInstance(),
                                                getNextStringName(),
                                                dynamic_cast<ConstantString *>(stringValue),
                                                true);
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
            lhs = createCast(lhs, FloatType::getInstance(),"binary");
        }
        else if (lhs->getType()->isFloatTy() && rhs->getType()->isIntegerTy())
        {
            rhs = createCast(rhs, FloatType::getInstance(),"binary");
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
        // "logical.rhs"
        BasicBlock *rhsBlock = createBasicBlock(debugMode?"logical.rhs."+std::to_string(node->line):"");
        // "logical.end"
        BasicBlock *mergeBlock = createBasicBlock(debugMode?"logical.end."+std::to_string(node->line):"");


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
        PhiInst *phi = createPhi(IntegerType::getInstance());
        phi->addIncoming(new ConstantInt(IntegerType::getInstance(),0), lhsBlock); // true from lhs
        phi->addIncoming(rhsCond, rhsEndBlock);                                     // result from rhs

        return phi;
    }
    else if (node->op == BinaryOp::Or)
    {
        // a || b: 如果 a 为 true，直接返回 true，否则计算 b
        BasicBlock *lhsBlock = currentBlock;
        // "logical.rhs"
        BasicBlock *rhsBlock = createBasicBlock(debugMode?"logical.rhs."+std::to_string(node->line):"");
        // "logical.end"
        BasicBlock *mergeBlock = createBasicBlock(debugMode?"lobical.end."+std::to_string(node->line):"");


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
        PhiInst *phi = createPhi(IntegerType::getInstance());
        phi->addIncoming(new ConstantInt(IntegerType::getInstance(),1), lhsBlock); // true from lhs
        phi->addIncoming(rhsCond, rhsEndBlock);                                     // result from rhs
        return phi;
    }

    throw std::runtime_error("Invalid logical operator,line: " + std::to_string(node->line));
}

Value *IRBuilder::visitUnaryExpr(std::shared_ptr<ast::UnaryExprNode> node)
{
    Value *operand = visitExpression(node->operand);
    // 如果操作数是常数,返回常数
    if(isConstantValue(operand))
    {

        switch(node->op)
        {
            // 正号操作不改变值
            case UnaryOp::Plus:
                return operand; 
            case UnaryOp::Minus:
                if (operand->getType()->isIntegerTy())
                {
                    // 如果是全局变量
                    if(auto it=dynamic_cast<GlobalVariable*>(operand))
                    {         
                        return new ConstantInt(IntegerType::getInstance(), -static_cast<ConstantInt*>(it->Initializer)->Value);
                    }
                    return new ConstantInt(IntegerType::getInstance(), -static_cast<ConstantInt*>(operand)->Value);
                }
                else if (operand->getType()->isFloatTy())
                {
                    if(auto it=dynamic_cast<GlobalVariable*>(operand))
                    {
                        return new ConstantFloat(FloatType::getInstance(), -static_cast<ConstantFloat*>(it->Initializer)->Value);
                    }
                    return new ConstantFloat(FloatType::getInstance(), -static_cast<ConstantFloat*>(operand)->Value);
                }
            case UnaryOp::Not:
                if(operand->getType()->isIntegerTy())
                {
                    if(auto it=dynamic_cast<GlobalVariable*>(operand))
                    {
                        return new ConstantInt(IntegerType::getInstance(), static_cast<ConstantInt*>(it->Initializer)->Value==0);
                    }
                    // 对整数类型取反
                    return new ConstantInt(IntegerType::getInstance(), static_cast<ConstantInt*>(operand)->Value==0);
                }
                else if(operand->getType()->isFloatTy())
                {
                    if(auto it=dynamic_cast<GlobalVariable*>(operand))
                    {
                        return new ConstantFloat(FloatType::getInstance(), static_cast<ConstantFloat*>(it->Initializer)->Value==0);
                    }
                    // 对浮点数取反 为0时候取反返回true
                    return new ConstantFloat(FloatType::getInstance(), static_cast<ConstantFloat*>(operand)->Value==0);
                }
        }
    }
    // 如果操作数不是常数，直接创建一元操作指令
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
    if(ptr->getType()->isIntegerTy() || ptr->getType()->isFloatTy())
    {
        // 如果是标量变量，直接返回其 SSA 值
        return ptr;
    }
    // 进入下面的语句只能是指针或常量数组
    // 如果是const数组
    if (constVarToValue.count(node->identifier))
    {
        vector<int> indices;
        bool isAllConstant = true;
        for(int i = 0; i < node->indices.size(); ++i)
        {
            auto indexValue = evaluateConstantExpr(node->indices[i]);
            if(!indexValue)
            {
                isAllConstant = false;
                break;
            } // 如果有一个下标不是常量，直接跳出
            auto constantInt= dynamic_cast<ConstantInt *>(indexValue);
            if(!constantInt)
            {
                throw std::runtime_error("Array index must be constant for const array,line: " + std::to_string(node->line));
            }
            // 如果是常量数组，直接返回对应的值
            indices.push_back(constantInt->Value);
        }
        // 如果是常量数组且下标全是常量，获取指定value直接返回
        if(isAllConstant)
        {
            auto ConstantValue=getConstantArrayValueByIndices(constVarToValue[node->identifier], indices);
            if(!ConstantValue)
            {
                throw std::runtime_error("ConstantArray is a nullptr,line : " + std::to_string(node->line));
            }
            // 如果是常量数组，直接返回对应的值
            return ConstantValue;
        }
    }
    // 处理数组索引
    if (!node->indices.empty()&&ptr->getType()->isPointerTy())
    {
        Vector<Value *> indices;
        for (auto &indexExpr : node->indices)
        {
            Value *index = visitExpression(indexExpr);
            if (index->getType()->isFloatTy())
            {
                index = createCast(index, IntegerType::getInstance(),"lvaluevisit");
            }
            indices.push_back(index);
        }       
        return createGetElementPtr(ptr, indices);
    }
    // 无下标且是指针则直接返回指针，不做处理   
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
        // 传入行号
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
                arg = createGetElementPtr(arg, indices);
            }
            else
            {
                break;
            }
        }
        // 如果类型不匹配，进行类型转换 不能直接用！=，否则比较的是指针类型而不是元素类型
        if (!expectedType->isTypeEqual(arg->getType(), expectedType)) 
        {
            arg = createCast(arg, expectedType,"call");
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
{    // 字符串用 i8* 表示
    return new ConstantString(StringType::getInstance(),node->value);
}

Value *IRBuilder::visitInitExpr(std::shared_ptr<ast::InitExprNode> node, Type *targetType)
{
    if (node->singleInitVal)
    {
        return visitExpression(node->singleInitVal);
    }
    // 处理数组初始化
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
            auto elemPtr = createGetElementPtr(arrayAlloca, indices);

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
    
    std::vector<size_t> arrayindices=dynamic_cast<ArrayType*>(targetType)->getArrayIndices();
    if(arrayindices.empty())
    {
        throw std::runtime_error("Array initialization: targetType is not array,line: " + std::to_string(node->line));
    }
    size_t depth=getInitExprMaxDepth(node);
    // 展平所有叶子节点，用于底层赋值
    flattenInitList(node, flat_inits, arrayindices,arrayindices.size()-depth);
    if(debugMode)
    {
        std::cout<<"begin at dimension: "<<arrayindices.size()-depth<<",line: "<<node->line<<std::endl;
        std::cout<< "Flattened init list size: " << flat_inits.size() <<",line : "+std::to_string(node->line)<< std::endl;
        for(size_t i = 0; i < flat_inits.size(); ++i) {
            if (flat_inits[i]) {
                std::cout << " ptr ";
            } else {
                std::cout << " nullptr" ;
            }
        }
        std::cout<<std::endl;
    }
    // 计算数组总元素个数（支持多维）
    auto arrayType = dynamic_cast<ArrayType *>(targetType);
    size_t totalElements=arrayType?arrayType->getArrayLength() : 1;
    if (flat_inits.size() > totalElements) {
        throw std::runtime_error("Initializer list has more elements than array dimension,line: " + std::to_string(node->line));
    }
    // 递归处理初始化并检查每一维的初始化项数量
    visitInitExprImpl(targetType, targetPtr, indices, node, flat_inits, flat_idx);
}

size_t IRBuilder::getInitExprMaxDepth(std::shared_ptr<ast::InitExprNode> node, size_t currentDepth)
{
    if (!node || node->singleInitVal) return currentDepth;
    size_t maxDepth = currentDepth;
    for (const auto &child : node->multiInitVal)
    {
        maxDepth = std::max(maxDepth, getInitExprMaxDepth(child, currentDepth + 1));
    }
    return maxDepth;
}
// 展开所有叶子节点到 flat_inits
void IRBuilder::flattenInitList(
    std::shared_ptr<ast::InitExprNode> node,
    Vector<std::shared_ptr<ast::InitExprNode>>& flat_inits,
    const std::vector<size_t>& dims,
    int dim // 当前递归到第几维
) {
    int dim_len = dims[dim];
    int filled = 0;

    // 情况1：全平铺（只有singleInitVal），直接顺序push
    if (node && node->singleInitVal) {
        flat_inits.push_back(node);
        return;
    }

    // 情况2：有嵌套，递归处理
    if (node && !node->multiInitVal.empty()) {
        for (auto& child : node->multiInitVal) {
            // 如果 child 是平铺（singleInitVal），且当前不是最后一维，说明是全平铺，直接顺序push
            if (child && child->singleInitVal && dim < dims.size() - 1) {
                flattenInitList(child, flat_inits, dims, dims.size() - 1); // 直接递归到最后一维
                ++filled;
            } else if (dim == dims.size() - 1) {
                // 最后一维
                if (child && child->singleInitVal) {
                    flat_inits.push_back(child);
                } else {
                    flat_inits.push_back(nullptr);
                }
                ++filled;
            } else {
                flattenInitList(child, flat_inits, dims, dim + 1);
                ++filled;
            }
        }
    }

    // 补零
    int remain = dim_len - filled;
    for (int i = 0; i < remain; ++i) {
        if (dim == dims.size() - 1) {
            flat_inits.push_back(nullptr);
        } else {
            flattenInitList(nullptr, flat_inits, dims, dim + 1);
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
        for (int idx : indices) {
            gep_indices.push_back(new ConstantInt(IntegerType::getInstance(), idx));
        }

        auto elemPtr = createGetElementPtr(targetPtr, gep_indices);

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
                // 经过静态检查，这里必定是常量
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
        throw std::runtime_error("Null expression in constant evaluation,line: "+ std::to_string(node->line));

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
        if(!lhs|| !rhs)
            return nullptr; // 如果有一个子表达式不是常量，返回 nullptr
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
                default: throw std::runtime_error("Unsupported op in const int expr,line: "+ std::to_string(node->line));
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
                default: throw std::runtime_error("Unsupported op in const float expr,line: "+ std::to_string(node->line));
            }
            return new ConstantFloat(FloatType::getInstance(), res);
        }
    }
    else if(auto uval=std::dynamic_pointer_cast<ast::UnaryExprNode>(node))
    {
        auto operand=evaluateConstantExpr(uval->operand);
        if(!operand)
            return nullptr; // 如果操作数不是常量，返回 nullptr
        if(operand->getType()->isIntegerTy())
        {
            int v=static_cast<ConstantInt*>(operand)->Value;
            int res=0;
            switch(uval->op)
            {
                case ast::UnaryOp::Plus:res=v;break;
                case ast::UnaryOp::Minus:res=0-v;break;
                default:throw std::runtime_error("Unsupported op in const int expr,line: "+ std::to_string(node->line));
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
                default:throw std::runtime_error("Unsupported op in const float expr,line: "+ std::to_string(node->line));
            }
            return new ConstantFloat(FloatType::getInstance(), res);
        }
    }
    // 常量变量引用（只允许 const 变量）
    else if (auto lval = std::dynamic_pointer_cast<ast::LValueExprNode>(node)) {
        auto it = constVarToValue.find(lval->identifier);
        if (it == constVarToValue.end())
            return nullptr; // 如果没有找到常量变量，返回 nullptr
        if(auto constInt= dynamic_cast<ConstantInt*>(it->second)) {
            return constInt;
        } else if(auto constFloat = dynamic_cast<ConstantFloat*>(it->second)) {
            return constFloat;
        } else if(auto constArray = dynamic_cast<ConstantArray*>(it->second)) {
            auto indices= lval->indices;
            auto indice_size=lval->indices.size();
            auto tmp_array=constArray;
            //获取元素
            for(int i=0;i<indice_size-1;i++){
                auto j=getExpressionConstantValue(indices[i]);
                tmp_array=dynamic_cast<ConstantArray*>(tmp_array->Elements[j]);
                //转换失败:常量计算不允许指针操作
                if(tmp_array==nullptr)
                {
                    throw std::runtime_error("Point is not allowed to appear in constant expression,line: " + std::to_string(node->line));
                }
            }
            // 最后一维
            return tmp_array->Elements[getExpressionConstantValue(indices[indice_size-1])];
        }
    }

    return nullptr; // 如果没有匹配到任何常量表达式，返回 nullptr
}
int IRBuilder::getExpressionConstantValue(std::shared_ptr<ast::ExprNode> node){
    auto value=evaluateConstantExpr(node);
    if(!value)
    {
        throw std::runtime_error("Expression is not constant,line: "+std::to_string(node->line));
    }
    if(auto int_value=dynamic_cast<ConstantInt*>(value)){
        return int_value->Value;
    }
    else if(auto float_value=dynamic_cast<ConstantFloat*>(value)){
        return (int)float_value->Value;
    }
    else{
        throw std::runtime_error("Unsupported constant expression type in getExpressionConstantValue,line: "+ std::to_string(node->line));
    }
}
bool IRBuilder::isConstVars(string name){
    auto it=constVarToValue.find(name);
    if(it==constVarToValue.end())return false;
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
            throw std::runtime_error("Modulo not supported for float" );
        opcode = Opcode::SRem;
        break;
    default:
        throw std::runtime_error("Invalid binary operator");
    }
    // 如果是常量表达式，直接计算结果
    if(isConstantValue(lhs) && isConstantValue(rhs))
    {
        if (lhs->getType()->isIntegerTy())
        {
            int l,r;
            if(auto it=dynamic_cast<GlobalVariable*>(lhs))
            {
                l=static_cast<ConstantInt*>(it->Initializer)->Value;
            }
            else  l = static_cast<ConstantInt*>(lhs)->Value;
            if (auto it=dynamic_cast<GlobalVariable*>(rhs))
            {
                r=static_cast<ConstantInt*>(it->Initializer)->Value;
            }
            else  r = static_cast<ConstantInt*>(rhs)->Value;
            int res = 0;
            switch (op)
            {
            case BinaryOp::Add: res = l + r; break;
            case BinaryOp::Sub: res = l - r; break;
            case BinaryOp::Mul: res = l * r; break;
            case BinaryOp::Div: res = l / r; break;
            case BinaryOp::Mod: res = l % r; break;
            default: throw std::runtime_error("Unsupported op in const int expr");
            }
            return new ConstantInt(IntegerType::getInstance(), res);
        }
        else if (isFloat)
        {
            float l,r;
            if(auto it=dynamic_cast<GlobalVariable*>(lhs))
            {
                l=static_cast<ConstantFloat*>(it->Initializer)->Value;
            }
            else  l = static_cast<ConstantFloat*>(lhs)->Value;
            if (auto it=dynamic_cast<GlobalVariable*>(rhs))
            {
                r=static_cast<ConstantFloat*>(it->Initializer)->Value;
            }
            else  r = static_cast<ConstantFloat*>(rhs)->Value;
            float res = 0.0f;
            switch (op)
            {
            case BinaryOp::Add: res = l + r; break;
            case BinaryOp::Sub: res = l - r; break;
            case BinaryOp::Mul: res = l * r; break;
            case BinaryOp::Div: res = l / r; break;
            default: throw std::runtime_error("Unsupported op in const float expr");
            }
            return new ConstantFloat(FloatType::getInstance(), res);
        }
    }
    //  否则返回一个新的二元操作指令
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
        // 如果是常量表达式，直接计算结果
        if(isConstantValue(lhs)&&isConstantValue(rhs))
        {
            float l,r;
            if(auto it=dynamic_cast<GlobalVariable*>(lhs))
            {
                l=static_cast<ConstantFloat*>(it->Initializer)->Value;
            }
            else  l = static_cast<ConstantFloat*>(lhs)->Value;
            if (auto it=dynamic_cast<GlobalVariable*>(rhs))
            {
                r=static_cast<ConstantFloat*>(it->Initializer)->Value;
            }
            else  r = static_cast<ConstantFloat*>(rhs)->Value;
            float res = 0.0f;
            switch (op)
            {
            case BinaryOp::Lt: res = l < r; break;
            case BinaryOp::Gt: res = l > r; break;
            case BinaryOp::Le: res = l <= r; break;
            case BinaryOp::Ge: res = l >= r; break;
            case BinaryOp::Eq: res = l == r; break;
            case BinaryOp::Ne: res = l != r; break;
            default: throw std::runtime_error("Unsupported op in const float expr");
            }
            return new ConstantFloat(FloatType::getInstance(),res);
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
        // 常量表达式直接赋值返回
        if(isConstantValue(lhs)&&isConstantValue(rhs))
        {
            int l,r;
            if(auto it=dynamic_cast<GlobalVariable*>(lhs))
            {
                l=static_cast<ConstantInt*>(it->Initializer)->Value;
            }
            else  l = static_cast<ConstantInt*>(lhs)->Value;
            if (auto it=dynamic_cast<GlobalVariable*>(rhs))
            {
                r=static_cast<ConstantInt*>(it->Initializer)->Value;
            }
            else  r = static_cast<ConstantInt*>(rhs)->Value;
            int res = 0;
            switch (op)
            {
            case BinaryOp::Lt: res = l < r; break;
            case BinaryOp::Gt: res = l > r; break;
            case BinaryOp::Le: res = l <= r; break;
            case BinaryOp::Ge: res = l >= r; break;
            case BinaryOp::Eq: res = l == r; break;
            case BinaryOp::Ne: res = l != r; break;
            default: throw std::runtime_error("Unsupported op in const int expr");
            }
            return new ConstantInt(IntegerType::getInstance(),res);
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
Value *IRBuilder::createGetElementPtr(Value *ptr, const Vector<Value *> &indices)
{
    auto gepInst = std::make_unique<GetElementPtrInst>(ptr, indices, getNextTempName());
    Value *result = gepInst.get();
    currentBlock->addInstruction(std::move(gepInst));
    return result;
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
    auto brInst = std::make_unique<BranchInst>(target);
    currentBlock->addInstruction(std::move(brInst));

    // 更新 CFG
    currentBlock->addSuccessor(target);
    target->addPredecessor(currentBlock);
}

void IRBuilder::createCondBranch(Value *condition, BasicBlock *trueBlock, BasicBlock *falseBlock)
{
    //如果condition已知，直接产生无条件跳转
    // if (isConstantValue(condition))
    // {
    //     if(auto IntValue=dynamic_cast<ConstantInt*>(condition))
    //     {
    //         if(IntValue->Value!=0)
    //         {
    //             createBranch(trueBlock);
    //         }
    //         else
    //         {
    //             createBranch(falseBlock);
    //         }
    //     }
    //     else if(auto FloatValue=dynamic_cast<ConstantFloat*>(condition))
    //     {
    //         if(FloatValue->Value!=0.0f)
    //         {
    //             createBranch(trueBlock);
    //         }
    //         else
    //         {
    //             createBranch(falseBlock);
    //         }
    //     }
    //     else if(auto GlobalValue=dynamic_cast<GlobalVariable*>(condition))
    //     {
    //         if(auto IntValue=dynamic_cast<ConstantInt*>(GlobalValue->Initializer))
    //         {
    //             if(IntValue->Value!=0)
    //             {
    //                 createBranch(trueBlock);
    //             }
    //             else
    //             {
    //                 createBranch(falseBlock);
    //             }
    //         }
    //         else if(auto FloatValue=dynamic_cast<ConstantFloat*>(GlobalValue->Initializer))
    //         {
    //             if(FloatValue->Value!=0.0f)
    //             {
    //                 createBranch(trueBlock);
    //             }
    //             else
    //             {
    //                 createBranch(falseBlock);
    //             }
    //         }
    //     }
    //     return; // 已处理常量情况，直接返回
    // }
    // 否则走正常的条件分支逻辑
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

PhiInst *IRBuilder::createPhi(Type *type, const String &name)
{
    std::string actualName = name.empty() ? getNextTempName() : name;
    auto phiInst = std::make_unique<PhiInst>(type, actualName);
    auto *result = phiInst.get();
    currentBlock->addInstruction(std::move(phiInst));
    return result;
}
// ===== 类型转换 ===== 
Type *IRBuilder::convertASTTypeToIRType(const ast::DataType &astType,bool isFunctionParam)
{
    switch (astType.baseType)
    {
    case PrimaryDataType::INT:
        if (astType.isArray())
        {
            Type *elemType = IntegerType::getInstance();
            const auto &sizes = astType.arraySizes();
            // 做函数参数数组自动退化为指针
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

Value *IRBuilder::createCast(Value *value, Type *targetType,string statement)
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
        throw std::runtime_error("Unsupported type conversion in creatcast:" + to_string(srcType->getTypeID()) + " to " + to_string(targetType->getTypeID())+" in: "+statement);
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
    else
    {
        throw std::runtime_error("Cannot convert to bool");
    }

    return createComparison(BinaryOp::Ne, value, zero);
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
        // 普通变量
        if (!(value->getType()->isPointerTy()||value->getType()->isArrayTy()||isConstVars(name))) 
        {
            PhiInst* phi = createPhi(value->getType());
            varToValue[name] = phi;                         // 更新 SSA 值为 PHI 节点
            basicBlockVarToValue[currentBlock][name] = phi; // 更新当前块的变量映射
        }
    }   
}
void IRBuilder::addPhiIncomings(BasicBlock *block)
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
                phi->addIncoming(it->second, pred); // 添加前驱块的值
            }
            // 如果没有，说明该变量在该前驱块未定义，为局部变量，不做处理
        }
    }
}
bool IRBuilder::isConstantValue(Value *value)
{
    // 只处理int float常量
    if (dynamic_cast<ConstantInt*>(value) || dynamic_cast<ConstantFloat*>(value)) 
    {
        return true;
    }
    else if(auto it=dynamic_cast<GlobalVariable*>(value))
    {
        return it->IsConstant&&isConstantValue(it->Initializer);
    }
    return false;
}
int IRBuilder::getArrayDims(string varName)
{
    auto ptr= varToValue.find(varName);
    if (ptr == varToValue.end())
    {
        throw std::runtime_error("Variable not found: " + varName);
    }
    int dims=1;
    // 不是指针抛出异常
    if(!ptr->second->getType()->isPointerTy())
    {
        throw std::runtime_error("Variable is not an array: " + varName);
    }
    Type *type = dynamic_cast<PointerType*>(ptr->second->getType())->ElementType;
    while (auto arrayType = dynamic_cast<ArrayType*>(type))
    {
        dims++;
        type = arrayType->ElementType; // 继续向下获取元素类型
    }
    return dims;
}
Value *IRBuilder::getConstantArrayValueByIndices(Constant *constant,const Vector<int> &indices)const
{
    if (indices.empty()) return nullptr;
    auto constArray = dynamic_cast<ConstantArray*>(constant);
    if (!constArray)
    {
        throw std::runtime_error("Variable is not a constant array");
    }
    // 遍历索引获取元素
    ConstantArray *tmpArray = constArray;
    for (size_t i = 0; i < indices.size(); ++i)
    {
        int index = indices[i];
        if (index < 0 || index >= tmpArray->Elements.size())
        {
            throw std::runtime_error("Index out of bounds for constant array,index: " + std::to_string(index));
        }
        // 如果是最后一个索引，返回对应的元素
        if (i == indices.size() - 1)
        {
            return tmpArray->Elements[index];
        }
        // 否则继续深入下一层数组
        tmpArray = dynamic_cast<ConstantArray*>(tmpArray->Elements[index]);
        if (!tmpArray)
        {
            throw std::runtime_error("Indexing into non-array element in constant array");
        }
    }
    return nullptr; // 理论上不会到这里
}
bool IRBuilder::isBlockNewDeclaredVar(const String &varName) const
{
    return std::find(NewDeclaredVarsInBlock.begin(), NewDeclaredVarsInBlock.end(), varName) != NewDeclaredVarsInBlock.end();
}

void IRBuilder::printValueTableInEveryBlock()
{
    for(auto &it : basicBlockVarToValue)
    {
        std::cout << "BasicBlock: " << it.first->getName() << std::endl;
        for(const auto &var : it.second)
        {
            std::cout << "  Variable: " << var.first << " -> " << var.second->toRef() << std::endl;
        }   
    }
}
void IRBuilder::printBasicBlockInfo()
{
    int j=0;
    auto _module=module.get();
    for (int i = 13; i < _module->Functions.size(); i++)
    {
        std::cout << module->Functions[i]->getName() << ":" << std::endl;
        for (const auto &it : _module->Functions[i]->BasicBlocks)
        {
            std::cout << "BasicBlockSuccs " << j << ":" << std::endl;
            std::cout << "                   Successors: ";
            for (auto suc : it->getSuccessors())
            {
                std::cout << suc->getName() << " ";
            }
            std::cout << std::endl;
            std::cout << "                   Predecessors: ";
            for (auto pre : it->getPredecessors())
            {
                std::cout << pre->getName() << " ";
            }
            std::cout << std::endl;
            j++;
        }
    }
}