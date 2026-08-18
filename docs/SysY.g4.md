# SysY.g4 文法参考手册

本文档对照仓库中的手写文法 `myCompiler/src/SysY.g4`（ANTLR 4.13.2），按规则逐条说明产生式、词法、优先级、`#label` 与 Visitor / AST 的对应关系，以及本实现相对官方 SysY 的扩展与限制。改文法、重生成、扩语言特性时请同时参考 [扩展文法与中端改造指南](./扩展文法与中端改造指南.md)。

---

## 1. 文件与工具链

| 项 | 路径 / 说明 |
|---|---|
| 手写文法 | `myCompiler/src/SysY.g4` |
| ANTLR 生成器 | 仓库根 `antlr-4.13.2-complete.jar` |
| C++ 运行时 | `myCompiler/3rd_party/antlr4-runtime/`（只链接，不生成代码） |
| 生成物（勿手改） | `myCompiler/src/frontend/generate/` |
| Parse Tree → AST | `frontend/ASTNodeVisitor.*` |
| AST 节点 | `frontend/ASTNode.*` |
| 语义约束 | `frontend/SemanticAnalysis.*`、`frontend/README.md` |

**文法头：**

```antlr
grammar SysY;
```

生成选项（本仓库约定）：`-Dlanguage=Cpp -no-listener -visitor`。因此每个 `#label` 会生成独立的 `visitXxx`，而不是 Listener 的 `enter/exit`。

重生成命令见 [Frontend.md](./Frontend.md) 与 [扩展文法与中端改造指南](./扩展文法与中端改造指南.md)「重生成前端」。改完 g4 后必须重跑 ANTLR，再 `./run.sh -rebuild`。

---

## 2. ANTLR4 约定（读本文件时用）

### 2.1 命名

- **语法规则**（parser rule）：小写开头，如 `compUnit`、`stmt`。会生成 `XxxContext`。
- **词法规则**（lexer rule）：大写开头，如 `CONST`、`Ident`、`IntConst`。会生成 token 类型。
- **`#label`**：写在某一产生式备选（alternative）末尾。ANTLR 为该备选生成独立 Context 子类和 `visitLabel`。

例：

```antlr
decl
    : constDecl    #constDeclaration
    | varDecl      #variableDeclaration
    ;
```

访问时走 `visitConstDeclaration` / `visitVariableDeclaration`，而不是笼统的 `visitDecl`（基类仍有 `visitDecl`，默认再分发到子 label）。

### 2.2 量词

| 写法 | 含义 |
|---|---|
| `X?` | 0 或 1 次 |
| `X*` | 0 或多次 |
| `X+` | 1 或多次 |
| `(A \| B)` | 二选一 |
| `(X (COMMA X)*)` | 逗号分隔的 1+ 列表 |

### 2.3 左递归与结合性

表达式层（`mulExp`、`addExp`、`relExp`、`eqExp`、`lAndExp`、`lOrExp`）全部写成**直接左递归**：

```antlr
addExp
    : mulExp
    | addExp (PLUS | MINUS) mulExp
    ;
```

ANTLR4 支持直接左递归，并按**左结合**改写。因此 `a - b - c` 解析为 `(a - b) - c`，而不是 `a - (b - c)`。

一元运算 `unaryOp unaryExp` 是右递归，因此 `!!-x` 为 `!(!(-x))`。

### 2.4 词法优先级

Lexer 按以下原则匹配：

1. **最长匹配**优先（`<=` 不会拆成 `<` 和 `=`）。
2. 长度相同时，**写在前面的规则优先**。因此关键字必须写在 `Ident` **之前**，否则 `int` 会被当成标识符。
3. 多字符算符若与单字符前缀冲突（如将来加 `+=`、`++`），应放在 `PLUS` **之前**，或依赖最长匹配。

### 2.5 悬挂 else（dangling else）

```antlr
IF LPAREN cond RPAREN stmt (ELSE stmt)?
```

`if (c1) if (c2) s1 else s2` 有歧义。ANTLR 默认 **shift**，即 `else` 绑定到最近的 `if`：

```
if (c1)
    if (c2) s1
    else s2
```

与 C / 官方 SysY 一致。

### 2.6 skip 通道

`COMMENT`、`WS` 使用 `-> skip`，不进入 Parser token 流，Visitor 看不到它们。

---

## 3. 语言能力一览（当前 g4）

**有：**

- 类型：`int`、`float`、`void`（仅函数返回值）
- 全局 / 局部：`const` 声明、变量声明、多维数组、花括号初始化列表
- 函数：标量形参、数组形参（第一维可省略或写出）
- 语句：赋值、表达式语句、块、`if`/`else`、`while`、`break`、`continue`、`return`
- 表达式：算术 `+ - * / %`、一元 `+ - !`、关系 `< > <= >=`、相等 `== !=`、逻辑 `&& ||`
- 字面量：十/八/十六进制整数、十/十六进制浮点、字符串（扩展）
- 注释：`//`、`/* */`

**无（文法层不存在）：**

