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
        std::unordered_map<String, Constant *> constVarToValue;      // 常量符号表
        std::unordered_map<String, Value *> varToValue;              // AST变量名到IR Value的映射 当前符号表
        std::vector<String> NewDeclaredVarsInBlock;                  // 当前基本块内新声明的变量列表 用于作用域嵌套管理
        std::stack<std::vector<String>> NewDeclaredVarsInBlockStack; // 块内新定义符号栈，用于嵌套作用域管理变量写回
        std::unordered_map<BasicBlock *,
                           std::unordered_map<String, Value *>>
            basicBlockVarToValue;                                        // 基本块到变量映射 用于作用域嵌套管理
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
        unsigned stringCounter;  // 字符串常量计数器
        // Debug
        bool debugMode = false;  // 是否开启调试模式
    public:
        // 构造与初始化
        IRBuilder(bool debugMode, const String &moduleName = "main_module")
            : currentFunction(nullptr), currentBlock(nullptr),
              tempVarCounter(0), labelCounter(0), stringCounter(0), debugMode(debugMode)
        {
            module = std::make_unique<Module>(moduleName);
            initializeLibraryFunctions();
        }

        void initializeLibraryFunctions();                                               // 初始化库函数
        std::unique_ptr<Module> buildModule(std::shared_ptr<ast::CompUnitNode> compUnit);// 主入口：构建整个模块
        // AST节点访问接口
        void visitCompUnit(std::shared_ptr<ast::CompUnitNode> node);                     // 访问编译单元
        void visitFunction(std::shared_ptr<ast::FuncNode> node);                         // 访问函数节点
        void visitBlock(std::shared_ptr<ast::BlockStmtNode> node, bool isRestore = true);// 访问块节点(isRestore用于判断是否将块内Value写回外层)

        // 语句访问
        void visitStatement(std::shared_ptr<ast::StmtNode> node, bool isRestore = true); // 访问语句节点
        void visitDeclStmt(std::shared_ptr<ast::DeclStmtNode> node);                     // 访问声明语句
        void visitAssignStmt(std::shared_ptr<ast::AssignStmtNode> node);                 // 访问赋值语句
        void visitExprStmt(std::shared_ptr<ast::ExprStmtNode> node);                     // 访问表达式语句
        void visitIfElseStmt(std::shared_ptr<ast::IfElseStmtNode> node);                 // 访问if-else语句
        void visitWhileStmt(std::shared_ptr<ast::WhileStmtNode> node);                   // 访问while语句
        void visitBreakStmt(std::shared_ptr<ast::BreakStmtNode> node);                   // 访问break语句
        void visitContinueStmt(std::shared_ptr<ast::ContinueStmtNode> node);             // 访问continue语句
        void visitReturnStmt(std::shared_ptr<ast::ReturnStmtNode> node);                 // 访问return语句

        // 表达式访问
        Value *visitExpression(std::shared_ptr<ast::ExprNode> node);                     // 表达式访问（返回Value*）
        Value *visitBinaryExpr(std::shared_ptr<ast::BinaryExprNode> node);               // 二元表达式访问
        Value *visitLogicalExpr(std::shared_ptr<ast::BinaryExprNode> node);              // 逻辑表达式短路求值
        Value *visitUnaryExpr(std::shared_ptr<ast::UnaryExprNode> node);                 // 一元表达式访问
        Value *visitLValueExpr(std::shared_ptr<ast::LValueExprNode> node);               // 左值表达式访问
        Value *visitCallExpr(std::shared_ptr<ast::CallExprNode> node);                   // 函数调用表达式访问
        Value *visitIntLiteralExpr(std::shared_ptr<ast::IntLiteralExprNode> node);       // 整数字面量访问
        Value *visitFloatLiteralExpr(std::shared_ptr<ast::FloatLiteralExprNode> node);   // 浮点数字面量访问
        Value *visitStringLiteralExpr(std::shared_ptr<ast::StringLiteralExprNode> node); // 字符串字面量访问
        Value *visitScalarInitExpr(std::shared_ptr<ast::InitExprNode> node,
                                        Type *targetType);                               // 初始化表达式访问
        void visitArrayInitExpr(std::shared_ptr<ast::InitExprNode> node,
                           Type *targetType,
                           Value *targetPtr);                                            // 处理数组初始化
        Constant *evaluateConstantArray(std::shared_ptr<ast::InitExprNode> node,
                                        ArrayType *arrayType);                           // 常量数组求值
        Constant *evaluateConstantExpr(std::shared_ptr<ast::ExprNode> node);             // 编译时常量表达式求值

        // 基本块管理
        BasicBlock *createBasicBlock(const String &name = "");                           // 创建基本块
        void setCurrentBlock(BasicBlock *block);                                         // 设置当前基本块

        // 指令生成辅助
        Value *createBinaryOp(ast::BinaryOp op, Value *lhs, Value *rhs);                        // 产生二元操作指令
        Value *createComparison(ast::BinaryOp op, Value *lhs, Value *rhs);                      // 产生比较指令
        Value *createUnaryOp(ast::UnaryOp op, Value *operand);                                  // 产生一元操作指令(转为二元操作)
        Value *createGetElementPtr(Value *ptr, const Vector<Value *> &indices);                 // 获取指针的元素地址
        Value *createLoad(Value *ptr);                                                          // 从指针加载值
        void createStore(Value *value, Value *ptr);                                             // 将值存储到指针地址
        Value *createAlloca(Type *type, const String &name = "");                               // 分配内存
        Value *createCall(Function *func, const Vector<Value *> &args);                         // 调用函数
        void createBranch(BasicBlock *target);                                                  // 无条件跳转
        void createCondBranch(Value *condition, BasicBlock *trueBlock, BasicBlock *falseBlock); // 条件跳转
        void createReturn(Value *value = nullptr);                                              // 返回指令
        PhiInst *createPhi(Type *type, const String &name = "");                                // 创建Phi指令(用于if和while合流)

        // 辅助函数
        // 计算初始表达式大括号最深层数
        size_t getInitExprMaxDepth(std::shared_ptr<ast::InitExprNode> node,
                                   size_t currentDepth = 0);                                    // 获取初始化表达式的最大深度
        void flattenInitList(std::shared_ptr<ast::InitExprNode> node,
                             Vector<std::shared_ptr<ast::InitExprNode>> &flat_inits,
                             const std::vector<size_t> &dims,
                             int dim);                                                          // 扁平化初始化列表
        void visitInitExprImpl(Type *targetType, Value *targetPtr,
                               Vector<int> &indices,
                               std::shared_ptr<ast::InitExprNode> initNode,
                               const Vector<std::shared_ptr<ast::InitExprNode>> &flat_inits,
                               size_t &flat_idx);                                               // 用于支持嵌套和平铺赋值
        Vector<shared_ptr<ast::InitExprNode>> getChildrenAtCurrentLevel(shared_ptr<ast::InitExprNode> node);
        void addPhiForVars();
        void addPhiIncomings(BasicBlock *block);
        int getExpressionConstantValue(std::shared_ptr<ast::ExprNode> node); // 获取AST表达式的常量值
        bool isConstVars(string name);                                       // 判断一个变量是否为const修饰变量
        bool isConstantValue(Value *value);                                  // 判断一个Value是否为常量值
        bool hasTerminatorInst(BasicBlock *block);                           // 判断基本块是否有终结指令
        bool isBlockNewDeclaredVar(const String &varName) const;             // 是否为块内新定义变量
        int getArrayDims(string varName);
        Value *getConstantArrayValueByIndices(Constant *constant,
                                              const Vector<int> &indices) const;          // 获取数组维度数量
        Type *convertASTTypeToIRType(const ast::DataType &astType, bool isFunctionParam); // AST类型转换IR类型
        Value *createCast(Value *value, Type *targetType, string statement);              // 生成类型强制转换指令 statement用于调试定位
        Value *convertToBool(Value *value);                                               // 转换为布尔值
        // 作用域管理(符号表压栈与弹出)
        void PushVarsStack(); // 作用域管理压栈(包括符号表和新定义变量列表)
        void PopVarsStack();  // 作用域管理弹栈(包括符号表和新定义变量列表)
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
        // 获取结果
        Module *getModule() { return module.get(); }
        String getModuleString() { return module->toString(); }

        // Debug输出
        string getValueTableInEveryBlock();
    };
}