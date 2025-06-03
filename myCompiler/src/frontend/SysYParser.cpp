
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
      "addExp", "relExp", "eqExp", "lAndExp", "lOrExp", "constExp", "ident", 
      "intConst", "floatConst"
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
      "IDENTIFIER", "DECIMAL_CONST", "OCTAL_CONST", "HEXADECIMAL_CONST", 
      "FLOAT_CONST", "COMMENT", "WS"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,40,380,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,2,28,7,
  	28,2,29,7,29,2,30,7,30,2,31,7,31,2,32,7,32,2,33,7,33,1,0,1,0,5,0,71,8,
  	0,10,0,12,0,74,9,0,1,0,1,0,1,1,1,1,3,1,80,8,1,1,2,1,2,1,2,1,2,1,2,5,2,
  	87,8,2,10,2,12,2,90,9,2,1,2,1,2,1,3,1,3,3,3,96,8,3,1,4,1,4,1,4,1,4,1,
  	4,5,4,103,8,4,10,4,12,4,106,9,4,1,4,1,4,1,4,1,5,1,5,1,5,1,5,1,5,5,5,116,
  	8,5,10,5,12,5,119,9,5,3,5,121,8,5,1,5,3,5,124,8,5,1,6,1,6,1,6,1,6,5,6,
  	130,8,6,10,6,12,6,133,9,6,1,6,1,6,1,7,1,7,1,7,1,7,1,7,5,7,142,8,7,10,
  	7,12,7,145,9,7,1,7,1,7,3,7,149,8,7,1,8,1,8,1,8,1,8,1,8,5,8,156,8,8,10,
  	8,12,8,159,9,8,3,8,161,8,8,1,8,3,8,164,8,8,1,9,1,9,1,9,1,9,3,9,170,8,
  	9,1,9,1,9,1,9,1,10,1,10,3,10,177,8,10,1,11,1,11,1,11,5,11,182,8,11,10,
  	11,12,11,185,9,11,1,12,1,12,1,12,1,12,1,12,1,12,1,12,1,12,1,12,1,12,1,
  	12,5,12,198,8,12,10,12,12,12,201,9,12,3,12,203,8,12,1,13,1,13,5,13,207,
  	8,13,10,13,12,13,210,9,13,1,13,1,13,1,14,1,14,3,14,216,8,14,1,15,1,15,
  	1,15,1,15,1,15,1,15,3,15,224,8,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,
  	1,15,1,15,3,15,235,8,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,
  	1,15,1,15,1,15,3,15,249,8,15,1,15,3,15,252,8,15,1,16,1,16,1,17,1,17,1,
  	18,1,18,1,18,1,18,1,18,5,18,263,8,18,10,18,12,18,266,9,18,1,19,1,19,1,
  	19,1,19,1,19,1,19,3,19,274,8,19,1,20,1,20,3,20,278,8,20,1,21,1,21,1,21,
  	1,21,3,21,284,8,21,1,21,1,21,1,21,1,21,1,21,3,21,291,8,21,1,22,1,22,1,
  	22,3,22,296,8,22,1,23,1,23,1,23,5,23,301,8,23,10,23,12,23,304,9,23,1,
  	24,1,24,1,24,1,24,1,24,1,24,5,24,312,8,24,10,24,12,24,315,9,24,1,25,1,
  	25,1,25,1,25,1,25,1,25,5,25,323,8,25,10,25,12,25,326,9,25,1,26,1,26,1,
  	26,1,26,1,26,1,26,5,26,334,8,26,10,26,12,26,337,9,26,1,27,1,27,1,27,1,
  	27,1,27,1,27,5,27,345,8,27,10,27,12,27,348,9,27,1,28,1,28,1,28,1,28,1,
  	28,1,28,5,28,356,8,28,10,28,12,28,359,9,28,1,29,1,29,1,29,1,29,1,29,1,
  	29,5,29,367,8,29,10,29,12,29,370,9,29,1,30,1,30,1,31,1,31,1,32,1,32,1,
  	33,1,33,1,33,0,6,48,50,52,54,56,58,34,0,2,4,6,8,10,12,14,16,18,20,22,
  	24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,54,56,58,60,62,64,66,0,5,
  	1,0,22,24,1,0,20,21,1,0,26,29,1,0,30,31,1,0,35,37,393,0,72,1,0,0,0,2,
  	79,1,0,0,0,4,81,1,0,0,0,6,95,1,0,0,0,8,97,1,0,0,0,10,123,1,0,0,0,12,125,
  	1,0,0,0,14,136,1,0,0,0,16,163,1,0,0,0,18,165,1,0,0,0,20,176,1,0,0,0,22,
  	178,1,0,0,0,24,202,1,0,0,0,26,204,1,0,0,0,28,215,1,0,0,0,30,251,1,0,0,
  	0,32,253,1,0,0,0,34,255,1,0,0,0,36,257,1,0,0,0,38,273,1,0,0,0,40,277,
  	1,0,0,0,42,290,1,0,0,0,44,295,1,0,0,0,46,297,1,0,0,0,48,305,1,0,0,0,50,
  	316,1,0,0,0,52,327,1,0,0,0,54,338,1,0,0,0,56,349,1,0,0,0,58,360,1,0,0,
  	0,60,371,1,0,0,0,62,373,1,0,0,0,64,375,1,0,0,0,66,377,1,0,0,0,68,71,3,
  	2,1,0,69,71,3,18,9,0,70,68,1,0,0,0,70,69,1,0,0,0,71,74,1,0,0,0,72,70,
  	1,0,0,0,72,73,1,0,0,0,73,75,1,0,0,0,74,72,1,0,0,0,75,76,5,0,0,1,76,1,
  	1,0,0,0,77,80,3,4,2,0,78,80,3,12,6,0,79,77,1,0,0,0,79,78,1,0,0,0,80,3,
  	1,0,0,0,81,82,5,1,0,0,82,83,3,6,3,0,83,88,3,8,4,0,84,85,5,18,0,0,85,87,
  	3,8,4,0,86,84,1,0,0,0,87,90,1,0,0,0,88,86,1,0,0,0,88,89,1,0,0,0,89,91,
  	1,0,0,0,90,88,1,0,0,0,91,92,5,19,0,0,92,5,1,0,0,0,93,96,5,2,0,0,94,96,
  	5,3,0,0,95,93,1,0,0,0,95,94,1,0,0,0,96,7,1,0,0,0,97,104,3,62,31,0,98,
  	99,5,16,0,0,99,100,3,60,30,0,100,101,5,17,0,0,101,103,1,0,0,0,102,98,
  	1,0,0,0,103,106,1,0,0,0,104,102,1,0,0,0,104,105,1,0,0,0,105,107,1,0,0,
  	0,106,104,1,0,0,0,107,108,5,11,0,0,108,109,3,10,5,0,109,9,1,0,0,0,110,
  	124,3,60,30,0,111,120,5,14,0,0,112,117,3,10,5,0,113,114,5,18,0,0,114,
  	116,3,10,5,0,115,113,1,0,0,0,116,119,1,0,0,0,117,115,1,0,0,0,117,118,
  	1,0,0,0,118,121,1,0,0,0,119,117,1,0,0,0,120,112,1,0,0,0,120,121,1,0,0,
  	0,121,122,1,0,0,0,122,124,5,15,0,0,123,110,1,0,0,0,123,111,1,0,0,0,124,
  	11,1,0,0,0,125,126,3,6,3,0,126,131,3,14,7,0,127,128,5,18,0,0,128,130,
  	3,14,7,0,129,127,1,0,0,0,130,133,1,0,0,0,131,129,1,0,0,0,131,132,1,0,
  	0,0,132,134,1,0,0,0,133,131,1,0,0,0,134,135,5,19,0,0,135,13,1,0,0,0,136,
  	143,3,62,31,0,137,138,5,16,0,0,138,139,3,60,30,0,139,140,5,17,0,0,140,
  	142,1,0,0,0,141,137,1,0,0,0,142,145,1,0,0,0,143,141,1,0,0,0,143,144,1,
  	0,0,0,144,148,1,0,0,0,145,143,1,0,0,0,146,147,5,11,0,0,147,149,3,16,8,
  	0,148,146,1,0,0,0,148,149,1,0,0,0,149,15,1,0,0,0,150,164,3,32,16,0,151,
  	160,5,14,0,0,152,157,3,16,8,0,153,154,5,18,0,0,154,156,3,16,8,0,155,153,
  	1,0,0,0,156,159,1,0,0,0,157,155,1,0,0,0,157,158,1,0,0,0,158,161,1,0,0,
  	0,159,157,1,0,0,0,160,152,1,0,0,0,160,161,1,0,0,0,161,162,1,0,0,0,162,
  	164,5,15,0,0,163,150,1,0,0,0,163,151,1,0,0,0,164,17,1,0,0,0,165,166,3,
  	20,10,0,166,167,3,62,31,0,167,169,5,12,0,0,168,170,3,22,11,0,169,168,
  	1,0,0,0,169,170,1,0,0,0,170,171,1,0,0,0,171,172,5,13,0,0,172,173,3,26,
  	13,0,173,19,1,0,0,0,174,177,5,4,0,0,175,177,3,6,3,0,176,174,1,0,0,0,176,
  	175,1,0,0,0,177,21,1,0,0,0,178,183,3,24,12,0,179,180,5,18,0,0,180,182,
  	3,24,12,0,181,179,1,0,0,0,182,185,1,0,0,0,183,181,1,0,0,0,183,184,1,0,
  	0,0,184,23,1,0,0,0,185,183,1,0,0,0,186,187,3,6,3,0,187,188,3,62,31,0,
  	188,203,1,0,0,0,189,190,3,6,3,0,190,191,3,62,31,0,191,192,5,16,0,0,192,
  	199,5,17,0,0,193,194,5,16,0,0,194,195,3,32,16,0,195,196,5,17,0,0,196,
  	198,1,0,0,0,197,193,1,0,0,0,198,201,1,0,0,0,199,197,1,0,0,0,199,200,1,
  	0,0,0,200,203,1,0,0,0,201,199,1,0,0,0,202,186,1,0,0,0,202,189,1,0,0,0,
  	203,25,1,0,0,0,204,208,5,14,0,0,205,207,3,28,14,0,206,205,1,0,0,0,207,
  	210,1,0,0,0,208,206,1,0,0,0,208,209,1,0,0,0,209,211,1,0,0,0,210,208,1,
  	0,0,0,211,212,5,15,0,0,212,27,1,0,0,0,213,216,3,2,1,0,214,216,3,30,15,
  	0,215,213,1,0,0,0,215,214,1,0,0,0,216,29,1,0,0,0,217,218,3,36,18,0,218,
  	219,5,11,0,0,219,220,3,32,16,0,220,221,5,19,0,0,221,252,1,0,0,0,222,224,
  	3,32,16,0,223,222,1,0,0,0,223,224,1,0,0,0,224,225,1,0,0,0,225,252,5,19,
  	0,0,226,252,3,26,13,0,227,228,5,5,0,0,228,229,5,12,0,0,229,230,3,34,17,
  	0,230,231,5,13,0,0,231,234,3,30,15,0,232,233,5,6,0,0,233,235,3,30,15,
  	0,234,232,1,0,0,0,234,235,1,0,0,0,235,252,1,0,0,0,236,237,5,7,0,0,237,
  	238,5,12,0,0,238,239,3,34,17,0,239,240,5,13,0,0,240,241,3,30,15,0,241,
  	252,1,0,0,0,242,243,5,8,0,0,243,252,5,19,0,0,244,245,5,9,0,0,245,252,
  	5,19,0,0,246,248,5,10,0,0,247,249,3,32,16,0,248,247,1,0,0,0,248,249,1,
  	0,0,0,249,250,1,0,0,0,250,252,5,19,0,0,251,217,1,0,0,0,251,223,1,0,0,
  	0,251,226,1,0,0,0,251,227,1,0,0,0,251,236,1,0,0,0,251,242,1,0,0,0,251,
  	244,1,0,0,0,251,246,1,0,0,0,252,31,1,0,0,0,253,254,3,50,25,0,254,33,1,
  	0,0,0,255,256,3,58,29,0,256,35,1,0,0,0,257,264,3,62,31,0,258,259,5,16,
  	0,0,259,260,3,32,16,0,260,261,5,17,0,0,261,263,1,0,0,0,262,258,1,0,0,
  	0,263,266,1,0,0,0,264,262,1,0,0,0,264,265,1,0,0,0,265,37,1,0,0,0,266,
  	264,1,0,0,0,267,268,5,12,0,0,268,269,3,32,16,0,269,270,5,13,0,0,270,274,
  	1,0,0,0,271,274,3,36,18,0,272,274,3,40,20,0,273,267,1,0,0,0,273,271,1,
  	0,0,0,273,272,1,0,0,0,274,39,1,0,0,0,275,278,3,64,32,0,276,278,3,66,33,
  	0,277,275,1,0,0,0,277,276,1,0,0,0,278,41,1,0,0,0,279,291,3,38,19,0,280,
  	281,3,62,31,0,281,283,5,12,0,0,282,284,3,46,23,0,283,282,1,0,0,0,283,
  	284,1,0,0,0,284,285,1,0,0,0,285,286,5,13,0,0,286,291,1,0,0,0,287,288,
  	3,44,22,0,288,289,3,42,21,0,289,291,1,0,0,0,290,279,1,0,0,0,290,280,1,
  	0,0,0,290,287,1,0,0,0,291,43,1,0,0,0,292,296,5,20,0,0,293,296,5,21,0,
  	0,294,296,5,25,0,0,295,292,1,0,0,0,295,293,1,0,0,0,295,294,1,0,0,0,296,
  	45,1,0,0,0,297,302,3,32,16,0,298,299,5,18,0,0,299,301,3,32,16,0,300,298,
  	1,0,0,0,301,304,1,0,0,0,302,300,1,0,0,0,302,303,1,0,0,0,303,47,1,0,0,
  	0,304,302,1,0,0,0,305,306,6,24,-1,0,306,307,3,42,21,0,307,313,1,0,0,0,
  	308,309,10,1,0,0,309,310,7,0,0,0,310,312,3,42,21,0,311,308,1,0,0,0,312,
  	315,1,0,0,0,313,311,1,0,0,0,313,314,1,0,0,0,314,49,1,0,0,0,315,313,1,
  	0,0,0,316,317,6,25,-1,0,317,318,3,48,24,0,318,324,1,0,0,0,319,320,10,
  	1,0,0,320,321,7,1,0,0,321,323,3,48,24,0,322,319,1,0,0,0,323,326,1,0,0,
  	0,324,322,1,0,0,0,324,325,1,0,0,0,325,51,1,0,0,0,326,324,1,0,0,0,327,
  	328,6,26,-1,0,328,329,3,50,25,0,329,335,1,0,0,0,330,331,10,1,0,0,331,
  	332,7,2,0,0,332,334,3,50,25,0,333,330,1,0,0,0,334,337,1,0,0,0,335,333,
  	1,0,0,0,335,336,1,0,0,0,336,53,1,0,0,0,337,335,1,0,0,0,338,339,6,27,-1,
  	0,339,340,3,52,26,0,340,346,1,0,0,0,341,342,10,1,0,0,342,343,7,3,0,0,
  	343,345,3,52,26,0,344,341,1,0,0,0,345,348,1,0,0,0,346,344,1,0,0,0,346,
  	347,1,0,0,0,347,55,1,0,0,0,348,346,1,0,0,0,349,350,6,28,-1,0,350,351,
  	3,54,27,0,351,357,1,0,0,0,352,353,10,1,0,0,353,354,5,32,0,0,354,356,3,
  	54,27,0,355,352,1,0,0,0,356,359,1,0,0,0,357,355,1,0,0,0,357,358,1,0,0,
  	0,358,57,1,0,0,0,359,357,1,0,0,0,360,361,6,29,-1,0,361,362,3,56,28,0,
  	362,368,1,0,0,0,363,364,10,1,0,0,364,365,5,33,0,0,365,367,3,56,28,0,366,
  	363,1,0,0,0,367,370,1,0,0,0,368,366,1,0,0,0,368,369,1,0,0,0,369,59,1,
  	0,0,0,370,368,1,0,0,0,371,372,3,50,25,0,372,61,1,0,0,0,373,374,5,34,0,
  	0,374,63,1,0,0,0,375,376,7,4,0,0,376,65,1,0,0,0,377,378,5,38,0,0,378,
  	67,1,0,0,0,39,70,72,79,88,95,104,117,120,123,131,143,148,157,160,163,
  	169,176,183,199,202,208,215,223,234,248,251,264,273,277,283,290,295,302,
  	313,324,335,346,357,368
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
    setState(72);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 30) != 0)) {
      setState(70);
      _errHandler->sync(this);
      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 0, _ctx)) {
      case 1: {
        setState(68);
        decl();
        break;
      }

      case 2: {
        setState(69);
        funcDef();
        break;
      }

      default:
        break;
      }
      setState(74);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(75);
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
    setState(79);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::CONST: {
        _localctx = _tracker.createInstance<SysYParser::ConstDeclarationContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(77);
        constDecl();
        break;
      }

      case SysYParser::INT:
      case SysYParser::FLOAT: {
        _localctx = _tracker.createInstance<SysYParser::VariableDeclarationContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(78);
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
    setState(81);
    match(SysYParser::CONST);
    setState(82);
    bType();
    setState(83);
    constDef();
    setState(88);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysYParser::COMMA) {
      setState(84);
      match(SysYParser::COMMA);
      setState(85);
      constDef();
      setState(90);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(91);
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
    setState(95);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::INT: {
        _localctx = _tracker.createInstance<SysYParser::TypeIntContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(93);
        match(SysYParser::INT);
        break;
      }

      case SysYParser::FLOAT: {
        _localctx = _tracker.createInstance<SysYParser::TypeFloatContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(94);
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

SysYParser::IdentContext* SysYParser::ConstDefContext::ident() {
  return getRuleContext<SysYParser::IdentContext>(0);
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
    setState(97);
    ident();
    setState(104);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysYParser::LBRACKET) {
      setState(98);
      match(SysYParser::LBRACKET);
      setState(99);
      constExp();
      setState(100);
      match(SysYParser::RBRACKET);
      setState(106);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(107);
    match(SysYParser::ASSIGN);
    setState(108);
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
    setState(123);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::LPAREN:
      case SysYParser::PLUS:
      case SysYParser::MINUS:
      case SysYParser::NOT:
      case SysYParser::IDENTIFIER:
      case SysYParser::DECIMAL_CONST:
      case SysYParser::OCTAL_CONST:
      case SysYParser::HEXADECIMAL_CONST:
      case SysYParser::FLOAT_CONST: {
        _localctx = _tracker.createInstance<SysYParser::ConstInitExprContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(110);
        constExp();
        break;
      }

      case SysYParser::LBRACE: {
        _localctx = _tracker.createInstance<SysYParser::ConstInitListContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(111);
        match(SysYParser::LBRACE);
        setState(120);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 532612665344) != 0)) {
          setState(112);
          constInitVal();
          setState(117);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == SysYParser::COMMA) {
            setState(113);
            match(SysYParser::COMMA);
            setState(114);
            constInitVal();
            setState(119);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
        }
        setState(122);
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
    setState(125);
    bType();
    setState(126);
    varDef();
    setState(131);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysYParser::COMMA) {
      setState(127);
      match(SysYParser::COMMA);
      setState(128);
      varDef();
      setState(133);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(134);
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

SysYParser::IdentContext* SysYParser::VarDefContext::ident() {
  return getRuleContext<SysYParser::IdentContext>(0);
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
    setState(136);
    ident();
    setState(143);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysYParser::LBRACKET) {
      setState(137);
      match(SysYParser::LBRACKET);
      setState(138);
      constExp();
      setState(139);
      match(SysYParser::RBRACKET);
      setState(145);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(148);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == SysYParser::ASSIGN) {
      setState(146);
      match(SysYParser::ASSIGN);
      setState(147);
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
    setState(163);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::LPAREN:
      case SysYParser::PLUS:
      case SysYParser::MINUS:
      case SysYParser::NOT:
      case SysYParser::IDENTIFIER:
      case SysYParser::DECIMAL_CONST:
      case SysYParser::OCTAL_CONST:
      case SysYParser::HEXADECIMAL_CONST:
      case SysYParser::FLOAT_CONST: {
        _localctx = _tracker.createInstance<SysYParser::InitExprContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(150);
        exp();
        break;
      }

      case SysYParser::LBRACE: {
        _localctx = _tracker.createInstance<SysYParser::InitListContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(151);
        match(SysYParser::LBRACE);
        setState(160);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 532612665344) != 0)) {
          setState(152);
          initVal();
          setState(157);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == SysYParser::COMMA) {
            setState(153);
            match(SysYParser::COMMA);
            setState(154);
            initVal();
            setState(159);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
        }
        setState(162);
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

