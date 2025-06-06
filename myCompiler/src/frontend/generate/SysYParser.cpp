
// Generated from SysY.g4 by ANTLR 4.13.2


#include "SysYVisitor.h"

#include "SysYParser.h"


using namespace antlrcpp;

using namespace antlr4;

namespace {

struct SysYParserStaticData final {
  SysYParserStaticData(std::vector<std::string> ruleNames,
                        std::vector<std::string> literalNames,
                        std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  SysYParserStaticData(const SysYParserStaticData&) = delete;
  SysYParserStaticData(SysYParserStaticData&&) = delete;
  SysYParserStaticData& operator=(const SysYParserStaticData&) = delete;
  SysYParserStaticData& operator=(SysYParserStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag sysyParserOnceFlag;
#if ANTLR4_USE_THREAD_LOCAL_CACHE
static thread_local
#endif
std::unique_ptr<SysYParserStaticData> sysyParserStaticData = nullptr;

void sysyParserInitialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  if (sysyParserStaticData != nullptr) {
    return;
  }
#else
  assert(sysyParserStaticData == nullptr);
#endif
  auto staticData = std::make_unique<SysYParserStaticData>(
    std::vector<std::string>{
      "compUnit", "decl", "constDecl", "bType", "constDef", "constInitVal", 
      "varDecl", "varDef", "initVal", "funcDef", "funcType", "funcFParams", 
      "funcFParam", "block", "blockItem", "stmt", "exp", "cond", "lVal", 
      "primaryExp", "number", "unaryExp", "unaryOp", "funcRParams", "mulExp", 
      "addExp", "relExp", "eqExp", "lAndExp", "lOrExp", "constExp"
    },
    std::vector<std::string>{
      "", "'const'", "'int'", "'float'", "'void'", "'if'", "'else'", "'while'", 
      "'break'", "'continue'", "'return'", "'='", "'('", "')'", "'{'", "'}'", 
      "'['", "']'", "','", "';'", "'+'", "'-'", "'*'", "'/'", "'%'", "'!'", 
      "'<'", "'>'", "'<='", "'>='", "'=='", "'!='", "'&&'", "'||'"
    },
    std::vector<std::string>{
      "", "CONST", "INT", "FLOAT", "VOID", "IF", "ELSE", "WHILE", "BREAK", 
      "CONTINUE", "RETURN", "ASSIGN", "LPAREN", "RPAREN", "LBRACE", "RBRACE", 
      "LBRACKET", "RBRACKET", "COMMA", "SEMICOLON", "PLUS", "MINUS", "MUL", 
      "DIV", "MOD", "NOT", "LT", "GT", "LE", "GE", "EQ", "NE", "AND", "OR", 
      "Ident", "IntConst", "FloatConst", "COMMENT", "WS"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,38,381,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,1,0,1,0,5,0,65,8,0,10,0,12,0,68,9,0,1,0,1,0,1,
  	1,1,1,3,1,74,8,1,1,2,1,2,1,2,1,2,1,2,5,2,81,8,2,10,2,12,2,84,9,2,1,2,
  	1,2,1,3,1,3,3,3,90,8,3,1,4,1,4,1,4,1,4,1,4,5,4,97,8,4,10,4,12,4,100,9,
  	4,1,4,1,4,1,4,1,5,1,5,1,5,1,5,1,5,5,5,110,8,5,10,5,12,5,113,9,5,3,5,115,
  	8,5,1,5,3,5,118,8,5,1,6,1,6,1,6,1,6,5,6,124,8,6,10,6,12,6,127,9,6,1,6,
  	1,6,1,7,1,7,1,7,1,7,1,7,5,7,136,8,7,10,7,12,7,139,9,7,1,7,1,7,3,7,143,
  	8,7,1,8,1,8,1,8,1,8,1,8,5,8,150,8,8,10,8,12,8,153,9,8,3,8,155,8,8,1,8,
  	3,8,158,8,8,1,9,1,9,1,9,1,9,3,9,164,8,9,1,9,1,9,1,9,1,10,1,10,3,10,171,
  	8,10,1,11,1,11,1,11,5,11,176,8,11,10,11,12,11,179,9,11,1,12,1,12,1,12,
  	1,12,1,12,1,12,1,12,1,12,1,12,1,12,1,12,5,12,192,8,12,10,12,12,12,195,
  	9,12,1,12,1,12,1,12,1,12,1,12,1,12,1,12,1,12,1,12,5,12,206,8,12,10,12,
  	12,12,209,9,12,3,12,211,8,12,1,13,1,13,5,13,215,8,13,10,13,12,13,218,
  	9,13,1,13,1,13,1,14,1,14,3,14,224,8,14,1,15,1,15,1,15,1,15,1,15,1,15,
  	3,15,232,8,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,3,15,243,8,
  	15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,3,15,257,
  	8,15,1,15,3,15,260,8,15,1,16,1,16,1,17,1,17,1,18,1,18,1,18,1,18,1,18,
  	5,18,271,8,18,10,18,12,18,274,9,18,1,19,1,19,1,19,1,19,1,19,1,19,3,19,
  	282,8,19,1,20,1,20,3,20,286,8,20,1,21,1,21,1,21,1,21,3,21,292,8,21,1,
  	21,1,21,1,21,1,21,3,21,298,8,21,1,22,1,22,1,22,3,22,303,8,22,1,23,1,23,
  	1,23,5,23,308,8,23,10,23,12,23,311,9,23,1,24,1,24,1,24,1,24,1,24,1,24,
  	5,24,319,8,24,10,24,12,24,322,9,24,1,25,1,25,1,25,1,25,1,25,1,25,5,25,
  	330,8,25,10,25,12,25,333,9,25,1,26,1,26,1,26,1,26,1,26,1,26,5,26,341,
  	8,26,10,26,12,26,344,9,26,1,27,1,27,1,27,1,27,1,27,1,27,5,27,352,8,27,
  	10,27,12,27,355,9,27,1,28,1,28,1,28,1,28,1,28,1,28,5,28,363,8,28,10,28,
  	12,28,366,9,28,1,29,1,29,1,29,1,29,1,29,1,29,5,29,374,8,29,10,29,12,29,
  	377,9,29,1,30,1,30,1,30,0,6,48,50,52,54,56,58,31,0,2,4,6,8,10,12,14,16,
  	18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,0,4,
  	1,0,22,24,1,0,20,21,1,0,26,29,1,0,30,31,399,0,66,1,0,0,0,2,73,1,0,0,0,
  	4,75,1,0,0,0,6,89,1,0,0,0,8,91,1,0,0,0,10,117,1,0,0,0,12,119,1,0,0,0,
  	14,130,1,0,0,0,16,157,1,0,0,0,18,159,1,0,0,0,20,170,1,0,0,0,22,172,1,
  	0,0,0,24,210,1,0,0,0,26,212,1,0,0,0,28,223,1,0,0,0,30,259,1,0,0,0,32,
  	261,1,0,0,0,34,263,1,0,0,0,36,265,1,0,0,0,38,281,1,0,0,0,40,285,1,0,0,
  	0,42,297,1,0,0,0,44,302,1,0,0,0,46,304,1,0,0,0,48,312,1,0,0,0,50,323,
  	1,0,0,0,52,334,1,0,0,0,54,345,1,0,0,0,56,356,1,0,0,0,58,367,1,0,0,0,60,
  	378,1,0,0,0,62,65,3,2,1,0,63,65,3,18,9,0,64,62,1,0,0,0,64,63,1,0,0,0,
  	65,68,1,0,0,0,66,64,1,0,0,0,66,67,1,0,0,0,67,69,1,0,0,0,68,66,1,0,0,0,
  	69,70,5,0,0,1,70,1,1,0,0,0,71,74,3,4,2,0,72,74,3,12,6,0,73,71,1,0,0,0,
  	73,72,1,0,0,0,74,3,1,0,0,0,75,76,5,1,0,0,76,77,3,6,3,0,77,82,3,8,4,0,
  	78,79,5,18,0,0,79,81,3,8,4,0,80,78,1,0,0,0,81,84,1,0,0,0,82,80,1,0,0,
  	0,82,83,1,0,0,0,83,85,1,0,0,0,84,82,1,0,0,0,85,86,5,19,0,0,86,5,1,0,0,
  	0,87,90,5,2,0,0,88,90,5,3,0,0,89,87,1,0,0,0,89,88,1,0,0,0,90,7,1,0,0,
  	0,91,98,5,34,0,0,92,93,5,16,0,0,93,94,3,60,30,0,94,95,5,17,0,0,95,97,
  	1,0,0,0,96,92,1,0,0,0,97,100,1,0,0,0,98,96,1,0,0,0,98,99,1,0,0,0,99,101,
  	1,0,0,0,100,98,1,0,0,0,101,102,5,11,0,0,102,103,3,10,5,0,103,9,1,0,0,
  	0,104,118,3,60,30,0,105,114,5,14,0,0,106,111,3,10,5,0,107,108,5,18,0,
  	0,108,110,3,10,5,0,109,107,1,0,0,0,110,113,1,0,0,0,111,109,1,0,0,0,111,
  	112,1,0,0,0,112,115,1,0,0,0,113,111,1,0,0,0,114,106,1,0,0,0,114,115,1,
  	0,0,0,115,116,1,0,0,0,116,118,5,15,0,0,117,104,1,0,0,0,117,105,1,0,0,
  	0,118,11,1,0,0,0,119,120,3,6,3,0,120,125,3,14,7,0,121,122,5,18,0,0,122,
  	124,3,14,7,0,123,121,1,0,0,0,124,127,1,0,0,0,125,123,1,0,0,0,125,126,
  	1,0,0,0,126,128,1,0,0,0,127,125,1,0,0,0,128,129,5,19,0,0,129,13,1,0,0,
  	0,130,137,5,34,0,0,131,132,5,16,0,0,132,133,3,60,30,0,133,134,5,17,0,
  	0,134,136,1,0,0,0,135,131,1,0,0,0,136,139,1,0,0,0,137,135,1,0,0,0,137,
  	138,1,0,0,0,138,142,1,0,0,0,139,137,1,0,0,0,140,141,5,11,0,0,141,143,
  	3,16,8,0,142,140,1,0,0,0,142,143,1,0,0,0,143,15,1,0,0,0,144,158,3,32,
  	16,0,145,154,5,14,0,0,146,151,3,16,8,0,147,148,5,18,0,0,148,150,3,16,
  	8,0,149,147,1,0,0,0,150,153,1,0,0,0,151,149,1,0,0,0,151,152,1,0,0,0,152,
  	155,1,0,0,0,153,151,1,0,0,0,154,146,1,0,0,0,154,155,1,0,0,0,155,156,1,
  	0,0,0,156,158,5,15,0,0,157,144,1,0,0,0,157,145,1,0,0,0,158,17,1,0,0,0,
  	159,160,3,20,10,0,160,161,5,34,0,0,161,163,5,12,0,0,162,164,3,22,11,0,
  	163,162,1,0,0,0,163,164,1,0,0,0,164,165,1,0,0,0,165,166,5,13,0,0,166,
  	167,3,26,13,0,167,19,1,0,0,0,168,171,5,4,0,0,169,171,3,6,3,0,170,168,
  	1,0,0,0,170,169,1,0,0,0,171,21,1,0,0,0,172,177,3,24,12,0,173,174,5,18,
  	0,0,174,176,3,24,12,0,175,173,1,0,0,0,176,179,1,0,0,0,177,175,1,0,0,0,
  	177,178,1,0,0,0,178,23,1,0,0,0,179,177,1,0,0,0,180,181,3,6,3,0,181,182,
  	5,34,0,0,182,211,1,0,0,0,183,184,3,6,3,0,184,185,5,34,0,0,185,186,5,16,
  	0,0,186,193,5,17,0,0,187,188,5,16,0,0,188,189,3,60,30,0,189,190,5,17,
  	0,0,190,192,1,0,0,0,191,187,1,0,0,0,192,195,1,0,0,0,193,191,1,0,0,0,193,
  	194,1,0,0,0,194,211,1,0,0,0,195,193,1,0,0,0,196,197,3,6,3,0,197,198,5,
  	34,0,0,198,199,5,16,0,0,199,200,3,60,30,0,200,207,5,17,0,0,201,202,5,
  	16,0,0,202,203,3,60,30,0,203,204,5,17,0,0,204,206,1,0,0,0,205,201,1,0,
  	0,0,206,209,1,0,0,0,207,205,1,0,0,0,207,208,1,0,0,0,208,211,1,0,0,0,209,
  	207,1,0,0,0,210,180,1,0,0,0,210,183,1,0,0,0,210,196,1,0,0,0,211,25,1,
  	0,0,0,212,216,5,14,0,0,213,215,3,28,14,0,214,213,1,0,0,0,215,218,1,0,
  	0,0,216,214,1,0,0,0,216,217,1,0,0,0,217,219,1,0,0,0,218,216,1,0,0,0,219,
  	220,5,15,0,0,220,27,1,0,0,0,221,224,3,2,1,0,222,224,3,30,15,0,223,221,
  	1,0,0,0,223,222,1,0,0,0,224,29,1,0,0,0,225,226,3,36,18,0,226,227,5,11,
  	0,0,227,228,3,32,16,0,228,229,5,19,0,0,229,260,1,0,0,0,230,232,3,32,16,
  	0,231,230,1,0,0,0,231,232,1,0,0,0,232,233,1,0,0,0,233,260,5,19,0,0,234,
  	260,3,26,13,0,235,236,5,5,0,0,236,237,5,12,0,0,237,238,3,34,17,0,238,
  	239,5,13,0,0,239,242,3,30,15,0,240,241,5,6,0,0,241,243,3,30,15,0,242,
  	240,1,0,0,0,242,243,1,0,0,0,243,260,1,0,0,0,244,245,5,7,0,0,245,246,5,
  	12,0,0,246,247,3,34,17,0,247,248,5,13,0,0,248,249,3,30,15,0,249,260,1,
  	0,0,0,250,251,5,8,0,0,251,260,5,19,0,0,252,253,5,9,0,0,253,260,5,19,0,
  	0,254,256,5,10,0,0,255,257,3,32,16,0,256,255,1,0,0,0,256,257,1,0,0,0,
  	257,258,1,0,0,0,258,260,5,19,0,0,259,225,1,0,0,0,259,231,1,0,0,0,259,
  	234,1,0,0,0,259,235,1,0,0,0,259,244,1,0,0,0,259,250,1,0,0,0,259,252,1,
  	0,0,0,259,254,1,0,0,0,260,31,1,0,0,0,261,262,3,50,25,0,262,33,1,0,0,0,
  	263,264,3,58,29,0,264,35,1,0,0,0,265,272,5,34,0,0,266,267,5,16,0,0,267,
  	268,3,32,16,0,268,269,5,17,0,0,269,271,1,0,0,0,270,266,1,0,0,0,271,274,
  	1,0,0,0,272,270,1,0,0,0,272,273,1,0,0,0,273,37,1,0,0,0,274,272,1,0,0,
  	0,275,276,5,12,0,0,276,277,3,32,16,0,277,278,5,13,0,0,278,282,1,0,0,0,
  	279,282,3,36,18,0,280,282,3,40,20,0,281,275,1,0,0,0,281,279,1,0,0,0,281,
  	280,1,0,0,0,282,39,1,0,0,0,283,286,5,35,0,0,284,286,5,36,0,0,285,283,
  	1,0,0,0,285,284,1,0,0,0,286,41,1,0,0,0,287,298,3,38,19,0,288,289,5,34,
  	0,0,289,291,5,12,0,0,290,292,3,46,23,0,291,290,1,0,0,0,291,292,1,0,0,
  	0,292,293,1,0,0,0,293,298,5,13,0,0,294,295,3,44,22,0,295,296,3,42,21,
  	0,296,298,1,0,0,0,297,287,1,0,0,0,297,288,1,0,0,0,297,294,1,0,0,0,298,
  	43,1,0,0,0,299,303,5,20,0,0,300,303,5,21,0,0,301,303,5,25,0,0,302,299,
  	1,0,0,0,302,300,1,0,0,0,302,301,1,0,0,0,303,45,1,0,0,0,304,309,3,32,16,
  	0,305,306,5,18,0,0,306,308,3,32,16,0,307,305,1,0,0,0,308,311,1,0,0,0,
  	309,307,1,0,0,0,309,310,1,0,0,0,310,47,1,0,0,0,311,309,1,0,0,0,312,313,
  	6,24,-1,0,313,314,3,42,21,0,314,320,1,0,0,0,315,316,10,1,0,0,316,317,
  	7,0,0,0,317,319,3,42,21,0,318,315,1,0,0,0,319,322,1,0,0,0,320,318,1,0,
  	0,0,320,321,1,0,0,0,321,49,1,0,0,0,322,320,1,0,0,0,323,324,6,25,-1,0,
  	324,325,3,48,24,0,325,331,1,0,0,0,326,327,10,1,0,0,327,328,7,1,0,0,328,
  	330,3,48,24,0,329,326,1,0,0,0,330,333,1,0,0,0,331,329,1,0,0,0,331,332,
  	1,0,0,0,332,51,1,0,0,0,333,331,1,0,0,0,334,335,6,26,-1,0,335,336,3,50,
  	25,0,336,342,1,0,0,0,337,338,10,1,0,0,338,339,7,2,0,0,339,341,3,50,25,
  	0,340,337,1,0,0,0,341,344,1,0,0,0,342,340,1,0,0,0,342,343,1,0,0,0,343,
  	53,1,0,0,0,344,342,1,0,0,0,345,346,6,27,-1,0,346,347,3,52,26,0,347,353,
  	1,0,0,0,348,349,10,1,0,0,349,350,7,3,0,0,350,352,3,52,26,0,351,348,1,
  	0,0,0,352,355,1,0,0,0,353,351,1,0,0,0,353,354,1,0,0,0,354,55,1,0,0,0,
  	355,353,1,0,0,0,356,357,6,28,-1,0,357,358,3,54,27,0,358,364,1,0,0,0,359,
  	360,10,1,0,0,360,361,5,32,0,0,361,363,3,54,27,0,362,359,1,0,0,0,363,366,
  	1,0,0,0,364,362,1,0,0,0,364,365,1,0,0,0,365,57,1,0,0,0,366,364,1,0,0,
  	0,367,368,6,29,-1,0,368,369,3,56,28,0,369,375,1,0,0,0,370,371,10,1,0,
  	0,371,372,5,33,0,0,372,374,3,56,28,0,373,370,1,0,0,0,374,377,1,0,0,0,
  	375,373,1,0,0,0,375,376,1,0,0,0,376,59,1,0,0,0,377,375,1,0,0,0,378,379,
  	3,50,25,0,379,61,1,0,0,0,40,64,66,73,82,89,98,111,114,117,125,137,142,
  	151,154,157,163,170,177,193,207,210,216,223,231,242,256,259,272,281,285,
  	291,297,302,309,320,331,342,353,364,375
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  sysyParserStaticData = std::move(staticData);
}

}

SysYParser::SysYParser(TokenStream *input) : SysYParser(input, antlr4::atn::ParserATNSimulatorOptions()) {}

SysYParser::SysYParser(TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options) : Parser(input) {
  SysYParser::initialize();
  _interpreter = new atn::ParserATNSimulator(this, *sysyParserStaticData->atn, sysyParserStaticData->decisionToDFA, sysyParserStaticData->sharedContextCache, options);
}

SysYParser::~SysYParser() {
  delete _interpreter;
}

const atn::ATN& SysYParser::getATN() const {
  return *sysyParserStaticData->atn;
}

std::string SysYParser::getGrammarFileName() const {
  return "SysY.g4";
}

const std::vector<std::string>& SysYParser::getRuleNames() const {
  return sysyParserStaticData->ruleNames;
}

const dfa::Vocabulary& SysYParser::getVocabulary() const {
  return sysyParserStaticData->vocabulary;
}

antlr4::atn::SerializedATNView SysYParser::getSerializedATN() const {
  return sysyParserStaticData->serializedATN;
}


//----------------- CompUnitContext ------------------------------------------------------------------

SysYParser::CompUnitContext::CompUnitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysYParser::CompUnitContext::EOF() {
  return getToken(SysYParser::EOF, 0);
}

std::vector<SysYParser::DeclContext *> SysYParser::CompUnitContext::decl() {
  return getRuleContexts<SysYParser::DeclContext>();
}

SysYParser::DeclContext* SysYParser::CompUnitContext::decl(size_t i) {
  return getRuleContext<SysYParser::DeclContext>(i);
}

std::vector<SysYParser::FuncDefContext *> SysYParser::CompUnitContext::funcDef() {
  return getRuleContexts<SysYParser::FuncDefContext>();
}

SysYParser::FuncDefContext* SysYParser::CompUnitContext::funcDef(size_t i) {
  return getRuleContext<SysYParser::FuncDefContext>(i);
}


size_t SysYParser::CompUnitContext::getRuleIndex() const {
  return SysYParser::RuleCompUnit;
}


std::any SysYParser::CompUnitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitCompUnit(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::CompUnitContext* SysYParser::compUnit() {
  CompUnitContext *_localctx = _tracker.createInstance<CompUnitContext>(_ctx, getState());
  enterRule(_localctx, 0, SysYParser::RuleCompUnit);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(66);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 30) != 0)) {
      setState(64);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 0, _ctx)) {
      case 1: {
        setState(62);
        decl();
        break;
      }

      case 2: {
        setState(63);
        funcDef();
        break;
      }

      default:
        break;
      }
      setState(68);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(69);
    match(SysYParser::EOF);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- DeclContext ------------------------------------------------------------------

SysYParser::DeclContext::DeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::DeclContext::getRuleIndex() const {
  return SysYParser::RuleDecl;
}

void SysYParser::DeclContext::copyFrom(DeclContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ConstDeclarationContext ------------------------------------------------------------------

SysYParser::ConstDeclContext* SysYParser::ConstDeclarationContext::constDecl() {
  return getRuleContext<SysYParser::ConstDeclContext>(0);
}

SysYParser::ConstDeclarationContext::ConstDeclarationContext(DeclContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ConstDeclarationContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitConstDeclaration(this);
  else
    return visitor->visitChildren(this);
}
//----------------- VariableDeclarationContext ------------------------------------------------------------------

SysYParser::VarDeclContext* SysYParser::VariableDeclarationContext::varDecl() {
  return getRuleContext<SysYParser::VarDeclContext>(0);
}

SysYParser::VariableDeclarationContext::VariableDeclarationContext(DeclContext *ctx) { copyFrom(ctx); }


std::any SysYParser::VariableDeclarationContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitVariableDeclaration(this);
  else
    return visitor->visitChildren(this);
}
SysYParser::DeclContext* SysYParser::decl() {
  DeclContext *_localctx = _tracker.createInstance<DeclContext>(_ctx, getState());
  enterRule(_localctx, 2, SysYParser::RuleDecl);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(73);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::CONST: {
        _localctx = _tracker.createInstance<SysYParser::ConstDeclarationContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(71);
        constDecl();
        break;
      }

      case SysYParser::INT:
      case SysYParser::FLOAT: {
        _localctx = _tracker.createInstance<SysYParser::VariableDeclarationContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(72);
        varDecl();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConstDeclContext ------------------------------------------------------------------

SysYParser::ConstDeclContext::ConstDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysYParser::ConstDeclContext::CONST() {
  return getToken(SysYParser::CONST, 0);
}

SysYParser::BTypeContext* SysYParser::ConstDeclContext::bType() {
  return getRuleContext<SysYParser::BTypeContext>(0);
}

std::vector<SysYParser::ConstDefContext *> SysYParser::ConstDeclContext::constDef() {
  return getRuleContexts<SysYParser::ConstDefContext>();
}

SysYParser::ConstDefContext* SysYParser::ConstDeclContext::constDef(size_t i) {
  return getRuleContext<SysYParser::ConstDefContext>(i);
}

tree::TerminalNode* SysYParser::ConstDeclContext::SEMICOLON() {
  return getToken(SysYParser::SEMICOLON, 0);
}

std::vector<tree::TerminalNode *> SysYParser::ConstDeclContext::COMMA() {
  return getTokens(SysYParser::COMMA);
}

tree::TerminalNode* SysYParser::ConstDeclContext::COMMA(size_t i) {
  return getToken(SysYParser::COMMA, i);
}


size_t SysYParser::ConstDeclContext::getRuleIndex() const {
  return SysYParser::RuleConstDecl;
}


std::any SysYParser::ConstDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitConstDecl(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::ConstDeclContext* SysYParser::constDecl() {
  ConstDeclContext *_localctx = _tracker.createInstance<ConstDeclContext>(_ctx, getState());
  enterRule(_localctx, 4, SysYParser::RuleConstDecl);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(75);
    match(SysYParser::CONST);
    setState(76);
    bType();
    setState(77);
    constDef();
    setState(82);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysYParser::COMMA) {
      setState(78);
      match(SysYParser::COMMA);
      setState(79);
      constDef();
      setState(84);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(85);
    match(SysYParser::SEMICOLON);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BTypeContext ------------------------------------------------------------------

SysYParser::BTypeContext::BTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::BTypeContext::getRuleIndex() const {
  return SysYParser::RuleBType;
}

void SysYParser::BTypeContext::copyFrom(BTypeContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- TypeIntContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::TypeIntContext::INT() {
  return getToken(SysYParser::INT, 0);
}

SysYParser::TypeIntContext::TypeIntContext(BTypeContext *ctx) { copyFrom(ctx); }


std::any SysYParser::TypeIntContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitTypeInt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- TypeFloatContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::TypeFloatContext::FLOAT() {
  return getToken(SysYParser::FLOAT, 0);
}

SysYParser::TypeFloatContext::TypeFloatContext(BTypeContext *ctx) { copyFrom(ctx); }


std::any SysYParser::TypeFloatContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitTypeFloat(this);
  else
    return visitor->visitChildren(this);
}
SysYParser::BTypeContext* SysYParser::bType() {
  BTypeContext *_localctx = _tracker.createInstance<BTypeContext>(_ctx, getState());
  enterRule(_localctx, 6, SysYParser::RuleBType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(89);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::INT: {
        _localctx = _tracker.createInstance<SysYParser::TypeIntContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(87);
        match(SysYParser::INT);
        break;
      }

      case SysYParser::FLOAT: {
        _localctx = _tracker.createInstance<SysYParser::TypeFloatContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(88);
        match(SysYParser::FLOAT);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConstDefContext ------------------------------------------------------------------

SysYParser::ConstDefContext::ConstDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysYParser::ConstDefContext::Ident() {
  return getToken(SysYParser::Ident, 0);
}

tree::TerminalNode* SysYParser::ConstDefContext::ASSIGN() {
  return getToken(SysYParser::ASSIGN, 0);
}

SysYParser::ConstInitValContext* SysYParser::ConstDefContext::constInitVal() {
  return getRuleContext<SysYParser::ConstInitValContext>(0);
}

std::vector<tree::TerminalNode *> SysYParser::ConstDefContext::LBRACKET() {
  return getTokens(SysYParser::LBRACKET);
}

tree::TerminalNode* SysYParser::ConstDefContext::LBRACKET(size_t i) {
  return getToken(SysYParser::LBRACKET, i);
}

std::vector<SysYParser::ConstExpContext *> SysYParser::ConstDefContext::constExp() {
  return getRuleContexts<SysYParser::ConstExpContext>();
}

SysYParser::ConstExpContext* SysYParser::ConstDefContext::constExp(size_t i) {
  return getRuleContext<SysYParser::ConstExpContext>(i);
}

std::vector<tree::TerminalNode *> SysYParser::ConstDefContext::RBRACKET() {
  return getTokens(SysYParser::RBRACKET);
}

tree::TerminalNode* SysYParser::ConstDefContext::RBRACKET(size_t i) {
  return getToken(SysYParser::RBRACKET, i);
}


size_t SysYParser::ConstDefContext::getRuleIndex() const {
  return SysYParser::RuleConstDef;
}


std::any SysYParser::ConstDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitConstDef(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::ConstDefContext* SysYParser::constDef() {
  ConstDefContext *_localctx = _tracker.createInstance<ConstDefContext>(_ctx, getState());
  enterRule(_localctx, 8, SysYParser::RuleConstDef);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(91);
    match(SysYParser::Ident);
    setState(98);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysYParser::LBRACKET) {
      setState(92);
      match(SysYParser::LBRACKET);
      setState(93);
      constExp();
      setState(94);
      match(SysYParser::RBRACKET);
      setState(100);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(101);
    match(SysYParser::ASSIGN);
    setState(102);
    constInitVal();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConstInitValContext ------------------------------------------------------------------

SysYParser::ConstInitValContext::ConstInitValContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::ConstInitValContext::getRuleIndex() const {
  return SysYParser::RuleConstInitVal;
}

void SysYParser::ConstInitValContext::copyFrom(ConstInitValContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ConstInitExprContext ------------------------------------------------------------------

SysYParser::ConstExpContext* SysYParser::ConstInitExprContext::constExp() {
  return getRuleContext<SysYParser::ConstExpContext>(0);
}

SysYParser::ConstInitExprContext::ConstInitExprContext(ConstInitValContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ConstInitExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitConstInitExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ConstInitListContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::ConstInitListContext::LBRACE() {
  return getToken(SysYParser::LBRACE, 0);
}

tree::TerminalNode* SysYParser::ConstInitListContext::RBRACE() {
  return getToken(SysYParser::RBRACE, 0);
}

std::vector<SysYParser::ConstInitValContext *> SysYParser::ConstInitListContext::constInitVal() {
  return getRuleContexts<SysYParser::ConstInitValContext>();
}

SysYParser::ConstInitValContext* SysYParser::ConstInitListContext::constInitVal(size_t i) {
  return getRuleContext<SysYParser::ConstInitValContext>(i);
}

std::vector<tree::TerminalNode *> SysYParser::ConstInitListContext::COMMA() {
  return getTokens(SysYParser::COMMA);
}

tree::TerminalNode* SysYParser::ConstInitListContext::COMMA(size_t i) {
  return getToken(SysYParser::COMMA, i);
}

SysYParser::ConstInitListContext::ConstInitListContext(ConstInitValContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ConstInitListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitConstInitList(this);
  else
    return visitor->visitChildren(this);
}
SysYParser::ConstInitValContext* SysYParser::constInitVal() {
  ConstInitValContext *_localctx = _tracker.createInstance<ConstInitValContext>(_ctx, getState());
  enterRule(_localctx, 10, SysYParser::RuleConstInitVal);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(117);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::LPAREN:
      case SysYParser::PLUS:
      case SysYParser::MINUS:
      case SysYParser::NOT:
      case SysYParser::Ident:
      case SysYParser::IntConst:
      case SysYParser::FloatConst: {
        _localctx = _tracker.createInstance<SysYParser::ConstInitExprContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(104);
        constExp();
        break;
      }

      case SysYParser::LBRACE: {
        _localctx = _tracker.createInstance<SysYParser::ConstInitListContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(105);
        match(SysYParser::LBRACE);
        setState(114);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 120295804928) != 0)) {
          setState(106);
          constInitVal();
          setState(111);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == SysYParser::COMMA) {
            setState(107);
            match(SysYParser::COMMA);
            setState(108);
            constInitVal();
            setState(113);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
        }
        setState(116);
        match(SysYParser::RBRACE);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- VarDeclContext ------------------------------------------------------------------

SysYParser::VarDeclContext::VarDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysYParser::BTypeContext* SysYParser::VarDeclContext::bType() {
  return getRuleContext<SysYParser::BTypeContext>(0);
}

std::vector<SysYParser::VarDefContext *> SysYParser::VarDeclContext::varDef() {
  return getRuleContexts<SysYParser::VarDefContext>();
}

SysYParser::VarDefContext* SysYParser::VarDeclContext::varDef(size_t i) {
  return getRuleContext<SysYParser::VarDefContext>(i);
}

tree::TerminalNode* SysYParser::VarDeclContext::SEMICOLON() {
  return getToken(SysYParser::SEMICOLON, 0);
}

std::vector<tree::TerminalNode *> SysYParser::VarDeclContext::COMMA() {
  return getTokens(SysYParser::COMMA);
}

tree::TerminalNode* SysYParser::VarDeclContext::COMMA(size_t i) {
  return getToken(SysYParser::COMMA, i);
}


size_t SysYParser::VarDeclContext::getRuleIndex() const {
  return SysYParser::RuleVarDecl;
}


std::any SysYParser::VarDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitVarDecl(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::VarDeclContext* SysYParser::varDecl() {
  VarDeclContext *_localctx = _tracker.createInstance<VarDeclContext>(_ctx, getState());
  enterRule(_localctx, 12, SysYParser::RuleVarDecl);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(119);
    bType();
    setState(120);
    varDef();
    setState(125);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysYParser::COMMA) {
      setState(121);
      match(SysYParser::COMMA);
      setState(122);
      varDef();
      setState(127);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(128);
    match(SysYParser::SEMICOLON);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- VarDefContext ------------------------------------------------------------------

SysYParser::VarDefContext::VarDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysYParser::VarDefContext::Ident() {
  return getToken(SysYParser::Ident, 0);
}

std::vector<tree::TerminalNode *> SysYParser::VarDefContext::LBRACKET() {
  return getTokens(SysYParser::LBRACKET);
}

tree::TerminalNode* SysYParser::VarDefContext::LBRACKET(size_t i) {
  return getToken(SysYParser::LBRACKET, i);
}

std::vector<SysYParser::ConstExpContext *> SysYParser::VarDefContext::constExp() {
  return getRuleContexts<SysYParser::ConstExpContext>();
}

SysYParser::ConstExpContext* SysYParser::VarDefContext::constExp(size_t i) {
  return getRuleContext<SysYParser::ConstExpContext>(i);
}

std::vector<tree::TerminalNode *> SysYParser::VarDefContext::RBRACKET() {
  return getTokens(SysYParser::RBRACKET);
}

tree::TerminalNode* SysYParser::VarDefContext::RBRACKET(size_t i) {
  return getToken(SysYParser::RBRACKET, i);
}

tree::TerminalNode* SysYParser::VarDefContext::ASSIGN() {
  return getToken(SysYParser::ASSIGN, 0);
}

SysYParser::InitValContext* SysYParser::VarDefContext::initVal() {
  return getRuleContext<SysYParser::InitValContext>(0);
}


size_t SysYParser::VarDefContext::getRuleIndex() const {
  return SysYParser::RuleVarDef;
}


std::any SysYParser::VarDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitVarDef(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::VarDefContext* SysYParser::varDef() {
  VarDefContext *_localctx = _tracker.createInstance<VarDefContext>(_ctx, getState());
  enterRule(_localctx, 14, SysYParser::RuleVarDef);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(130);
    match(SysYParser::Ident);
    setState(137);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysYParser::LBRACKET) {
      setState(131);
      match(SysYParser::LBRACKET);
      setState(132);
      constExp();
      setState(133);
      match(SysYParser::RBRACKET);
      setState(139);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(142);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == SysYParser::ASSIGN) {
      setState(140);
      match(SysYParser::ASSIGN);
      setState(141);
      initVal();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- InitValContext ------------------------------------------------------------------

SysYParser::InitValContext::InitValContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::InitValContext::getRuleIndex() const {
  return SysYParser::RuleInitVal;
}

void SysYParser::InitValContext::copyFrom(InitValContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- InitListContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::InitListContext::LBRACE() {
  return getToken(SysYParser::LBRACE, 0);
}

tree::TerminalNode* SysYParser::InitListContext::RBRACE() {
  return getToken(SysYParser::RBRACE, 0);
}

std::vector<SysYParser::InitValContext *> SysYParser::InitListContext::initVal() {
  return getRuleContexts<SysYParser::InitValContext>();
}

SysYParser::InitValContext* SysYParser::InitListContext::initVal(size_t i) {
  return getRuleContext<SysYParser::InitValContext>(i);
}

std::vector<tree::TerminalNode *> SysYParser::InitListContext::COMMA() {
  return getTokens(SysYParser::COMMA);
}

tree::TerminalNode* SysYParser::InitListContext::COMMA(size_t i) {
  return getToken(SysYParser::COMMA, i);
}

SysYParser::InitListContext::InitListContext(InitValContext *ctx) { copyFrom(ctx); }


std::any SysYParser::InitListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitInitList(this);
  else
    return visitor->visitChildren(this);
}
//----------------- InitExprContext ------------------------------------------------------------------

SysYParser::ExpContext* SysYParser::InitExprContext::exp() {
  return getRuleContext<SysYParser::ExpContext>(0);
}

SysYParser::InitExprContext::InitExprContext(InitValContext *ctx) { copyFrom(ctx); }


std::any SysYParser::InitExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitInitExpr(this);
  else
    return visitor->visitChildren(this);
}
SysYParser::InitValContext* SysYParser::initVal() {
  InitValContext *_localctx = _tracker.createInstance<InitValContext>(_ctx, getState());
  enterRule(_localctx, 16, SysYParser::RuleInitVal);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(157);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::LPAREN:
      case SysYParser::PLUS:
      case SysYParser::MINUS:
      case SysYParser::NOT:
      case SysYParser::Ident:
      case SysYParser::IntConst:
      case SysYParser::FloatConst: {
        _localctx = _tracker.createInstance<SysYParser::InitExprContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(144);
        exp();
        break;
      }

      case SysYParser::LBRACE: {
        _localctx = _tracker.createInstance<SysYParser::InitListContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(145);
        match(SysYParser::LBRACE);
        setState(154);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 120295804928) != 0)) {
          setState(146);
          initVal();
          setState(151);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == SysYParser::COMMA) {
            setState(147);
            match(SysYParser::COMMA);
            setState(148);
            initVal();
            setState(153);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
        }
        setState(156);
        match(SysYParser::RBRACE);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncDefContext ------------------------------------------------------------------

SysYParser::FuncDefContext::FuncDefContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysYParser::FuncTypeContext* SysYParser::FuncDefContext::funcType() {
  return getRuleContext<SysYParser::FuncTypeContext>(0);
}

tree::TerminalNode* SysYParser::FuncDefContext::Ident() {
  return getToken(SysYParser::Ident, 0);
}

tree::TerminalNode* SysYParser::FuncDefContext::LPAREN() {
  return getToken(SysYParser::LPAREN, 0);
}

tree::TerminalNode* SysYParser::FuncDefContext::RPAREN() {
  return getToken(SysYParser::RPAREN, 0);
}

SysYParser::BlockContext* SysYParser::FuncDefContext::block() {
  return getRuleContext<SysYParser::BlockContext>(0);
}

SysYParser::FuncFParamsContext* SysYParser::FuncDefContext::funcFParams() {
  return getRuleContext<SysYParser::FuncFParamsContext>(0);
}


size_t SysYParser::FuncDefContext::getRuleIndex() const {
  return SysYParser::RuleFuncDef;
}


std::any SysYParser::FuncDefContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitFuncDef(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::FuncDefContext* SysYParser::funcDef() {
  FuncDefContext *_localctx = _tracker.createInstance<FuncDefContext>(_ctx, getState());
  enterRule(_localctx, 18, SysYParser::RuleFuncDef);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(159);
    funcType();
    setState(160);
    match(SysYParser::Ident);
    setState(161);
    match(SysYParser::LPAREN);
    setState(163);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == SysYParser::INT

    || _la == SysYParser::FLOAT) {
      setState(162);
      funcFParams();
    }
    setState(165);
    match(SysYParser::RPAREN);
    setState(166);
    block();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncTypeContext ------------------------------------------------------------------

SysYParser::FuncTypeContext::FuncTypeContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::FuncTypeContext::getRuleIndex() const {
  return SysYParser::RuleFuncType;
}

void SysYParser::FuncTypeContext::copyFrom(FuncTypeContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- TypeVoidContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::TypeVoidContext::VOID() {
  return getToken(SysYParser::VOID, 0);
}

SysYParser::TypeVoidContext::TypeVoidContext(FuncTypeContext *ctx) { copyFrom(ctx); }


std::any SysYParser::TypeVoidContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitTypeVoid(this);
  else
    return visitor->visitChildren(this);
}
//----------------- TypeBTypeContext ------------------------------------------------------------------

SysYParser::BTypeContext* SysYParser::TypeBTypeContext::bType() {
  return getRuleContext<SysYParser::BTypeContext>(0);
}

SysYParser::TypeBTypeContext::TypeBTypeContext(FuncTypeContext *ctx) { copyFrom(ctx); }


std::any SysYParser::TypeBTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitTypeBType(this);
  else
    return visitor->visitChildren(this);
}
SysYParser::FuncTypeContext* SysYParser::funcType() {
  FuncTypeContext *_localctx = _tracker.createInstance<FuncTypeContext>(_ctx, getState());
  enterRule(_localctx, 20, SysYParser::RuleFuncType);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(170);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::VOID: {
        _localctx = _tracker.createInstance<SysYParser::TypeVoidContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(168);
        match(SysYParser::VOID);
        break;
      }

      case SysYParser::INT:
      case SysYParser::FLOAT: {
        _localctx = _tracker.createInstance<SysYParser::TypeBTypeContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(169);
        bType();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncFParamsContext ------------------------------------------------------------------

SysYParser::FuncFParamsContext::FuncFParamsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<SysYParser::FuncFParamContext *> SysYParser::FuncFParamsContext::funcFParam() {
  return getRuleContexts<SysYParser::FuncFParamContext>();
}

SysYParser::FuncFParamContext* SysYParser::FuncFParamsContext::funcFParam(size_t i) {
  return getRuleContext<SysYParser::FuncFParamContext>(i);
}

std::vector<tree::TerminalNode *> SysYParser::FuncFParamsContext::COMMA() {
  return getTokens(SysYParser::COMMA);
}

tree::TerminalNode* SysYParser::FuncFParamsContext::COMMA(size_t i) {
  return getToken(SysYParser::COMMA, i);
}


size_t SysYParser::FuncFParamsContext::getRuleIndex() const {
  return SysYParser::RuleFuncFParams;
}


std::any SysYParser::FuncFParamsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitFuncFParams(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::FuncFParamsContext* SysYParser::funcFParams() {
  FuncFParamsContext *_localctx = _tracker.createInstance<FuncFParamsContext>(_ctx, getState());
  enterRule(_localctx, 22, SysYParser::RuleFuncFParams);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(172);
    funcFParam();
    setState(177);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysYParser::COMMA) {
      setState(173);
      match(SysYParser::COMMA);
      setState(174);
      funcFParam();
      setState(179);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncFParamContext ------------------------------------------------------------------

SysYParser::FuncFParamContext::FuncFParamContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::FuncFParamContext::getRuleIndex() const {
  return SysYParser::RuleFuncFParam;
}

void SysYParser::FuncFParamContext::copyFrom(FuncFParamContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ArrayParamNoSizeContext ------------------------------------------------------------------

SysYParser::BTypeContext* SysYParser::ArrayParamNoSizeContext::bType() {
  return getRuleContext<SysYParser::BTypeContext>(0);
}

tree::TerminalNode* SysYParser::ArrayParamNoSizeContext::Ident() {
  return getToken(SysYParser::Ident, 0);
}

std::vector<tree::TerminalNode *> SysYParser::ArrayParamNoSizeContext::LBRACKET() {
  return getTokens(SysYParser::LBRACKET);
}

tree::TerminalNode* SysYParser::ArrayParamNoSizeContext::LBRACKET(size_t i) {
  return getToken(SysYParser::LBRACKET, i);
}

std::vector<tree::TerminalNode *> SysYParser::ArrayParamNoSizeContext::RBRACKET() {
  return getTokens(SysYParser::RBRACKET);
}

tree::TerminalNode* SysYParser::ArrayParamNoSizeContext::RBRACKET(size_t i) {
  return getToken(SysYParser::RBRACKET, i);
}

std::vector<SysYParser::ConstExpContext *> SysYParser::ArrayParamNoSizeContext::constExp() {
  return getRuleContexts<SysYParser::ConstExpContext>();
}

SysYParser::ConstExpContext* SysYParser::ArrayParamNoSizeContext::constExp(size_t i) {
  return getRuleContext<SysYParser::ConstExpContext>(i);
}

SysYParser::ArrayParamNoSizeContext::ArrayParamNoSizeContext(FuncFParamContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ArrayParamNoSizeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitArrayParamNoSize(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ArrayParamWithSizeContext ------------------------------------------------------------------

SysYParser::BTypeContext* SysYParser::ArrayParamWithSizeContext::bType() {
  return getRuleContext<SysYParser::BTypeContext>(0);
}

tree::TerminalNode* SysYParser::ArrayParamWithSizeContext::Ident() {
  return getToken(SysYParser::Ident, 0);
}

std::vector<tree::TerminalNode *> SysYParser::ArrayParamWithSizeContext::LBRACKET() {
  return getTokens(SysYParser::LBRACKET);
}

tree::TerminalNode* SysYParser::ArrayParamWithSizeContext::LBRACKET(size_t i) {
  return getToken(SysYParser::LBRACKET, i);
}

std::vector<SysYParser::ConstExpContext *> SysYParser::ArrayParamWithSizeContext::constExp() {
  return getRuleContexts<SysYParser::ConstExpContext>();
}

SysYParser::ConstExpContext* SysYParser::ArrayParamWithSizeContext::constExp(size_t i) {
  return getRuleContext<SysYParser::ConstExpContext>(i);
}

std::vector<tree::TerminalNode *> SysYParser::ArrayParamWithSizeContext::RBRACKET() {
  return getTokens(SysYParser::RBRACKET);
}

tree::TerminalNode* SysYParser::ArrayParamWithSizeContext::RBRACKET(size_t i) {
  return getToken(SysYParser::RBRACKET, i);
}

SysYParser::ArrayParamWithSizeContext::ArrayParamWithSizeContext(FuncFParamContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ArrayParamWithSizeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitArrayParamWithSize(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ScalarParamContext ------------------------------------------------------------------

SysYParser::BTypeContext* SysYParser::ScalarParamContext::bType() {
  return getRuleContext<SysYParser::BTypeContext>(0);
}

tree::TerminalNode* SysYParser::ScalarParamContext::Ident() {
  return getToken(SysYParser::Ident, 0);
}

SysYParser::ScalarParamContext::ScalarParamContext(FuncFParamContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ScalarParamContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitScalarParam(this);
  else
    return visitor->visitChildren(this);
}
SysYParser::FuncFParamContext* SysYParser::funcFParam() {
  FuncFParamContext *_localctx = _tracker.createInstance<FuncFParamContext>(_ctx, getState());
  enterRule(_localctx, 24, SysYParser::RuleFuncFParam);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(210);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 20, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<SysYParser::ScalarParamContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(180);
      bType();
      setState(181);
      match(SysYParser::Ident);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<SysYParser::ArrayParamNoSizeContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(183);
      bType();
      setState(184);
      match(SysYParser::Ident);
      setState(185);
      match(SysYParser::LBRACKET);
      setState(186);
      match(SysYParser::RBRACKET);
      setState(193);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == SysYParser::LBRACKET) {
        setState(187);
        match(SysYParser::LBRACKET);
        setState(188);
        constExp();
        setState(189);
        match(SysYParser::RBRACKET);
        setState(195);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<SysYParser::ArrayParamWithSizeContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(196);
      bType();
      setState(197);
      match(SysYParser::Ident);
      setState(198);
      match(SysYParser::LBRACKET);
      setState(199);
      constExp();
      setState(200);
      match(SysYParser::RBRACKET);
      setState(207);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == SysYParser::LBRACKET) {
        setState(201);
        match(SysYParser::LBRACKET);
        setState(202);
        constExp();
        setState(203);
        match(SysYParser::RBRACKET);
        setState(209);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BlockContext ------------------------------------------------------------------

SysYParser::BlockContext::BlockContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysYParser::BlockContext::LBRACE() {
  return getToken(SysYParser::LBRACE, 0);
}

tree::TerminalNode* SysYParser::BlockContext::RBRACE() {
  return getToken(SysYParser::RBRACE, 0);
}

std::vector<SysYParser::BlockItemContext *> SysYParser::BlockContext::blockItem() {
  return getRuleContexts<SysYParser::BlockItemContext>();
}

SysYParser::BlockItemContext* SysYParser::BlockContext::blockItem(size_t i) {
  return getRuleContext<SysYParser::BlockItemContext>(i);
}


size_t SysYParser::BlockContext::getRuleIndex() const {
  return SysYParser::RuleBlock;
}


std::any SysYParser::BlockContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitBlock(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::BlockContext* SysYParser::block() {
  BlockContext *_localctx = _tracker.createInstance<BlockContext>(_ctx, getState());
  enterRule(_localctx, 26, SysYParser::RuleBlock);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(212);
    match(SysYParser::LBRACE);
    setState(216);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 120296331182) != 0)) {
      setState(213);
      blockItem();
      setState(218);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(219);
    match(SysYParser::RBRACE);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BlockItemContext ------------------------------------------------------------------

SysYParser::BlockItemContext::BlockItemContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::BlockItemContext::getRuleIndex() const {
  return SysYParser::RuleBlockItem;
}

void SysYParser::BlockItemContext::copyFrom(BlockItemContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ItemStmtContext ------------------------------------------------------------------

SysYParser::StmtContext* SysYParser::ItemStmtContext::stmt() {
  return getRuleContext<SysYParser::StmtContext>(0);
}

SysYParser::ItemStmtContext::ItemStmtContext(BlockItemContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ItemStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitItemStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ItemDeclContext ------------------------------------------------------------------

SysYParser::DeclContext* SysYParser::ItemDeclContext::decl() {
  return getRuleContext<SysYParser::DeclContext>(0);
}

SysYParser::ItemDeclContext::ItemDeclContext(BlockItemContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ItemDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitItemDecl(this);
  else
    return visitor->visitChildren(this);
}
SysYParser::BlockItemContext* SysYParser::blockItem() {
  BlockItemContext *_localctx = _tracker.createInstance<BlockItemContext>(_ctx, getState());
  enterRule(_localctx, 28, SysYParser::RuleBlockItem);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(223);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::CONST:
      case SysYParser::INT:
      case SysYParser::FLOAT: {
        _localctx = _tracker.createInstance<SysYParser::ItemDeclContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(221);
        decl();
        break;
      }

      case SysYParser::IF:
      case SysYParser::WHILE:
      case SysYParser::BREAK:
      case SysYParser::CONTINUE:
      case SysYParser::RETURN:
      case SysYParser::LPAREN:
      case SysYParser::LBRACE:
      case SysYParser::SEMICOLON:
      case SysYParser::PLUS:
      case SysYParser::MINUS:
      case SysYParser::NOT:
      case SysYParser::Ident:
      case SysYParser::IntConst:
      case SysYParser::FloatConst: {
        _localctx = _tracker.createInstance<SysYParser::ItemStmtContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(222);
        stmt();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StmtContext ------------------------------------------------------------------

SysYParser::StmtContext::StmtContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::StmtContext::getRuleIndex() const {
  return SysYParser::RuleStmt;
}

void SysYParser::StmtContext::copyFrom(StmtContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ExprStmtContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::ExprStmtContext::SEMICOLON() {
  return getToken(SysYParser::SEMICOLON, 0);
}

SysYParser::ExpContext* SysYParser::ExprStmtContext::exp() {
  return getRuleContext<SysYParser::ExpContext>(0);
}

SysYParser::ExprStmtContext::ExprStmtContext(StmtContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ExprStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitExprStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- WhileStmtContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::WhileStmtContext::WHILE() {
  return getToken(SysYParser::WHILE, 0);
}

tree::TerminalNode* SysYParser::WhileStmtContext::LPAREN() {
  return getToken(SysYParser::LPAREN, 0);
}

SysYParser::CondContext* SysYParser::WhileStmtContext::cond() {
  return getRuleContext<SysYParser::CondContext>(0);
}

tree::TerminalNode* SysYParser::WhileStmtContext::RPAREN() {
  return getToken(SysYParser::RPAREN, 0);
}

SysYParser::StmtContext* SysYParser::WhileStmtContext::stmt() {
  return getRuleContext<SysYParser::StmtContext>(0);
}

SysYParser::WhileStmtContext::WhileStmtContext(StmtContext *ctx) { copyFrom(ctx); }


std::any SysYParser::WhileStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitWhileStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- IfStmtContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::IfStmtContext::IF() {
  return getToken(SysYParser::IF, 0);
}

tree::TerminalNode* SysYParser::IfStmtContext::LPAREN() {
  return getToken(SysYParser::LPAREN, 0);
}

SysYParser::CondContext* SysYParser::IfStmtContext::cond() {
  return getRuleContext<SysYParser::CondContext>(0);
}

tree::TerminalNode* SysYParser::IfStmtContext::RPAREN() {
  return getToken(SysYParser::RPAREN, 0);
}

std::vector<SysYParser::StmtContext *> SysYParser::IfStmtContext::stmt() {
  return getRuleContexts<SysYParser::StmtContext>();
}

SysYParser::StmtContext* SysYParser::IfStmtContext::stmt(size_t i) {
  return getRuleContext<SysYParser::StmtContext>(i);
}

tree::TerminalNode* SysYParser::IfStmtContext::ELSE() {
  return getToken(SysYParser::ELSE, 0);
}

SysYParser::IfStmtContext::IfStmtContext(StmtContext *ctx) { copyFrom(ctx); }


std::any SysYParser::IfStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitIfStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- BlockStmtContext ------------------------------------------------------------------

SysYParser::BlockContext* SysYParser::BlockStmtContext::block() {
  return getRuleContext<SysYParser::BlockContext>(0);
}

SysYParser::BlockStmtContext::BlockStmtContext(StmtContext *ctx) { copyFrom(ctx); }


std::any SysYParser::BlockStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitBlockStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AssignStmtContext ------------------------------------------------------------------

SysYParser::LValContext* SysYParser::AssignStmtContext::lVal() {
  return getRuleContext<SysYParser::LValContext>(0);
}

tree::TerminalNode* SysYParser::AssignStmtContext::ASSIGN() {
  return getToken(SysYParser::ASSIGN, 0);
}

SysYParser::ExpContext* SysYParser::AssignStmtContext::exp() {
  return getRuleContext<SysYParser::ExpContext>(0);
}

tree::TerminalNode* SysYParser::AssignStmtContext::SEMICOLON() {
  return getToken(SysYParser::SEMICOLON, 0);
}

SysYParser::AssignStmtContext::AssignStmtContext(StmtContext *ctx) { copyFrom(ctx); }


std::any SysYParser::AssignStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitAssignStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- BreakStmtContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::BreakStmtContext::BREAK() {
  return getToken(SysYParser::BREAK, 0);
}

tree::TerminalNode* SysYParser::BreakStmtContext::SEMICOLON() {
  return getToken(SysYParser::SEMICOLON, 0);
}

SysYParser::BreakStmtContext::BreakStmtContext(StmtContext *ctx) { copyFrom(ctx); }


std::any SysYParser::BreakStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitBreakStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ReturnStmtContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::ReturnStmtContext::RETURN() {
  return getToken(SysYParser::RETURN, 0);
}

tree::TerminalNode* SysYParser::ReturnStmtContext::SEMICOLON() {
  return getToken(SysYParser::SEMICOLON, 0);
}

SysYParser::ExpContext* SysYParser::ReturnStmtContext::exp() {
  return getRuleContext<SysYParser::ExpContext>(0);
}

SysYParser::ReturnStmtContext::ReturnStmtContext(StmtContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ReturnStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitReturnStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ContinueStmtContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::ContinueStmtContext::CONTINUE() {
  return getToken(SysYParser::CONTINUE, 0);
}

tree::TerminalNode* SysYParser::ContinueStmtContext::SEMICOLON() {
  return getToken(SysYParser::SEMICOLON, 0);
}

SysYParser::ContinueStmtContext::ContinueStmtContext(StmtContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ContinueStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitContinueStmt(this);
  else
    return visitor->visitChildren(this);
}
SysYParser::StmtContext* SysYParser::stmt() {
  StmtContext *_localctx = _tracker.createInstance<StmtContext>(_ctx, getState());
  enterRule(_localctx, 30, SysYParser::RuleStmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(259);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<SysYParser::AssignStmtContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(225);
      lVal();
      setState(226);
      match(SysYParser::ASSIGN);
      setState(227);
      exp();
      setState(228);
      match(SysYParser::SEMICOLON);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<SysYParser::ExprStmtContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(231);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 120295788544) != 0)) {
        setState(230);
        exp();
      }
      setState(233);
      match(SysYParser::SEMICOLON);
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<SysYParser::BlockStmtContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(234);
      block();
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<SysYParser::IfStmtContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(235);
      match(SysYParser::IF);
      setState(236);
      match(SysYParser::LPAREN);
      setState(237);
      cond();
      setState(238);
      match(SysYParser::RPAREN);
      setState(239);
      stmt();
      setState(242);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 24, _ctx)) {
      case 1: {
        setState(240);
        match(SysYParser::ELSE);
        setState(241);
        stmt();
        break;
      }

      default:
        break;
      }
      break;
    }

    case 5: {
      _localctx = _tracker.createInstance<SysYParser::WhileStmtContext>(_localctx);
      enterOuterAlt(_localctx, 5);
      setState(244);
      match(SysYParser::WHILE);
      setState(245);
      match(SysYParser::LPAREN);
      setState(246);
      cond();
      setState(247);
      match(SysYParser::RPAREN);
      setState(248);
      stmt();
      break;
    }

    case 6: {
      _localctx = _tracker.createInstance<SysYParser::BreakStmtContext>(_localctx);
      enterOuterAlt(_localctx, 6);
      setState(250);
      match(SysYParser::BREAK);
      setState(251);
      match(SysYParser::SEMICOLON);
      break;
    }

    case 7: {
      _localctx = _tracker.createInstance<SysYParser::ContinueStmtContext>(_localctx);
      enterOuterAlt(_localctx, 7);
      setState(252);
      match(SysYParser::CONTINUE);
      setState(253);
      match(SysYParser::SEMICOLON);
      break;
    }

    case 8: {
      _localctx = _tracker.createInstance<SysYParser::ReturnStmtContext>(_localctx);
      enterOuterAlt(_localctx, 8);
      setState(254);
      match(SysYParser::RETURN);
      setState(256);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 120295788544) != 0)) {
        setState(255);
        exp();
      }
      setState(258);
      match(SysYParser::SEMICOLON);
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExpContext ------------------------------------------------------------------

SysYParser::ExpContext::ExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysYParser::AddExpContext* SysYParser::ExpContext::addExp() {
  return getRuleContext<SysYParser::AddExpContext>(0);
}


size_t SysYParser::ExpContext::getRuleIndex() const {
  return SysYParser::RuleExp;
}


std::any SysYParser::ExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitExp(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::ExpContext* SysYParser::exp() {
  ExpContext *_localctx = _tracker.createInstance<ExpContext>(_ctx, getState());
  enterRule(_localctx, 32, SysYParser::RuleExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(261);
    addExp(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- CondContext ------------------------------------------------------------------

SysYParser::CondContext::CondContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysYParser::LOrExpContext* SysYParser::CondContext::lOrExp() {
  return getRuleContext<SysYParser::LOrExpContext>(0);
}


size_t SysYParser::CondContext::getRuleIndex() const {
  return SysYParser::RuleCond;
}


std::any SysYParser::CondContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitCond(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::CondContext* SysYParser::cond() {
  CondContext *_localctx = _tracker.createInstance<CondContext>(_ctx, getState());
  enterRule(_localctx, 34, SysYParser::RuleCond);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(263);
    lOrExp(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LValContext ------------------------------------------------------------------

SysYParser::LValContext::LValContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysYParser::LValContext::Ident() {
  return getToken(SysYParser::Ident, 0);
}

std::vector<tree::TerminalNode *> SysYParser::LValContext::LBRACKET() {
  return getTokens(SysYParser::LBRACKET);
}

tree::TerminalNode* SysYParser::LValContext::LBRACKET(size_t i) {
  return getToken(SysYParser::LBRACKET, i);
}

std::vector<SysYParser::ExpContext *> SysYParser::LValContext::exp() {
  return getRuleContexts<SysYParser::ExpContext>();
}

SysYParser::ExpContext* SysYParser::LValContext::exp(size_t i) {
  return getRuleContext<SysYParser::ExpContext>(i);
}

std::vector<tree::TerminalNode *> SysYParser::LValContext::RBRACKET() {
  return getTokens(SysYParser::RBRACKET);
}

tree::TerminalNode* SysYParser::LValContext::RBRACKET(size_t i) {
  return getToken(SysYParser::RBRACKET, i);
}


size_t SysYParser::LValContext::getRuleIndex() const {
  return SysYParser::RuleLVal;
}


std::any SysYParser::LValContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitLVal(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::LValContext* SysYParser::lVal() {
  LValContext *_localctx = _tracker.createInstance<LValContext>(_ctx, getState());
  enterRule(_localctx, 36, SysYParser::RuleLVal);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(265);
    match(SysYParser::Ident);
    setState(272);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(266);
        match(SysYParser::LBRACKET);
        setState(267);
        exp();
        setState(268);
        match(SysYParser::RBRACKET); 
      }
      setState(274);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 27, _ctx);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrimaryExpContext ------------------------------------------------------------------

SysYParser::PrimaryExpContext::PrimaryExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::PrimaryExpContext::getRuleIndex() const {
  return SysYParser::RulePrimaryExp;
}

void SysYParser::PrimaryExpContext::copyFrom(PrimaryExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- LValExpContext ------------------------------------------------------------------

SysYParser::LValContext* SysYParser::LValExpContext::lVal() {
  return getRuleContext<SysYParser::LValContext>(0);
}

SysYParser::LValExpContext::LValExpContext(PrimaryExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::LValExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitLValExp(this);
  else
    return visitor->visitChildren(this);
}
//----------------- NumberExpContext ------------------------------------------------------------------

SysYParser::NumberContext* SysYParser::NumberExpContext::number() {
  return getRuleContext<SysYParser::NumberContext>(0);
}

SysYParser::NumberExpContext::NumberExpContext(PrimaryExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::NumberExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitNumberExp(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ParenExpContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::ParenExpContext::LPAREN() {
  return getToken(SysYParser::LPAREN, 0);
}

SysYParser::ExpContext* SysYParser::ParenExpContext::exp() {
  return getRuleContext<SysYParser::ExpContext>(0);
}

tree::TerminalNode* SysYParser::ParenExpContext::RPAREN() {
  return getToken(SysYParser::RPAREN, 0);
}

SysYParser::ParenExpContext::ParenExpContext(PrimaryExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ParenExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitParenExp(this);
  else
    return visitor->visitChildren(this);
}
SysYParser::PrimaryExpContext* SysYParser::primaryExp() {
  PrimaryExpContext *_localctx = _tracker.createInstance<PrimaryExpContext>(_ctx, getState());
  enterRule(_localctx, 38, SysYParser::RulePrimaryExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(281);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::LPAREN: {
        _localctx = _tracker.createInstance<SysYParser::ParenExpContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(275);
        match(SysYParser::LPAREN);
        setState(276);
        exp();
        setState(277);
        match(SysYParser::RPAREN);
        break;
      }

      case SysYParser::Ident: {
        _localctx = _tracker.createInstance<SysYParser::LValExpContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(279);
        lVal();
        break;
      }

      case SysYParser::IntConst:
      case SysYParser::FloatConst: {
        _localctx = _tracker.createInstance<SysYParser::NumberExpContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(280);
        number();
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- NumberContext ------------------------------------------------------------------

SysYParser::NumberContext::NumberContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::NumberContext::getRuleIndex() const {
  return SysYParser::RuleNumber;
}

void SysYParser::NumberContext::copyFrom(NumberContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- FloatNumContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::FloatNumContext::FloatConst() {
  return getToken(SysYParser::FloatConst, 0);
}

SysYParser::FloatNumContext::FloatNumContext(NumberContext *ctx) { copyFrom(ctx); }


std::any SysYParser::FloatNumContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitFloatNum(this);
  else
    return visitor->visitChildren(this);
}
//----------------- IntNumContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::IntNumContext::IntConst() {
  return getToken(SysYParser::IntConst, 0);
}

SysYParser::IntNumContext::IntNumContext(NumberContext *ctx) { copyFrom(ctx); }


std::any SysYParser::IntNumContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitIntNum(this);
  else
    return visitor->visitChildren(this);
}
SysYParser::NumberContext* SysYParser::number() {
  NumberContext *_localctx = _tracker.createInstance<NumberContext>(_ctx, getState());
  enterRule(_localctx, 40, SysYParser::RuleNumber);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(285);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::IntConst: {
        _localctx = _tracker.createInstance<SysYParser::IntNumContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(283);
        match(SysYParser::IntConst);
        break;
      }

      case SysYParser::FloatConst: {
        _localctx = _tracker.createInstance<SysYParser::FloatNumContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(284);
        match(SysYParser::FloatConst);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UnaryExpContext ------------------------------------------------------------------

SysYParser::UnaryExpContext::UnaryExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::UnaryExpContext::getRuleIndex() const {
  return SysYParser::RuleUnaryExp;
}

void SysYParser::UnaryExpContext::copyFrom(UnaryExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- OpUnaryExpContext ------------------------------------------------------------------

SysYParser::UnaryOpContext* SysYParser::OpUnaryExpContext::unaryOp() {
  return getRuleContext<SysYParser::UnaryOpContext>(0);
}

SysYParser::UnaryExpContext* SysYParser::OpUnaryExpContext::unaryExp() {
  return getRuleContext<SysYParser::UnaryExpContext>(0);
}

SysYParser::OpUnaryExpContext::OpUnaryExpContext(UnaryExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::OpUnaryExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitOpUnaryExp(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ToPrimaryExpContext ------------------------------------------------------------------

SysYParser::PrimaryExpContext* SysYParser::ToPrimaryExpContext::primaryExp() {
  return getRuleContext<SysYParser::PrimaryExpContext>(0);
}

SysYParser::ToPrimaryExpContext::ToPrimaryExpContext(UnaryExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ToPrimaryExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitToPrimaryExp(this);
  else
    return visitor->visitChildren(this);
}
//----------------- CallExpContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::CallExpContext::Ident() {
  return getToken(SysYParser::Ident, 0);
}

tree::TerminalNode* SysYParser::CallExpContext::LPAREN() {
  return getToken(SysYParser::LPAREN, 0);
}

tree::TerminalNode* SysYParser::CallExpContext::RPAREN() {
  return getToken(SysYParser::RPAREN, 0);
}

SysYParser::FuncRParamsContext* SysYParser::CallExpContext::funcRParams() {
  return getRuleContext<SysYParser::FuncRParamsContext>(0);
}

SysYParser::CallExpContext::CallExpContext(UnaryExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::CallExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitCallExp(this);
  else
    return visitor->visitChildren(this);
}
SysYParser::UnaryExpContext* SysYParser::unaryExp() {
  UnaryExpContext *_localctx = _tracker.createInstance<UnaryExpContext>(_ctx, getState());
  enterRule(_localctx, 42, SysYParser::RuleUnaryExp);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(297);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 31, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<SysYParser::ToPrimaryExpContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(287);
      primaryExp();
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<SysYParser::CallExpContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(288);
      match(SysYParser::Ident);
      setState(289);
      match(SysYParser::LPAREN);
      setState(291);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 120295788544) != 0)) {
        setState(290);
        funcRParams();
      }
      setState(293);
      match(SysYParser::RPAREN);
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<SysYParser::OpUnaryExpContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(294);
      unaryOp();
      setState(295);
      unaryExp();
      break;
    }

    default:
      break;
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- UnaryOpContext ------------------------------------------------------------------

SysYParser::UnaryOpContext::UnaryOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::UnaryOpContext::getRuleIndex() const {
  return SysYParser::RuleUnaryOp;
}

void SysYParser::UnaryOpContext::copyFrom(UnaryOpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- OpPlusContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::OpPlusContext::PLUS() {
  return getToken(SysYParser::PLUS, 0);
}

SysYParser::OpPlusContext::OpPlusContext(UnaryOpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::OpPlusContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitOpPlus(this);
  else
    return visitor->visitChildren(this);
}
//----------------- OpNotContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::OpNotContext::NOT() {
  return getToken(SysYParser::NOT, 0);
}

SysYParser::OpNotContext::OpNotContext(UnaryOpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::OpNotContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitOpNot(this);
  else
    return visitor->visitChildren(this);
}
//----------------- OpMinusContext ------------------------------------------------------------------

tree::TerminalNode* SysYParser::OpMinusContext::MINUS() {
  return getToken(SysYParser::MINUS, 0);
}

SysYParser::OpMinusContext::OpMinusContext(UnaryOpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::OpMinusContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitOpMinus(this);
  else
    return visitor->visitChildren(this);
}
SysYParser::UnaryOpContext* SysYParser::unaryOp() {
  UnaryOpContext *_localctx = _tracker.createInstance<UnaryOpContext>(_ctx, getState());
  enterRule(_localctx, 44, SysYParser::RuleUnaryOp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(302);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::PLUS: {
        _localctx = _tracker.createInstance<SysYParser::OpPlusContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(299);
        match(SysYParser::PLUS);
        break;
      }

      case SysYParser::MINUS: {
        _localctx = _tracker.createInstance<SysYParser::OpMinusContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(300);
        match(SysYParser::MINUS);
        break;
      }

      case SysYParser::NOT: {
        _localctx = _tracker.createInstance<SysYParser::OpNotContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(301);
        match(SysYParser::NOT);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncRParamsContext ------------------------------------------------------------------

SysYParser::FuncRParamsContext::FuncRParamsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<SysYParser::ExpContext *> SysYParser::FuncRParamsContext::exp() {
  return getRuleContexts<SysYParser::ExpContext>();
}

SysYParser::ExpContext* SysYParser::FuncRParamsContext::exp(size_t i) {
  return getRuleContext<SysYParser::ExpContext>(i);
}

std::vector<tree::TerminalNode *> SysYParser::FuncRParamsContext::COMMA() {
  return getTokens(SysYParser::COMMA);
}

tree::TerminalNode* SysYParser::FuncRParamsContext::COMMA(size_t i) {
  return getToken(SysYParser::COMMA, i);
}


size_t SysYParser::FuncRParamsContext::getRuleIndex() const {
  return SysYParser::RuleFuncRParams;
}


std::any SysYParser::FuncRParamsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitFuncRParams(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::FuncRParamsContext* SysYParser::funcRParams() {
  FuncRParamsContext *_localctx = _tracker.createInstance<FuncRParamsContext>(_ctx, getState());
  enterRule(_localctx, 46, SysYParser::RuleFuncRParams);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(304);
    exp();
    setState(309);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysYParser::COMMA) {
      setState(305);
      match(SysYParser::COMMA);
      setState(306);
      exp();
      setState(311);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- MulExpContext ------------------------------------------------------------------

SysYParser::MulExpContext::MulExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::MulExpContext::getRuleIndex() const {
  return SysYParser::RuleMulExp;
}

void SysYParser::MulExpContext::copyFrom(MulExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- MulDivModExpContext ------------------------------------------------------------------

SysYParser::MulExpContext* SysYParser::MulDivModExpContext::mulExp() {
  return getRuleContext<SysYParser::MulExpContext>(0);
}

SysYParser::UnaryExpContext* SysYParser::MulDivModExpContext::unaryExp() {
  return getRuleContext<SysYParser::UnaryExpContext>(0);
}

tree::TerminalNode* SysYParser::MulDivModExpContext::MUL() {
  return getToken(SysYParser::MUL, 0);
}

tree::TerminalNode* SysYParser::MulDivModExpContext::DIV() {
  return getToken(SysYParser::DIV, 0);
}

tree::TerminalNode* SysYParser::MulDivModExpContext::MOD() {
  return getToken(SysYParser::MOD, 0);
}

SysYParser::MulDivModExpContext::MulDivModExpContext(MulExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::MulDivModExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitMulDivModExp(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ToUnaryExp_mulContext ------------------------------------------------------------------

SysYParser::UnaryExpContext* SysYParser::ToUnaryExp_mulContext::unaryExp() {
  return getRuleContext<SysYParser::UnaryExpContext>(0);
}

SysYParser::ToUnaryExp_mulContext::ToUnaryExp_mulContext(MulExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ToUnaryExp_mulContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitToUnaryExp_mul(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::MulExpContext* SysYParser::mulExp() {
   return mulExp(0);
}

SysYParser::MulExpContext* SysYParser::mulExp(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  SysYParser::MulExpContext *_localctx = _tracker.createInstance<MulExpContext>(_ctx, parentState);
  SysYParser::MulExpContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 48;
  enterRecursionRule(_localctx, 48, SysYParser::RuleMulExp, precedence);

    size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    _localctx = _tracker.createInstance<ToUnaryExp_mulContext>(_localctx);
    _ctx = _localctx;
    previousContext = _localctx;

    setState(313);
    unaryExp();
    _ctx->stop = _input->LT(-1);
    setState(320);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 34, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        auto newContext = _tracker.createInstance<MulDivModExpContext>(_tracker.createInstance<MulExpContext>(parentContext, parentState));
        _localctx = newContext;
        pushNewRecursionContext(newContext, startState, RuleMulExp);
        setState(315);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(316);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 29360128) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(317);
        unaryExp(); 
      }
      setState(322);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 34, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- AddExpContext ------------------------------------------------------------------

SysYParser::AddExpContext::AddExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::AddExpContext::getRuleIndex() const {
  return SysYParser::RuleAddExp;
}

void SysYParser::AddExpContext::copyFrom(AddExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ToMulExp_addContext ------------------------------------------------------------------

SysYParser::MulExpContext* SysYParser::ToMulExp_addContext::mulExp() {
  return getRuleContext<SysYParser::MulExpContext>(0);
}

SysYParser::ToMulExp_addContext::ToMulExp_addContext(AddExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ToMulExp_addContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitToMulExp_add(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AddSubExpContext ------------------------------------------------------------------

SysYParser::AddExpContext* SysYParser::AddSubExpContext::addExp() {
  return getRuleContext<SysYParser::AddExpContext>(0);
}

SysYParser::MulExpContext* SysYParser::AddSubExpContext::mulExp() {
  return getRuleContext<SysYParser::MulExpContext>(0);
}

tree::TerminalNode* SysYParser::AddSubExpContext::PLUS() {
  return getToken(SysYParser::PLUS, 0);
}

tree::TerminalNode* SysYParser::AddSubExpContext::MINUS() {
  return getToken(SysYParser::MINUS, 0);
}

SysYParser::AddSubExpContext::AddSubExpContext(AddExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::AddSubExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitAddSubExp(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::AddExpContext* SysYParser::addExp() {
   return addExp(0);
}

SysYParser::AddExpContext* SysYParser::addExp(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  SysYParser::AddExpContext *_localctx = _tracker.createInstance<AddExpContext>(_ctx, parentState);
  SysYParser::AddExpContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 50;
  enterRecursionRule(_localctx, 50, SysYParser::RuleAddExp, precedence);

    size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    _localctx = _tracker.createInstance<ToMulExp_addContext>(_localctx);
    _ctx = _localctx;
    previousContext = _localctx;

    setState(324);
    mulExp(0);
    _ctx->stop = _input->LT(-1);
    setState(331);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        auto newContext = _tracker.createInstance<AddSubExpContext>(_tracker.createInstance<AddExpContext>(parentContext, parentState));
        _localctx = newContext;
        pushNewRecursionContext(newContext, startState, RuleAddExp);
        setState(326);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(327);
        _la = _input->LA(1);
        if (!(_la == SysYParser::PLUS

        || _la == SysYParser::MINUS)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(328);
        mulExp(0); 
      }
      setState(333);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- RelExpContext ------------------------------------------------------------------

SysYParser::RelExpContext::RelExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::RelExpContext::getRuleIndex() const {
  return SysYParser::RuleRelExp;
}

void SysYParser::RelExpContext::copyFrom(RelExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- RelOpExpContext ------------------------------------------------------------------

SysYParser::RelExpContext* SysYParser::RelOpExpContext::relExp() {
  return getRuleContext<SysYParser::RelExpContext>(0);
}

SysYParser::AddExpContext* SysYParser::RelOpExpContext::addExp() {
  return getRuleContext<SysYParser::AddExpContext>(0);
}

tree::TerminalNode* SysYParser::RelOpExpContext::LT() {
  return getToken(SysYParser::LT, 0);
}

tree::TerminalNode* SysYParser::RelOpExpContext::GT() {
  return getToken(SysYParser::GT, 0);
}

tree::TerminalNode* SysYParser::RelOpExpContext::LE() {
  return getToken(SysYParser::LE, 0);
}

tree::TerminalNode* SysYParser::RelOpExpContext::GE() {
  return getToken(SysYParser::GE, 0);
}

SysYParser::RelOpExpContext::RelOpExpContext(RelExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::RelOpExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitRelOpExp(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ToAddExp_relContext ------------------------------------------------------------------

SysYParser::AddExpContext* SysYParser::ToAddExp_relContext::addExp() {
  return getRuleContext<SysYParser::AddExpContext>(0);
}

SysYParser::ToAddExp_relContext::ToAddExp_relContext(RelExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ToAddExp_relContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitToAddExp_rel(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::RelExpContext* SysYParser::relExp() {
   return relExp(0);
}

SysYParser::RelExpContext* SysYParser::relExp(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  SysYParser::RelExpContext *_localctx = _tracker.createInstance<RelExpContext>(_ctx, parentState);
  SysYParser::RelExpContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 52;
  enterRecursionRule(_localctx, 52, SysYParser::RuleRelExp, precedence);

    size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    _localctx = _tracker.createInstance<ToAddExp_relContext>(_localctx);
    _ctx = _localctx;
    previousContext = _localctx;

    setState(335);
    addExp(0);
    _ctx->stop = _input->LT(-1);
    setState(342);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        auto newContext = _tracker.createInstance<RelOpExpContext>(_tracker.createInstance<RelExpContext>(parentContext, parentState));
        _localctx = newContext;
        pushNewRecursionContext(newContext, startState, RuleRelExp);
        setState(337);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(338);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 1006632960) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(339);
        addExp(0); 
      }
      setState(344);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- EqExpContext ------------------------------------------------------------------

SysYParser::EqExpContext::EqExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::EqExpContext::getRuleIndex() const {
  return SysYParser::RuleEqExp;
}

void SysYParser::EqExpContext::copyFrom(EqExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ToRelExp_eqContext ------------------------------------------------------------------

SysYParser::RelExpContext* SysYParser::ToRelExp_eqContext::relExp() {
  return getRuleContext<SysYParser::RelExpContext>(0);
}

SysYParser::ToRelExp_eqContext::ToRelExp_eqContext(EqExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ToRelExp_eqContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitToRelExp_eq(this);
  else
    return visitor->visitChildren(this);
}
//----------------- EqOpExpContext ------------------------------------------------------------------

SysYParser::EqExpContext* SysYParser::EqOpExpContext::eqExp() {
  return getRuleContext<SysYParser::EqExpContext>(0);
}

SysYParser::RelExpContext* SysYParser::EqOpExpContext::relExp() {
  return getRuleContext<SysYParser::RelExpContext>(0);
}

tree::TerminalNode* SysYParser::EqOpExpContext::EQ() {
  return getToken(SysYParser::EQ, 0);
}

tree::TerminalNode* SysYParser::EqOpExpContext::NE() {
  return getToken(SysYParser::NE, 0);
}

SysYParser::EqOpExpContext::EqOpExpContext(EqExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::EqOpExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitEqOpExp(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::EqExpContext* SysYParser::eqExp() {
   return eqExp(0);
}

SysYParser::EqExpContext* SysYParser::eqExp(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  SysYParser::EqExpContext *_localctx = _tracker.createInstance<EqExpContext>(_ctx, parentState);
  SysYParser::EqExpContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 54;
  enterRecursionRule(_localctx, 54, SysYParser::RuleEqExp, precedence);

    size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    _localctx = _tracker.createInstance<ToRelExp_eqContext>(_localctx);
    _ctx = _localctx;
    previousContext = _localctx;

    setState(346);
    relExp(0);
    _ctx->stop = _input->LT(-1);
    setState(353);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 37, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        auto newContext = _tracker.createInstance<EqOpExpContext>(_tracker.createInstance<EqExpContext>(parentContext, parentState));
        _localctx = newContext;
        pushNewRecursionContext(newContext, startState, RuleEqExp);
        setState(348);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(349);
        _la = _input->LA(1);
        if (!(_la == SysYParser::EQ

        || _la == SysYParser::NE)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(350);
        relExp(0); 
      }
      setState(355);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 37, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- LAndExpContext ------------------------------------------------------------------

SysYParser::LAndExpContext::LAndExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::LAndExpContext::getRuleIndex() const {
  return SysYParser::RuleLAndExp;
}

void SysYParser::LAndExpContext::copyFrom(LAndExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- LandOpExpContext ------------------------------------------------------------------

SysYParser::LAndExpContext* SysYParser::LandOpExpContext::lAndExp() {
  return getRuleContext<SysYParser::LAndExpContext>(0);
}

tree::TerminalNode* SysYParser::LandOpExpContext::AND() {
  return getToken(SysYParser::AND, 0);
}

SysYParser::EqExpContext* SysYParser::LandOpExpContext::eqExp() {
  return getRuleContext<SysYParser::EqExpContext>(0);
}

SysYParser::LandOpExpContext::LandOpExpContext(LAndExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::LandOpExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitLandOpExp(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ToEqExp_landContext ------------------------------------------------------------------

SysYParser::EqExpContext* SysYParser::ToEqExp_landContext::eqExp() {
  return getRuleContext<SysYParser::EqExpContext>(0);
}

SysYParser::ToEqExp_landContext::ToEqExp_landContext(LAndExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ToEqExp_landContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitToEqExp_land(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::LAndExpContext* SysYParser::lAndExp() {
   return lAndExp(0);
}

SysYParser::LAndExpContext* SysYParser::lAndExp(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  SysYParser::LAndExpContext *_localctx = _tracker.createInstance<LAndExpContext>(_ctx, parentState);
  SysYParser::LAndExpContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 56;
  enterRecursionRule(_localctx, 56, SysYParser::RuleLAndExp, precedence);

    

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    _localctx = _tracker.createInstance<ToEqExp_landContext>(_localctx);
    _ctx = _localctx;
    previousContext = _localctx;

    setState(357);
    eqExp(0);
    _ctx->stop = _input->LT(-1);
    setState(364);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 38, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        auto newContext = _tracker.createInstance<LandOpExpContext>(_tracker.createInstance<LAndExpContext>(parentContext, parentState));
        _localctx = newContext;
        pushNewRecursionContext(newContext, startState, RuleLAndExp);
        setState(359);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(360);
        match(SysYParser::AND);
        setState(361);
        eqExp(0); 
      }
      setState(366);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 38, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- LOrExpContext ------------------------------------------------------------------

SysYParser::LOrExpContext::LOrExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t SysYParser::LOrExpContext::getRuleIndex() const {
  return SysYParser::RuleLOrExp;
}

void SysYParser::LOrExpContext::copyFrom(LOrExpContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- LorOpExpContext ------------------------------------------------------------------

SysYParser::LOrExpContext* SysYParser::LorOpExpContext::lOrExp() {
  return getRuleContext<SysYParser::LOrExpContext>(0);
}

tree::TerminalNode* SysYParser::LorOpExpContext::OR() {
  return getToken(SysYParser::OR, 0);
}

SysYParser::LAndExpContext* SysYParser::LorOpExpContext::lAndExp() {
  return getRuleContext<SysYParser::LAndExpContext>(0);
}

SysYParser::LorOpExpContext::LorOpExpContext(LOrExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::LorOpExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitLorOpExp(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ToLAndExp_lorContext ------------------------------------------------------------------

SysYParser::LAndExpContext* SysYParser::ToLAndExp_lorContext::lAndExp() {
  return getRuleContext<SysYParser::LAndExpContext>(0);
}

SysYParser::ToLAndExp_lorContext::ToLAndExp_lorContext(LOrExpContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ToLAndExp_lorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitToLAndExp_lor(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::LOrExpContext* SysYParser::lOrExp() {
   return lOrExp(0);
}

SysYParser::LOrExpContext* SysYParser::lOrExp(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  SysYParser::LOrExpContext *_localctx = _tracker.createInstance<LOrExpContext>(_ctx, parentState);
  SysYParser::LOrExpContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 58;
  enterRecursionRule(_localctx, 58, SysYParser::RuleLOrExp, precedence);

    

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    _localctx = _tracker.createInstance<ToLAndExp_lorContext>(_localctx);
    _ctx = _localctx;
    previousContext = _localctx;

    setState(368);
    lAndExp(0);
    _ctx->stop = _input->LT(-1);
    setState(375);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 39, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        auto newContext = _tracker.createInstance<LorOpExpContext>(_tracker.createInstance<LOrExpContext>(parentContext, parentState));
        _localctx = newContext;
        pushNewRecursionContext(newContext, startState, RuleLOrExp);
        setState(370);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(371);
        match(SysYParser::OR);
        setState(372);
        lAndExp(0); 
      }
      setState(377);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 39, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- ConstExpContext ------------------------------------------------------------------

SysYParser::ConstExpContext::ConstExpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

SysYParser::AddExpContext* SysYParser::ConstExpContext::addExp() {
  return getRuleContext<SysYParser::AddExpContext>(0);
}


size_t SysYParser::ConstExpContext::getRuleIndex() const {
  return SysYParser::RuleConstExp;
}


std::any SysYParser::ConstExpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitConstExp(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::ConstExpContext* SysYParser::constExp() {
  ConstExpContext *_localctx = _tracker.createInstance<ConstExpContext>(_ctx, getState());
  enterRule(_localctx, 60, SysYParser::RuleConstExp);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(378);
    addExp(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

bool SysYParser::sempred(RuleContext *context, size_t ruleIndex, size_t predicateIndex) {
  switch (ruleIndex) {
    case 24: return mulExpSempred(antlrcpp::downCast<MulExpContext *>(context), predicateIndex);
    case 25: return addExpSempred(antlrcpp::downCast<AddExpContext *>(context), predicateIndex);
    case 26: return relExpSempred(antlrcpp::downCast<RelExpContext *>(context), predicateIndex);
    case 27: return eqExpSempred(antlrcpp::downCast<EqExpContext *>(context), predicateIndex);
    case 28: return lAndExpSempred(antlrcpp::downCast<LAndExpContext *>(context), predicateIndex);
    case 29: return lOrExpSempred(antlrcpp::downCast<LOrExpContext *>(context), predicateIndex);

  default:
    break;
  }
  return true;
}

bool SysYParser::mulExpSempred(MulExpContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 0: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool SysYParser::addExpSempred(AddExpContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 1: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool SysYParser::relExpSempred(RelExpContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 2: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool SysYParser::eqExpSempred(EqExpContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 3: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool SysYParser::lAndExpSempred(LAndExpContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 4: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

bool SysYParser::lOrExpSempred(LOrExpContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 5: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

void SysYParser::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  sysyParserInitialize();
#else
  ::antlr4::internal::call_once(sysyParserOnceFlag, sysyParserInitialize);
#endif
}