- `for`、`do-while`、`switch`
- `<<` `>>`、复合赋值 `+=` 等、自增 `++` `--`
- `double`、`unsigned`、`char`、`bool`、指针、结构体
- 三元 `?:`、逗号表达式（`,` 只作分隔符）

扩展这些特性的步骤见 [扩展文法与中端改造指南](./扩展文法与中端改造指南.md)。

---

## 4. 编译单元与声明

### 4.1 `compUnit` — 开始符号

```antlr
compUnit: (decl | funcDef)* EOF;
```

一个 `.sy` 文件是**零个或多个**顶层声明或函数定义，然后必须吃掉 `EOF`。

- 允许空文件（仅空白/注释）。
- 全局变量与函数可交错出现。
- **语义**（非文法）：必须恰好有一个 `int main()`，且无参数。见 `frontend/README.md`。

**Visitor：** `visitCompUnit` → `CompUnitNode`。

示例：

```c
int a = 1;
int add(int x, int y) { return x + y; }
int main() { return add(a, 2); }
```

Parse Tree 骨架：

```
compUnit
├── decl → varDecl          // int a = 1;
├── funcDef                 // add
└── funcDef                 // main
```

---

### 4.2 `decl`

```antlr
decl
    : constDecl    #constDeclaration
    | varDecl      #variableDeclaration
    ;
```

| Label | 含义 | Visitor | AST |
|---|---|---|---|
| `#constDeclaration` | 常量声明 | `visitConstDeclaration` | 转调 `constDecl` |
| `#variableDeclaration` | 变量声明 | `visitVariableDeclaration` | 转调 `varDecl` |

一条 `decl` 在 AST 侧通常展开为 **`vector<DeclStmtNode>`**（因为 `const int a=1, b=2;` 是一条声明、多个定义）。

---

### 4.3 `bType` — 基本类型

```antlr
bType
    : INT          #typeInt
    | FLOAT        #typeFloat
    ;
```

仅 `int` / `float`。`void` 不在这里，只出现在 `funcType`。

Visitor 里 `visitTypeInt` / `visitTypeFloat` 可走默认实现；实际类型多半用 `ctx->bType()->getText()` 再 `convertToPrimaryDataType`。

---

### 4.4 `constDecl` / `constDef` / `constInitVal`

```antlr
constDecl: CONST bType constDef (COMMA constDef)* SEMICOLON;
constDef: Ident (LBRACKET constExp RBRACKET)* ASSIGN constInitVal;
constInitVal
    : constExp     #constInitExpr
    | LBRACE (constInitVal (COMMA constInitVal)*)? RBRACE   #constInitList
    ;
```

要点：

- 必须以 `const` 开头，类型只能是 `bType`。
- **必须有初值**（`ASSIGN` 不可省）。`const int a;` 非法。
- 维数用 `constExp`（编译期常量，语义阶段再检查）。
- 可一次声明多个：`const int a=1, b[2]={1,2};`
- 初值两种：
  - `#constInitExpr`：单个常量表达式
  - `#constInitList`：`{ ... }`，可空（`{}` 表示全 0）、可嵌套

示例：

```c
const int N = 10, A[2][2] = {{1, 2}, {3, 4}};
const float PI = 3.14;
```

**语义补充（文法不检查）：**

- 全局 / 局部 const 初值都必须是常量表达式。
- 数组维长必须是非负整数常量。
- 整型数组初值列表中不能出现浮点元素。
- 列表元素个数不能超过数组总长；不足则隐式补 0 / 0.0。

---

### 4.5 `varDecl` / `varDef` / `initVal`

```antlr
varDecl: bType varDef (COMMA varDef)* SEMICOLON;
varDef: Ident (LBRACKET constExp RBRACKET)* (ASSIGN initVal)?;
initVal
    : exp          #initExpr
    | LBRACE (initVal (COMMA initVal)*)? RBRACE    #initList
    ;
```

与 const 的差异：

| | const | var |
|---|---|---|
| 关键字 | 必须 `const` | 无 |
| 初值 | **必须** | 可选 |
| 初值表达式 | `constExp`（`addExp`） | `exp`（`lOrExp`） |

- 局部未初始化变量：语义上未定义，IR 通常仍 alloca，不写初值。
- 全局未初始化：语义规定元素为 `0` / `0.0`。
- **全局变量初值必须是常量表达式**（语义阶段，不是文法阶段）。文法允许 `int g = f();`，语义应报错。

数组初始化列表的三种合法形态（语义，与官方 SysY 一致）：

1. `{}`：全 0。
2. 与维数完全对应，或展平：`a[3][2]` 可用 `{{1,2},{3,4},{5,6}}` 或 `{1,2,3,4,5,6}`。
3. 某维不足则该维其余补 0：`{{1,2},{3},{5}}` → `{{1,2},{3,0},{5,0}}`。

---

## 5. 函数

### 5.1 `funcDef` / `funcType`

```antlr
funcDef: funcType Ident LPAREN funcFParams? RPAREN block;
funcType
    : VOID         #typeVoid
    | bType        #typeBType
    ;
```