SysYParser::IdentContext* SysYParser::FuncDefContext::ident() {
  return getRuleContext<SysYParser::IdentContext>(0);
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
    setState(165);
    funcType();
    setState(166);
    ident();
    setState(167);
    match(SysYParser::LPAREN);
    setState(169);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == SysYParser::INT

    || _la == SysYParser::FLOAT) {
      setState(168);
      funcFParams();
    }
    setState(171);
    match(SysYParser::RPAREN);
    setState(172);
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
    setState(176);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::VOID: {
        _localctx = _tracker.createInstance<SysYParser::TypeVoidContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(174);
        match(SysYParser::VOID);
        break;
      }

      case SysYParser::INT:
      case SysYParser::FLOAT: {
        _localctx = _tracker.createInstance<SysYParser::TypeBTypeContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(175);
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
    setState(178);
    funcFParam();
    setState(183);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysYParser::COMMA) {
      setState(179);
      match(SysYParser::COMMA);
      setState(180);
      funcFParam();
      setState(185);
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

//----------------- ScalarParamContext ------------------------------------------------------------------

SysYParser::BTypeContext* SysYParser::ScalarParamContext::bType() {
  return getRuleContext<SysYParser::BTypeContext>(0);
}

SysYParser::IdentContext* SysYParser::ScalarParamContext::ident() {
  return getRuleContext<SysYParser::IdentContext>(0);
}

SysYParser::ScalarParamContext::ScalarParamContext(FuncFParamContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ScalarParamContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitScalarParam(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ArrayParamContext ------------------------------------------------------------------

SysYParser::BTypeContext* SysYParser::ArrayParamContext::bType() {
  return getRuleContext<SysYParser::BTypeContext>(0);
}

SysYParser::IdentContext* SysYParser::ArrayParamContext::ident() {
  return getRuleContext<SysYParser::IdentContext>(0);
}

std::vector<tree::TerminalNode *> SysYParser::ArrayParamContext::LBRACKET() {
  return getTokens(SysYParser::LBRACKET);
}

tree::TerminalNode* SysYParser::ArrayParamContext::LBRACKET(size_t i) {
  return getToken(SysYParser::LBRACKET, i);
}

std::vector<tree::TerminalNode *> SysYParser::ArrayParamContext::RBRACKET() {
  return getTokens(SysYParser::RBRACKET);
}

tree::TerminalNode* SysYParser::ArrayParamContext::RBRACKET(size_t i) {
  return getToken(SysYParser::RBRACKET, i);
}

std::vector<SysYParser::ExpContext *> SysYParser::ArrayParamContext::exp() {
  return getRuleContexts<SysYParser::ExpContext>();
}

SysYParser::ExpContext* SysYParser::ArrayParamContext::exp(size_t i) {
  return getRuleContext<SysYParser::ExpContext>(i);
}

SysYParser::ArrayParamContext::ArrayParamContext(FuncFParamContext *ctx) { copyFrom(ctx); }


std::any SysYParser::ArrayParamContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitArrayParam(this);
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
    setState(202);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 19, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<SysYParser::ScalarParamContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(186);
      bType();
      setState(187);
      ident();
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<SysYParser::ArrayParamContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(189);
      bType();
      setState(190);
      ident();
      setState(191);
      match(SysYParser::LBRACKET);
      setState(192);
      match(SysYParser::RBRACKET);
      setState(199);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == SysYParser::LBRACKET) {
        setState(193);
        match(SysYParser::LBRACKET);
        setState(194);
        exp();
        setState(195);
        match(SysYParser::RBRACKET);
        setState(201);
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
    setState(204);
    match(SysYParser::LBRACE);
    setState(208);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 532613191598) != 0)) {
      setState(205);
      blockItem();
      setState(210);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(211);
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
    setState(215);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::CONST:
      case SysYParser::INT:
      case SysYParser::FLOAT: {
        _localctx = _tracker.createInstance<SysYParser::ItemDeclContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(213);
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
      case SysYParser::IDENTIFIER:
      case SysYParser::DECIMAL_CONST:
      case SysYParser::OCTAL_CONST:
      case SysYParser::HEXADECIMAL_CONST:
      case SysYParser::FLOAT_CONST: {
        _localctx = _tracker.createInstance<SysYParser::ItemStmtContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(214);
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
    setState(251);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 25, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<SysYParser::AssignStmtContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(217);
      lVal();
      setState(218);
      match(SysYParser::ASSIGN);
      setState(219);
      exp();
      setState(220);
      match(SysYParser::SEMICOLON);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<SysYParser::ExprStmtContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(223);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 532612648960) != 0)) {
        setState(222);
        exp();
      }
      setState(225);
      match(SysYParser::SEMICOLON);
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<SysYParser::BlockStmtContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(226);
      block();
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<SysYParser::IfStmtContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(227);
      match(SysYParser::IF);
      setState(228);
      match(SysYParser::LPAREN);
      setState(229);
      cond();
      setState(230);
      match(SysYParser::RPAREN);
      setState(231);
      stmt();
      setState(234);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23, _ctx)) {
      case 1: {
        setState(232);
        match(SysYParser::ELSE);
        setState(233);
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
      setState(236);
      match(SysYParser::WHILE);
      setState(237);
      match(SysYParser::LPAREN);
      setState(238);
      cond();
      setState(239);
      match(SysYParser::RPAREN);
      setState(240);
      stmt();
      break;
    }

    case 6: {
      _localctx = _tracker.createInstance<SysYParser::BreakStmtContext>(_localctx);
      enterOuterAlt(_localctx, 6);
      setState(242);
      match(SysYParser::BREAK);
      setState(243);
      match(SysYParser::SEMICOLON);
      break;
    }

    case 7: {
      _localctx = _tracker.createInstance<SysYParser::ContinueStmtContext>(_localctx);
      enterOuterAlt(_localctx, 7);
      setState(244);
      match(SysYParser::CONTINUE);
      setState(245);
      match(SysYParser::SEMICOLON);
      break;
    }

    case 8: {
      _localctx = _tracker.createInstance<SysYParser::ReturnStmtContext>(_localctx);
      enterOuterAlt(_localctx, 8);
      setState(246);
      match(SysYParser::RETURN);
      setState(248);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 532612648960) != 0)) {
        setState(247);
        exp();
      }
      setState(250);
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
    setState(253);
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
    setState(255);
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

