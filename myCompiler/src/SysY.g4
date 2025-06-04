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
constDef: ident (LBRACKET constExp RBRACKET)* ASSIGN constInitVal;
constInitVal
    : constExp     #constInitExpr
    | LBRACE (constInitVal (COMMA constInitVal)*)? RBRACE   #constInitList
    ;

// 变量声明
varDecl: bType varDef (COMMA varDef)* SEMICOLON;
varDef: ident (LBRACKET constExp RBRACKET)* (ASSIGN initVal)?;
initVal
    : exp          #initExpr
    | LBRACE (initVal (COMMA initVal)*)? RBRACE    #initList
    ;

// 函数定义
funcDef: funcType ident LPAREN funcFParams? RPAREN block;
funcType
    : VOID         #typeVoid
    | bType        #typeBType // For int or float return types
    ;
funcFParams: funcFParam (COMMA funcFParam)*;
funcFParam
    : bType ident                                         #scalarParam
    | bType ident LBRACKET RBRACKET (LBRACKET exp RBRACKET)* #arrayParam // Represents int a[] or int a[][exp] etc.
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
exp: addExp;
cond: lOrExp;
lVal: ident (LBRACKET exp RBRACKET)*;

primaryExp
    : LPAREN exp RPAREN #parenExp
    | lVal              #lValExp
    | number            #numberExp
    ;
number
    : intConst          #intNum
    | floatConst        #floatNum
    ;

unaryExp
    : primaryExp                      #toPrimaryExp
    | ident LPAREN funcRParams? RPAREN #callExp
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
ident: IDENTIFIER; // Using a fragment for IDENTIFIER is common
intConst: DECIMAL_CONST | OCTAL_CONST | HEXADECIMAL_CONST;
floatConst: FLOAT_CONST;

fragment IDENTIFIER_NONDIGIT: '_' | [a-zA-Z$];
fragment IDENTIFIER_DIGIT: [0-9];
IDENTIFIER: (IDENTIFIER_NONDIGIT (IDENTIFIER_NONDIGIT | IDENTIFIER_DIGIT)*);

fragment NONZERO_DIGIT: [1-9];
DECIMAL_CONST: NONZERO_DIGIT IDENTIFIER_DIGIT* | '0'; // Allow '0' as a decimal const
OCTAL_CONST: '0' OCTAL_DIGIT+ ; // Ensure at least one octal digit after '0'
HEXADECIMAL_CONST: ('0x' | '0X') HEXADECIMAL_DIGIT+ ; // Ensure at least one hex digit
fragment OCTAL_DIGIT: [0-7];
fragment HEXADECIMAL_DIGIT: [0-9a-fA-F];

fragment FLOAT_PART_A: IDENTIFIER_DIGIT+ '.' IDENTIFIER_DIGIT*;
fragment FLOAT_PART_B: '.' IDENTIFIER_DIGIT+;
fragment EXPONENT_PART: [eE] [+-]? IDENTIFIER_DIGIT+;
fragment HEX_PREFIX: '0' [xX];
fragment HEX_DIGIT_SEQUENCE: HEXADECIMAL_DIGIT+;
fragment HEX_FRACTIONAL_CONSTANT:
    HEX_DIGIT_SEQUENCE? '.' HEX_DIGIT_SEQUENCE    // 0x.ABC 或 0x123.DEF
    | HEX_DIGIT_SEQUENCE '.'                      // 0x123.
    ;
fragment BINARY_EXPONENT_PART: [pP] [+-]? IDENTIFIER_DIGIT+;
FLOAT_CONST: 
    // 十进制浮点数
    (FLOAT_PART_A | FLOAT_PART_B) EXPONENT_PART?
    | IDENTIFIER_DIGIT+ EXPONENT_PART
    // 十六进制浮点数
    | HEX_PREFIX HEX_FRACTIONAL_CONSTANT BINARY_EXPONENT_PART
    | HEX_PREFIX HEX_DIGIT_SEQUENCE BINARY_EXPONENT_PART
    ;

// 忽略注释和空白字符
COMMENT: ('//' ~[\r\n]* | '/*' .*? '*/') -> skip;
WS: [ \t\r\n]+ -> skip;