- 返回类型：`void` / `int` / `float`。
- 函数体必须是 `block`，不能是单条语句。
- 无参时括号不可省：`int main()`，不能写成 `int main`。
- 文法**不支持**函数声明（prototype），只有定义。
- 文法**不支持**函数嵌套定义（`funcDef` 只出现在 `compUnit`）。

**Visitor：** `visitFuncDef` → `FuncNode`。

注意：当前 `visitTypeBType` 实现里写死返回 `INT`，真正返回类型是 `visitFuncDef` 用 `ctx->funcType()->getText()` 解析的。改 `funcType` 时不要只改其中一个。

---

### 5.2 `funcFParams` / `funcFParam`

```antlr
funcFParams: funcFParam (COMMA funcFParam)*;
funcFParam:
      bType Ident                                                    # scalarParam
    | bType Ident LBRACKET RBRACKET (LBRACKET constExp RBRACKET)*    # arrayParamNoSize
    | bType Ident LBRACKET constExp RBRACKET (LBRACKET constExp RBRACKET)* # arrayParamWithSize
    ;
```

| Label | 源码形态 | 语义 |
|---|---|---|
| `#scalarParam` | `int x` | 按值传递 |
| `#arrayParamNoSize` | `int a[]`、`int a[][10]` | 第一维省略。Visitor 用维长 `-1` 占位 |
| `#arrayParamWithSize` | `int a[10]`、`int a[2][3]` | 第一维也写出。SysY 官方通常要求第一维省略；本 g4 **额外允许**写出第一维 |

数组形参按指针/首地址传递，第一维长度在调用约定里不参与步长计算；后续维必须是常量，用于 GEP。

**不支持：**

- 默认参数
- `const` 形参（`const int x` 无法匹配：`funcFParam` 以 `bType` 开头）
- 函数指针形参

---

### 5.3 `funcRParams` — 实参

```antlr
funcRParams: exp (COMMA exp)*;
```

出现在 `Ident LPAREN funcRParams? RPAREN`（`#callExp`）。

- 实参是 `exp`，不是 `cond`。
- 本实现扩展了 `STRING_LITERAL` 作为 `primaryExp`，因此 `putf("%d\n", x)` 能在文法层通过。
- 参数个数、类型匹配在语义阶段检查。

---

## 6. 语句

### 6.1 `block` / `blockItem`

```antlr
block: LBRACE blockItem* RBRACE;
blockItem
    : decl         #itemDecl
    | stmt         #itemStmt
    ;
```

- 块内可交错声明与语句（C99 风格，不是 C89 的“声明必须在前”）。
- 空块 `{}` 合法。
- `#itemDecl` 返回 `vector<StmtNode>`（一条 `int a,b;` 拆成多个 `DeclStmtNode`）。
- `#itemStmt` 把单条语句包成单元素 vector，便于 `visitBlock` 统一拼接。

---

### 6.2 `stmt`

```antlr
stmt
    : lVal ASSIGN exp SEMICOLON               #assignStmt
    | exp? SEMICOLON                          #exprStmt
    | block                                   #blockStmt
    | IF LPAREN cond RPAREN stmt (ELSE stmt)? #ifStmt
    | WHILE LPAREN cond RPAREN stmt           #whileStmt
    | BREAK SEMICOLON                         #breakStmt
    | CONTINUE SEMICOLON                      #continueStmt
    | RETURN exp? SEMICOLON                   #returnStmt
    ;
```

#### `#assignStmt`

```c
a = 1;
arr[i][j] = x + y;
```

- 左值只能是 `lVal`（标识符或下标），不能是函数调用、字面量。
- `a + b = 1;` 非法（`a + b` 不是 `lVal`）。
- 没有复合赋值。`a += 1` 会在 `+=` 处词法失败（`+` 与 `=` 是两个 token，无法匹配本规则）。

#### `#exprStmt`

```c
foo();
1 + 2;
;
```

- `exp?` 允许空语句 `;`。
- 函数调用作为语句走这里，不是独立的 call-stmt 规则。

**二义性：** `a = 1;` 同时像赋值和“表达式 + 分号”。ANTLR 按**备选顺序**，`#assignStmt` 写在前面，优先匹配赋值。这是正确的：赋值不是 `exp` 的一部分（`exp` 里没有 `=`）。

#### `#blockStmt`

语句位置上的 `{ ... }`，与函数体的 `block` 共用同一规则。语义上会开新作用域。

#### `#ifStmt`

```c
if (cond) stmt
if (cond) stmt else stmt
```

- `cond` 必须带括号。
- then/else 是任意 `stmt`，不必是块。
- 无 `else if` 关键字；`else if` 只是 `else` 后跟另一条 `if` 语句。

#### `#whileStmt`

```c
while (cond) stmt
```

无 `for`。`break` / `continue` 只对 while（及将来的循环）有意义。

#### `#breakStmt` / `#continueStmt`

必须带分号。是否在循环内由语义检查；文法允许在函数任意位置写 `break;`。

#### `#returnStmt`

```c
return;
return exp;
```

- `void` 函数应 `return;` 或不写（语义）。
- `int`/`float` 函数应带返回值（语义）。
- `main` 的返回值作为进程退出码（运行时约定）。

---

## 7. 表达式与优先级

### 7.1 分层总表