SysYParser::IdentContext* SysYParser::LValContext::ident() {
  return getRuleContext<SysYParser::IdentContext>(0);
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
    setState(257);
    ident();
    setState(264);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(258);
        match(SysYParser::LBRACKET);
        setState(259);
        exp();
        setState(260);
        match(SysYParser::RBRACKET); 
      }
      setState(266);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 26, _ctx);
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
    setState(273);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::LPAREN: {
        _localctx = _tracker.createInstance<SysYParser::ParenExpContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(267);
        match(SysYParser::LPAREN);
        setState(268);
        exp();
        setState(269);
        match(SysYParser::RPAREN);
        break;
      }

      case SysYParser::IDENTIFIER: {
        _localctx = _tracker.createInstance<SysYParser::LValExpContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(271);
        lVal();
        break;
      }

      case SysYParser::DECIMAL_CONST:
      case SysYParser::OCTAL_CONST:
      case SysYParser::HEXADECIMAL_CONST:
      case SysYParser::FLOAT_CONST: {
        _localctx = _tracker.createInstance<SysYParser::NumberExpContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(272);
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

SysYParser::FloatConstContext* SysYParser::FloatNumContext::floatConst() {
  return getRuleContext<SysYParser::FloatConstContext>(0);
}

SysYParser::FloatNumContext::FloatNumContext(NumberContext *ctx) { copyFrom(ctx); }


std::any SysYParser::FloatNumContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitFloatNum(this);
  else
    return visitor->visitChildren(this);
}
//----------------- IntNumContext ------------------------------------------------------------------

