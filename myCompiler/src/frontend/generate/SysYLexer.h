
// Generated from SysY.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  SysYLexer : public antlr4::Lexer {
public:
  enum {
    CONST = 1, TENSOR = 2, INT = 3, FLOAT = 4, VOID = 5, IF = 6, ELSE = 7, 
    WHILE = 8, BREAK = 9, CONTINUE = 10, RETURN = 11, ASSIGN = 12, LPAREN = 13, 
    RPAREN = 14, LBRACE = 15, RBRACE = 16, LBRACKET = 17, RBRACKET = 18, 
    COMMA = 19, SEMICOLON = 20, PLUS = 21, MINUS = 22, MUL = 23, DIV = 24, 
    MOD = 25, AT = 26, NOT = 27, LT = 28, GT = 29, LE = 30, GE = 31, EQ = 32, 
    NE = 33, AND = 34, OR = 35, Ident = 36, IntConst = 37, FloatConst = 38, 
    STRING_LITERAL = 39, COMMENT = 40, WS = 41
  };

  explicit SysYLexer(antlr4::CharStream *input);

  ~SysYLexer() override;


  std::string getGrammarFileName() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const std::vector<std::string>& getChannelNames() const override;

  const std::vector<std::string>& getModeNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  const antlr4::atn::ATN& getATN() const override;

  // By default the static state used to implement the lexer is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:

  // Individual action functions triggered by action() above.

  // Individual semantic predicate functions triggered by sempred() above.

};

