#include "IRBuilder.h"
#include <stdexcept>
#include <sstream>
#include <functional>
#include <utility>
using namespace ir_builder;
using namespace ast;

// ===== 库函数初始化函数 =====
void IRBuilder::initializeLibraryFunctions()
{
    // int getint();
    {
        FunctionType *funcType = new FunctionType(IntegerType::getInstance(), {});
        module->addFunction(funcType, "getint");
    }
    // int getch();
    {
        FunctionType *funcType = new FunctionType(IntegerType::getInstance(), {});
        module->addFunction(funcType, "getch");
    }
    // float getfloat();
    {
        FunctionType *funcType = new FunctionType(FloatType::getInstance(), {});
        module->addFunction(funcType, "getfloat");
    }
    // int getarray(int a[]);
    {
        Vector<Type *> params = {new PointerType(IntegerType::getInstance())};
        FunctionType *funcType = new FunctionType(IntegerType::getInstance(), params);
        module->addFunction(funcType, "getarray");
    }
    // int getfarray(float a[]);
    {
        Vector<Type *> params = {new PointerType(FloatType::getInstance())};
        FunctionType *funcType = new FunctionType(IntegerType::getInstance(), params);
        module->addFunction(funcType, "getfarray");
    }
    // void putint(int a);
    {
        Vector<Type *> params = {IntegerType::getInstance()};
        FunctionType *funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putint");
    }
    // void putch(int a);
    {
        Vector<Type *> params = {IntegerType::getInstance()};
        FunctionType *funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putch");
    }
    // void putfloat(float a);
    {
        Vector<Type *> params = {FloatType::getInstance()};
        FunctionType *funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putfloat");
    }
    // void putarray(int n, int a[]);
    {
        Vector<Type *> params = {IntegerType::getInstance(), new PointerType(IntegerType::getInstance())};
        FunctionType *funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putarray");
    }
    // void putfarray(int n, float a[]);
    {
        Vector<Type *> params = {IntegerType::getInstance(), new PointerType(FloatType::getInstance())};
        FunctionType *funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putfarray");
    }
    // void putf(string a); 这里假设 string 用 i8* 表示
    {
        Vector<Type *> params = {StringType::getInstance()};
        FunctionType *funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "putf");
    }
    // void starttime();
    {
        // 传入行号
        Vector<Type *> params = {IntegerType::getInstance()};
        FunctionType *funcType = new FunctionType(VoidType::getInstance(), params);
        module->addFunction(funcType, "_sysy_starttime");
    }
    // void stoptime();
    {
        Vector<Type *> params = {IntegerType::getInstance()};
        FunctionType *funcType = new FunctionType(VoidType::getInstance(), params);
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
    Type *retType = convertASTTypeToIRType(node->returnType, false);
    Vector<Type *> paramTypes;
    for (auto &param : node->params)
    {
        // 此处已退化数组为指针
        paramTypes.push_back(convertASTTypeToIRType(param->type, true));
    }
    FunctionType *funcType = new FunctionType(retType, paramTypes);
    Function *func = module->addFunction(funcType, node->identifier);
    currentFunction = func;

    // 创建入口基本块
    // entry
    string entryblock_name = debugMode ? "entry" : "";
    BasicBlock *entryBlock = createBasicBlock(entryblock_name);
    setCurrentBlock(entryBlock);

    // 进入新的作用域 访问参数之前调用，防止形参实参之间干扰或者不同函数形参名相同产生干扰
    PushVarsStack();

    for (size_t i = 0; i < node->params.size(); i++)
    {
        Argument *arg = func->addArgument(paramTypes[i], node->params[i]->identifier);
        size_t SerialNumber=varToValue.find(node->params[i]->identifier) == varToValue.end() ? 0 : varToValue[node->params[i]->identifier].getSerialNumber() + 1;
        varToValue[node->params[i]->identifier] = ValueInfo(arg, SerialNumber);
        basicBlockVarToValue[currentBlock][node->params[i]->identifier] = ValueInfo(arg, SerialNumber);
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
            if (!hasTerminatorInst(currentBlock))
            {
                throw std::runtime_error("Non-void function must return a value,line: " + std::to_string(node->body->line));
            }
        }
    }
    if (debugMode)
    {
        auto notUsedArgs = func->getIndexOfNotUsedArguments();
        if (!notUsedArgs.empty())
        {
            debugOutput << "Warning: Function '" << func->getName() << "' has unused arguments at indices: ";
            for (size_t idx : notUsedArgs)
            {
                debugOutput << idx << " ";
            }
        }
    }
    // 退出作用域
    PopVarsStack();
    currentFunction = nullptr;
}
// 这里的isRestore可以删除
void IRBuilder::visitBlock(std::shared_ptr<ast::BlockStmtNode> node, bool isRestore)
{
    // 进入新作用域
    PushVarsStack();
    // 访问 block 内所有语句
    for (auto &stmt : node->stmts)
    {
        visitStatement(stmt);
        if (currentBlock->hasTerminator())
            break;
    }
    // 恢复作用域
    // 这里不能调用PopVarsStack，因为需要将当前块内新声明的变量写回外层作用域
    varToValue = varToValueStack.top();
    varToValueStack.pop();
    auto outerneedToWriteBackVarToValue = needToWriteBackVarToValueStack.top();
    needToWriteBackVarToValueStack.pop();
    // 只在 isRestore 为真时写回外层变量
    if (isRestore)
    {
        // 将当前块内非新声明的变量写回外层作用域
        for (auto [varName, value] : needToWriteBackVarToValue)
        {
            varToValue[varName].setValue(value);
        }
        // 如果isRestore为真，则代表为非ifelse或者while块，需要写回bascicBlockVarToValue
        basicBlockVarToValue[currentBlock] = varToValue;
    }
    // 恢复当前块新声明的变量列表和需要写回的变量列表
    newDeclaredVarsInBlock = newDeclaredVarsInBlockStack.top();
    newDeclaredVarsInBlockStack.pop();
    // 如果不是新定义的变量，则将需要写回的变量恢复到外层作用域
    if (isRestore)
    {
        for (auto [varName, value] : needToWriteBackVarToValue)
        {
            if (!isBlockNewDeclaredVar(varName))
            {
                // 如果不是新声明的变量，则将其写回外层needToWriteBackVarToValue
                outerneedToWriteBackVarToValue[varName] = value;
            }
        }
    }
    needToWriteBackVarToValue = outerneedToWriteBackVarToValue;
}

void IRBuilder::visitStatement(std::shared_ptr<ast::StmtNode> node, bool isRestore)
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
        visitBlock(blockStmt, isRestore);
    }
}