文法用**分层 + 左递归**编码优先级。从低到高：

| 层 | 规则 | 运算符 | 结合性 | 提升 label（无运算时） | 运算 label |
|---|---|---|---|---|---|
| 0 | `exp` / `cond` | （别名） | — | — | — |
| 1 | `lOrExp` | `\|\|` | 左 | `#toLAndExp_lor` | `#lorOpExp` |
| 2 | `lAndExp` | `&&` | 左 | `#toEqExp_land` | `#landOpExp` |
| 3 | `eqExp` | `==` `!=` | 左 | `#toRelExp_eq` | `#eqOpExp` |
| 4 | `relExp` | `<` `>` `<=` `>=` | 左 | `#toAddExp_rel` | `#relOpExp` |
| 5 | `addExp` | `+` `-` | 左 | `#toMulExp_add` | `#addSubExp` |
| 6 | `mulExp` | `*` `/` `%` | 左 | `#toUnaryExp_mul` | `#mulDivModExp` |
| 7 | `unaryExp` | `+` `-` `!`（前缀）、函数调用 | 右（一元） | `#toPrimaryExp` | `#opUnaryExp` / `#callExp` |
| 8 | `primaryExp` | `()`、左值、数字、字符串 | — | 各 label | — |

`constExp` **不是**完整 `exp`，而是直接等于 `addExp`（见 7.4）。

### 7.2 `exp` 与 `cond`

```antlr
exp: lOrExp;
cond: lOrExp;
```

文法上二者完全相同，都是逻辑或表达式。区分是语义 / IR 的：

| 出现位置 | 规则 | 典型用途 |
|---|---|---|
| 赋值右值、实参、`return`、表达式语句、变量初值 | `exp` | 算术值 |
| `if` / `while` 条件 | `cond` | 需要转 i1 / 短路 |

`frontend/README.md` 写过「表达式不能使用与或非和比较指令」：指 **`exp` 语境下的语义限制**（或 IR 生成策略），不是 g4 禁止。g4 允许 `int x = a < b;` 通过语法分析。

短路：`&&` / `||` 的短路求值在 IR 构建，不在文法。

### 7.3 各层产生式

```antlr
lOrExp
    : lAndExp                       #toLAndExp_lor
    | lOrExp OR lAndExp             #lorOpExp
    ;

lAndExp
    : eqExp                         #toEqExp_land
    | lAndExp AND eqExp             #landOpExp
    ;

eqExp
    : relExp                        #toRelExp_eq
    | eqExp (EQ | NE) relExp        #eqOpExp
    ;

relExp
    : addExp                        #toAddExp_rel
    | relExp (LT | GT | LE | GE) addExp #relOpExp
    ;

addExp
    : mulExp                        #toMulExp_add
    | addExp (PLUS | MINUS) mulExp  #addSubExp
    ;

mulExp
    : unaryExp                      #toUnaryExp_mul
    | mulExp (MUL | DIV | MOD) unaryExp #mulDivModExp
    ;
```

同一层多个算符共享一个 label（如 `#addSubExp` 同时覆盖 `+` 和 `-`）。Visitor 里用 `ctx->PLUS()` / `ctx->MINUS()` 等判断具体算符。

**没有**按算符再拆 `#addExp` / `#subExp`。g4 注释里写过 “Could be split”，当前未拆。

解析例子：`a + b * c - d`

```
addExp                          #addSubExp  (-)
├── addExp                      #addSubExp  (+)
│   ├── addExp → mulExp → a
│   └── mulExp                  #mulDivModExp (*)
│       ├── mulExp → b
│       └── unaryExp → c
└── mulExp → d
```

即 `(a + (b * c)) - d`。

### 7.4 `constExp`

```antlr
constExp: addExp;
```

常量表达式在**文法**上只下沉到加减层：

- 允许：`1+2*3`、`-4`、`1.0`、括号、（文法上）函数调用与左值
- **不允许**（文法直接失败）：`1 < 2`、`a && b`、`!x` 作为顶层 const 里的关系/逻辑——因为它们在 `relExp` / `lAndExp` / `unaryOp` 的 `!` 仍可通过 `addExp → ... → unaryExp` 进来

更精确地说：

- `!`、函数调用、左值 **可以**出现在 `constExp` 里（它们属于 `unaryExp` / `primaryExp`，在 `addExp` 之下）。
- `< > <= >= == != && ||` **不能**出现在 `constExp` 里（它们在 `addExp` 之上）。

真正的“必须是编译期可求值、无副作用、无 void 调用”由语义分析 / 常量折叠保证。

### 7.5 `lVal`

```antlr
lVal: Ident (LBRACKET exp RBRACKET)*;
```

- `x`：变量。
- `a[i]`、`a[i][j]`：数组元素。下标是 `exp`（运行期），不是 `constExp`。
- 维数是否与声明一致、下标是否为整数：语义检查。
- 不能写 `*p`、`p->x`（无指针文法）。

**Visitor：** `visitLVal` → `LValueExprNode`（名字 + 下标表达式列表）。

### 7.6 `primaryExp` / `number`

