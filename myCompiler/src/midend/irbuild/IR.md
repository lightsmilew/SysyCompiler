# IRDataStructure.h 结构与说明

本文件定义了 SysY 编译器 IR（中间表示）的核心数据结构，包括类型系统、值系统、指令系统、基本块、函数和模块等。本文档详细解释每个类、字段和函数的作用，便于开发者理解和维护。

---

## 1. 类型系统 Type

### Type（基类）
- **TypeID ID**：类型唯一标识（枚举）。
- **getTypeID()**：获取类型ID。
- **isVoidTy()/isIntegerTy()/isFloatTy()/isPointerTy()/isArrayTy()/isFunctionTy()/isStringTy()**：类型判断。
- **isTypeEqual(Type *a, Type *b)**：判断两个类型是否相等。
- **toString()**：类型转字符串（纯虚函数）。

#### 子类
- **IntegerType**：整型类型（单例，toString 返回 "i32"）。
- **FloatType**：浮点类型（单例，toString 返回 "float"）。
- **StringType**：字符串类型（单例，toString 返回 "i8*"）。
- **VoidType**：空类型（单例，toString 返回 "void"）。
- **PointerType**：指针类型，指向某种元素类型。
    - **ElementType**：指针指向的类型。
    - **toString()**：如 "i32*"。
- **ArrayType**：数组类型，包含元素类型和元素数量。
    - **ElementType**：元素类型。
    - **NumElements**：元素个数。
    - **getNumElements()**：获取数组长度。
    - **getArrayLength()**：获取总长度。
    - **getArrayIndices()**：获取每一维长度。
    - **getElementType()**：获取元素类型。
    - **toString()**：如 "[10 x i32]"。
- **FunctionType**：函数类型，包含返回类型和参数类型列表。
    - **ReturnType**：返回类型。
    - **ParamTypes**：参数类型列表。
    - **toString()**：如 "i32 (i32, float)"。

---

## 2. 值系统 Value/User/Constant

### Value
- **Type *Ty**：值的类型。
- **string Name**：值的名称。
- **vector<User *> Users**：所有使用该值的用户（指令）。
- **getType()/setType()**：获取/设置类型。
- **getName()/setName()**：获取/设置名称。
- **getUsers()**：获取所有使用者。
- **isGlobal()**：是否为全局变量。
- **addUser()/removeUser()**：添加/移除使用者。
- **replaceAllUsesWith(Value *newValue)**：将所有使用本值的地方替换为新值。
- **toRef()**：输出引用形式（如%var）。
- **toString()**：转字符串（纯虚函数）。

### User
继承自 Value，表示有操作数的值（如指令）。
- **vector<Value *> Operands**：操作数列表。
- **removeThisFromOperands()**：从所有操作数的 Users 列表中移除自己。
- **addOperand()**：添加操作数。
- **setOperands()**：设置操作数。
- **replaceOperand()**：替换操作数。
- **getNumOperands()**：获取操作数数量。
- **getOperands()**：获取所有操作数。
- **getOperandByIndex()**：获取指定索引的操作数。
- **setOperandByIndex()**：设置指定索引的操作数。
- **removeOperandByIndex()**：移除指定索引的操作数。
- **toString()**：转字符串。

### Constant 及子类
- **Constant**：常量基类，继承自 Value。
    - **toRef()**：常量输出值本身，不是引用。
- **ConstantInt**：整型常量，字段 `int Value`。
- **ConstantFloat**：浮点常量，字段 `float Value`。
- **ConstantString**：字符串常量，字段 `string Value`。
- **ConstantArray**：数组常量，字段 `vector<Constant *> Elements`。

---

## 3. 全局变量 GlobalVariable
继承自 Value，表示全局变量。  
- **Type *OriginalType**：原始类型（未退化前）。
- **Constant *Initializer**：初始值。
- **bool IsConstant**：是否为常量。
- **isArray()**：是否为数组类型。
- **getDims()**：获取维度。
- **getTotallength()**：获取总长度。
- **getGroundElementType()**：获取基本元素类型。
- **toString()**：转字符串。

---

## 4. 操作码 Opcode