void IRBuilder::visitDeclStmt(std::shared_ptr<ast::DeclStmtNode> node)
{
    Type *varType = convertASTTypeToIRType(node->type, false);
    // 如果当前已经定义了同名变量，则记录，退出作用域时不将该变量的ssa值写出
    // 如果是全局变量，则不需要检查是否已经定义，可以直接覆盖，因为不需要写回，全局变量只存指针
    if (auto it = varToValue.find(node->identifier); it != varToValue.end())
    {
        if (!dynamic_cast<GlobalVariable *>(it->second.getValue()))
        {
            newDeclaredVarsInBlock.push_back(node->identifier);
        }
    }
    // 检查数组维度是否合法
    if (varType->isArrayTy())
    {
        for (auto it : node->type.arraySizes())
        {
            int indice = getExpressionConstantValue(it);
            if (indice <= 0)
                throw std::runtime_error("Array indices is not allowed to be less than zero,line: " + std::to_string(node->line));
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
                // evaluateConstantArray可能返回null，此时代表是{}初始化
                initializer = evaluateConstantArray(node->initializer, static_cast<ArrayType *>(varType));
            }
            else
            {
                // 经过静态检查，这里必定是常量，不用判空
                initializer = evaluateConstantExpr(node->initializer->singleInitVal, varType);
            }
        }
        GlobalVariable *globalVar = module->addGlobalVariable(varType, node->identifier, initializer, node->type.isConst());
        varToValue[node->identifier] = ValueInfo(globalVar, 0);
        // const变量加入常量表 const int i=10;把10加入下表
        if (node->type.isConst())
        {
            constVarToValue[node->identifier] = initializer;
        }
    }
    else
    {
        // 局部变量
        if (varType->isArrayTy())
        {
            // 数组用内存模型
            Value *alloca = createAlloca(varType);
            size_t SerialNumber=varToValue.find(node->identifier) == varToValue.end() ? 0 : varToValue[node->identifier].getSerialNumber()+1;
            varToValue[node->identifier] = ValueInfo(alloca, SerialNumber);
            // 这里增加一个空块用于后端写入数组初始化赋值
            BasicBlock *arrayInitBlock = createBasicBlock(debugMode ? "array_init." + node->identifier : "");
            BasicBlock *arrayInitEndBlock = createBasicBlock(debugMode ? "array_init_end." + node->identifier : "");
            createBranch(arrayInitBlock);

            arrayInitBlock->addSuccessor(arrayInitEndBlock);
            arrayInitEndBlock->addPredecessor(arrayInitBlock);
            setCurrentBlock(arrayInitEndBlock);
            if (node->initializer && !node->type.isConst())
            {
                visitArrayInitExpr(node->initializer, varType, alloca);
            }
            else if (node->initializer && node->type.isConst())
            {
                visitArrayInitExpr(node->initializer, varType, alloca);
                Constant *initializer = evaluateConstantArray(node->initializer, static_cast<ArrayType *>(varType));
                constVarToValue[node->identifier] = initializer;
            }
        }
        else
        {
            // 标量变量直接用SSA
            Value *initValue = nullptr;
            if (node->initializer && !node->type.isConst())
            {
                initValue = visitScalarInitExpr(node->initializer, varType);
            }
            else if (node->initializer && node->type.isConst())
            {
                // 经过静态检查，这里必定是常量，不用判空
                Constant *initializer = evaluateConstantExpr(node->initializer->singleInitVal, varType);
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
            varToValue[node->identifier].setValue(initValue);
            basicBlockVarToValue[currentBlock][node->identifier].setValue(initValue);
            // 在当前基本块中记录变量的 SSA 值
            // 只有是当非新定义的变量时才记录
            // if (!isBlockNewDeclaredVar(node->identifier))
            // {
            //     basicBlockVarToValue[currentBlock][node->identifier].setValue(initValue);
            // }
            if(isBlockNewDeclaredVar(node->identifier))
            { 
                // 序号自增唯一确定一个变量
                varToValue[node->identifier].plusSerialNumber();
                basicBlockVarToValue[currentBlock][node->identifier].plusSerialNumber();
            }

            // basicBlockVarToValue[currentBlock][node->identifier] = initValue;
        }
    }
}

void IRBuilder::visitAssignStmt(std::shared_ptr<ast::AssignStmtNode> node)
{
    Value *lvalue = visitLValueExpr(node->lvalue);
    Value *rvalue = visitExpression(node->rvalue);

    if (lvalue->getType()->isPointerTy())
    {
        auto ptrType = dynamic_cast<PointerType *>(lvalue->getType());
        if (ptrType->ElementType != rvalue->getType())
        {
            // 如果指针类型的元素类型和右值类型不匹配，进行类型转换
            rvalue = createCast(rvalue, ptrType->ElementType, "assign in memory model");
        }
        // 指针类型用store
        createStore(rvalue, lvalue);
    }
    else
    {
        if (rvalue->getType() != lvalue->getType())
        {
            rvalue = createCast(rvalue, lvalue->getType(), "assign in scalar");
        }
        // 如果是标量变量，直接更新SSA值
        varToValue[node->lvalue->identifier].setValue(rvalue);
        basicBlockVarToValue[currentBlock][node->lvalue->identifier].setValue(rvalue);
        // basicBlockVarToValue[currentBlock][node->lvalue->identifier] = rvalue;
        if (!isBlockNewDeclaredVar(node->lvalue->identifier))
        {
            // 如果不是新声明的变量，添加到需要写回的变量映射
            // basicBlockVarToValue用于phi合流
            //basicBlockVarToValue[currentBlock][node->lvalue->identifier] = rvalue;
            needToWriteBackVarToValue[node->lvalue->identifier] = rvalue;
        }
        if (debugMode)
        {
            debugOutput << "Assign: " << node->lvalue->identifier << " = " << rvalue->toRef() << " in line " << node->line << "\n";
        }
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
    string thenblock_name = debugMode ? "if.then." + std::to_string(node->line) : "";
    BasicBlock *thenBlock = createBasicBlock(thenblock_name);
    // if.else
    string elseblock_name = debugMode ? "if.else." + std::to_string(node->line) : "";
    BasicBlock *elseBlock = node->else_body ? createBasicBlock(elseblock_name) : nullptr;
    // if.end
    string mergeblock_name = debugMode ? "if.merge." + std::to_string(node->line) : "";
    BasicBlock *mergeBlock = createBasicBlock(mergeblock_name);
    // 记录分支前变量状态
    auto tmp_block = currentBlock;
    // 条件跳转
    createCondBranch(condition, thenBlock, elseBlock ? elseBlock : mergeBlock);
    // then 分支
    setCurrentBlock(thenBlock);
    // 进入新作用域
    PushVarsStack();
    auto thenVariantVars = findBlockVariantVars(node->then_body);
    vector<std::string> elseVariantVars;

    visitStatement(node->then_body, false);
    // 恢复作用域
    PopVarsStack();
    if (!currentBlock->hasTerminator())
    {
        createBranch(mergeBlock);
    }
    if (elseBlock)
    {
        setCurrentBlock(elseBlock);
        // 进入新作用域
        PushVarsStack();
        // 这里需要扫描一遍else.body，获取循环改变量，用于phi插入
        elseVariantVars = findBlockVariantVars(node->else_body);
        // 访问 else 分支
        visitStatement(node->else_body, false);
        // 恢复作用域
        PopVarsStack();
        if (!currentBlock->hasTerminator())
        {
            createBranch(mergeBlock);
        }
    }
    // 合流块
    setCurrentBlock(mergeBlock);
    // 修改判断,如果合流块没有前驱则返回,不生产phi指令
    if (mergeBlock->getPredecessors().empty())
        return;
    // 生成phi占位
    std::unordered_set<std::string> VariantVarsSet(thenVariantVars.begin(), thenVariantVars.end());
    VariantVarsSet.insert(elseVariantVars.begin(), elseVariantVars.end());
    std::vector<std::string> mergeVars(VariantVarsSet.begin(), VariantVarsSet.end());
    addPhiForVars(mergeVars);
    // 插入phi输入
    addPhiIncomings(currentBlock);
}

void IRBuilder::visitWhileStmt(std::shared_ptr<ast::WhileStmtNode> node)
{
    // while.cond
    string condblock_name = debugMode ? "while.cond." + std::to_string(node->line) : "";
    BasicBlock *condBlock = createBasicBlock(condblock_name);
    // 生成phi占位
    auto tmpblock = currentBlock;
    // 这里需要扫描一遍while.body，获取循环改变量，用于phi插入
    setCurrentBlock(condBlock);
    // 更新当前块的变量映射,原因为condblock是第一块
    auto LoopVariantVars = findBlockVariantVars(node->body);
    addPhiForVars(LoopVariantVars);
    setCurrentBlock(tmpblock);
    // while.body
    string bodyblock_name = debugMode ? "while.body." + std::to_string(node->line) : "";
    BasicBlock *bodyBlock = createBasicBlock(bodyblock_name);
    // while.end
    string exitblock_name = debugMode ? "while.exit." + std::to_string(node->line) : "";
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
    // 进入新作用域
    PushVarsStack();
    visitStatement(node->body, false);
    // 恢复作用域
    PopVarsStack();
    // 弹出循环上下文
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
    // 这里插入是因为循环结束块前驱有可能不止while.cond
    // 因为如果循环体内有break就会来到while.exit，所以也要合流
    addPhiForVars(LoopVariantVars);
    addPhiIncomings(currentBlock);
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
            retValue = createCast(retValue, expectedType, "return");
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
    if (node == nullptr)
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
            int dims = getArrayDims(lvalueExpr->identifier);
            if (lvalueExpr->indices.size() < dims)
            {
                // 如果是数组，且下标不够，则返回指针
                return ptr;
            }
            // 如果是指针类型，且下标足够，则需要进行 load 操作
            else if (lvalueExpr->indices.size() == dims)
            {
                return createLoad(ptr);
            }
            else
            {
                // 如果下标超过数组维度，抛出异常
                throw std::runtime_error("Array index out of bounds,line: " + std::to_string(node->line));
            }
        }
        else
            throw std::runtime_error("Invalid LValue expression,line: " + std::to_string(node->line));
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
            lhs = createCast(lhs, FloatType::getInstance(), "binary");
        }
        else if (lhs->getType()->isFloatTy() && rhs->getType()->isIntegerTy())
        {
            rhs = createCast(rhs, FloatType::getInstance(), "binary");
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
        return createComparison(node->op, lhs, rhs, node->line);
    default:
        return createBinaryOp(node->op, lhs, rhs, node->line);
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
        string rhsblock_name = debugMode ? "logical.rhs." + std::to_string(node->line) : "";
        BasicBlock *rhsBlock = createBasicBlock(rhsblock_name);
        // "logical.end"
        string mergeblock_name = debugMode ? "logical.end." + std::to_string(node->line) : "";
        BasicBlock *mergeBlock = createBasicBlock(mergeblock_name);

        Value *lhs = visitExpression(node->left);

        // 将 lhs 转换为布尔值
        Value *lhsCond = convertToBool(lhs, node->line);
        createCondBranch(lhsCond, rhsBlock, mergeBlock);

        // RHS 块
        setCurrentBlock(rhsBlock);
        Value *rhs = visitExpression(node->right);
        Value *rhsCond = convertToBool(rhs, node->line);
        createBranch(mergeBlock);
        BasicBlock *rhsEndBlock = currentBlock;

        // 合并块
        setCurrentBlock(mergeBlock);
        PhiInst *phi = createPhi(IntegerType::getInstance());
        phi->addIncoming(new ConstantInt(IntegerType::getInstance(), 0), lhsBlock); // true from lhs
        phi->addIncoming(rhsCond, rhsEndBlock);                                     // result from rhs

        return phi;
    }
    else if (node->op == BinaryOp::Or)
    {
        // a || b: 如果 a 为 true，直接返回 true，否则计算 b
        BasicBlock *lhsBlock = currentBlock;
        // "logical.rhs"
        string rhsblock_name = debugMode ? "logical.rhs." + std::to_string(node->line) : "";
        BasicBlock *rhsBlock = createBasicBlock(rhsblock_name);
        // "logical.end"
        string mergeblock_name = debugMode ? "logical.end." + std::to_string(node->line) : "";
        BasicBlock *mergeBlock = createBasicBlock(mergeblock_name);

        Value *lhs = visitExpression(node->left);

        // 将 lhs 转换为布尔值
        Value *lhsCond = convertToBool(lhs, node->line);
        createCondBranch(lhsCond, mergeBlock, rhsBlock);

        // RHS 块
        setCurrentBlock(rhsBlock);
        Value *rhs = visitExpression(node->right);
        Value *rhsCond = convertToBool(rhs, node->line);
        createBranch(mergeBlock);
        BasicBlock *rhsEndBlock = currentBlock;

        // 合并块
        setCurrentBlock(mergeBlock);
        PhiInst *phi = createPhi(IntegerType::getInstance());
        phi->addIncoming(new ConstantInt(IntegerType::getInstance(), 1), lhsBlock); // true from lhs
        phi->addIncoming(rhsCond, rhsEndBlock);                                     // result from rhs
        return phi;
    }

    throw std::runtime_error("Invalid logical operator,line: " + std::to_string(node->line));
}

