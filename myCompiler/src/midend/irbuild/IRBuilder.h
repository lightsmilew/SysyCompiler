#include "../../frontend/ASTNode.h"
#include "../../frontend/SemanticAnalysis.h"
#include "IRDataStructure.h"
#include <stack>
#include <unordered_map>

namespace ir_builder
{
    // IRBuilder 的最佳实践：使用访问者模式
    class IRBuilder
    {
    private:
        // === 核心数据结构 ===
        std::unique_ptr<Module> module; // 当前模块
        Function *currentFunction;      // 当前函数
        BasicBlock *currentBlock;       // 当前基本块

        // === 符号表管理 ===
        std::unordered_map<String, Constant*> constVarInitValues;        // 常量符号表                               
        std::unordered_map<String, Value *> varToValue;                  // AST变量名到IR Value的映射 当前符号表
        std::vector<String>block_new_declared_vars;                      // 当前基本块内新声明的变量列表 用于作用域嵌套管理
        std::stack<std::unordered_map<String, Value *>> varToValueStack; // 变量映射栈 用于作用域嵌套管理

        // === 控制流管理 ===
        struct LoopContext
        {
            BasicBlock *continueBlock;
            BasicBlock *breakBlock;
            LoopContext(BasicBlock *cont, BasicBlock *brk)
                : continueBlock(cont), breakBlock(brk) {}
        };
        std::stack<LoopContext> loopStack; // 循环上下文栈

        // === 计数器 ===
        unsigned tempVarCounter; // 临时变量计数器
        unsigned labelCounter;   // 标签计数器

    public:
        // === 构造与初始化 ===
        IRBuilder( const String &moduleName = "main_module")
            : currentFunction(nullptr), currentBlock(nullptr),
              tempVarCounter(0), labelCounter(0)
        {
            module = std::make_unique<Module>(moduleName);
            initializeLibraryFunctions(); // 初始化库函数
        }
        // === 初始化库函数 ===
        void initializeLibraryFunctions();
        // === 主入口：构建整个模块 ===
        std::unique_ptr<Module> buildModule(std::shared_ptr<ast::CompUnitNode> compUnit);

        // === AST节点访问接口 ===
        void visitCompUnit(std::shared_ptr<ast::CompUnitNode> node);//✔
        void visitFunction(std::shared_ptr<ast::FuncNode> node);//✔
        void visitBlock(std::shared_ptr<ast::BlockStmtNode> node,bool restoreScope = true);//✔

        // 语句访问
        void visitStatement(std::shared_ptr<ast::StmtNode> node,bool restoreScope = true);
        void visitDeclStmt(std::shared_ptr<ast::DeclStmtNode> node);//✔
        void visitAssignStmt(std::shared_ptr<ast::AssignStmtNode> node);//✔
        void visitExprStmt(std::shared_ptr<ast::ExprStmtNode> node);
        void visitIfElseStmt(std::shared_ptr<ast::IfElseStmtNode> node);
        void visitWhileStmt(std::shared_ptr<ast::WhileStmtNode> node);
        void visitBreakStmt(std::shared_ptr<ast::BreakStmtNode> node);
        void visitContinueStmt(std::shared_ptr<ast::ContinueStmtNode> node);
        void visitReturnStmt(std::shared_ptr<ast::ReturnStmtNode> node);

        // 表达式访问（返回Value*）
        Value *visitExpression(std::shared_ptr<ast::ExprNode> node);
        Value *visitBinaryExpr(std::shared_ptr<ast::BinaryExprNode> node);
        Value *visitLogicalExpr(std::shared_ptr<ast::BinaryExprNode> node); // 逻辑表达式短路求值
        Value *visitUnaryExpr(std::shared_ptr<ast::UnaryExprNode> node);
        Value *visitLValueExpr(std::shared_ptr<ast::LValueExprNode> node);
        Value *visitCallExpr(std::shared_ptr<ast::CallExprNode> node);
        Value *visitIntLiteralExpr(std::shared_ptr<ast::IntLiteralExprNode> node);
        Value *visitFloatLiteralExpr(std::shared_ptr<ast::FloatLiteralExprNode> node);
        Value *visitStringLiteralExpr(std::shared_ptr<ast::StringLiteralExprNode> node);
        Value *visitInitExpr(std::shared_ptr<ast::InitExprNode> node, Type *targetType);
        // 新增重载 处理数组初始化
        void visitInitExpr(std::shared_ptr<ast::InitExprNode> node, Type *targetType, Value *targetPtr);

        // 常量数组求值
        Constant *evaluateConstantArray(std::shared_ptr<ast::InitExprNode> node, ArrayType *arrayType);
        // 编译时常量表达式求值
        Constant *evaluateConstantExpr(std::shared_ptr<ast::ExprNode> node);

        // 基本块管理
        BasicBlock *createBasicBlock(const String &name = "",const Vector<BasicBlock*> &beforeblocks = {});
        void setCurrentBlock(BasicBlock *block);

        // 指令生成辅助
        Value *createBinaryOp(ast::BinaryOp op, Value *lhs, Value *rhs);
        Value *createComparison(ast::BinaryOp op, Value *lhs, Value *rhs);
        Value *createUnaryOp(ast::UnaryOp op, Value *operand);
        Value *createLoad(Value *ptr);
        void createStore(Value *value, Value *ptr);
        Value *createAlloca(Type *type, const String &name = "");
        Value *createCall(Function *func, const Vector<Value *> &args);
        void createBranch(BasicBlock *target);                                                  // 无条件跳转
        void createCondBranch(Value *condition, BasicBlock *trueBlock, BasicBlock *falseBlock); // 条件跳转
        void createReturn(Value *value = nullptr);                                              // 返回指令
        PhiInst *createPhi(Type *type, const String &name = "");
        // 辅助函数 用于支持嵌套和平铺赋值
        void flattenInitList(std::shared_ptr<ast::InitExprNode> node, Vector<std::shared_ptr<ast::InitExprNode>>& flat_inits);
        void visitInitExprImpl(Type *targetType, Value *targetPtr,
                                  Vector<int>& indices,
                                  std::shared_ptr<ast::InitExprNode> initNode,
                                  const Vector<std::shared_ptr<ast::InitExprNode>>& flat_inits,
                                  size_t& flat_idx);
        size_t getArrayTotalElements(Type* type);
        Vector<shared_ptr<ast::InitExprNode>> getChildrenAtCurrentLevel(shared_ptr<ast::InitExprNode> node);

        int getExpressionConstantValue(std::shared_ptr<ast::ExprNode> node);                    // 获取表达式的常量值
        bool isConstVariable(Value *value);                                                     // 判断一个变量是否为const修饰变量
        bool hasTerminatorInst(BasicBlock *block);   
        
        // 判断一个基本块是否有终止指令 找不到就递归查找前驱
        // 类型转换
        Type *convertASTTypeToIRType(const ast::DataType &astType,bool isFunctionParam);
        Value *createCast(Value *value, Type *targetType);
        Value *convertToBool(Value *value);                                                     // 转换为布尔值
        // 临时变量名生成
        String getNextTempName()
        {
            return "t" + std::to_string(tempVarCounter++);
        }

        String getNextLabelName()
        {
            return "label" + std::to_string(labelCounter++);
        }
        bool isBlockNewDeclaredVar(const String &varName) const
        {
            return std::find(block_new_declared_vars.begin(), block_new_declared_vars.end(), varName) != block_new_declared_vars.end();
        }
        // === 获取结果 ===
        Module *getModule() { return module.get(); }
        String getModuleString() { return module->toString(); }
    };
}