SysYParser::IntConstContext* SysYParser::IntNumContext::intConst() {
  return getRuleContext<SysYParser::IntConstContext>(0);
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
    setState(277);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::DECIMAL_CONST:
      case SysYParser::OCTAL_CONST:
      case SysYParser::HEXADECIMAL_CONST: {
        _localctx = _tracker.createInstance<SysYParser::IntNumContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(275);
        intConst();
        break;
      }

      case SysYParser::FLOAT_CONST: {
        _localctx = _tracker.createInstance<SysYParser::FloatNumContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(276);
        floatConst();
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

SysYParser::IdentContext* SysYParser::CallExpContext::ident() {
  return getRuleContext<SysYParser::IdentContext>(0);
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
    setState(290);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 30, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<SysYParser::ToPrimaryExpContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(279);
      primaryExp();
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<SysYParser::CallExpContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(280);
      ident();
      setState(281);
      match(SysYParser::LPAREN);
      setState(283);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 532612648960) != 0)) {
        setState(282);
        funcRParams();
      }
      setState(285);
      match(SysYParser::RPAREN);
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<SysYParser::OpUnaryExpContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(287);
      unaryOp();
      setState(288);
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
    setState(295);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case SysYParser::PLUS: {
        _localctx = _tracker.createInstance<SysYParser::OpPlusContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(292);
        match(SysYParser::PLUS);
        break;
      }

      case SysYParser::MINUS: {
        _localctx = _tracker.createInstance<SysYParser::OpMinusContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(293);
        match(SysYParser::MINUS);
        break;
      }

      case SysYParser::NOT: {
        _localctx = _tracker.createInstance<SysYParser::OpNotContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(294);
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
    setState(297);
    exp();
    setState(302);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == SysYParser::COMMA) {
      setState(298);
      match(SysYParser::COMMA);
      setState(299);
      exp();
      setState(304);
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

    setState(306);
    unaryExp();
    _ctx->stop = _input->LT(-1);
    setState(313);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        auto newContext = _tracker.createInstance<MulDivModExpContext>(_tracker.createInstance<MulExpContext>(parentContext, parentState));
        _localctx = newContext;
        pushNewRecursionContext(newContext, startState, RuleMulExp);
        setState(308);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(309);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 29360128) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(310);
        unaryExp(); 
      }
      setState(315);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 33, _ctx);
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

    setState(317);
    mulExp(0);
    _ctx->stop = _input->LT(-1);
    setState(324);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 34, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        auto newContext = _tracker.createInstance<AddSubExpContext>(_tracker.createInstance<AddExpContext>(parentContext, parentState));
        _localctx = newContext;
        pushNewRecursionContext(newContext, startState, RuleAddExp);
        setState(319);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(320);
        _la = _input->LA(1);
        if (!(_la == SysYParser::PLUS

        || _la == SysYParser::MINUS)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(321);
        mulExp(0); 
      }
      setState(326);
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

    setState(328);
    addExp(0);
    _ctx->stop = _input->LT(-1);
    setState(335);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 35, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        auto newContext = _tracker.createInstance<RelOpExpContext>(_tracker.createInstance<RelExpContext>(parentContext, parentState));
        _localctx = newContext;
        pushNewRecursionContext(newContext, startState, RuleRelExp);
        setState(330);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(331);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 1006632960) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(332);
        addExp(0); 
      }
      setState(337);
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

    setState(339);
    relExp(0);
    _ctx->stop = _input->LT(-1);
    setState(346);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 36, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        auto newContext = _tracker.createInstance<EqOpExpContext>(_tracker.createInstance<EqExpContext>(parentContext, parentState));
        _localctx = newContext;
        pushNewRecursionContext(newContext, startState, RuleEqExp);
        setState(341);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(342);
        _la = _input->LA(1);
        if (!(_la == SysYParser::EQ

        || _la == SysYParser::NE)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        setState(343);
        relExp(0); 
      }
      setState(348);
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

    setState(350);
    eqExp(0);
    _ctx->stop = _input->LT(-1);
    setState(357);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 37, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        auto newContext = _tracker.createInstance<LandOpExpContext>(_tracker.createInstance<LAndExpContext>(parentContext, parentState));
        _localctx = newContext;
        pushNewRecursionContext(newContext, startState, RuleLAndExp);
        setState(352);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(353);
        match(SysYParser::AND);
        setState(354);
        eqExp(0); 
      }
      setState(359);
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

    setState(361);
    lAndExp(0);
    _ctx->stop = _input->LT(-1);
    setState(368);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 38, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        auto newContext = _tracker.createInstance<LorOpExpContext>(_tracker.createInstance<LOrExpContext>(parentContext, parentState));
        _localctx = newContext;
        pushNewRecursionContext(newContext, startState, RuleLOrExp);
        setState(363);

        if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
        setState(364);
        match(SysYParser::OR);
        setState(365);
        lAndExp(0); 
      }
      setState(370);
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
    setState(371);
    addExp(0);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- IdentContext ------------------------------------------------------------------