```antlr
primaryExp
    : LPAREN exp RPAREN #parenExp
    | lVal              #lValExp
    | number            #numberExp
    | STRING_LITERAL    #stringLiteralExp
    ;

number
    : IntConst          #intNum
    | FloatConst        #floatNum
    ;
```

| Label | 形态 | AST |
|---|---|---|
| `#parenExp` | `(exp)` | 直接返回内层 `ExprNode`（括号不建节点） |
| `#lValExp` | 左值当右值 | `LValueExprNode` 向上转为 `ExprNode` |
| `#numberExp` | 数字 | `IntLiteralExprNode` / `FloatLiteralExprNode` |
| `#stringLiteralExp` | `"..."` | `StringLiteralExprNode`（去掉首尾引号） |

`#stringLiteralExp` 是**本编译器扩展**，官方 SysY 核心文法通常没有字符串；用于 `putf` 等运行时库。

### 7.7 `unaryExp` / `unaryOp`

```antlr
unaryExp
    : primaryExp                      #toPrimaryExp
    | Ident LPAREN funcRParams? RPAREN #callExp
    | unaryOp unaryExp                #opUnaryExp
    ;

unaryOp
    : PLUS        #opPlus
    | MINUS       #opMinus
    | NOT         #opNot
    ;
```

- 函数调用是 `unaryExp` 的一臂，不是 `primaryExp`。因此 `foo()[0]` **无法**解析（调用结果不能再下标）。这与 SysY「函数不能返回数组」一致。
- `+x`、`-x`、`!x` 可叠：`--x` 是 `-(-x)`，不是自减。
- `!` 的操作数类型、结果类型（通常转 int 0/1）由语义 / IR 处理。

调用与左值的区分：`foo` 是 `lVal`；`foo()` 是 `#callExp`。Lexer 看到 `Ident` 后若跟 `(` 走调用。

---

## 8. 词法规则

词法区从 `CONST` 开始，到 `WS` 结束。生成 token 名与 g4 规则名一致。

### 8.1 关键字

```antlr
CONST: 'const';
INT: 'int';
FLOAT: 'float';
VOID: 'void';
IF: 'if';
ELSE: 'else';
WHILE: 'while';
BREAK: 'break';
CONTINUE: 'continue';
RETURN: 'return';
```

必须位于 `Ident` 之前。大小写敏感：`Int`、`IF` 不是关键字，会成为标识符。

当前**不是**关键字的保留字（写成标识符合法，但可能与库函数撞名）：`for`、`do`、`switch`、`case`、`default`、`unsigned`、`double`、`char`、`struct` 等。

### 8.2 分隔符与赋值

```antlr
ASSIGN: '=';
LPAREN: '(';
RPAREN: ')';
LBRACE: '{';
RBRACE: '}';
LBRACKET: '[';
RBRACKET: ']';
COMMA: ',';
SEMICOLON: ';';
```

`=` 只作赋值 / 初始化，不作相等（相等是 `EQ: '=='`）。

### 8.3 算术与逻辑算符

```antlr
PLUS: '+';
MINUS: '-';
MUL: '*';
DIV: '/';
MOD: '%';
NOT: '!';

LT: '<';
GT: '>';
LE: '<=';
GE: '>=';
EQ: '==';
NE: '!=';
AND: '&&';
OR: '||';
```

- 没有 `&` `|` `^` `~`（位运算）、没有 `<<` `>>`。
- `!` 只作逻辑非，不与 `!=` 冲突（最长匹配）。
- 单 `&` 或 `|` 无法匹配 `AND`/`OR`，会词法错误。

### 8.4 `Ident`

```antlr
Ident: [_a-zA-Z][_a-zA-Z0-9]*;
```

- 首字符：字母或 `_`，不能是数字。
- 后续：字母、数字、`_`。
- 无 Unicode 标识符。
- 关键字优先，故 `int` 不会成为 `Ident`。

### 8.5 `IntConst`

```antlr
IntConst:   [1-9][0-9]*                    // 十进制：123
            | '0'                          // 单独的 0
            | '0'[0-7]+                    // 八进制：01, 012
            | ('0x' | '0X') [0-9a-fA-F]+  // 十六进制：0x123, 0XAbc
            ;
```

| 形态 | 例 | Visitor 解析 |
|---|---|---|
| 十进制 | `42`、`1` | `stoi(..., 10)` |
| 零 | `0` | 十进制 0 |
| 八进制 | `012`、`07` | 前导 `0` 且其余为 0–7 → `stoi(..., 8)` |
| 十六进制 | `0x1f`、`0XABC` | `stoi(..., 16)` |

**注意：**

- `08`、`09`：不能当八进制（含 8/9），也不能当十进制（十进制不以 0 开头）。会词法失败或被拆成 `0` 和 `8`（取决于后续字符）。不要写前导零的十进制。
- 没有后缀 `u`/`l`/`ll`。
- 溢出：`stoi` 在语义/AST 阶段，超出 `int32` 会抛异常，g4 不限制位数。

### 8.6 `FloatConst`