Value *IRBuilder::visitUnaryExpr(std::shared_ptr<ast::UnaryExprNode> node)
{
    Value *operand = visitExpression(node->operand);
    // 如果操作数是常数,返回常数
    if (isConstantValue(operand))
    {

        switch (node->op)
        {
        // 正号操作不改变值
        case UnaryOp::Plus:
            return operand;
        case UnaryOp::Minus:
            if (operand->getType()->isIntegerTy())
            {
                // 如果是全局变量
                if (auto it = dynamic_cast<GlobalVariable *>(operand))
                {
                    return new ConstantInt(IntegerType::getInstance(), -static_cast<ConstantInt *>(it->Initializer)->Value);
                }
                return new ConstantInt(IntegerType::getInstance(), -static_cast<ConstantInt *>(operand)->Value);
            }
            else if (operand->getType()->isFloatTy())
            {
                if (auto it = dynamic_cast<GlobalVariable *>(operand))
                {
                    return new ConstantFloat(FloatType::getInstance(), -static_cast<ConstantFloat *>(it->Initializer)->Value);
                }
                return new ConstantFloat(FloatType::getInstance(), -static_cast<ConstantFloat *>(operand)->Value);
            }
        case UnaryOp::Not:
            if (operand->getType()->isIntegerTy())
            {
                if (auto it = dynamic_cast<GlobalVariable *>(operand))
                {
                    return new ConstantInt(IntegerType::getInstance(), static_cast<ConstantInt *>(it->Initializer)->Value == 0);
                }
                // 对整数类型取反
                return new ConstantInt(IntegerType::getInstance(), static_cast<ConstantInt *>(operand)->Value == 0);
            }
            else if (operand->getType()->isFloatTy())
            {
                if (auto it = dynamic_cast<GlobalVariable *>(operand))
                {
                    return new ConstantFloat(FloatType::getInstance(), static_cast<ConstantFloat *>(it->Initializer)->Value == 0);
                }
                // 对浮点数取反 为0时候取反返回true
                return new ConstantFloat(FloatType::getInstance(), static_cast<ConstantFloat *>(operand)->Value == 0);
            }
        }
    }
    // 如果操作数不是常数，直接创建一元操作指令
    return createUnaryOp(node->op, operand, node->line);
}

