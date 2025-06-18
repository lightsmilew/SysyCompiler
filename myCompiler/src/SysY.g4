grammar SysY;

// 开始符号
compUnit: (decl | funcDef)* EOF;

// 声明
decl
    : constDecl    #constDeclaration
    | varDecl      #variableDeclaration
    ;

// 常量声明
constDecl: CONST bType constDef (COMMA constDef)* SEMICOLON;
bType
    : INT          #typeInt
    | FLOAT        #typeFloat
    ;
constDef: Ident (LBRACKET constExp RBRACKET)* ASSIGN constInitVal;
constInitVal
    : constExp     #constInitExpr
    | LBRACE (constInitVal (COMMA constInitVal)*)? RBRACE   #constInitList
    ;

// 变量声明
varDecl: bType varDef (COMMA varDef)* SEMICOLON;
varDef: Ident (LBRACKET constExp RBRACKET)* (ASSIGN initVal)?;
initVal
    : exp          #initExpr
    | LBRACE (initVal (COMMA initVal)*)? RBRACE    #initList
    ;

// 函数定义
funcDef: funcType Ident LPAREN funcFParams? RPAREN block;
funcType
    : VOID         #typeVoid
    | bType        #typeBType // For int or float return types
    ;
funcFParams: funcFParam (COMMA funcFParam)*;
funcFParam: 
    bType Ident                                                    # scalarParam
    | bType Ident LBRACKET RBRACKET (LBRACKET constExp RBRACKET)* # arrayParamNoSize  
    | bType Ident LBRACKET constExp RBRACKET (LBRACKET constExp RBRACKET)* # arrayParamWithSize
    ;

// 语句块
block: LBRACE blockItem* RBRACE;
blockItem
    : decl         #itemDecl
    | stmt         #itemStmt
    ;

// 语句
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

// 表达式
exp: lOrExp; // The top-level expression is a logical OR expression
cond: lOrExp;
lVal: Ident (LBRACKET exp RBRACKET)*;

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

funcRParams: exp (COMMA exp)*;

mulExp
    : unaryExp                      #toUnaryExp_mul
    | mulExp (MUL | DIV | MOD) unaryExp #mulDivModExp // Could be split further if needed: #mulExp | #divExp | #modExp
    ;
addExp
    : mulExp                        #toMulExp_add
    | addExp (PLUS | MINUS) mulExp  #addSubExp // Could be split: #addExp | #subExp
    ;
relExp
    : addExp                        #toAddExp_rel
    | relExp (LT | GT | LE | GE) addExp #relOpExp // Could be split
    ;
eqExp
    : relExp                        #toRelExp_eq
    | eqExp (EQ | NE) relExp        #eqOpExp // Could be split
    ;
lAndExp
    : eqExp                         #toEqExp_land
    | lAndExp AND eqExp             #landOpExp
    ;
lOrExp
    : lAndExp                       #toLAndExp_lor
    | lOrExp OR lAndExp             #lorOpExp
    ;

constExp: addExp; // constExp is usually a subset of exp, often enforced semantically

// --- 词法符号定义 ---
// 关键字和操作符
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

ASSIGN: '=';
LPAREN: '(';
RPAREN: ')';
LBRACE: '{';
RBRACE: '}';
LBRACKET: '[';
RBRACKET: ']';
COMMA: ',';
SEMICOLON: ';';

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

// 现有词法单元
Ident: [_a-zA-Z][_a-zA-Z0-9]*; // Using a fragment for IDENTIFIER is common
IntConst:   [1-9][0-9]*                    // 十进制：123, 456
            | '0'                          // 单独的0（十进制）
            | '0'[0-7]+                    // 八进制：01, 012, 0567
            | ('0x' | '0X') [0-9a-fA-F]+  // 十六进制：0x123, 0XAbc
            ;
FloatConst: 
            // 十进制
            [0-9]+ '.' [0-9]*
            | '.' [0-9]+ ([eE][+-]? [0-9]+)?
            | [0-9]+ ('.' [0-9]*)? [eE][+-]? [0-9]+
            // 十六进制
            | ('0x' | '0X') [0-9a-fA-F]+ '.' [0-9a-fA-F]*
            | ('0x' | '0X') '.' [0-9a-fA-F]+ ([pP][+-]? [0-9a-fA-F]+)?
            | ('0x' | '0X') [0-9a-fA-F]+ ('.' [0-9a-fA-F]*)? [pP][+-]? [0-9a-fA-F]+;

STRING_LITERAL: '"' (~["\\\r\n] | '\\' .)* '"';

// 忽略注释和空白字符
COMMENT: ('//' ~[\r\n]* | '/*' .*? '*/') -> skip;
WS: [ \t\r\n]+ -> skip;