```antlr
FloatConst:
    // 十进制
      [0-9]+ '.' [0-9]*
    | '.' [0-9]+ ([eE][+-]? [0-9]+)?
    | [0-9]+ ('.' [0-9]*)? [eE][+-]? [0-9]+
    // 十六进制
    | ('0x' | '0X') [0-9a-fA-F]+ '.' [0-9a-fA-F]*
    | ('0x' | '0X') '.' [0-9a-fA-F]+ ([pP][+-]? [0-9a-fA-F]+)?
    | ('0x' | '0X') [0-9a-fA-F]+ ('.' [0-9a-fA-F]*)? [pP][+-]? [0-9a-fA-F]+
    ;
```

合法例子：

| 类型 | 例子 |
|---|---|
| 小数 | `3.14`、`3.`、`.5` |
| 十进制指数 | `1e-3`、`2.0E+10`、`1.e2` |
| 十六进制小数 | `0x1.0`、`0x.8` |
| 十六进制 + p 指数 | `0x1.fp3`、`0X1P-2` |

AST 用 `std::stof` 转成 `float`（IEEE 单精度），不是 `double`。

**与 `IntConst` 的切分：**

- `1` 是 `IntConst`（没有 `.` 也没有 `e`）。
- `1.`、`1e0` 是 `FloatConst`。
- `0x10` 是 `IntConst`；`0x10.0`、`0x10p0` 是 `FloatConst`。

十六进制浮点的 `p` 指数在部分备选里是可选的（如 `0x.8`），与 C 标准不完全相同；以本 g4 为准。

### 8.7 `STRING_LITERAL`

```antlr
STRING_LITERAL: '"' (~["\\\r\n] | '\\' .)* '"';
```

- 双引号包围。
- 允许 `\"`、`\\`、`\n` 等：`\\` 后跟任意一个字符。
- **不允许**原始换行（`\r`/`\n` 不能出现在未转义内容里）。
- 不支持相邻字符串拼接（`"a" "b"` 是两个 token）。
- Visitor 去掉首尾 `"`，**不**在此处解释转义（`\n` 可能仍是两个字符 `\` 和 `n`，取决于后续是否再 unescape）。改字符串语义时要看 `visitStringLiteralExp`。

### 8.8 注释与空白

```antlr
COMMENT: ('//' ~[\r\n]* | '/*' .*? '*/') -> skip;
WS: [ \t\r\n]+ -> skip;
```

- `//` 到行末。
- `/* ... */` 非贪婪，到最近的 `*/`。不支持嵌套：`/* a /* b */ c */` 会在第一个 `*/` 结束。
- 空白：空格、Tab、CR、LF。

---

## 9. Token 编号（生成物快照）

以当前 `SysYLexer.h` 为准（改 g4 后会变）：

| # | Token | # | Token |
|---|---|---|---|
| 1 | `CONST` | 21 | `MINUS` |
| 2 | `INT` | 22 | `MUL` |
| 3 | `FLOAT` | 23 | `DIV` |
| 4 | `VOID` | 24 | `MOD` |
| 5 | `IF` | 25 | `NOT` |
| 6 | `ELSE` | 26 | `LT` |
| 7 | `WHILE` | 27 | `GT` |
| 8 | `BREAK` | 28 | `LE` |
| 9 | `CONTINUE` | 29 | `GE` |
| 10 | `RETURN` | 30 | `EQ` |
| 11 | `ASSIGN` | 31 | `NE` |
| 12 | `LPAREN` | 32 | `AND` |
| 13 | `RPAREN` | 33 | `OR` |
| 14 | `LBRACE` | 34 | `Ident` |
| 15 | `RBRACE` | 35 | `IntConst` |
| 16 | `LBRACKET` | 36 | `FloatConst` |
| 17 | `RBRACKET` | 37 | `STRING_LITERAL` |
| 18 | `COMMA` | 38 | `COMMENT`（skip） |
| 19 | `SEMICOLON` | 39 | `WS`（skip） |
| 20 | `PLUS` | | |

`COMMENT`、`WS` 被 skip，不会进入 Parser 的语法规则，但枚举里仍占号。**不要把编号写进手写代码**，应使用 `SysYLexer::INT` 这类符号常量。

---

## 10. `#label` → Visitor → AST 全表

手写 `ASTNodeVisitor` 按 g4 顺序实现。下表便于改文法时核对「每个新 label 都要有 visit」。

### 10.1 声明与类型

| g4 label / 规则 | `visit*` | 典型返回 / AST |
|---|---|---|
| `compUnit` | `visitCompUnit` | `CompUnitNode` |
| `#constDeclaration` | `visitConstDeclaration` | 转 `constDecl` |
| `#variableDeclaration` | `visitVariableDeclaration` | 转 `varDecl` |
| `constDecl` | `visitConstDecl` | `vector<DeclStmtNode>` |
| `#typeInt` / `#typeFloat` | 默认 / `getText()` | `int` / `float` |
| `constDef` | 在 `visitConstDecl` 内手解 | 无独立 visit |
| `#constInitExpr` | `visitConstInitExpr` | `InitExprNode` |
| `#constInitList` | `visitConstInitList` | `vector<InitExprNode>` |
| `varDecl` | `visitVarDecl` | `vector<DeclStmtNode>` |
| `varDef` | 在 `visitVarDecl` 内手解 | 无独立 visit |
| `#initExpr` | `visitInitExpr` | `InitExprNode` |
| `#initList` | `visitInitList` | `vector<InitExprNode>` |