Value *IRBuilder::visitLValueExpr(std::shared_ptr<ast::LValueExprNode> node)
{

    auto it = varToValue.find(node->identifier);
    if (it == varToValue.end())
    {
        throw std::runtime_error("Undefined variable: " + node->identifier + ",line:" + std::to_string(node->line));
    }

    Value *ptr = it->second.getValue();
    if (ptr->getType()->isIntegerTy() || ptr->getType()->isFloatTy())
    {
        // 如果是标量变量，直接返回其 SSA 值
        return ptr;
    }
    // 进入下面的语句只能是指针或常量数组
    // 如果是const数组或者const全局变量
    if (isConstVars(node->identifier))
    {
        vector<int> indices;
        bool isAllConstant = true;
        for (int i = 0; i < node->indices.size(); ++i)
        {
            auto indexValue = evaluateConstantExpr(node->indices[i], IntegerType::getInstance());
            if (!indexValue)
            {
                isAllConstant = false;
                break;
            } // 如果有一个下标不是常量，直接跳出
            auto constantInt = dynamic_cast<ConstantInt *>(indexValue);
            if (!constantInt)
            {
                throw std::runtime_error("Array index must be constant for const array,line: " + std::to_string(node->line));
            }
            // 如果是常量数组，直接返回对应的值
            indices.push_back(constantInt->Value);
        }
        // 如果是常量数组且下标全是常量或者是常量全局变量（全局变量用内存模型），获取指定value直接返回
        if (isAllConstant)
        {
            auto groundType = dynamic_cast<PointerType *>(ptr->getType())->ElementType;
            if (groundType->isArrayTy())
            {
                // 如果是数组类型，获取底层类型
                groundType = static_cast<ArrayType *>(groundType)->getGroundElementType();
            }
            return getConstantArrayValueByIndices(groundType,
                                                  constVarToValue[node->identifier], indices);
        }
    }
    // 处理数组索引
    if (!node->indices.empty() && ptr->getType()->isPointerTy())
    {
        Vector<Value *> indices;
        for (auto &indexExpr : node->indices)
        {
            Value *index = visitExpression(indexExpr);
            if (index->getType()->isFloatTy())
            {
                index = createCast(index, IntegerType::getInstance(), "lvaluevisit");
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
        throw std::runtime_error("Undefined function: " + node->callee + ",line:" + std::to_string(node->line));
    }
    Vector<Value *> args;
    // 特判三个函数starttime, stoptime, putf
    // _sysy_starttime 和 _sysy_stoptime 函数单独处理
    if (func->getName() == "_sysy_starttime" || func->getName() == "_sysy_stoptime")
    {
        // 传入行号
        args.push_back(new ConstantInt(IntegerType::getInstance(), node->line));
        return createCall(func, args);
    }
    // putf 可变参数处理
    if (func->getName() == "putf")
    {
        // 第一个参数（字符串）类型检查
        if (node->args.empty())
            throw std::runtime_error("putf requires at least one argument,line:" + std::to_string(node->line));
        Value *strArg = visitExpression(node->args[0]);
        Type *expectedType = func->getFunctionType()->ParamTypes[0];
        if (!expectedType->isTypeEqual(strArg->getType(), expectedType))
        {
            // 不是字符串直接报错，目前不支持其他类型强转为字符串
            throw std::runtime_error("putf first argument must be a string,line: " + std::to_string(node->line));
        }
        args.push_back(strArg);
        // 后续参数全部直接传递
        for (size_t i = 1; i < node->args.size(); ++i)
        {
            args.push_back(visitExpression(node->args[i]));
        }
        return createCall(func, args);
    }
    // 其他函数的处理
    for (size_t i = 0; i < node->args.size(); i++)
    {
        Value *arg = visitExpression(node->args[i]);
        Type *expectedType = func->getFunctionType()->ParamTypes[i];
        // 多维数组退化：只要 expectedType 是指针，arg 是数组指针，且元素类型不一致，就递归GEP(0)
        while (expectedType->isPointerTy() && arg->getType()->isPointerTy())
        {
            Type *argElemType = static_cast<PointerType *>(arg->getType())->ElementType;
            Type *expElemType = static_cast<PointerType *>(expectedType)->ElementType;
            if (argElemType->isArrayTy() && !argElemType->isTypeEqual(argElemType, expElemType))
            {
                // 退化一维
                Vector<Value *> indices;
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
            arg = createCast(arg, expectedType, "call");
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
{ // 字符串用 i8* 表示
    return new ConstantString(StringType::getInstance(), node->value);
}

// 这个函数用来处理标量初始值
Value *IRBuilder::visitScalarInitExpr(std::shared_ptr<ast::InitExprNode> node, Type *targetType)
{
    if (node->singleInitVal)
    {
        Value *result = visitExpression(node->singleInitVal);
        // 如果类型不匹配，进行类型转换
        if (result->getType() != targetType)
        {
            result = createCast(result, targetType, "scalar init");
        }
        return result;
    }
    else
    {
        throw std::runtime_error("InitExprNode must have a singleInitVal for scalar initialization,line: " + std::to_string(node->line));
    }
}
// 新增重载 处理数组初始化表达式
// 支持平铺和嵌套初始化的递归数组初始化
void IRBuilder::visitArrayInitExpr(std::shared_ptr<ast::InitExprNode> node, Type *targetType, Value *targetPtr)
{
    Vector<int> indices;
    size_t flat_idx = 0;
    Vector<std::shared_ptr<ast::InitExprNode>> flat_inits;

    std::vector<size_t> arrayindices = dynamic_cast<ArrayType *>(targetType)->getArrayIndices();
    if (arrayindices.empty())
    {
        throw std::runtime_error("Array initialization: targetType is not array,line: " + std::to_string(node->line));
    }
    size_t depth = getInitExprMaxDepth(node);
    // 展平所有叶子节点，用于底层赋值
    flattenInitList(node, flat_inits, arrayindices, arrayindices.size() - depth);
    // 计算数组总元素个数（支持多维）
    auto arrayType = dynamic_cast<ArrayType *>(targetType);
    size_t totalElements = arrayType ? arrayType->getArrayLength() : 1;
    if (flat_inits.size() > totalElements)
    {
        throw std::runtime_error("Initializer list has more elements than array dimension,line: " + std::to_string(node->line));
    }
    // 递归处理初始化并检查每一维的初始化项数量
    visitInitExprImpl(targetType, targetPtr, indices, node, flat_inits, flat_idx);
}

size_t IRBuilder::getInitExprMaxDepth(std::shared_ptr<ast::InitExprNode> node, size_t currentDepth)
{
    if (!node || node->singleInitVal)
        return currentDepth;
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
    Vector<std::shared_ptr<ast::InitExprNode>> &flat_inits,
    const std::vector<size_t> &dims,
    int dim // 当前递归到第几维
)
{
    // 防止越界
    if (dim < 0 || dim >= dims.size())
        return;
    int dim_len = dims[dim];
    int filled = 0;
    // 情况1：全平铺（只有singleInitVal），直接顺序push
    if (node && node->singleInitVal)
    {
        flat_inits.push_back(node);
        return;
    }

    // 情况2：有嵌套，递归处理
    if (node && !node->multiInitVal.empty())
    {
        for (auto &child : node->multiInitVal)
        {
            // 如果 child 是平铺（singleInitVal），且当前不是最后一维，说明是全平铺，直接顺序push
            if (child && child->singleInitVal && dim < dims.size() - 1)
            {
                flattenInitList(child, flat_inits, dims, dims.size() - 1); // 直接递归到最后一维
                ++filled;
            }
            else if (dim == dims.size() - 1)
            {
                // 最后一维
                if (child && child->singleInitVal)
                {
                    flat_inits.push_back(child);
                }
                else
                {
                    flat_inits.push_back(nullptr);
                }
                ++filled;
            }
            else
            {
                flattenInitList(child, flat_inits, dims, dim + 1);
                ++filled;
            }
        }
    }

    // 中间补零
    int remain = dim_len - filled;
    for (int i = 0; i < remain; ++i)
    {
        if (dim == dims.size() - 1)
        {
            flat_inits.push_back(nullptr);
        }
        else
        {
            flattenInitList(nullptr, flat_inits, dims, dim + 1);
        }
    }
}

void IRBuilder::visitInitExprImpl(Type *targetType, Value *targetPtr,
                                  Vector<int> &indices,
                                  std::shared_ptr<ast::InitExprNode> initNode,
                                  const Vector<std::shared_ptr<ast::InitExprNode>> &flat_inits,
                                  size_t &flat_idx)
{
    if (auto arrayType = dynamic_cast<ArrayType *>(targetType))
    {
        int arrayIndex = arrayType->getNumElements();
        Type *elemType = arrayType->ElementType;

        auto children = getChildrenAtCurrentLevel(initNode);

        for (int i = 0; i < arrayIndex; ++i)
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
        Value *val;
        if (flat_idx < flat_inits.size() && flat_inits[flat_idx] && flat_inits[flat_idx]->singleInitVal)
        {
            val = visitExpression(flat_inits[flat_idx]->singleInitVal);
            Type *groundType = targetType->isArrayTy() ? static_cast<ArrayType *>(targetType)->getGroundElementType() : targetType;
            // 如果类型不匹配，进行类型转换
            if (val->getType() != groundType)
            {
                val = createCast(val, groundType, "init expr");
            }
        }
        // 末尾补零
        else
        {
            if (targetType->isFloatTy())
            {
                val = new ConstantFloat(FloatType::getInstance(), 0.0f);
            }
            else
            {
                val = new ConstantInt(IntegerType::getInstance(), 0);
            }
        }
        // 计算 GEP 索引
        Vector<Value *> gep_indices;
        for (int idx : indices)
        {
            gep_indices.push_back(new ConstantInt(IntegerType::getInstance(), idx));
        }
        bool isZero = false;
        if (auto constantInt = dynamic_cast<ConstantInt *>(val))
        {
            isZero = constantInt->Value == 0;
        }
        else if (auto constantFloat = dynamic_cast<ConstantFloat *>(val))
        {
            isZero = constantFloat->Value == 0.0f;
        }
        if (!isZero)
        {
            auto elemPtr = createGetElementPtr(targetPtr, gep_indices);
            createStore(val, elemPtr);
        }
        ++flat_idx;
    }
}
// 用于数组初始化 递归返回一个ConstantArray
Constant *IRBuilder::evaluateConstantArray(std::shared_ptr<ast::InitExprNode> node, ArrayType *arrayType)
{
    if (node->multiInitVal.empty())
    {
        // 如果没有初始化值，返回 nullptr
        // 这时为{}初始化，代表没有初值
        return nullptr;
    }
    // 1. 展平所有叶子节点
    std::vector<size_t> dims = arrayType->getArrayIndices();
    Vector<std::shared_ptr<ast::InitExprNode>> flat_inits;
    flattenInitList(node, flat_inits, dims, 0);
    // 判断是否全部为0或者空指针,如果是则返回空指针用于优化{0}成{}；
    bool isAllZero = true;
    for(int i=0;i<flat_inits.size();i++)
    {
        if(!flat_inits[i])continue; // 如果是空指针，跳过
        if(auto intLiteral = std::dynamic_pointer_cast<ast::IntLiteralExprNode>(flat_inits[i]->singleInitVal))
        {
            if(intLiteral->value != 0)
            {
                isAllZero = false;
                break;
            }
            else continue; // 如果是0，继续
        }
    }
    // 示例{0，0，0}，如果全部为0，则返回nullptr
    // 这种情况下，表示数组没有实际初始值，可以优化为 nullptr，减小代码体积
    if(isAllZero)
    {
        return nullptr; // 如果全部为0，返回nullptr表示空数组
    }
    // 2. 递归构造 ConstantArray
    size_t flat_idx = 0;
    std::function<Constant *(ArrayType *, int)> buildArray = [&](ArrayType *arrTy, int dim) -> Constant *
    {
        int len = arrTy->getNumElements();
        Type *elemTy = arrTy->ElementType;
        Vector<Constant *> elements;
        if (elemTy->isArrayTy())
        {
            for (int i = 0; i < len; ++i)
            {
                elements.push_back(buildArray(static_cast<ArrayType *>(elemTy), dim + 1));
            }
        }
        else
        {

            auto groundType = static_cast<ArrayType *>(arrTy)->getGroundElementType();
            for (int i = 0; i < len; ++i)
            {
                if (flat_idx < flat_inits.size() && flat_inits[flat_idx] && flat_inits[flat_idx]->singleInitVal)
                {
                    elements.push_back(evaluateConstantExpr(flat_inits[flat_idx]->singleInitVal, groundType));
                }
                else if (elemTy->isIntegerTy())
                {
                    elements.push_back(new ConstantInt(IntegerType::getInstance(), 0));
                }
                else if (elemTy->isFloatTy())
                {
                    elements.push_back(new ConstantFloat(FloatType::getInstance(), 0.0f));
                }
                ++flat_idx;
            }
        }
        return new ConstantArray(arrTy, elements);
    };
    return buildArray(arrayType, 0);
}
Constant *IRBuilder::evaluateConstantExpr(std::shared_ptr<ast::ExprNode> node, Type *targetType)
{
    Constant *result = evaluateConstantExprImp(node);
    if (!result)
        return nullptr; // 如果结果是 nullptr，直接返回
                        // 如果结果类型和目标类型不一致，进行类型转换
    if (auto constantfloat = dynamic_cast<ConstantFloat *>(result))
    {
        if (targetType->isIntegerTy())
        {
            result = new ConstantInt(IntegerType::getInstance(), (int)constantfloat->Value);
        }
    }
    else if (auto constantInt = dynamic_cast<ConstantInt *>(result))
    {
        if (targetType->isFloatTy())
        {
            result = new ConstantFloat(FloatType::getInstance(), (float)constantInt->Value);
        }
    }
    return result;
}
Constant *IRBuilder::evaluateConstantExprImp(std::shared_ptr<ast::ExprNode> node)
{
    if (!node)
        throw std::runtime_error("Null expression in constant evaluation,line: " + std::to_string(node->line));

    // 整型字面量
    if (auto intLiteral = std::dynamic_pointer_cast<ast::IntLiteralExprNode>(node))
        return new ConstantInt(IntegerType::getInstance(), intLiteral->value);

    // 浮点字面量
    else if (auto floatLiteral = std::dynamic_pointer_cast<ast::FloatLiteralExprNode>(node))
        return new ConstantFloat(FloatType::getInstance(), floatLiteral->value);

    // 常量二元表达式
    else if (auto binExpr = std::dynamic_pointer_cast<ast::BinaryExprNode>(node))
    {
        auto lhs = evaluateConstantExprImp(binExpr->left);
        auto rhs = evaluateConstantExprImp(binExpr->right);
        if (!lhs || !rhs)
            return nullptr; // 如果有一个子表达式不是常量，返回 nullptr
        // 这里只处理 int/float 常量
        if (lhs->getType()->isIntegerTy() && rhs->getType()->isIntegerTy())
        {
            int l = static_cast<ConstantInt *>(lhs)->Value;
            int r = static_cast<ConstantInt *>(rhs)->Value;
            int res = 0;
            switch (binExpr->op)
            {
            case ast::BinaryOp::Add:
                res = l + r;
                break;
            case ast::BinaryOp::Sub:
                res = l - r;
                break;
            case ast::BinaryOp::Mul:
                res = l * r;
                break;
            case ast::BinaryOp::Div:
                res = l / r;
                break;
            case ast::BinaryOp::Mod:
                res = l % r;
                break;
            default:
                throw std::runtime_error("Unsupported op in const int expr,line: " + std::to_string(node->line));
            }
            return new ConstantInt(IntegerType::getInstance(), res);
        }
        else if (lhs->getType()->isFloatTy() || rhs->getType()->isFloatTy())
        {
            float l = lhs->getType()->isFloatTy() ? static_cast<ConstantFloat *>(lhs)->Value : static_cast<ConstantInt *>(lhs)->Value;
            float r = rhs->getType()->isFloatTy() ? static_cast<ConstantFloat *>(rhs)->Value : static_cast<ConstantInt *>(rhs)->Value;
            float res = 0;
            switch (binExpr->op)
            {
            case ast::BinaryOp::Add:
                res = l + r;
                break;
            case ast::BinaryOp::Sub:
                res = l - r;
                break;
            case ast::BinaryOp::Mul:
                res = l * r;
                break;
            case ast::BinaryOp::Div:
                res = l / r;
                break;
            default:
                throw std::runtime_error("Unsupported op in const float expr,line: " + std::to_string(node->line));
            }
            return new ConstantFloat(FloatType::getInstance(), res);
        }
    }
    else if (auto uval = std::dynamic_pointer_cast<ast::UnaryExprNode>(node))
    {
        auto operand = evaluateConstantExprImp(uval->operand);
        if (!operand)
            return nullptr; // 如果操作数不是常量，返回 nullptr
        if (operand->getType()->isIntegerTy())
        {
            int v = static_cast<ConstantInt *>(operand)->Value;
            int res = 0;
            switch (uval->op)
            {
            case ast::UnaryOp::Plus:
                res = v;
                break;
            case ast::UnaryOp::Minus:
                res = 0 - v;
                break;
            default:
                throw std::runtime_error("Unsupported op in const int expr,line: " + std::to_string(node->line));
            }
            return new ConstantInt(IntegerType::getInstance(), res);
        }
        else if (operand->getType()->isFloatTy())
        {
            float v = static_cast<ConstantFloat *>(operand)->Value;
            float res = 0;
            switch (uval->op)
            {
            case ast::UnaryOp::Plus:
                res = v;
                break;
            case ast::UnaryOp::Minus:
                res = 0 - v;
                break;
            default:
                throw std::runtime_error("Unsupported op in const float expr,line: " + std::to_string(node->line));
            }
            return new ConstantFloat(FloatType::getInstance(), res);
        }
    }
    // 常量变量引用（只允许 const 变量）
    else if (auto lval = std::dynamic_pointer_cast<ast::LValueExprNode>(node))
    {
        auto it = constVarToValue.find(lval->identifier);
        if (it == constVarToValue.end())
            return nullptr; // 如果没有找到常量变量，返回 nullptr
        // 如果是空指针，代表是{}初始化,判断是int还是float
        if (!it->second)
        {
            auto ptr = varToValue.find(lval->identifier)->second.getValue();
            auto groundType = dynamic_cast<PointerType *>(ptr->getType())->ElementType;
            if (groundType->isArrayTy())
            {
                // 如果是数组类型，获取底层类型
                groundType = static_cast<ArrayType *>(groundType)->getGroundElementType();
            }
            if (groundType->isIntegerTy())
            {
                // 如果是整数类型，返回一个常量0
                return new ConstantInt(IntegerType::getInstance(), 0);
            }
            else if (groundType->isFloatTy())
            {
                // 如果是浮点数类型，返回一个常量0.0
                return new ConstantFloat(FloatType::getInstance(), 0.0f);
            }
            return nullptr; // 其他类型返回 nullptr
        }
        if (auto constInt = dynamic_cast<ConstantInt *>(it->second))
        {
            return constInt;
        }
        else if (auto constFloat = dynamic_cast<ConstantFloat *>(it->second))
        {
            return constFloat;
        }
        else if (auto constArray = dynamic_cast<ConstantArray *>(it->second))
        {
            auto indices = lval->indices;
            auto indice_size = lval->indices.size();
            auto tmp_array = constArray;
            // 获取元素
            for (int i = 0; i < indice_size - 1; i++)
            {
                auto j = getExpressionConstantValue(indices[i]);
                tmp_array = dynamic_cast<ConstantArray *>(tmp_array->Elements[j]);
                // 转换失败:常量计算不允许指针操作
                if (tmp_array == nullptr)
                {
                    throw std::runtime_error("Point is not allowed to appear in constant expression,line: " + std::to_string(node->line));
                }
            }
            // 最后一维
            return tmp_array->Elements[getExpressionConstantValue(indices[indice_size - 1])];
        }
    }

    return nullptr; // 如果没有匹配到任何常量表达式，返回 nullptr
}
int IRBuilder::getExpressionConstantValue(std::shared_ptr<ast::ExprNode> node)
{
    auto value = evaluateConstantExprImp(node);
    if (!value)
    {
        throw std::runtime_error("Expression is not constant,line: " + std::to_string(node->line));
    }
    if (auto int_value = dynamic_cast<ConstantInt *>(value))
    {
        return int_value->Value;
    }
    else if (auto float_value = dynamic_cast<ConstantFloat *>(value))
    {
        return (int)float_value->Value;
    }
    else
    {
        throw std::runtime_error("Unsupported constant expression type in getExpressionConstantValue,line: " + std::to_string(node->line));
    }
}
bool IRBuilder::isConstVars(string name)
{
    auto it = constVarToValue.find(name);
    if (it == constVarToValue.end())
        return false;
    return true;
}
// ===== 基本块管理 =====
BasicBlock *IRBuilder::createBasicBlock(const String &name)
{
    String actualName = (name.empty() || name == "") ? getNextLabelName() : name;
    auto basicblock = currentFunction->addBasicBlock(actualName);
    // 复制符号表
    basicBlockVarToValue[basicblock] = varToValue;
    return basicblock;
}

void IRBuilder::setCurrentBlock(BasicBlock *block)
{
    currentBlock = block;
}

// ===== 指令生成辅助 =====
Value *IRBuilder::createBinaryOp(ast::BinaryOp op, Value *lhs, Value *rhs, int line)
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
    // 如果是常量表达式，直接计算结果
    if (isConstantValue(lhs) && isConstantValue(rhs))
    {
        if (lhs->getType()->isIntegerTy())
        {
            int l, r;
            if (auto it = dynamic_cast<GlobalVariable *>(lhs))
            {
                l = static_cast<ConstantInt *>(it->Initializer)->Value;
            }
            else
                l = static_cast<ConstantInt *>(lhs)->Value;
            if (auto it = dynamic_cast<GlobalVariable *>(rhs))
            {
                r = static_cast<ConstantInt *>(it->Initializer)->Value;
            }
            else
                r = static_cast<ConstantInt *>(rhs)->Value;
            int res = 0;
            switch (op)
            {
            case BinaryOp::Add:
                res = l + r;
                break;
            case BinaryOp::Sub:
                res = l - r;
                break;
            case BinaryOp::Mul:
                res = l * r;
                break;
            case BinaryOp::Div:
                res = l / r;
                break;
            case BinaryOp::Mod:
                res = l % r;
                break;
            default:
                throw std::runtime_error("Unsupported op in const int expr");
            }
            return new ConstantInt(IntegerType::getInstance(), res);
        }
        else if (isFloat)
        {
            float l, r;
            if (auto it = dynamic_cast<GlobalVariable *>(lhs))
            {
                l = static_cast<ConstantFloat *>(it->Initializer)->Value;
            }
            else
                l = static_cast<ConstantFloat *>(lhs)->Value;
            if (auto it = dynamic_cast<GlobalVariable *>(rhs))
            {
                r = static_cast<ConstantFloat *>(it->Initializer)->Value;
            }
            else
                r = static_cast<ConstantFloat *>(rhs)->Value;
            float res = 0.0f;
            switch (op)
            {
            case BinaryOp::Add:
                res = l + r;
                break;
            case BinaryOp::Sub:
                res = l - r;
                break;
            case BinaryOp::Mul:
                res = l * r;
                break;
            case BinaryOp::Div:
                res = l / r;
                break;
            default:
                throw std::runtime_error("Unsupported op in const float expr");
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

Value *IRBuilder::createComparison(ast::BinaryOp op, Value *lhs, Value *rhs, int line)
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
        if (isConstantValue(lhs) && isConstantValue(rhs))
        {
            float l, r;
            if (auto it = dynamic_cast<GlobalVariable *>(lhs))
            {
                l = static_cast<ConstantFloat *>(it->Initializer)->Value;
            }
            else
                l = static_cast<ConstantFloat *>(lhs)->Value;
            if (auto it = dynamic_cast<GlobalVariable *>(rhs))
            {
                r = static_cast<ConstantFloat *>(it->Initializer)->Value;
            }
            else
                r = static_cast<ConstantFloat *>(rhs)->Value;
            int res = 0;
            switch (op)
            {
            case BinaryOp::Lt:
                res = l < r;
                break;
            case BinaryOp::Gt:
                res = l > r;
                break;
            case BinaryOp::Le:
                res = l <= r;
                break;
            case BinaryOp::Ge:
                res = l >= r;
                break;
            case BinaryOp::Eq:
                res = l == r;
                break;
            case BinaryOp::Ne:
                res = l != r;
                break;
            default:
                throw std::runtime_error("Unsupported op in const float expr");
            }
            return new ConstantInt(IntegerType::getInstance(), res);
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
        if (isConstantValue(lhs) && isConstantValue(rhs))
        {
            int l, r;
            if (auto it = dynamic_cast<GlobalVariable *>(lhs))
            {
                l = static_cast<ConstantInt *>(it->Initializer)->Value;
            }
            else
                l = static_cast<ConstantInt *>(lhs)->Value;
            if (auto it = dynamic_cast<GlobalVariable *>(rhs))
            {
                r = static_cast<ConstantInt *>(it->Initializer)->Value;
            }
            else
                r = static_cast<ConstantInt *>(rhs)->Value;
            int res = 0;
            switch (op)
            {
            case BinaryOp::Lt:
                res = l < r;
                break;
            case BinaryOp::Gt:
                res = l > r;
                break;
            case BinaryOp::Le:
                res = l <= r;
                break;
            case BinaryOp::Ge:
                res = l >= r;
                break;
            case BinaryOp::Eq:
                res = l == r;
                break;
            case BinaryOp::Ne:
                res = l != r;
                break;
            default:
                throw std::runtime_error("Unsupported op in const int expr");
            }
            return new ConstantInt(IntegerType::getInstance(), res);
        }
        auto icmp = std::make_unique<ICmpInst>(pred, lhs, rhs, getNextTempName());
        Value *result = icmp.get();
        currentBlock->addInstruction(std::move(icmp));
        return result;
    }
}

Value *IRBuilder::createUnaryOp(ast::UnaryOp op, Value *operand, int line)
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
        return createBinaryOp(BinaryOp::Sub, zero, operand, line);
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
        return createComparison(BinaryOp::Eq, operand, zero, line);
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
    // 如果condition已知，直接产生无条件跳转
    //  if (isConstantValue(condition))
    //  {
    //      if(auto IntValue=dynamic_cast<ConstantInt*>(condition))
    //      {
    //          if(IntValue->Value!=0)
    //          {
    //              createBranch(trueBlock);
    //          }
    //          else
    //          {
    //              createBranch(falseBlock);
    //          }
    //      }
    //      else if(auto FloatValue=dynamic_cast<ConstantFloat*>(condition))
    //      {
    //          if(FloatValue->Value!=0.0f)
    //          {
    //              createBranch(trueBlock);
    //          }
    //          else
    //          {
    //              createBranch(falseBlock);
    //          }
    //      }
    //      else if(auto GlobalValue=dynamic_cast<GlobalVariable*>(condition))
    //      {
    //          if(auto IntValue=dynamic_cast<ConstantInt*>(GlobalValue->Initializer))
    //          {
    //              if(IntValue->Value!=0)
    //              {
    //                  createBranch(trueBlock);
    //              }
    //              else
    //              {
    //                  createBranch(falseBlock);
    //              }
    //          }
    //          else if(auto FloatValue=dynamic_cast<ConstantFloat*>(GlobalValue->Initializer))
    //          {
    //              if(FloatValue->Value!=0.0f)
    //              {
    //                  createBranch(trueBlock);
    //              }
    //              else
    //              {
    //                  createBranch(falseBlock);
    //              }
    //          }
    //      }
    //      return; // 已处理常量情况，直接返回
    //  }
    //  否则走正常的条件分支逻辑
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
Type *IRBuilder::convertASTTypeToIRType(const ast::DataType &astType, bool isFunctionParam)
{
    switch (astType.baseType)
    {
    case PrimaryDataType::INT:
        if (astType.isArray())
        {
            Type *elemType = IntegerType::getInstance();
            const auto &sizes = astType.arraySizes();
            // 做函数参数数组自动退化为指针
            if (isFunctionParam)
            {
                for (int i = sizes.size() - 1; i >= 1; i--)
                {
                    elemType = new ArrayType(elemType, getExpressionConstantValue(sizes[i]));
                }
                return new PointerType(elemType);
            }
            for (int i = sizes.size() - 1; i >= 0; i--)
            {
                elemType = new ArrayType(elemType, getExpressionConstantValue(sizes[i]));
            }
            return elemType;
        }
        return IntegerType::getInstance();
    case PrimaryDataType::FLOAT:
        if (astType.isArray())
        {
            Type *elemType = FloatType::getInstance();
            const auto &sizes = astType.arraySizes();
            if (isFunctionParam)
            {
                for (int i = sizes.size() - 1; i >= 1; i--)
                {
                    elemType = new ArrayType(elemType, getExpressionConstantValue(sizes[i]));
                }
                return new PointerType(elemType);
            }
            for (int i = sizes.size() - 1; i >= 0; i--)
            {
                elemType = new ArrayType(elemType, getExpressionConstantValue(sizes[i]));
            }
            return elemType;
        }
        return FloatType::getInstance();
    case PrimaryDataType::VOID:
        return VoidType::getInstance();
    default:
        throw std::runtime_error("Unsupported type in convertASTtoIR");
    }
}

Value *IRBuilder::createCast(Value *value, Type *targetType, string statement)
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
    else if (srcType->isPointerTy() && targetType->isPointerTy())
    {
        if (srcType->isTypeEqual(srcType, targetType))
        {
            return value; // 指针类型一致，直接返回
        }
        auto srcPtrType = dynamic_cast<PointerType *>(srcType);
        auto targetPtrType = dynamic_cast<PointerType *>(targetType);
        throw std::runtime_error("Unsupported pointer type cast");
    }
    // 不支持的类型转换
    else
    {
        throw std::runtime_error("Unsupported type conversion in createCast:" + to_string(srcType->getTypeID()) + " to " + to_string(targetType->getTypeID()) + " in: " + statement);
    }

    auto castInst = std::make_unique<CastInst>(castOp, value, targetType, getNextTempName());
    Value *result = castInst.get();
    currentBlock->addInstruction(std::move(castInst));
    return result;
}

Value *IRBuilder::convertToBool(Value *value, int line)
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
    // 这里不需要调试
    return createComparison(BinaryOp::Ne, value, zero, line);
}

Vector<shared_ptr<ast::InitExprNode>> IRBuilder::getChildrenAtCurrentLevel(
    shared_ptr<ast::InitExprNode> node)
{
    if (!node)
        return {};
    if (node->multiInitVal.empty())
    {
        return {node}; // 单个值视为一个子项
    }
    else
    {
        return node->multiInitVal; // 多个子项
    }
}
bool IRBuilder::hasTerminatorInst(BasicBlock *block)
{
    if (block->hasTerminator())
        return true;
    else
    {
        bool result = true;
        for (auto pre : block->Predecessors)
        {
            if (pre->Parent != currentFunction)
                return false;
            result = hasTerminatorInst(pre);
            if (!result)
                return result;
        }
        return result;
    }
}
vector<std::string> IRBuilder::findBlockVariantVars(std::shared_ptr<ast::StmtNode> node)
{
    std::vector<std::string> blockInvariantVars;
    std::vector<std::string> newDeclaredVars;
    findBlockVariantVarsImp(node, blockInvariantVars, newDeclaredVars);
    return blockInvariantVars;
}
void IRBuilder::findBlockVariantVarsImp(std::shared_ptr<ast::StmtNode> node, std::vector<std::string> &BlockVariantVars, std::vector<std::string> &newDeclaredVars)
{
    if (!node)
        return;
    if (auto declNode = std::dynamic_pointer_cast<ast::DeclStmtNode>(node))
    {
        auto find = varToValue.find(declNode->identifier);
        if (find != varToValue.end())
        {
            // 如果变量已经存在，说明是重复声明
            newDeclaredVars.push_back(declNode->identifier);
            return;
        }
    }
    else if (auto assignNode = std::dynamic_pointer_cast<ast::AssignStmtNode>(node))
    {
        auto findVarsTable = varToValue.find(assignNode->lvalue->identifier);
        auto findInNewVars = std::find(newDeclaredVars.begin(), newDeclaredVars.end(), assignNode->lvalue->identifier);
        auto findInBlockVariant = std::find(BlockVariantVars.begin(), BlockVariantVars.end(), assignNode->lvalue->identifier);
        if (findVarsTable != varToValue.end() && findInNewVars == newDeclaredVars.end() && findInBlockVariant == BlockVariantVars.end())
        {
            // 如果变量已经存在且不是新声明的变量，说明是循环变元
            BlockVariantVars.push_back(assignNode->lvalue->identifier);
        }
    }
    else if (auto ifNode = std::dynamic_pointer_cast<ast::IfElseStmtNode>(node))
    {
        findBlockVariantVarsImp(ifNode->then_body, BlockVariantVars, newDeclaredVars);
        if (ifNode->else_body)
            findBlockVariantVarsImp(ifNode->else_body, BlockVariantVars, newDeclaredVars);
    }
    else if (auto whileNode = std::dynamic_pointer_cast<ast::WhileStmtNode>(node))
    {
        findBlockVariantVarsImp(whileNode->body, BlockVariantVars, newDeclaredVars);
    }
    else if (auto blockNode = std::dynamic_pointer_cast<ast::BlockStmtNode>(node))
    {
        // 这里复制一份newDeclaredVars，避免递归时修改原有的变量列表
        std::vector<std::string> newDeclaredVarsCopy = newDeclaredVars;
        for (auto &stmt : blockNode->stmts)
        {
            findBlockVariantVarsImp(stmt, BlockVariantVars, newDeclaredVarsCopy);
        }
    }
}
void IRBuilder::addPhiForVars(vector<std::string> &BlockVariantVars)
{

    for (const auto &[name, valueInfo] : varToValue)
    {
        Value *value = valueInfo.getValue();
        if (BlockVariantVars.empty() || std::find(BlockVariantVars.begin(), BlockVariantVars.end(), name) == BlockVariantVars.end())
        {
            // 如果是循环不变量，直接跳过
            continue;
        }
        // 普通变量,全局变量不生成phi
        if (!(value->getType()->isPointerTy() || value->getType()->isArrayTy() || isConstVars(name)))
        {
            PhiInst *phi = createPhi(value->getType());
            varToValue[name].setValue(phi);                         // 更新 SSA 值为 PHI 节点
            basicBlockVarToValue[currentBlock][name].setValue(phi); // 更新当前块的变量映射
            needToWriteBackVarToValue[name] = phi;          // 添加到需要写回的变量列表
        }
    }
}
void IRBuilder::addPhiIncomings(BasicBlock *block)
{
    // 遍历合流块所有变量
    for (const auto &[name, valueInfo] : basicBlockVarToValue[block])
    {
        Value *value = valueInfo.getValue();
        // 只处理 phi
        auto phi = dynamic_cast<PhiInst *>(value);
        if (!phi)
            continue;
        // 遍历所有前驱块
        for (auto pred : block->getPredecessors())
        {
            // 如果前驱块有该变量的 SSA 值,而且是同一个变量
            auto it = basicBlockVarToValue[pred].find(name);
            if (it != basicBlockVarToValue[pred].end() && it->second.getValue() != value&&it->second.getSerialNumber() == valueInfo.getSerialNumber())
                // 添加前驱块的值到 phi 的 incoming 列表
            {
                phi->addIncoming(it->second.getValue(), pred); // 添加前驱块的值
            }
            // 如果没有，说明该变量在该前驱块未定义，为局部变量，不做处理
        }
    }
}
bool IRBuilder::isConstantValue(Value *value)
{
    // 只处理int float常量
    if (dynamic_cast<ConstantInt *>(value) || dynamic_cast<ConstantFloat *>(value))
    {
        return true;
    }
    else if (auto it = dynamic_cast<GlobalVariable *>(value))
    {
        return it->IsConstant && isConstantValue(it->Initializer);
    }
    return false;
}
int IRBuilder::getArrayDims(string varName)
{
    // 这个函数处理三种情况:局部变量数组、全局变量数组和全局普通变量，第三个要返回0，前两者正常走流程
    auto ptrInfo = varToValue.find(varName);
    if (ptrInfo == varToValue.end())
    {
        throw std::runtime_error("Variable not found: " + varName);
    }
    // 不是指针抛出异常
    if (!ptrInfo->second.getValue()->getType()->isPointerTy())
    {
        throw std::runtime_error("Variable is not an array: " + varName);
    }
    Type *type = dynamic_cast<PointerType *>(ptrInfo->second.getValue()->getType())->ElementType;
    // 如果是基本类型且是全局变量，返回0
    if ((type->isIntegerTy() || type->isFloatTy()))
    {
        if (auto globalVar = dynamic_cast<GlobalVariable *>(ptrInfo->second.getValue()))
        {
            auto basic_type = globalVar->OriginalType;
            if (basic_type->isIntegerTy() || basic_type->isFloatTy())
            {
                return 0;
            }
        }
    }
    int dims = 1;
    while (auto arrayType = dynamic_cast<ArrayType *>(type))
    {
        dims++;
        type = arrayType->ElementType; // 继续向下获取元素类型
    }
    return dims;
}
Value *IRBuilder::getConstantArrayValueByIndices(Type *elementType, Constant *constant, const Vector<int> &indices) const
{
    // 常量为空则代表const数组初始化表达式为{}
    if (!constant)
    {
        if (elementType->isIntegerTy())
            return new ConstantInt(IntegerType::getInstance(), 0); // 如果常量为空，返回一个默认的零值
        else if (elementType->isFloatTy())
            return new ConstantFloat(FloatType::getInstance(), 0.0f); // 如果常量为空，返回一个默认的零值
        else
            throw std::runtime_error("Unsupported element type for empty constant array");
    }
    if (indices.empty())
        return constant; // 如果没有索引，直接返回常量，此时代表全局常量普通变量
    auto constArray = dynamic_cast<ConstantArray *>(constant);
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
        tmpArray = dynamic_cast<ConstantArray *>(tmpArray->Elements[index]);
        if (!tmpArray)
        {
            throw std::runtime_error("Indexing into non-array element in constant array");
        }
    }
    throw std::runtime_error("Unexpected end of constant array indexing");
    // 不会到这里
}
void IRBuilder::PushVarsStack()
{
    // 进入新作用域
    varToValueStack.push(varToValue);
    newDeclaredVarsInBlockStack.push(newDeclaredVarsInBlock);
    needToWriteBackVarToValueStack.push(needToWriteBackVarToValue);
    // 清空当前块新声明的变量列表
    newDeclaredVarsInBlock.clear();
    // 清空需要写回的变量列表
    needToWriteBackVarToValue.clear();
}
void IRBuilder::PopVarsStack()
{
    // 恢复作用域
    varToValue = varToValueStack.top();
    varToValueStack.pop();
    newDeclaredVarsInBlock = newDeclaredVarsInBlockStack.top();
    newDeclaredVarsInBlockStack.pop();
    needToWriteBackVarToValue = needToWriteBackVarToValueStack.top();
    needToWriteBackVarToValueStack.pop();
}
bool IRBuilder::isBlockNewDeclaredVar(const String &varName) const
{
    return std::find(newDeclaredVarsInBlock.begin(), newDeclaredVarsInBlock.end(), varName) != newDeclaredVarsInBlock.end();
}

string IRBuilder::getValueTableInEveryBlock()
{
    std::stringstream ss;
    for (auto &it : basicBlockVarToValue)
    {
        ss << "BasicBlock: " << it.first->getName() << std::endl;
        for (const auto &var : it.second)
        {
            ss << "  Variable: " << var.first << " -> " << var.second.getValue()->toRef() << std::endl;
        }
    }
    return ss.str();
}