grammar SysY;

// 开始符号
compUnit: (decl | funcDef)*;

// 声明
decl: constDecl | varDecl;

// 常量声明
constDecl: 'const' bType constDef (',' constDef)* ';';
bType: 'int' | 'float';
constDef: ident ('[' constExp ']')* '=' constInitVal;
constInitVal: constExp | '{' (constInitVal (',' constInitVal)*)? '}';

// 变量声明
varDecl: bType varDef (',' varDef)* ';';
varDef: ident ('[' constExp ']')* ('=' initVal)?;
initVal: exp | '{' (initVal (',' initVal)*)? '}';

// 函数定义
funcDef: funcType ident '(' (funcFParams)? ')' block;
funcType: 'void' | 'int' | 'float';
funcFParams: funcFParam (',' funcFParam)*;
funcFParam: bType ident ('[' ']' ('[' exp ']')*)?;

// 语句块
block: '{' (blockItem)* '}';
blockItem: decl | stmt;

// 语句
stmt: lVal '=' exp ';'
    | exp? ';'
    | block
    | 'if' '(' cond ')' stmt ('else' stmt)?
    | 'while' '(' cond ')' stmt
    | 'break' ';'
    | 'continue' ';'
    |'return' exp? ';';

// 表达式
exp: addExp;
cond: lOrExp;
lVal: ident ('[' exp ']')*;
primaryExp: '(' exp ')' | lVal | number;
number: intConst | floatConst;
unaryExp: primaryExp
    | ident '(' (funcRParams)? ')'
    | unaryOp unaryExp;
unaryOp: '+' | '-' | '!';
funcRParams: exp (',' exp)*;
mulExp: unaryExp | mulExp ('*' | '/' | '%') unaryExp;
addExp: mulExp | addExp ('+' | '-') mulExp;
relExp: addExp | relExp ('<' | '>' | '<=' | '>=') addExp;
eqExp: relExp | eqExp ('==' | '!=') relExp;
lAndExp: eqExp | lAndExp '&&' eqExp;
lOrExp: lAndExp | lOrExp '||' lAndExp;
constExp: addExp;

// 词法单元
ident: IDENTIFIER;
intConst: DECIMAL_CONST | OCTAL_CONST | HEXADECIMAL_CONST;
floatConst: FLOAT_CONST;

fragment IDENTIFIER_NONDIGIT: '_' | [a-zA-Z$];
fragment IDENTIFIER_DIGIT: [0-9];
IDENTIFIER: (IDENTIFIER_NONDIGIT (IDENTIFIER_NONDIGIT | IDENTIFIER_DIGIT)*);

fragment NONZERO_DIGIT: [1-9];
DECIMAL_CONST: NONZERO_DIGIT (IDENTIFIER_DIGIT)*;
OCTAL_CONST: '0' (OCTAL_DIGIT)*;
HEXADECIMAL_CONST: ('0x' | '0X') HEXADECIMAL_DIGIT (HEXADECIMAL_DIGIT)*;
fragment OCTAL_DIGIT: [0-7];
fragment HEXADECIMAL_DIGIT: [0-9a-fA-F];

fragment FLOAT_PART: (IDENTIFIER_DIGIT)+ '.' (IDENTIFIER_DIGIT)* | '.' (IDENTIFIER_DIGIT)+;
fragment EXPONENT_PART: [eE] [+-]? (IDENTIFIER_DIGIT)+;
FLOAT_CONST: FLOAT_PART (EXPONENT_PART)?;

// 忽略注释和空白字符
COMMENT: ('//' ~[\r\n]* | '/*'.*? '*/') -> skip;
WS: [ \t\r\n]+ -> skip;