### 10.2 函数

| g4 | `visit*` | AST |
|---|---|---|
| `funcDef` | `visitFuncDef` | `FuncNode` |
| `#typeVoid` | `visitTypeVoid` | `VOID` |
| `#typeBType` | `visitTypeBType` | 见实现（类型文本在 `funcDef` 解析） |
| `funcFParams` | `visitFuncFParams` | `vector<DeclStmtNode>` |
| `#scalarParam` | `visitScalarParam` | `DeclStmtNode` |
| `#arrayParamNoSize` | `visitArrayParamNoSize` | `DeclStmtNode`（首维 `-1`） |
| `#arrayParamWithSize` | `visitArrayParamWithSize` | `DeclStmtNode` |
| `funcRParams` | `visitFuncRParams` | `vector<ExprNode>` |

### 10.3 语句

| g4 | `visit*` | AST |
|---|---|---|
| `block` | `visitBlock` | `BlockStmtNode` |
| `#itemDecl` | `visitItemDecl` | `vector<StmtNode>` |
| `#itemStmt` | `visitItemStmt` | `vector<StmtNode>` |
| `#assignStmt` | `visitAssignStmt` | `AssignStmtNode` |
| `#exprStmt` | `visitExprStmt` | 表达式语句 / 空语句 |
| `#blockStmt` | `visitBlockStmt` | 转 `block` |
| `#ifStmt` | `visitIfStmt` | `IfElseStmtNode` |
| `#whileStmt` | `visitWhileStmt` | `WhileStmtNode` |
| `#breakStmt` | `visitBreakStmt` | `BreakStmtNode` |
| `#continueStmt` | `visitContinueStmt` | `ContinueStmtNode` |
| `#returnStmt` | `visitReturnStmt` | `ReturnStmtNode` |

### 10.4 表达式

| g4 | `visit*` | AST |
|---|---|---|
| `exp` | `visitExp` | 转 `lOrExp` |
| `cond` | `visitCond` | 转 `lOrExp` |
| `lVal` | `visitLVal` | `LValueExprNode` |
| `#parenExp` | `visitParenExp` | 内层表达式 |
| `#lValExp` | `visitLValExp` | `LValueExprNode` as `ExprNode` |
| `#numberExp` | `visitNumberExp` | 数字字面量 |
| `#stringLiteralExp` | `visitStringLiteralExp` | `StringLiteralExprNode` |
| `#intNum` | `visitIntNum` | `IntLiteralExprNode` |
| `#floatNum` | `visitFloatNum` | `FloatLiteralExprNode` |
| `#toPrimaryExp` | `visitToPrimaryExp` | 转 `primaryExp` |
| `#callExp` | `visitCallExp` | `CallExprNode` |
| `#opUnaryExp` | `visitOpUnaryExp` | `UnaryExprNode` |
| `#opPlus` / `#opMinus` / `#opNot` | `visitOpPlus` 等 | 算符字符串 |
| `#toUnaryExp_mul` | `visitToUnaryExp_mul` | 转 `unaryExp` |
| `#mulDivModExp` | `visitMulDivModExp` | 二元 `*` `/` `%` |
| `#toMulExp_add` | `visitToMulExp_add` | 转 `mulExp` |
| `#addSubExp` | `visitAddSubExp` | 二元 `+` `-` |
| `#toAddExp_rel` | `visitToAddExp_rel` | 转 `addExp` |
| `#relOpExp` | `visitRelOpExp` | 二元关系 |
| `#toRelExp_eq` | `visitToRelExp_eq` | 转 `relExp` |
| `#eqOpExp` | `visitEqOpExp` | `==` `!=` |
| `#toEqExp_land` | `visitToEqExp_land` | 转 `eqExp` |
| `#landOpExp` | `visitLandOpExp` | `&&` |
| `#toLAndExp_lor` | `visitToLAndExp_lor` | 转 `lAndExp` |
| `#lorOpExp` | `visitLorOpExp` | `\|\|` |
| `constExp` | `visitConstExp` | 转 `addExp` |

提升类 label（`#toXxx`）只做“降一层”，不建新 AST 节点。运算类 label 建二元 / 一元节点。

---

## 11. 文法结构图

```
compUnit
├── decl
│   ├── constDecl → bType → constDef* → constInitVal
│   └── varDecl   → bType → varDef*   → initVal?
└── funcDef
    ├── funcType (void | bType)
    ├── Ident ( )
    ├── funcFParams? → funcFParam (scalar | array)
    └── block → blockItem*
                    ├── decl
                    └── stmt
                        ├── assign / expr / block / if / while
                        ├── break / continue / return
                        └── 其中 cond / exp / lVal 进入表达式层

表达式（低优先级 → 高）
lOrExp → lAndExp → eqExp → relExp → addExp → mulExp → unaryExp → primaryExp
                                              ↑
                                         constExp 停在这里
```

---

