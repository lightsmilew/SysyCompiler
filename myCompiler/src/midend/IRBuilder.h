#include "../frontend/ASTNode.h"
#include "../frontend/SemanticAnalysis.h"
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
        std::unordered_map<std::string, Value *> varToValue;                  // AST变量名到IR Value的映射 当前符号表
        std::stack<std::unordered_map<std::string, Value *>> varToValueStack; // 变量映射栈 用于作用域嵌套管理

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
        IRBuilder(SemanticAnalyzer *analyzer, const std::string &moduleName = "main_module")
            : semanticAnalyzer(analyzer), currentFunction(nullptr), currentBlock(nullptr),
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
        void visitCompUnit(std::shared_ptr<ast::CompUnitNode> node);
        void visitFunction(std::shared_ptr<ast::FuncNode> node);
        void visitBlock(std::shared_ptr<ast::BlockStmtNode> node);

        // 语句访问
        void visitStatement(std::shared_ptr<ast::StmtNode> node);
        void visitDeclStmt(std::shared_ptr<ast::DeclStmtNode> node);
        void visitAssignStmt(std::shared_ptr<ast::AssignStmtNode> node);
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

        // 编译时常量表达式求值
        Constant *evaluateConstantExpr(std::shared_ptr<ast::ExprNode> node);

        // 基本块管理
        BasicBlock *createBasicBlock(const std::string &name = "");
        void setCurrentBlock(BasicBlock *block);

        // 指令生成辅助
        Value *createBinaryOp(ast::BinaryOp op, Value *lhs, Value *rhs);
        Value *createComparison(ast::BinaryOp op, Value *lhs, Value *rhs);
        Value *createUnaryOp(ast::UnaryOp op, Value *operand);
        Value *createLoad(Value *ptr);
        void createStore(Value *value, Value *ptr);
        Value *createAlloca(Type *type, const std::string &name = "");
        Value *createCall(Function *func, const std::vector<Value *> &args);
        void createBranch(BasicBlock *target);                                                  // 无条件跳转
        void createCondBranch(Value *condition, BasicBlock *trueBlock, BasicBlock *falseBlock); // 条件跳转
        void createReturn(Value *value = nullptr);                                              // 返回指令
        PHINode *createPhi(Type *type, const std::string &name = "");                           // PHI 指令

        // 类型转换
        Type *convertASTTypeToIRType(const ast::DataType &astType);
        Value *createCast(Value *value, Type *targetType);
        Value *convertToBool(Value *value); // 转换为布尔值

        // 临时变量名生成
        std::string getNextTempName()
        {
            return "t" + std::to_string(tempVarCounter++);
        }

        std::string getNextLabelName()
        {
            return "label" + std::to_string(labelCounter++);
        }

        // === 获取结果 ===
        Module *getModule() { return module.get(); }
        std::string getModuleString() { return module->toString(); }
    };
}