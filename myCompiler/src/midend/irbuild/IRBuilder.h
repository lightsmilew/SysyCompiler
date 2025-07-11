#include "../../frontend/ASTNode.h"
#include "../../frontend/SemanticAnalysis.h"
#include "IRDataStructure.h"
#include <stack>
#include <unordered_map>

namespace ir_builder
{
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
        std::vector<String> blockNewDeclaredVars;                         // 当前基本块内新声明的变量列表 用于作用域嵌套管理
        std::unordered_map<BasicBlock*,std::unordered_map<String, Value*>> basicBlockVarToValue; // 基本块到变量映射 用于作用域嵌套管理
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
        unsigned tempVarCounter;           // 临时变量计数器
        unsigned labelCounter;             // 标签计数器
        unsigned stringCounter;            // 字符串常量计数器

    public:
        // 构造与初始化 
        IRBuilder( const String &moduleName = "main_module")
            : currentFunction(nullptr), currentBlock(nullptr),
              tempVarCounter(0), labelCounter(0), stringCounter(0)
        {
            module = std::make_unique<Module>(moduleName);
            initializeLibraryFunctions(); 
        }

        void initializeLibraryFunctions();                                                      // 初始化库函数
        std::unique_ptr<Module> buildModule(std::shared_ptr<ast::CompUnitNode> compUnit);       // 主入口：构建整个模块 
        // AST节点访问接口 
        void visitCompUnit(std::shared_ptr<ast::CompUnitNode> node);
        void visitFunction(std::shared_ptr<ast::FuncNode> node);
        void visitBlock(std::shared_ptr<ast::BlockStmtNode> node,bool isRestore=true);

        // 语句访问
        void visitStatement(std::shared_ptr<ast::StmtNode> node,bool isRestore=true);
        void visitDeclStmt(std::shared_ptr<ast::DeclStmtNode> node);
        void visitAssignStmt(std::shared_ptr<ast::AssignStmtNode> node);
        void visitExprStmt(std::shared_ptr<ast::ExprStmtNode> node);
        void visitIfElseStmt(std::shared_ptr<ast::IfElseStmtNode> node);
        void visitWhileStmt(std::shared_ptr<ast::WhileStmtNode> node);
        void visitBreakStmt(std::shared_ptr<ast::BreakStmtNode> node);
        void visitContinueStmt(std::shared_ptr<ast::ContinueStmtNode> node);
        void visitReturnStmt(std::shared_ptr<ast::ReturnStmtNode> node);

        // 表达式访问
        Value *visitExpression(std::shared_ptr<ast::ExprNode> node);                            // 表达式访问（返回Value*）
        Value *visitBinaryExpr(std::shared_ptr<ast::BinaryExprNode> node);
        Value *visitLogicalExpr(std::shared_ptr<ast::BinaryExprNode> node);                     // 逻辑表达式短路求值
        Value *visitUnaryExpr(std::shared_ptr<ast::UnaryExprNode> node);
        Value *visitLValueExpr(std::shared_ptr<ast::LValueExprNode> node);
        Value *visitCallExpr(std::shared_ptr<ast::CallExprNode> node);
        Value *visitIntLiteralExpr(std::shared_ptr<ast::IntLiteralExprNode> node);
        Value *visitFloatLiteralExpr(std::shared_ptr<ast::FloatLiteralExprNode> node);
        Value *visitStringLiteralExpr(std::shared_ptr<ast::StringLiteralExprNode> node);
        Value *visitInitExpr(std::shared_ptr<ast::InitExprNode> node, Type *targetType);
        void visitInitExpr(std::shared_ptr<ast::InitExprNode> node,
                                Type *targetType,
                                Value *targetPtr);                                              // 新增重载 处理数组初始化
        Constant *evaluateConstantArray(std::shared_ptr<ast::InitExprNode> node, 
                                             ArrayType *arrayType);                             // 常量数组求值
        Constant *evaluateConstantExpr(std::shared_ptr<ast::ExprNode> node);                    // 编译时常量表达式求值

        // 基本块管理
        BasicBlock *createBasicBlock(const String &name = "");
        void setCurrentBlock(BasicBlock *block);

        // 指令生成辅助
        Value *createBinaryOp(ast::BinaryOp op, Value *lhs, Value *rhs);
        Value *createComparison(ast::BinaryOp op, Value *lhs, Value *rhs);
        Value *createUnaryOp(ast::UnaryOp op, Value *operand);
        Value *createGetElementPtr(Value *ptr, const Vector<Value *> &indices);                  // 获取指针的元素地址
        Value *createLoad(Value *ptr);
        void createStore(Value *value, Value *ptr);
        Value *createAlloca(Type *type, const String &name = "");
        Value *createCall(Function *func, const Vector<Value *> &args);
        void createBranch(BasicBlock *target);                                                  // 无条件跳转
        void createCondBranch(Value *condition, BasicBlock *trueBlock, BasicBlock *falseBlock); // 条件跳转
        void createReturn(Value *value = nullptr);                                              // 返回指令
        PhiInst *createPhi(Type *type, const String &name = "");


        // 辅助函数
        void flattenInitList(std::shared_ptr<ast::InitExprNode> node, Vector<std::shared_ptr<ast::InitExprNode>>& flat_inits);
        void visitInitExprImpl(Type *targetType, Value *targetPtr,
                                  Vector<int>& indices,
                                  std::shared_ptr<ast::InitExprNode> initNode,
                                  const Vector<std::shared_ptr<ast::InitExprNode>>& flat_inits,
                                  size_t& flat_idx);                                            //用于支持嵌套和平铺赋值
        size_t getArrayTotalElements(Type* type);
        Vector<shared_ptr<ast::InitExprNode>> getChildrenAtCurrentLevel(shared_ptr<ast::InitExprNode> node);
        void addPhiForVars();
        void addPhiForVarsIncomings(BasicBlock *block);
        int getExpressionConstantValue(std::shared_ptr<ast::ExprNode> node);                    // 获取表达式的常量值
        bool isConstVariable(string name);                                                      // 判断一个变量是否为const修饰变量
        bool isConstantValue(Value *value);                                                     
        bool hasTerminatorInst(BasicBlock *block);
        bool isBlockNewDeclaredVar(const String &varName)const; 
        int getArrayDims(string varName);                                                       // 获取数组维度数量
                                                            
        Type *convertASTTypeToIRType(const ast::DataType &astType,bool isFunctionParam);        // 类型转换
        Value *createCast(Value *value, Type *targetType,string statement);                     // 类型转换 statement用于调试定位
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
        String getNextStringName()
        {
            return "s" + std::to_string(stringCounter++);
        }    
        // 调试函数
        void printBlockValue();
        // 获取结果 
        Module *getModule() { return module.get(); }
        String getModuleString() { return module->toString(); }

    };
}