## 12. 与官方 SysY / 语义的差异

文法是「能写出来并解析」的上界；语义更严。对照时分三层看：

| 点 | 本 g4 | 官方 / 本编译器语义 |
|---|---|---|
| 字符串字面量 | 有 `STRING_LITERAL` | 官方核心语言通常无；本编译器给 `putf` 用 |
| 数组形参第一维 | 可省略或写出 | 官方常见写法是必须 `[]`；写出第一维被本前端接受 |
| `exp` 含 `&&` `\|\|` 比较 | 文法允许 | 语义文档倾向：普通表达式不用逻辑/比较，条件才用 |
| `constExp` | 语法 = `addExp` | 语义：必须编译期可求值，无 void 调用 |
| 全局初值 | 文法可用任意 `exp` | 语义必须是常量表达式 |
| `main` | 文法只是普通函数 | 必须 `int main()` 无参、有且仅有一个 |
| `break`/`continue` | 任意语句位置 | 必须在循环内 |
| 数组维长 | `constExp` | 必须非负整数常量 |
| 下标 | `exp` | 必须是整数（不必是常量） |
| `for` / 移位 / `unsigned` / 指针 / `double` | 无 | 扩展指南中的规划项 |

---

## 13. 改 g4 的检查清单

1. **关键字**加在 `Ident` 之前；**多字符算符**加在单字符前缀之前（或确认最长匹配足够）。
2. 新备选加 **`#label`**，名字唯一、驼峰，与现有风格一致（`#forStmt` 而不是 `#For_stmt`）。
3. 左递归只用于「同优先级左结合」；不要间接左递归。
4. 可选子规则用 `?`，Visitor 里先判空：`if (ctx->funcFParams())`。
5. 重生成后在 `SysYBaseVisitor.h` / `SysYLexer.h` 确认新 `visitXxx` 和 token 名存在。
6. 在 `ASTNodeVisitor.h/.cpp` **实现**对应 `visitXxx`（不要只留 BaseVisitor 默认 `visitChildren`）。
7. 补 AST 节点、语义、IR、后端（见扩展指南）。
8. 不要手改 `frontend/generate/`。

常见坑：

- 字符集转义写错（如 `~['\\\r\n]`）会导致 Lexer 规则进不去，只剩 Parser 隐式 token。
- 新规则名与已有 token 冲突（Parser 规则应小写）。
- 只改 g4 不重生成：编译的仍是旧 Visitor 接口。
- WSL 下用 Windows `java.exe` 时，`-jar` / `-o` / `.g4` 都要走 `wslpath -w`，否则生成物可能写到错误目录。

---

## 14. 完整规则索引（按 g4 出现顺序）

| 行区 | 规则 | 角色 |
|---|---|---|
| 语法 | `compUnit` | 编译单元 |
| | `decl` | 声明分发 |
| | `constDecl` `bType` `constDef` `constInitVal` | 常量 |
| | `varDecl` `varDef` `initVal` | 变量 |
| | `funcDef` `funcType` `funcFParams` `funcFParam` | 函数 |
| | `block` `blockItem` | 块 |
| | `stmt` | 语句 |
| | `exp` `cond` `lVal` | 表达式入口 / 左值 |
| | `primaryExp` `number` | 原子 |
| | `unaryExp` `unaryOp` `funcRParams` | 一元 / 调用 |
| | `mulExp` `addExp` `relExp` `eqExp` `lAndExp` `lOrExp` | 二元分层 |
| | `constExp` | 常量表达式 |
| 词法 | 关键字 10 个 | `const` … `return` |
| | 分隔符 9 个 | `=` `()` `{}` `[]` `,` `;` |
| | 算符 15 个 | `+ - * / % !` 关系 逻辑 |
| | `Ident` `IntConst` `FloatConst` `STRING_LITERAL` | 名字与字面量 |
| | `COMMENT` `WS` | skip |

源文件全文：[`myCompiler/src/SysY.g4`](../myCompiler/src/SysY.g4)。

---

## 15. 最小合法程序与对应树

```c
int main() {
    return 0;
}
```

```
compUnit
└── funcDef
    ├── funcType → bType → INT          #typeBType / #typeInt
    ├── Ident = "main"
    ├── ( 无 funcFParams )
    └── block
        └── blockItem                   #itemStmt
            └── stmt                    #returnStmt
                └── exp → … → number    #intNum  0
```

带数组与调用的片段：

```c
int sum(int a[], int n) {
    int i = 0, s = 0;
    while (i < n) {
        s = s + a[i];
        i = i + 1;
    }
    return s;
}
```

- `int a[]` → `#arrayParamNoSize`
- `int n` → `#scalarParam`
- `int i = 0, s = 0;` → 一条 `varDecl`、两个 `varDef`
- `i < n` → `cond` → `#relOpExp`
- `a[i]` → `lVal`（Ident + 一维 `exp`）
- `s = s + a[i];` → `#assignStmt`，右值 `#addSubExp`

---

*文档对应 `SysY.g4` 当前版本。若只改了 g4 未同步本文，以 `.g4` 与 `frontend/generate/SysYVisitor.h` 为准。*