- **Opcode**：枚举类型，表示指令的操作类型，包括：
    - 终结指令：Ret, Br
    - 二元运算：Add, Sub, Mul, SDiv, SRem, FAdd, FSub, FMul, FDiv
    - 比较运算：ICmp, FCmp
    - 内存操作：Alloca, Load, Store, GetElementPtr
    - 类型转换：SIToFP, FPToSI
    - 其他操作：Call, Phi, Copy

---

## 5. 指令系统 Instruction 及子类

### Instruction
继承自 User，表示一条IR指令。
- **Opcode Op**：操作码。
- **clone()**：克隆指令。
- **getOpcode()**：获取操作码。
- **cloneWithRename()**：重命名克隆。
- **getOpcodeName()**：获取操作码名称。
- **isBinaryOp()**：是否为二元操作。
- **isComparisonOp()**：是否为比较操作。
- **isTerminator()**：是否为终结指令。
- **isCopy()**：是否为复制指令。
- **mayHaveSideEffects()**：是否有副作用。
- **hasResult()**：是否有结果。
- **toString()**：转字符串（纯虚函数）。

#### 主要子类
- **BinaryOperator**：二元操作指令，getLHS()/getRHS() 获取左右操作数。
- **UnaryOperator**：一元操作指令，getOperand() 获取操作数。
- **ICmpInst/FCmpInst**：整数/浮点比较指令，带谓词。
- **AllocaInst**：内存分配指令，getAllocatedSize() 获取分配大小。
- **LoadInst/StoreInst**：内存读写指令。
- **CallInst**：函数调用指令，getCalledFunction() 获取被调用函数。
- **ReturnInst**：返回指令，getReturnValue() 获取返回值。
- **BranchInst**：分支指令，TrueBlock/FalseBlock 分支目标块。
- **PhiInst**：Phi合流指令（SSA），管理前驱块和值。
- **GetElementPtrInst**：获取数组元素地址指令。
- **CastInst**：类型转换指令。
- **CopyInst**：复制指令。

---

## 6. 基本块 BasicBlock

- **vector<unique_ptr<Instruction>> Instructions**：指令列表。
- **Function *Parent**：所属函数。
- **vector<BasicBlock *> Predecessors/Successors**：前驱/后继基本块。
- **addInstruction()**：添加指令。
- **insertBeforeTerminator()**：在终结指令前插入。
- **insert()**：插入到指定位置。
- **addPredecessor()/removePredecessor()**：添加/移除前驱。
- **addSuccessor()/removeSuccessor()**：添加/移除后继。
- **getPredecessors()/getSuccessors()**：获取前驱/后继。
- **getTerminator()**：获取终结指令。
- **getInstructions()**：获取所有指令。
- **hasTerminator()**：是否有终结指令。
- **containsByName()**：是否包含指定指令。
- **toString()**：转字符串。

---

## 7. 参数 Argument

- **Function *Parent**：所属函数。
- **unsigned ArgNo**：参数编号。
- **toString()**：转字符串。

---

## 8. 函数 Function

- **vector<unique_ptr<BasicBlock>> BasicBlocks**：基本块列表。
- **vector<unique_ptr<Argument>> Arguments**：参数列表。
- **Module *Parent**：所属模块。
- **addBasicBlock()**：添加基本块。
- **getEntryBlock()**：获取入口基本块。
- **getBasicBlocks()**：获取所有基本块。
- **addArgument()**：添加参数。
- **getArguments()**：获取参数。
- **getFunctionType()**：获取函数类型。
- **getInstructionCount()**：获取指令数量。
- **isLibraryFunction()**：是否为库函数。
- **isRecursive()**：是否为递归函数。
- **toString()**：转字符串。

---

## 9. 模块 Module

- **string Name**：模块名。
- **vector<unique_ptr<Function>> Functions**：函数列表。
- **vector<unique_ptr<GlobalVariable>> GlobalVariables**：全局变量列表。
- **addFunction()**：添加函数。
- **addGlobalVariable()**：添加全局变量。
- **getFunction()**：查找函数。
- **getGlobalVariable()**：查找全局变量。
- **toString()**：转字符串。
- **printBasicBlockInfo()**：调试输出基本块后继信息。

---

## 结语

本文件是 IR 层的核心数据结构，建议结合 IRDataStructure.cpp 查看实现细节。如需进一步了解 IR