SysYParser::IdentContext::IdentContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysYParser::IdentContext::IDENTIFIER() {
  return getToken(SysYParser::IDENTIFIER, 0);
}


size_t SysYParser::IdentContext::getRuleIndex() const {
  return SysYParser::RuleIdent;
}


std::any SysYParser::IdentContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitIdent(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::IdentContext* SysYParser::ident() {
  IdentContext *_localctx = _tracker.createInstance<IdentContext>(_ctx, getState());
  enterRule(_localctx, 62, SysYParser::RuleIdent);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(373);
    match(SysYParser::IDENTIFIER);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- IntConstContext ------------------------------------------------------------------

SysYParser::IntConstContext::IntConstContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysYParser::IntConstContext::DECIMAL_CONST() {
  return getToken(SysYParser::DECIMAL_CONST, 0);
}

tree::TerminalNode* SysYParser::IntConstContext::OCTAL_CONST() {
  return getToken(SysYParser::OCTAL_CONST, 0);
}

tree::TerminalNode* SysYParser::IntConstContext::HEXADECIMAL_CONST() {
  return getToken(SysYParser::HEXADECIMAL_CONST, 0);
}


size_t SysYParser::IntConstContext::getRuleIndex() const {
  return SysYParser::RuleIntConst;
}


std::any SysYParser::IntConstContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitIntConst(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::IntConstContext* SysYParser::intConst() {
  IntConstContext *_localctx = _tracker.createInstance<IntConstContext>(_ctx, getState());
  enterRule(_localctx, 64, SysYParser::RuleIntConst);
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
    setState(375);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 240518168576) != 0))) {
    _errHandler->recoverInline(this);
    }
    else {
      _errHandler->reportMatch(this);
      consume();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FloatConstContext ------------------------------------------------------------------

SysYParser::FloatConstContext::FloatConstContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* SysYParser::FloatConstContext::FLOAT_CONST() {
  return getToken(SysYParser::FLOAT_CONST, 0);
}


size_t SysYParser::FloatConstContext::getRuleIndex() const {
  return SysYParser::RuleFloatConst;
}


std::any SysYParser::FloatConstContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<SysYVisitor*>(visitor))
    return parserVisitor->visitFloatConst(this);
  else
    return visitor->visitChildren(this);
}

SysYParser::FloatConstContext* SysYParser::floatConst() {
  FloatConstContext *_localctx = _tracker.createInstance<FloatConstContext>(_ctx, getState());
  enterRule(_localctx, 66, SysYParser::RuleFloatConst);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(377);
    match(SysYParser::FLOAT_CONST);
   
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
