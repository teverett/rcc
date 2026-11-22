
// Generated from /Users/tom/projects/pcc/grammar/c/CParser.g4 by ANTLR 4.13.2


#include "CParserVisitor.h"

#include "CParser.h"


using namespace antlrcpp;

using namespace antlr4;

namespace {

struct CParserStaticData final {
  CParserStaticData(std::vector<std::string> ruleNames,
                        std::vector<std::string> literalNames,
                        std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  CParserStaticData(const CParserStaticData&) = delete;
  CParserStaticData(CParserStaticData&&) = delete;
  CParserStaticData& operator=(const CParserStaticData&) = delete;
  CParserStaticData& operator=(CParserStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag cparserParserOnceFlag;
#if ANTLR4_USE_THREAD_LOCAL_CACHE
static thread_local
#endif
std::unique_ptr<CParserStaticData> cparserParserStaticData = nullptr;

void cparserParserInitialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  if (cparserParserStaticData != nullptr) {
    return;
  }
#else
  assert(cparserParserStaticData == nullptr);
#endif
  auto staticData = std::make_unique<CParserStaticData>(
    std::vector<std::string>{
      "program", "typeSpec", "baseTypeSpec", "topLevelDecl", "structDecl", 
      "structMember", "enumDecl", "enumMember", "functionDecl", "returnTypeSpec", 
      "paramList", "param", "globalDecl", "externDeclarator", "globalDeclarator", 
      "arrayInit", "arrayInitElem", "block", "statement", "simpleStmt", 
      "localDeclarator", "staticLocalDeclarator", "switchCase", "assignOp", 
      "expr", "condExpr", "argList"
    },
    std::vector<std::string>{
      "", "'int'", "'char'", "'short'", "'long'", "'float'", "'double'", 
      "'signed'", "'unsigned'", "'void'", "'sizeof'", "'if'", "'else'", 
      "'while'", "'for'", "'switch'", "'case'", "'default'", "'return'", 
      "'break'", "'continue'", "'goto'", "'extern'", "'static'", "'const'", 
      "'register'", "'struct'", "'enum'", "'('", "')'", "'['", "']'", "'{'", 
      "", "'@'", "';'", "','", "'='", "'*'", "'&'", "'-'", "'!'", "'~'", 
      "'/'", "'%'", "'+'", "'<<'", "'>>'", "'<='", "'>='", "'<'", "'>'", 
      "'=='", "'!='", "'^'", "'|'", "'&&'", "'||'", "'+='", "'-='", "'*='", 
      "'/='", "'%='", "'&='", "'^='", "'|='", "'<<='", "'>>='", "'++'", 
      "'--'", "'\\u003F'", "':'", "'.'", "'->'"
    },
    std::vector<std::string>{
      "", "INT_KW", "CHAR_KW", "SHORT_KW", "LONG_KW", "FLOAT_KW", "DOUBLE_KW", 
      "SIGNED_KW", "UNSIGNED_KW", "VOID_KW", "SIZEOF_KW", "IF_KW", "ELSE_KW", 
      "WHILE_KW", "FOR_KW", "SWITCH_KW", "CASE_KW", "DEFAULT_KW", "RETURN_KW", 
      "BREAK_KW", "CONTINUE_KW", "GOTO_KW", "EXTERN_KW", "STATIC_KW", "CONST_KW", 
      "REGISTER_KW", "STRUCT_KW", "ENUM_KW", "LPAREN", "RPAREN", "LBRACK", 
      "RBRACK", "LBRACE", "RBRACE", "AT", "SEMI", "COMMA", "ASSIGN", "STAR", 
      "AMP", "MINUS", "BANG", "TILDE", "SLASH", "PERCENT", "PLUS", "SHL", 
      "SHR", "LE", "GE", "LT", "GT", "EQ", "NE", "CARET", "PIPE", "ANDAND", 
      "OROR", "PLUSEQ", "MINUSEQ", "STAREQ", "SLASHEQ", "PERCENTEQ", "AMPEQ", 
      "CARETEQ", "PIPEEQ", "SHLEQ", "SHREQ", "INC", "DEC", "QUESTION", "COLON", 
      "DOT", "ARROW", "IDENT", "NUMBER", "FLOAT_NUMBER", "CHARLIT", "STRINGLIT", 
      "LINE_COMMENT", "BLOCK_COMMENT", "WS", "ASM_BEGIN", "ASM_TEXT", "ASM_END"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,84,707,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,1,0,5,0,56,8,0,10,
  	0,12,0,59,9,0,1,0,1,0,1,1,1,1,5,1,65,8,1,10,1,12,1,68,9,1,1,2,3,2,71,
  	8,2,1,2,1,2,3,2,75,8,2,1,2,1,2,3,2,79,8,2,1,2,3,2,82,8,2,1,2,1,2,3,2,
  	86,8,2,1,2,1,2,1,2,3,2,91,8,2,1,2,3,2,94,8,2,1,2,1,2,3,2,98,8,2,1,2,1,
  	2,1,2,1,2,1,2,1,2,1,2,3,2,107,8,2,1,3,1,3,1,3,1,3,3,3,113,8,3,1,4,1,4,
  	1,4,1,4,3,4,119,8,4,1,4,1,4,4,4,123,8,4,11,4,12,4,124,1,4,1,4,1,4,1,5,
  	1,5,1,5,1,5,1,5,3,5,135,8,5,1,5,1,5,1,6,1,6,1,6,1,6,1,6,1,6,5,6,145,8,
  	6,10,6,12,6,148,9,6,1,6,3,6,151,8,6,1,6,1,6,1,6,1,7,1,7,1,7,3,7,159,8,
  	7,1,7,3,7,162,8,7,1,8,1,8,1,8,1,8,1,8,3,8,169,8,8,1,8,1,8,1,8,3,8,174,
  	8,8,1,8,1,8,1,8,3,8,179,8,8,1,8,1,8,1,8,1,8,3,8,185,8,8,1,8,1,8,1,8,3,
  	8,190,8,8,1,8,1,8,3,8,194,8,8,1,8,1,8,3,8,198,8,8,3,8,200,8,8,1,9,1,9,
  	3,9,204,8,9,1,10,1,10,1,10,5,10,209,8,10,10,10,12,10,212,9,10,1,10,3,
  	10,215,8,10,1,11,3,11,218,8,11,1,11,3,11,221,8,11,1,11,1,11,1,11,1,12,
  	1,12,3,12,228,8,12,1,12,1,12,1,12,1,12,5,12,234,8,12,10,12,12,12,237,
  	9,12,1,12,1,12,1,12,3,12,242,8,12,1,12,3,12,245,8,12,1,12,1,12,1,12,1,
  	12,5,12,251,8,12,10,12,12,12,254,9,12,1,12,1,12,3,12,258,8,12,1,13,1,
  	13,1,13,1,13,4,13,264,8,13,11,13,12,13,265,1,13,3,13,269,8,13,1,14,1,
  	14,1,14,1,14,4,14,275,8,14,11,14,12,14,276,1,14,1,14,3,14,281,8,14,1,
  	14,1,14,3,14,285,8,14,1,14,1,14,1,14,3,14,290,8,14,1,14,1,14,3,14,294,
  	8,14,3,14,296,8,14,1,14,1,14,3,14,300,8,14,3,14,302,8,14,1,15,1,15,1,
  	15,1,15,5,15,308,8,15,10,15,12,15,311,9,15,1,15,1,15,1,16,3,16,316,8,
  	16,1,16,1,16,3,16,320,8,16,1,17,1,17,5,17,324,8,17,10,17,12,17,327,9,
  	17,1,17,1,17,1,18,1,18,1,18,1,18,1,18,1,18,1,18,1,18,3,18,339,8,18,1,
  	18,1,18,1,18,1,18,1,18,1,18,1,18,1,18,1,18,3,18,350,8,18,1,18,1,18,3,
  	18,354,8,18,1,18,1,18,3,18,358,8,18,1,18,1,18,1,18,1,18,1,18,1,18,1,18,
  	1,18,5,18,368,8,18,10,18,12,18,371,9,18,1,18,1,18,1,18,1,18,3,18,377,
  	8,18,1,18,1,18,1,18,1,18,1,18,1,18,1,18,1,18,1,18,1,18,1,18,1,18,1,18,
  	3,18,392,8,18,1,18,1,18,1,18,1,18,1,18,3,18,399,8,18,1,19,3,19,402,8,
  	19,1,19,3,19,405,8,19,1,19,1,19,1,19,1,19,5,19,411,8,19,10,19,12,19,414,
  	9,19,1,19,1,19,3,19,418,8,19,1,19,1,19,1,19,1,19,5,19,424,8,19,10,19,
  	12,19,427,9,19,1,19,3,19,430,8,19,1,20,1,20,1,20,1,20,4,20,436,8,20,11,
  	20,12,20,437,1,20,1,20,3,20,442,8,20,1,20,1,20,1,20,3,20,447,8,20,3,20,
  	449,8,20,1,21,1,21,1,21,1,21,4,21,455,8,21,11,21,12,21,456,1,21,1,21,
  	3,21,461,8,21,1,21,1,21,1,21,3,21,466,8,21,1,21,1,21,3,21,470,8,21,3,
  	21,472,8,21,3,21,474,8,21,1,22,1,22,3,22,478,8,22,1,22,1,22,3,22,482,
  	8,22,1,22,1,22,5,22,486,8,22,10,22,12,22,489,9,22,1,22,1,22,1,22,5,22,
  	494,8,22,10,22,12,22,497,9,22,3,22,499,8,22,1,23,1,23,1,24,1,24,1,24,
  	1,24,1,24,1,24,1,24,1,24,1,24,1,24,4,24,513,8,24,11,24,12,24,514,1,24,
  	1,24,1,24,1,24,1,24,1,24,1,24,1,24,1,24,1,24,1,24,1,24,1,24,1,24,3,24,
  	531,8,24,1,24,1,24,1,24,1,24,1,24,1,24,1,24,1,24,1,24,1,24,3,24,543,8,
  	24,1,24,1,24,1,24,1,24,3,24,549,8,24,1,25,1,25,1,25,1,25,1,25,1,25,1,
  	25,1,25,4,25,559,8,25,11,25,12,25,560,1,25,1,25,1,25,1,25,1,25,1,25,1,
  	25,1,25,1,25,3,25,572,8,25,1,25,1,25,1,25,1,25,1,25,1,25,3,25,580,8,25,
  	1,25,1,25,1,25,1,25,1,25,1,25,1,25,4,25,589,8,25,11,25,12,25,590,1,25,
  	1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,
  	1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,
  	1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,4,25,631,8,25,11,25,12,25,
  	632,1,25,1,25,1,25,3,25,638,8,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,
  	25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,
  	25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,
  	25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,3,25,683,8,25,1,25,1,25,1,25,1,
  	25,1,25,1,25,1,25,3,25,692,8,25,5,25,694,8,25,10,25,12,25,697,9,25,1,
  	26,1,26,1,26,5,26,702,8,26,10,26,12,26,705,9,26,1,26,0,1,50,27,0,2,4,
  	6,8,10,12,14,16,18,20,22,24,26,28,30,32,34,36,38,40,42,44,46,48,50,52,
  	0,10,1,0,7,8,1,0,75,76,2,0,37,37,58,67,1,0,68,69,1,0,40,42,2,0,38,38,
  	43,44,2,0,40,40,45,45,1,0,46,47,1,0,48,51,1,0,52,53,831,0,57,1,0,0,0,
  	2,62,1,0,0,0,4,106,1,0,0,0,6,112,1,0,0,0,8,114,1,0,0,0,10,129,1,0,0,0,
  	12,138,1,0,0,0,14,155,1,0,0,0,16,199,1,0,0,0,18,203,1,0,0,0,20,214,1,
  	0,0,0,22,217,1,0,0,0,24,257,1,0,0,0,26,268,1,0,0,0,28,301,1,0,0,0,30,
  	303,1,0,0,0,32,319,1,0,0,0,34,321,1,0,0,0,36,398,1,0,0,0,38,429,1,0,0,
  	0,40,448,1,0,0,0,42,473,1,0,0,0,44,498,1,0,0,0,46,500,1,0,0,0,48,548,
  	1,0,0,0,50,637,1,0,0,0,52,698,1,0,0,0,54,56,3,6,3,0,55,54,1,0,0,0,56,
  	59,1,0,0,0,57,55,1,0,0,0,57,58,1,0,0,0,58,60,1,0,0,0,59,57,1,0,0,0,60,
  	61,5,0,0,1,61,1,1,0,0,0,62,66,3,4,2,0,63,65,5,38,0,0,64,63,1,0,0,0,65,
  	68,1,0,0,0,66,64,1,0,0,0,66,67,1,0,0,0,67,3,1,0,0,0,68,66,1,0,0,0,69,
  	71,7,0,0,0,70,69,1,0,0,0,70,71,1,0,0,0,71,72,1,0,0,0,72,107,5,2,0,0,73,
  	75,7,0,0,0,74,73,1,0,0,0,74,75,1,0,0,0,75,76,1,0,0,0,76,78,5,3,0,0,77,
  	79,5,1,0,0,78,77,1,0,0,0,78,79,1,0,0,0,79,107,1,0,0,0,80,82,7,0,0,0,81,
  	80,1,0,0,0,81,82,1,0,0,0,82,83,1,0,0,0,83,107,5,1,0,0,84,86,7,0,0,0,85,
  	84,1,0,0,0,85,86,1,0,0,0,86,87,1,0,0,0,87,88,5,4,0,0,88,90,5,4,0,0,89,
  	91,5,1,0,0,90,89,1,0,0,0,90,91,1,0,0,0,91,107,1,0,0,0,92,94,7,0,0,0,93,
  	92,1,0,0,0,93,94,1,0,0,0,94,95,1,0,0,0,95,97,5,4,0,0,96,98,5,1,0,0,97,
  	96,1,0,0,0,97,98,1,0,0,0,98,107,1,0,0,0,99,107,7,0,0,0,100,107,5,5,0,
  	0,101,107,5,6,0,0,102,103,5,26,0,0,103,107,5,74,0,0,104,105,5,27,0,0,
  	105,107,5,74,0,0,106,70,1,0,0,0,106,74,1,0,0,0,106,81,1,0,0,0,106,85,
  	1,0,0,0,106,93,1,0,0,0,106,99,1,0,0,0,106,100,1,0,0,0,106,101,1,0,0,0,
  	106,102,1,0,0,0,106,104,1,0,0,0,107,5,1,0,0,0,108,113,3,16,8,0,109,113,
  	3,24,12,0,110,113,3,8,4,0,111,113,3,12,6,0,112,108,1,0,0,0,112,109,1,
  	0,0,0,112,110,1,0,0,0,112,111,1,0,0,0,113,7,1,0,0,0,114,115,5,26,0,0,
  	115,118,5,74,0,0,116,117,5,34,0,0,117,119,5,74,0,0,118,116,1,0,0,0,118,
  	119,1,0,0,0,119,120,1,0,0,0,120,122,5,32,0,0,121,123,3,10,5,0,122,121,
  	1,0,0,0,123,124,1,0,0,0,124,122,1,0,0,0,124,125,1,0,0,0,125,126,1,0,0,
  	0,126,127,5,33,0,0,127,128,5,35,0,0,128,9,1,0,0,0,129,130,3,2,1,0,130,
  	134,5,74,0,0,131,132,5,30,0,0,132,133,5,75,0,0,133,135,5,31,0,0,134,131,
  	1,0,0,0,134,135,1,0,0,0,135,136,1,0,0,0,136,137,5,35,0,0,137,11,1,0,0,
  	0,138,139,5,27,0,0,139,140,5,74,0,0,140,141,5,32,0,0,141,146,3,14,7,0,
  	142,143,5,36,0,0,143,145,3,14,7,0,144,142,1,0,0,0,145,148,1,0,0,0,146,
  	144,1,0,0,0,146,147,1,0,0,0,147,150,1,0,0,0,148,146,1,0,0,0,149,151,5,
  	36,0,0,150,149,1,0,0,0,150,151,1,0,0,0,151,152,1,0,0,0,152,153,5,33,0,
  	0,153,154,5,35,0,0,154,13,1,0,0,0,155,161,5,74,0,0,156,158,5,37,0,0,157,
  	159,5,40,0,0,158,157,1,0,0,0,158,159,1,0,0,0,159,160,1,0,0,0,160,162,
  	5,75,0,0,161,156,1,0,0,0,161,162,1,0,0,0,162,15,1,0,0,0,163,164,5,22,
  	0,0,164,165,3,18,9,0,165,166,5,74,0,0,166,168,5,28,0,0,167,169,3,20,10,
  	0,168,167,1,0,0,0,168,169,1,0,0,0,169,170,1,0,0,0,170,173,5,29,0,0,171,
  	172,5,34,0,0,172,174,5,74,0,0,173,171,1,0,0,0,173,174,1,0,0,0,174,175,
  	1,0,0,0,175,176,5,35,0,0,176,200,1,0,0,0,177,179,5,23,0,0,178,177,1,0,
  	0,0,178,179,1,0,0,0,179,180,1,0,0,0,180,181,3,18,9,0,181,182,5,74,0,0,
  	182,184,5,28,0,0,183,185,3,20,10,0,184,183,1,0,0,0,184,185,1,0,0,0,185,
  	186,1,0,0,0,186,189,5,29,0,0,187,188,5,34,0,0,188,190,5,74,0,0,189,187,
  	1,0,0,0,189,190,1,0,0,0,190,193,1,0,0,0,191,192,5,34,0,0,192,194,5,75,
  	0,0,193,191,1,0,0,0,193,194,1,0,0,0,194,197,1,0,0,0,195,198,3,34,17,0,
  	196,198,5,35,0,0,197,195,1,0,0,0,197,196,1,0,0,0,198,200,1,0,0,0,199,
  	163,1,0,0,0,199,178,1,0,0,0,200,17,1,0,0,0,201,204,3,2,1,0,202,204,5,
  	9,0,0,203,201,1,0,0,0,203,202,1,0,0,0,204,19,1,0,0,0,205,210,3,22,11,
  	0,206,207,5,36,0,0,207,209,3,22,11,0,208,206,1,0,0,0,209,212,1,0,0,0,
  	210,208,1,0,0,0,210,211,1,0,0,0,211,215,1,0,0,0,212,210,1,0,0,0,213,215,
  	5,9,0,0,214,205,1,0,0,0,214,213,1,0,0,0,215,21,1,0,0,0,216,218,5,25,0,
  	0,217,216,1,0,0,0,217,218,1,0,0,0,218,220,1,0,0,0,219,221,5,24,0,0,220,
  	219,1,0,0,0,220,221,1,0,0,0,221,222,1,0,0,0,222,223,3,2,1,0,223,224,5,
  	74,0,0,224,23,1,0,0,0,225,227,5,22,0,0,226,228,5,24,0,0,227,226,1,0,0,
  	0,227,228,1,0,0,0,228,229,1,0,0,0,229,230,3,2,1,0,230,235,3,26,13,0,231,
  	232,5,36,0,0,232,234,3,26,13,0,233,231,1,0,0,0,234,237,1,0,0,0,235,233,
  	1,0,0,0,235,236,1,0,0,0,236,238,1,0,0,0,237,235,1,0,0,0,238,239,5,35,
  	0,0,239,258,1,0,0,0,240,242,5,23,0,0,241,240,1,0,0,0,241,242,1,0,0,0,
  	242,244,1,0,0,0,243,245,5,24,0,0,244,243,1,0,0,0,244,245,1,0,0,0,245,
  	246,1,0,0,0,246,247,3,2,1,0,247,252,3,28,14,0,248,249,5,36,0,0,249,251,
  	3,28,14,0,250,248,1,0,0,0,251,254,1,0,0,0,252,250,1,0,0,0,252,253,1,0,
  	0,0,253,255,1,0,0,0,254,252,1,0,0,0,255,256,5,35,0,0,256,258,1,0,0,0,
  	257,225,1,0,0,0,257,241,1,0,0,0,258,25,1,0,0,0,259,263,5,74,0,0,260,261,
  	5,30,0,0,261,262,5,75,0,0,262,264,5,31,0,0,263,260,1,0,0,0,264,265,1,
  	0,0,0,265,263,1,0,0,0,265,266,1,0,0,0,266,269,1,0,0,0,267,269,5,74,0,
  	0,268,259,1,0,0,0,268,267,1,0,0,0,269,27,1,0,0,0,270,274,5,74,0,0,271,
  	272,5,30,0,0,272,273,5,75,0,0,273,275,5,31,0,0,274,271,1,0,0,0,275,276,
  	1,0,0,0,276,274,1,0,0,0,276,277,1,0,0,0,277,280,1,0,0,0,278,279,5,37,
  	0,0,279,281,3,30,15,0,280,278,1,0,0,0,280,281,1,0,0,0,281,284,1,0,0,0,
  	282,283,5,34,0,0,283,285,5,75,0,0,284,282,1,0,0,0,284,285,1,0,0,0,285,
  	302,1,0,0,0,286,295,5,74,0,0,287,293,5,37,0,0,288,290,5,40,0,0,289,288,
  	1,0,0,0,289,290,1,0,0,0,290,291,1,0,0,0,291,294,7,1,0,0,292,294,5,74,
  	0,0,293,289,1,0,0,0,293,292,1,0,0,0,294,296,1,0,0,0,295,287,1,0,0,0,295,
  	296,1,0,0,0,296,299,1,0,0,0,297,298,5,34,0,0,298,300,5,75,0,0,299,297,
  	1,0,0,0,299,300,1,0,0,0,300,302,1,0,0,0,301,270,1,0,0,0,301,286,1,0,0,
  	0,302,29,1,0,0,0,303,304,5,32,0,0,304,309,3,32,16,0,305,306,5,36,0,0,
  	306,308,3,32,16,0,307,305,1,0,0,0,308,311,1,0,0,0,309,307,1,0,0,0,309,
  	310,1,0,0,0,310,312,1,0,0,0,311,309,1,0,0,0,312,313,5,33,0,0,313,31,1,
  	0,0,0,314,316,5,40,0,0,315,314,1,0,0,0,315,316,1,0,0,0,316,317,1,0,0,
  	0,317,320,7,1,0,0,318,320,5,74,0,0,319,315,1,0,0,0,319,318,1,0,0,0,320,
  	33,1,0,0,0,321,325,5,32,0,0,322,324,3,36,18,0,323,322,1,0,0,0,324,327,
  	1,0,0,0,325,323,1,0,0,0,325,326,1,0,0,0,326,328,1,0,0,0,327,325,1,0,0,
  	0,328,329,5,33,0,0,329,35,1,0,0,0,330,399,3,34,17,0,331,332,5,11,0,0,
  	332,333,5,28,0,0,333,334,3,48,24,0,334,335,5,29,0,0,335,338,3,36,18,0,
  	336,337,5,12,0,0,337,339,3,36,18,0,338,336,1,0,0,0,338,339,1,0,0,0,339,
  	399,1,0,0,0,340,341,5,13,0,0,341,342,5,28,0,0,342,343,3,48,24,0,343,344,
  	5,29,0,0,344,345,3,36,18,0,345,399,1,0,0,0,346,347,5,14,0,0,347,349,5,
  	28,0,0,348,350,3,38,19,0,349,348,1,0,0,0,349,350,1,0,0,0,350,351,1,0,
  	0,0,351,353,5,35,0,0,352,354,3,48,24,0,353,352,1,0,0,0,353,354,1,0,0,
  	0,354,355,1,0,0,0,355,357,5,35,0,0,356,358,3,38,19,0,357,356,1,0,0,0,
  	357,358,1,0,0,0,358,359,1,0,0,0,359,360,5,29,0,0,360,399,3,36,18,0,361,
  	362,5,15,0,0,362,363,5,28,0,0,363,364,3,48,24,0,364,365,5,29,0,0,365,
  	369,5,32,0,0,366,368,3,44,22,0,367,366,1,0,0,0,368,371,1,0,0,0,369,367,
  	1,0,0,0,369,370,1,0,0,0,370,372,1,0,0,0,371,369,1,0,0,0,372,373,5,33,
  	0,0,373,399,1,0,0,0,374,376,5,18,0,0,375,377,3,48,24,0,376,375,1,0,0,
  	0,376,377,1,0,0,0,377,378,1,0,0,0,378,399,5,35,0,0,379,380,5,19,0,0,380,
  	399,5,35,0,0,381,382,5,20,0,0,382,399,5,35,0,0,383,384,5,21,0,0,384,385,
  	5,74,0,0,385,399,5,35,0,0,386,387,5,74,0,0,387,388,5,71,0,0,388,399,3,
  	36,18,0,389,391,5,82,0,0,390,392,5,83,0,0,391,390,1,0,0,0,391,392,1,0,
  	0,0,392,393,1,0,0,0,393,399,5,84,0,0,394,395,3,38,19,0,395,396,5,35,0,
  	0,396,399,1,0,0,0,397,399,5,35,0,0,398,330,1,0,0,0,398,331,1,0,0,0,398,
  	340,1,0,0,0,398,346,1,0,0,0,398,361,1,0,0,0,398,374,1,0,0,0,398,379,1,
  	0,0,0,398,381,1,0,0,0,398,383,1,0,0,0,398,386,1,0,0,0,398,389,1,0,0,0,
  	398,394,1,0,0,0,398,397,1,0,0,0,399,37,1,0,0,0,400,402,5,25,0,0,401,400,
  	1,0,0,0,401,402,1,0,0,0,402,404,1,0,0,0,403,405,5,24,0,0,404,403,1,0,
  	0,0,404,405,1,0,0,0,405,406,1,0,0,0,406,407,3,2,1,0,407,412,3,40,20,0,
  	408,409,5,36,0,0,409,411,3,40,20,0,410,408,1,0,0,0,411,414,1,0,0,0,412,
  	410,1,0,0,0,412,413,1,0,0,0,413,430,1,0,0,0,414,412,1,0,0,0,415,417,5,
  	23,0,0,416,418,5,24,0,0,417,416,1,0,0,0,417,418,1,0,0,0,418,419,1,0,0,
  	0,419,420,3,2,1,0,420,425,3,42,21,0,421,422,5,36,0,0,422,424,3,42,21,
  	0,423,421,1,0,0,0,424,427,1,0,0,0,425,423,1,0,0,0,425,426,1,0,0,0,426,
  	430,1,0,0,0,427,425,1,0,0,0,428,430,3,48,24,0,429,401,1,0,0,0,429,415,
  	1,0,0,0,429,428,1,0,0,0,430,39,1,0,0,0,431,435,5,74,0,0,432,433,5,30,
  	0,0,433,434,5,75,0,0,434,436,5,31,0,0,435,432,1,0,0,0,436,437,1,0,0,0,
  	437,435,1,0,0,0,437,438,1,0,0,0,438,441,1,0,0,0,439,440,5,37,0,0,440,
  	442,3,30,15,0,441,439,1,0,0,0,441,442,1,0,0,0,442,449,1,0,0,0,443,446,
  	5,74,0,0,444,445,5,37,0,0,445,447,3,48,24,0,446,444,1,0,0,0,446,447,1,
  	0,0,0,447,449,1,0,0,0,448,431,1,0,0,0,448,443,1,0,0,0,449,41,1,0,0,0,
  	450,454,5,74,0,0,451,452,5,30,0,0,452,453,5,75,0,0,453,455,5,31,0,0,454,
  	451,1,0,0,0,455,456,1,0,0,0,456,454,1,0,0,0,456,457,1,0,0,0,457,460,1,
  	0,0,0,458,459,5,37,0,0,459,461,3,30,15,0,460,458,1,0,0,0,460,461,1,0,
  	0,0,461,474,1,0,0,0,462,471,5,74,0,0,463,469,5,37,0,0,464,466,5,40,0,
  	0,465,464,1,0,0,0,465,466,1,0,0,0,466,467,1,0,0,0,467,470,5,75,0,0,468,
  	470,5,74,0,0,469,465,1,0,0,0,469,468,1,0,0,0,470,472,1,0,0,0,471,463,
  	1,0,0,0,471,472,1,0,0,0,472,474,1,0,0,0,473,450,1,0,0,0,473,462,1,0,0,
  	0,474,43,1,0,0,0,475,481,5,16,0,0,476,478,5,40,0,0,477,476,1,0,0,0,477,
  	478,1,0,0,0,478,479,1,0,0,0,479,482,5,75,0,0,480,482,5,74,0,0,481,477,
  	1,0,0,0,481,480,1,0,0,0,482,483,1,0,0,0,483,487,5,71,0,0,484,486,3,36,
  	18,0,485,484,1,0,0,0,486,489,1,0,0,0,487,485,1,0,0,0,487,488,1,0,0,0,
  	488,499,1,0,0,0,489,487,1,0,0,0,490,491,5,17,0,0,491,495,5,71,0,0,492,
  	494,3,36,18,0,493,492,1,0,0,0,494,497,1,0,0,0,495,493,1,0,0,0,495,496,
  	1,0,0,0,496,499,1,0,0,0,497,495,1,0,0,0,498,475,1,0,0,0,498,490,1,0,0,
  	0,499,45,1,0,0,0,500,501,7,2,0,0,501,47,1,0,0,0,502,503,5,38,0,0,503,
  	504,3,50,25,0,504,505,3,46,23,0,505,506,3,48,24,0,506,549,1,0,0,0,507,
  	512,5,74,0,0,508,509,5,30,0,0,509,510,3,48,24,0,510,511,5,31,0,0,511,
  	513,1,0,0,0,512,508,1,0,0,0,513,514,1,0,0,0,514,512,1,0,0,0,514,515,1,
  	0,0,0,515,516,1,0,0,0,516,517,3,46,23,0,517,518,3,48,24,0,518,549,1,0,
  	0,0,519,520,5,74,0,0,520,521,3,46,23,0,521,522,3,48,24,0,522,549,1,0,
  	0,0,523,524,3,50,25,0,524,525,5,72,0,0,525,530,5,74,0,0,526,527,5,30,
  	0,0,527,528,3,48,24,0,528,529,5,31,0,0,529,531,1,0,0,0,530,526,1,0,0,
  	0,530,531,1,0,0,0,531,532,1,0,0,0,532,533,3,46,23,0,533,534,3,48,24,0,
  	534,549,1,0,0,0,535,536,3,50,25,0,536,537,5,73,0,0,537,542,5,74,0,0,538,
  	539,5,30,0,0,539,540,3,48,24,0,540,541,5,31,0,0,541,543,1,0,0,0,542,538,
  	1,0,0,0,542,543,1,0,0,0,543,544,1,0,0,0,544,545,3,46,23,0,545,546,3,48,
  	24,0,546,549,1,0,0,0,547,549,3,50,25,0,548,502,1,0,0,0,548,507,1,0,0,
  	0,548,519,1,0,0,0,548,523,1,0,0,0,548,535,1,0,0,0,548,547,1,0,0,0,549,
  	49,1,0,0,0,550,551,6,25,-1,0,551,552,5,74,0,0,552,638,7,3,0,0,553,558,
  	5,74,0,0,554,555,5,30,0,0,555,556,3,48,24,0,556,557,5,31,0,0,557,559,
  	1,0,0,0,558,554,1,0,0,0,559,560,1,0,0,0,560,558,1,0,0,0,560,561,1,0,0,
  	0,561,562,1,0,0,0,562,563,7,3,0,0,563,638,1,0,0,0,564,565,5,38,0,0,565,
  	566,3,50,25,0,566,567,7,3,0,0,567,638,1,0,0,0,568,569,5,74,0,0,569,571,
  	5,28,0,0,570,572,3,52,26,0,571,570,1,0,0,0,571,572,1,0,0,0,572,573,1,
  	0,0,0,573,638,5,29,0,0,574,575,5,28,0,0,575,576,3,48,24,0,576,577,5,29,
  	0,0,577,579,5,28,0,0,578,580,3,52,26,0,579,578,1,0,0,0,579,580,1,0,0,
  	0,580,581,1,0,0,0,581,582,5,29,0,0,582,638,1,0,0,0,583,588,5,74,0,0,584,
  	585,5,30,0,0,585,586,3,48,24,0,586,587,5,31,0,0,587,589,1,0,0,0,588,584,
  	1,0,0,0,589,590,1,0,0,0,590,588,1,0,0,0,590,591,1,0,0,0,591,638,1,0,0,
  	0,592,638,5,74,0,0,593,638,5,75,0,0,594,638,5,76,0,0,595,638,5,77,0,0,
  	596,638,5,78,0,0,597,598,5,28,0,0,598,599,3,48,24,0,599,600,5,29,0,0,
  	600,638,1,0,0,0,601,602,5,28,0,0,602,603,3,2,1,0,603,604,5,29,0,0,604,
  	605,3,50,25,22,605,638,1,0,0,0,606,607,5,10,0,0,607,608,5,28,0,0,608,
  	609,3,2,1,0,609,610,5,29,0,0,610,638,1,0,0,0,611,612,5,10,0,0,612,613,
  	5,28,0,0,613,614,3,48,24,0,614,615,5,29,0,0,615,638,1,0,0,0,616,617,5,
  	38,0,0,617,638,3,50,25,19,618,619,5,39,0,0,619,638,3,50,25,18,620,621,
  	7,4,0,0,621,638,3,50,25,15,622,623,7,3,0,0,623,638,5,74,0,0,624,625,7,
  	3,0,0,625,630,5,74,0,0,626,627,5,30,0,0,627,628,3,48,24,0,628,629,5,31,
  	0,0,629,631,1,0,0,0,630,626,1,0,0,0,631,632,1,0,0,0,632,630,1,0,0,0,632,
  	633,1,0,0,0,633,638,1,0,0,0,634,635,7,3,0,0,635,636,5,38,0,0,636,638,
  	3,50,25,12,637,550,1,0,0,0,637,553,1,0,0,0,637,564,1,0,0,0,637,568,1,
  	0,0,0,637,574,1,0,0,0,637,583,1,0,0,0,637,592,1,0,0,0,637,593,1,0,0,0,
  	637,594,1,0,0,0,637,595,1,0,0,0,637,596,1,0,0,0,637,597,1,0,0,0,637,601,
  	1,0,0,0,637,606,1,0,0,0,637,611,1,0,0,0,637,616,1,0,0,0,637,618,1,0,0,
  	0,637,620,1,0,0,0,637,622,1,0,0,0,637,624,1,0,0,0,637,634,1,0,0,0,638,
  	695,1,0,0,0,639,640,10,11,0,0,640,641,7,5,0,0,641,694,3,50,25,12,642,
  	643,10,10,0,0,643,644,7,6,0,0,644,694,3,50,25,11,645,646,10,9,0,0,646,
  	647,7,7,0,0,647,694,3,50,25,10,648,649,10,8,0,0,649,650,7,8,0,0,650,694,
  	3,50,25,9,651,652,10,7,0,0,652,653,7,9,0,0,653,694,3,50,25,8,654,655,
  	10,6,0,0,655,656,5,39,0,0,656,694,3,50,25,7,657,658,10,5,0,0,658,659,
  	5,54,0,0,659,694,3,50,25,6,660,661,10,4,0,0,661,662,5,55,0,0,662,694,
  	3,50,25,5,663,664,10,3,0,0,664,665,5,56,0,0,665,694,3,50,25,4,666,667,
  	10,2,0,0,667,668,5,57,0,0,668,694,3,50,25,3,669,670,10,1,0,0,670,671,
  	5,70,0,0,671,672,3,48,24,0,672,673,5,71,0,0,673,674,3,50,25,1,674,694,
  	1,0,0,0,675,676,10,17,0,0,676,677,5,72,0,0,677,682,5,74,0,0,678,679,5,
  	30,0,0,679,680,3,48,24,0,680,681,5,31,0,0,681,683,1,0,0,0,682,678,1,0,
  	0,0,682,683,1,0,0,0,683,694,1,0,0,0,684,685,10,16,0,0,685,686,5,73,0,
  	0,686,691,5,74,0,0,687,688,5,30,0,0,688,689,3,48,24,0,689,690,5,31,0,
  	0,690,692,1,0,0,0,691,687,1,0,0,0,691,692,1,0,0,0,692,694,1,0,0,0,693,
  	639,1,0,0,0,693,642,1,0,0,0,693,645,1,0,0,0,693,648,1,0,0,0,693,651,1,
  	0,0,0,693,654,1,0,0,0,693,657,1,0,0,0,693,660,1,0,0,0,693,663,1,0,0,0,
  	693,666,1,0,0,0,693,669,1,0,0,0,693,675,1,0,0,0,693,684,1,0,0,0,694,697,
  	1,0,0,0,695,693,1,0,0,0,695,696,1,0,0,0,696,51,1,0,0,0,697,695,1,0,0,
  	0,698,703,3,48,24,0,699,700,5,36,0,0,700,702,3,48,24,0,701,699,1,0,0,
  	0,702,705,1,0,0,0,703,701,1,0,0,0,703,704,1,0,0,0,704,53,1,0,0,0,705,
  	703,1,0,0,0,96,57,66,70,74,78,81,85,90,93,97,106,112,118,124,134,146,
  	150,158,161,168,173,178,184,189,193,197,199,203,210,214,217,220,227,235,
  	241,244,252,257,265,268,276,280,284,289,293,295,299,301,309,315,319,325,
  	338,349,353,357,369,376,391,398,401,404,412,417,425,429,437,441,446,448,
  	456,460,465,469,471,473,477,481,487,495,498,514,530,542,548,560,571,579,
  	590,632,637,682,691,693,695,703
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  cparserParserStaticData = std::move(staticData);
}

}

CParser::CParser(TokenStream *input) : CParser(input, antlr4::atn::ParserATNSimulatorOptions()) {}

CParser::CParser(TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options) : Parser(input) {
  CParser::initialize();
  _interpreter = new atn::ParserATNSimulator(this, *cparserParserStaticData->atn, cparserParserStaticData->decisionToDFA, cparserParserStaticData->sharedContextCache, options);
}

CParser::~CParser() {
  delete _interpreter;
}

const atn::ATN& CParser::getATN() const {
  return *cparserParserStaticData->atn;
}

std::string CParser::getGrammarFileName() const {
  return "CParser.g4";
}

const std::vector<std::string>& CParser::getRuleNames() const {
  return cparserParserStaticData->ruleNames;
}

const dfa::Vocabulary& CParser::getVocabulary() const {
  return cparserParserStaticData->vocabulary;
}

antlr4::atn::SerializedATNView CParser::getSerializedATN() const {
  return cparserParserStaticData->serializedATN;
}


//----------------- ProgramContext ------------------------------------------------------------------

CParser::ProgramContext::ProgramContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CParser::ProgramContext::EOF() {
  return getToken(CParser::EOF, 0);
}

std::vector<CParser::TopLevelDeclContext *> CParser::ProgramContext::topLevelDecl() {
  return getRuleContexts<CParser::TopLevelDeclContext>();
}

CParser::TopLevelDeclContext* CParser::ProgramContext::topLevelDecl(size_t i) {
  return getRuleContext<CParser::TopLevelDeclContext>(i);
}


size_t CParser::ProgramContext::getRuleIndex() const {
  return CParser::RuleProgram;
}


std::any CParser::ProgramContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitProgram(this);
  else
    return visitor->visitChildren(this);
}

CParser::ProgramContext* CParser::program() {
  ProgramContext *_localctx = _tracker.createInstance<ProgramContext>(_ctx, getState());
  enterRule(_localctx, 0, CParser::RuleProgram);
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
    setState(57);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 230687742) != 0)) {
      setState(54);
      topLevelDecl();
      setState(59);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(60);
    match(CParser::EOF);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TypeSpecContext ------------------------------------------------------------------

CParser::TypeSpecContext::TypeSpecContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CParser::BaseTypeSpecContext* CParser::TypeSpecContext::baseTypeSpec() {
  return getRuleContext<CParser::BaseTypeSpecContext>(0);
}

std::vector<tree::TerminalNode *> CParser::TypeSpecContext::STAR() {
  return getTokens(CParser::STAR);
}

tree::TerminalNode* CParser::TypeSpecContext::STAR(size_t i) {
  return getToken(CParser::STAR, i);
}


size_t CParser::TypeSpecContext::getRuleIndex() const {
  return CParser::RuleTypeSpec;
}


std::any CParser::TypeSpecContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitTypeSpec(this);
  else
    return visitor->visitChildren(this);
}

CParser::TypeSpecContext* CParser::typeSpec() {
  TypeSpecContext *_localctx = _tracker.createInstance<TypeSpecContext>(_ctx, getState());
  enterRule(_localctx, 2, CParser::RuleTypeSpec);
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
    setState(62);
    baseTypeSpec();
    setState(66);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CParser::STAR) {
      setState(63);
      match(CParser::STAR);
      setState(68);
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

//----------------- BaseTypeSpecContext ------------------------------------------------------------------

CParser::BaseTypeSpecContext::BaseTypeSpecContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t CParser::BaseTypeSpecContext::getRuleIndex() const {
  return CParser::RuleBaseTypeSpec;
}

void CParser::BaseTypeSpecContext::copyFrom(BaseTypeSpecContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- CharTypeContext ------------------------------------------------------------------

tree::TerminalNode* CParser::CharTypeContext::CHAR_KW() {
  return getToken(CParser::CHAR_KW, 0);
}

tree::TerminalNode* CParser::CharTypeContext::SIGNED_KW() {
  return getToken(CParser::SIGNED_KW, 0);
}

tree::TerminalNode* CParser::CharTypeContext::UNSIGNED_KW() {
  return getToken(CParser::UNSIGNED_KW, 0);
}

CParser::CharTypeContext::CharTypeContext(BaseTypeSpecContext *ctx) { copyFrom(ctx); }


std::any CParser::CharTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitCharType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- DoubleTypeContext ------------------------------------------------------------------

tree::TerminalNode* CParser::DoubleTypeContext::DOUBLE_KW() {
  return getToken(CParser::DOUBLE_KW, 0);
}

CParser::DoubleTypeContext::DoubleTypeContext(BaseTypeSpecContext *ctx) { copyFrom(ctx); }


std::any CParser::DoubleTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitDoubleType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- FloatTypeContext ------------------------------------------------------------------

tree::TerminalNode* CParser::FloatTypeContext::FLOAT_KW() {
  return getToken(CParser::FLOAT_KW, 0);
}

CParser::FloatTypeContext::FloatTypeContext(BaseTypeSpecContext *ctx) { copyFrom(ctx); }


std::any CParser::FloatTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitFloatType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- SignedOrUnsignedTypeContext ------------------------------------------------------------------

tree::TerminalNode* CParser::SignedOrUnsignedTypeContext::SIGNED_KW() {
  return getToken(CParser::SIGNED_KW, 0);
}

tree::TerminalNode* CParser::SignedOrUnsignedTypeContext::UNSIGNED_KW() {
  return getToken(CParser::UNSIGNED_KW, 0);
}

CParser::SignedOrUnsignedTypeContext::SignedOrUnsignedTypeContext(BaseTypeSpecContext *ctx) { copyFrom(ctx); }


std::any CParser::SignedOrUnsignedTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitSignedOrUnsignedType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- StructTypeContext ------------------------------------------------------------------

tree::TerminalNode* CParser::StructTypeContext::STRUCT_KW() {
  return getToken(CParser::STRUCT_KW, 0);
}

tree::TerminalNode* CParser::StructTypeContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

CParser::StructTypeContext::StructTypeContext(BaseTypeSpecContext *ctx) { copyFrom(ctx); }


std::any CParser::StructTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitStructType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- IntTypeContext ------------------------------------------------------------------

tree::TerminalNode* CParser::IntTypeContext::INT_KW() {
  return getToken(CParser::INT_KW, 0);
}

tree::TerminalNode* CParser::IntTypeContext::SIGNED_KW() {
  return getToken(CParser::SIGNED_KW, 0);
}

tree::TerminalNode* CParser::IntTypeContext::UNSIGNED_KW() {
  return getToken(CParser::UNSIGNED_KW, 0);
}

CParser::IntTypeContext::IntTypeContext(BaseTypeSpecContext *ctx) { copyFrom(ctx); }


std::any CParser::IntTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitIntType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ShortTypeContext ------------------------------------------------------------------

tree::TerminalNode* CParser::ShortTypeContext::SHORT_KW() {
  return getToken(CParser::SHORT_KW, 0);
}

tree::TerminalNode* CParser::ShortTypeContext::INT_KW() {
  return getToken(CParser::INT_KW, 0);
}

tree::TerminalNode* CParser::ShortTypeContext::SIGNED_KW() {
  return getToken(CParser::SIGNED_KW, 0);
}

tree::TerminalNode* CParser::ShortTypeContext::UNSIGNED_KW() {
  return getToken(CParser::UNSIGNED_KW, 0);
}

CParser::ShortTypeContext::ShortTypeContext(BaseTypeSpecContext *ctx) { copyFrom(ctx); }


std::any CParser::ShortTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitShortType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- EnumTypeContext ------------------------------------------------------------------

tree::TerminalNode* CParser::EnumTypeContext::ENUM_KW() {
  return getToken(CParser::ENUM_KW, 0);
}

tree::TerminalNode* CParser::EnumTypeContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

CParser::EnumTypeContext::EnumTypeContext(BaseTypeSpecContext *ctx) { copyFrom(ctx); }


std::any CParser::EnumTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitEnumType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- LongLongTypeContext ------------------------------------------------------------------

std::vector<tree::TerminalNode *> CParser::LongLongTypeContext::LONG_KW() {
  return getTokens(CParser::LONG_KW);
}

tree::TerminalNode* CParser::LongLongTypeContext::LONG_KW(size_t i) {
  return getToken(CParser::LONG_KW, i);
}

tree::TerminalNode* CParser::LongLongTypeContext::INT_KW() {
  return getToken(CParser::INT_KW, 0);
}

tree::TerminalNode* CParser::LongLongTypeContext::SIGNED_KW() {
  return getToken(CParser::SIGNED_KW, 0);
}

tree::TerminalNode* CParser::LongLongTypeContext::UNSIGNED_KW() {
  return getToken(CParser::UNSIGNED_KW, 0);
}

CParser::LongLongTypeContext::LongLongTypeContext(BaseTypeSpecContext *ctx) { copyFrom(ctx); }


std::any CParser::LongLongTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitLongLongType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- LongTypeContext ------------------------------------------------------------------

tree::TerminalNode* CParser::LongTypeContext::LONG_KW() {
  return getToken(CParser::LONG_KW, 0);
}

tree::TerminalNode* CParser::LongTypeContext::INT_KW() {
  return getToken(CParser::INT_KW, 0);
}

tree::TerminalNode* CParser::LongTypeContext::SIGNED_KW() {
  return getToken(CParser::SIGNED_KW, 0);
}

tree::TerminalNode* CParser::LongTypeContext::UNSIGNED_KW() {
  return getToken(CParser::UNSIGNED_KW, 0);
}

CParser::LongTypeContext::LongTypeContext(BaseTypeSpecContext *ctx) { copyFrom(ctx); }


std::any CParser::LongTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitLongType(this);
  else
    return visitor->visitChildren(this);
}
CParser::BaseTypeSpecContext* CParser::baseTypeSpec() {
  BaseTypeSpecContext *_localctx = _tracker.createInstance<BaseTypeSpecContext>(_ctx, getState());
  enterRule(_localctx, 4, CParser::RuleBaseTypeSpec);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(106);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 10, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<CParser::CharTypeContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(70);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::SIGNED_KW

      || _la == CParser::UNSIGNED_KW) {
        setState(69);
        _la = _input->LA(1);
        if (!(_la == CParser::SIGNED_KW

        || _la == CParser::UNSIGNED_KW)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
      }
      setState(72);
      match(CParser::CHAR_KW);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<CParser::ShortTypeContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(74);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::SIGNED_KW

      || _la == CParser::UNSIGNED_KW) {
        setState(73);
        _la = _input->LA(1);
        if (!(_la == CParser::SIGNED_KW

        || _la == CParser::UNSIGNED_KW)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
      }
      setState(76);
      match(CParser::SHORT_KW);
      setState(78);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::INT_KW) {
        setState(77);
        match(CParser::INT_KW);
      }
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<CParser::IntTypeContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(81);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::SIGNED_KW

      || _la == CParser::UNSIGNED_KW) {
        setState(80);
        _la = _input->LA(1);
        if (!(_la == CParser::SIGNED_KW

        || _la == CParser::UNSIGNED_KW)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
      }
      setState(83);
      match(CParser::INT_KW);
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<CParser::LongLongTypeContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(85);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::SIGNED_KW

      || _la == CParser::UNSIGNED_KW) {
        setState(84);
        _la = _input->LA(1);
        if (!(_la == CParser::SIGNED_KW

        || _la == CParser::UNSIGNED_KW)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
      }
      setState(87);
      match(CParser::LONG_KW);
      setState(88);
      match(CParser::LONG_KW);
      setState(90);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::INT_KW) {
        setState(89);
        match(CParser::INT_KW);
      }
      break;
    }

    case 5: {
      _localctx = _tracker.createInstance<CParser::LongTypeContext>(_localctx);
      enterOuterAlt(_localctx, 5);
      setState(93);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::SIGNED_KW

      || _la == CParser::UNSIGNED_KW) {
        setState(92);
        _la = _input->LA(1);
        if (!(_la == CParser::SIGNED_KW

        || _la == CParser::UNSIGNED_KW)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
      }
      setState(95);
      match(CParser::LONG_KW);
      setState(97);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::INT_KW) {
        setState(96);
        match(CParser::INT_KW);
      }
      break;
    }

    case 6: {
      _localctx = _tracker.createInstance<CParser::SignedOrUnsignedTypeContext>(_localctx);
      enterOuterAlt(_localctx, 6);
      setState(99);
      _la = _input->LA(1);
      if (!(_la == CParser::SIGNED_KW

      || _la == CParser::UNSIGNED_KW)) {
      _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      break;
    }

    case 7: {
      _localctx = _tracker.createInstance<CParser::FloatTypeContext>(_localctx);
      enterOuterAlt(_localctx, 7);
      setState(100);
      match(CParser::FLOAT_KW);
      break;
    }

    case 8: {
      _localctx = _tracker.createInstance<CParser::DoubleTypeContext>(_localctx);
      enterOuterAlt(_localctx, 8);
      setState(101);
      match(CParser::DOUBLE_KW);
      break;
    }

    case 9: {
      _localctx = _tracker.createInstance<CParser::StructTypeContext>(_localctx);
      enterOuterAlt(_localctx, 9);
      setState(102);
      match(CParser::STRUCT_KW);
      setState(103);
      match(CParser::IDENT);
      break;
    }

    case 10: {
      _localctx = _tracker.createInstance<CParser::EnumTypeContext>(_localctx);
      enterOuterAlt(_localctx, 10);
      setState(104);
      match(CParser::ENUM_KW);
      setState(105);
      match(CParser::IDENT);
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

//----------------- TopLevelDeclContext ------------------------------------------------------------------

CParser::TopLevelDeclContext::TopLevelDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CParser::FunctionDeclContext* CParser::TopLevelDeclContext::functionDecl() {
  return getRuleContext<CParser::FunctionDeclContext>(0);
}

CParser::GlobalDeclContext* CParser::TopLevelDeclContext::globalDecl() {
  return getRuleContext<CParser::GlobalDeclContext>(0);
}

CParser::StructDeclContext* CParser::TopLevelDeclContext::structDecl() {
  return getRuleContext<CParser::StructDeclContext>(0);
}

CParser::EnumDeclContext* CParser::TopLevelDeclContext::enumDecl() {
  return getRuleContext<CParser::EnumDeclContext>(0);
}


size_t CParser::TopLevelDeclContext::getRuleIndex() const {
  return CParser::RuleTopLevelDecl;
}


std::any CParser::TopLevelDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitTopLevelDecl(this);
  else
    return visitor->visitChildren(this);
}

CParser::TopLevelDeclContext* CParser::topLevelDecl() {
  TopLevelDeclContext *_localctx = _tracker.createInstance<TopLevelDeclContext>(_ctx, getState());
  enterRule(_localctx, 6, CParser::RuleTopLevelDecl);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(112);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 11, _ctx)) {
    case 1: {
      enterOuterAlt(_localctx, 1);
      setState(108);
      functionDecl();
      break;
    }

    case 2: {
      enterOuterAlt(_localctx, 2);
      setState(109);
      globalDecl();
      break;
    }

    case 3: {
      enterOuterAlt(_localctx, 3);
      setState(110);
      structDecl();
      break;
    }

    case 4: {
      enterOuterAlt(_localctx, 4);
      setState(111);
      enumDecl();
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

//----------------- StructDeclContext ------------------------------------------------------------------

CParser::StructDeclContext::StructDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CParser::StructDeclContext::STRUCT_KW() {
  return getToken(CParser::STRUCT_KW, 0);
}

std::vector<tree::TerminalNode *> CParser::StructDeclContext::IDENT() {
  return getTokens(CParser::IDENT);
}

tree::TerminalNode* CParser::StructDeclContext::IDENT(size_t i) {
  return getToken(CParser::IDENT, i);
}

tree::TerminalNode* CParser::StructDeclContext::LBRACE() {
  return getToken(CParser::LBRACE, 0);
}

tree::TerminalNode* CParser::StructDeclContext::RBRACE() {
  return getToken(CParser::RBRACE, 0);
}

tree::TerminalNode* CParser::StructDeclContext::SEMI() {
  return getToken(CParser::SEMI, 0);
}

tree::TerminalNode* CParser::StructDeclContext::AT() {
  return getToken(CParser::AT, 0);
}

std::vector<CParser::StructMemberContext *> CParser::StructDeclContext::structMember() {
  return getRuleContexts<CParser::StructMemberContext>();
}

CParser::StructMemberContext* CParser::StructDeclContext::structMember(size_t i) {
  return getRuleContext<CParser::StructMemberContext>(i);
}


size_t CParser::StructDeclContext::getRuleIndex() const {
  return CParser::RuleStructDecl;
}


std::any CParser::StructDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitStructDecl(this);
  else
    return visitor->visitChildren(this);
}

CParser::StructDeclContext* CParser::structDecl() {
  StructDeclContext *_localctx = _tracker.createInstance<StructDeclContext>(_ctx, getState());
  enterRule(_localctx, 8, CParser::RuleStructDecl);
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
    setState(114);
    match(CParser::STRUCT_KW);
    setState(115);
    match(CParser::IDENT);
    setState(118);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CParser::AT) {
      setState(116);
      match(CParser::AT);
      setState(117);
      antlrcpp::downCast<StructDeclContext *>(_localctx)->packedTag = match(CParser::IDENT);
    }
    setState(120);
    match(CParser::LBRACE);
    setState(122); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(121);
      structMember();
      setState(124); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 201327102) != 0));
    setState(126);
    match(CParser::RBRACE);
    setState(127);
    match(CParser::SEMI);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StructMemberContext ------------------------------------------------------------------

CParser::StructMemberContext::StructMemberContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CParser::TypeSpecContext* CParser::StructMemberContext::typeSpec() {
  return getRuleContext<CParser::TypeSpecContext>(0);
}

tree::TerminalNode* CParser::StructMemberContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::StructMemberContext::SEMI() {
  return getToken(CParser::SEMI, 0);
}

tree::TerminalNode* CParser::StructMemberContext::LBRACK() {
  return getToken(CParser::LBRACK, 0);
}

tree::TerminalNode* CParser::StructMemberContext::RBRACK() {
  return getToken(CParser::RBRACK, 0);
}

tree::TerminalNode* CParser::StructMemberContext::NUMBER() {
  return getToken(CParser::NUMBER, 0);
}


size_t CParser::StructMemberContext::getRuleIndex() const {
  return CParser::RuleStructMember;
}


std::any CParser::StructMemberContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitStructMember(this);
  else
    return visitor->visitChildren(this);
}

CParser::StructMemberContext* CParser::structMember() {
  StructMemberContext *_localctx = _tracker.createInstance<StructMemberContext>(_ctx, getState());
  enterRule(_localctx, 10, CParser::RuleStructMember);
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
    setState(129);
    typeSpec();
    setState(130);
    match(CParser::IDENT);
    setState(134);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CParser::LBRACK) {
      setState(131);
      match(CParser::LBRACK);
      setState(132);
      antlrcpp::downCast<StructMemberContext *>(_localctx)->size = match(CParser::NUMBER);
      setState(133);
      match(CParser::RBRACK);
    }
    setState(136);
    match(CParser::SEMI);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- EnumDeclContext ------------------------------------------------------------------

CParser::EnumDeclContext::EnumDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CParser::EnumDeclContext::ENUM_KW() {
  return getToken(CParser::ENUM_KW, 0);
}

tree::TerminalNode* CParser::EnumDeclContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::EnumDeclContext::LBRACE() {
  return getToken(CParser::LBRACE, 0);
}

std::vector<CParser::EnumMemberContext *> CParser::EnumDeclContext::enumMember() {
  return getRuleContexts<CParser::EnumMemberContext>();
}

CParser::EnumMemberContext* CParser::EnumDeclContext::enumMember(size_t i) {
  return getRuleContext<CParser::EnumMemberContext>(i);
}

tree::TerminalNode* CParser::EnumDeclContext::RBRACE() {
  return getToken(CParser::RBRACE, 0);
}

tree::TerminalNode* CParser::EnumDeclContext::SEMI() {
  return getToken(CParser::SEMI, 0);
}

std::vector<tree::TerminalNode *> CParser::EnumDeclContext::COMMA() {
  return getTokens(CParser::COMMA);
}

tree::TerminalNode* CParser::EnumDeclContext::COMMA(size_t i) {
  return getToken(CParser::COMMA, i);
}


size_t CParser::EnumDeclContext::getRuleIndex() const {
  return CParser::RuleEnumDecl;
}


std::any CParser::EnumDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitEnumDecl(this);
  else
    return visitor->visitChildren(this);
}

CParser::EnumDeclContext* CParser::enumDecl() {
  EnumDeclContext *_localctx = _tracker.createInstance<EnumDeclContext>(_ctx, getState());
  enterRule(_localctx, 12, CParser::RuleEnumDecl);
  size_t _la = 0;

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
    setState(138);
    match(CParser::ENUM_KW);
    setState(139);
    match(CParser::IDENT);
    setState(140);
    match(CParser::LBRACE);
    setState(141);
    enumMember();
    setState(146);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 15, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(142);
        match(CParser::COMMA);
        setState(143);
        enumMember(); 
      }
      setState(148);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 15, _ctx);
    }
    setState(150);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CParser::COMMA) {
      setState(149);
      match(CParser::COMMA);
    }
    setState(152);
    match(CParser::RBRACE);
    setState(153);
    match(CParser::SEMI);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- EnumMemberContext ------------------------------------------------------------------

CParser::EnumMemberContext::EnumMemberContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CParser::EnumMemberContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::EnumMemberContext::ASSIGN() {
  return getToken(CParser::ASSIGN, 0);
}

tree::TerminalNode* CParser::EnumMemberContext::NUMBER() {
  return getToken(CParser::NUMBER, 0);
}

tree::TerminalNode* CParser::EnumMemberContext::MINUS() {
  return getToken(CParser::MINUS, 0);
}


size_t CParser::EnumMemberContext::getRuleIndex() const {
  return CParser::RuleEnumMember;
}


std::any CParser::EnumMemberContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitEnumMember(this);
  else
    return visitor->visitChildren(this);
}

CParser::EnumMemberContext* CParser::enumMember() {
  EnumMemberContext *_localctx = _tracker.createInstance<EnumMemberContext>(_ctx, getState());
  enterRule(_localctx, 14, CParser::RuleEnumMember);
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
    setState(155);
    match(CParser::IDENT);
    setState(161);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CParser::ASSIGN) {
      setState(156);
      match(CParser::ASSIGN);
      setState(158);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::MINUS) {
        setState(157);
        antlrcpp::downCast<EnumMemberContext *>(_localctx)->sign = match(CParser::MINUS);
      }
      setState(160);
      antlrcpp::downCast<EnumMemberContext *>(_localctx)->val = match(CParser::NUMBER);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FunctionDeclContext ------------------------------------------------------------------

CParser::FunctionDeclContext::FunctionDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t CParser::FunctionDeclContext::getRuleIndex() const {
  return CParser::RuleFunctionDecl;
}

void CParser::FunctionDeclContext::copyFrom(FunctionDeclContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ExternFunctionDeclContext ------------------------------------------------------------------

tree::TerminalNode* CParser::ExternFunctionDeclContext::EXTERN_KW() {
  return getToken(CParser::EXTERN_KW, 0);
}

CParser::ReturnTypeSpecContext* CParser::ExternFunctionDeclContext::returnTypeSpec() {
  return getRuleContext<CParser::ReturnTypeSpecContext>(0);
}

std::vector<tree::TerminalNode *> CParser::ExternFunctionDeclContext::IDENT() {
  return getTokens(CParser::IDENT);
}

tree::TerminalNode* CParser::ExternFunctionDeclContext::IDENT(size_t i) {
  return getToken(CParser::IDENT, i);
}

tree::TerminalNode* CParser::ExternFunctionDeclContext::LPAREN() {
  return getToken(CParser::LPAREN, 0);
}

tree::TerminalNode* CParser::ExternFunctionDeclContext::RPAREN() {
  return getToken(CParser::RPAREN, 0);
}

tree::TerminalNode* CParser::ExternFunctionDeclContext::SEMI() {
  return getToken(CParser::SEMI, 0);
}

CParser::ParamListContext* CParser::ExternFunctionDeclContext::paramList() {
  return getRuleContext<CParser::ParamListContext>(0);
}

tree::TerminalNode* CParser::ExternFunctionDeclContext::AT() {
  return getToken(CParser::AT, 0);
}

CParser::ExternFunctionDeclContext::ExternFunctionDeclContext(FunctionDeclContext *ctx) { copyFrom(ctx); }


std::any CParser::ExternFunctionDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitExternFunctionDecl(this);
  else
    return visitor->visitChildren(this);
}
//----------------- LocalFunctionDeclContext ------------------------------------------------------------------

CParser::ReturnTypeSpecContext* CParser::LocalFunctionDeclContext::returnTypeSpec() {
  return getRuleContext<CParser::ReturnTypeSpecContext>(0);
}

std::vector<tree::TerminalNode *> CParser::LocalFunctionDeclContext::IDENT() {
  return getTokens(CParser::IDENT);
}

tree::TerminalNode* CParser::LocalFunctionDeclContext::IDENT(size_t i) {
  return getToken(CParser::IDENT, i);
}

tree::TerminalNode* CParser::LocalFunctionDeclContext::LPAREN() {
  return getToken(CParser::LPAREN, 0);
}

tree::TerminalNode* CParser::LocalFunctionDeclContext::RPAREN() {
  return getToken(CParser::RPAREN, 0);
}

CParser::BlockContext* CParser::LocalFunctionDeclContext::block() {
  return getRuleContext<CParser::BlockContext>(0);
}

tree::TerminalNode* CParser::LocalFunctionDeclContext::SEMI() {
  return getToken(CParser::SEMI, 0);
}

CParser::ParamListContext* CParser::LocalFunctionDeclContext::paramList() {
  return getRuleContext<CParser::ParamListContext>(0);
}

std::vector<tree::TerminalNode *> CParser::LocalFunctionDeclContext::AT() {
  return getTokens(CParser::AT);
}

tree::TerminalNode* CParser::LocalFunctionDeclContext::AT(size_t i) {
  return getToken(CParser::AT, i);
}

tree::TerminalNode* CParser::LocalFunctionDeclContext::STATIC_KW() {
  return getToken(CParser::STATIC_KW, 0);
}

tree::TerminalNode* CParser::LocalFunctionDeclContext::NUMBER() {
  return getToken(CParser::NUMBER, 0);
}

CParser::LocalFunctionDeclContext::LocalFunctionDeclContext(FunctionDeclContext *ctx) { copyFrom(ctx); }


std::any CParser::LocalFunctionDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitLocalFunctionDecl(this);
  else
    return visitor->visitChildren(this);
}
CParser::FunctionDeclContext* CParser::functionDecl() {
  FunctionDeclContext *_localctx = _tracker.createInstance<FunctionDeclContext>(_ctx, getState());
  enterRule(_localctx, 16, CParser::RuleFunctionDecl);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(199);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CParser::EXTERN_KW: {
        _localctx = _tracker.createInstance<CParser::ExternFunctionDeclContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(163);
        match(CParser::EXTERN_KW);
        setState(164);
        returnTypeSpec();
        setState(165);
        match(CParser::IDENT);
        setState(166);
        match(CParser::LPAREN);
        setState(168);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 251659262) != 0)) {
          setState(167);
          paramList();
        }
        setState(170);
        match(CParser::RPAREN);
        setState(173);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CParser::AT) {
          setState(171);
          match(CParser::AT);
          setState(172);
          antlrcpp::downCast<ExternFunctionDeclContext *>(_localctx)->tag = match(CParser::IDENT);
        }
        setState(175);
        match(CParser::SEMI);
        break;
      }

      case CParser::INT_KW:
      case CParser::CHAR_KW:
      case CParser::SHORT_KW:
      case CParser::LONG_KW:
      case CParser::FLOAT_KW:
      case CParser::DOUBLE_KW:
      case CParser::SIGNED_KW:
      case CParser::UNSIGNED_KW:
      case CParser::VOID_KW:
      case CParser::STATIC_KW:
      case CParser::STRUCT_KW:
      case CParser::ENUM_KW: {
        _localctx = _tracker.createInstance<CParser::LocalFunctionDeclContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(178);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CParser::STATIC_KW) {
          setState(177);
          antlrcpp::downCast<LocalFunctionDeclContext *>(_localctx)->stat = match(CParser::STATIC_KW);
        }
        setState(180);
        returnTypeSpec();
        setState(181);
        match(CParser::IDENT);
        setState(182);
        match(CParser::LPAREN);
        setState(184);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 251659262) != 0)) {
          setState(183);
          paramList();
        }
        setState(186);
        match(CParser::RPAREN);
        setState(189);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 23, _ctx)) {
        case 1: {
          setState(187);
          match(CParser::AT);
          setState(188);
          antlrcpp::downCast<LocalFunctionDeclContext *>(_localctx)->tag = match(CParser::IDENT);
          break;
        }

        default:
          break;
        }
        setState(193);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CParser::AT) {
          setState(191);
          match(CParser::AT);
          setState(192);
          antlrcpp::downCast<LocalFunctionDeclContext *>(_localctx)->addr = match(CParser::NUMBER);
        }
        setState(197);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case CParser::LBRACE: {
            setState(195);
            block();
            break;
          }

          case CParser::SEMI: {
            setState(196);
            match(CParser::SEMI);
            break;
          }

        default:
          throw NoViableAltException(this);
        }
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

//----------------- ReturnTypeSpecContext ------------------------------------------------------------------

CParser::ReturnTypeSpecContext::ReturnTypeSpecContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CParser::TypeSpecContext* CParser::ReturnTypeSpecContext::typeSpec() {
  return getRuleContext<CParser::TypeSpecContext>(0);
}

tree::TerminalNode* CParser::ReturnTypeSpecContext::VOID_KW() {
  return getToken(CParser::VOID_KW, 0);
}


size_t CParser::ReturnTypeSpecContext::getRuleIndex() const {
  return CParser::RuleReturnTypeSpec;
}


std::any CParser::ReturnTypeSpecContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitReturnTypeSpec(this);
  else
    return visitor->visitChildren(this);
}

CParser::ReturnTypeSpecContext* CParser::returnTypeSpec() {
  ReturnTypeSpecContext *_localctx = _tracker.createInstance<ReturnTypeSpecContext>(_ctx, getState());
  enterRule(_localctx, 18, CParser::RuleReturnTypeSpec);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(203);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CParser::INT_KW:
      case CParser::CHAR_KW:
      case CParser::SHORT_KW:
      case CParser::LONG_KW:
      case CParser::FLOAT_KW:
      case CParser::DOUBLE_KW:
      case CParser::SIGNED_KW:
      case CParser::UNSIGNED_KW:
      case CParser::STRUCT_KW:
      case CParser::ENUM_KW: {
        enterOuterAlt(_localctx, 1);
        setState(201);
        typeSpec();
        break;
      }

      case CParser::VOID_KW: {
        enterOuterAlt(_localctx, 2);
        setState(202);
        match(CParser::VOID_KW);
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

//----------------- ParamListContext ------------------------------------------------------------------

CParser::ParamListContext::ParamListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CParser::ParamContext *> CParser::ParamListContext::param() {
  return getRuleContexts<CParser::ParamContext>();
}

CParser::ParamContext* CParser::ParamListContext::param(size_t i) {
  return getRuleContext<CParser::ParamContext>(i);
}

std::vector<tree::TerminalNode *> CParser::ParamListContext::COMMA() {
  return getTokens(CParser::COMMA);
}

tree::TerminalNode* CParser::ParamListContext::COMMA(size_t i) {
  return getToken(CParser::COMMA, i);
}

tree::TerminalNode* CParser::ParamListContext::VOID_KW() {
  return getToken(CParser::VOID_KW, 0);
}


size_t CParser::ParamListContext::getRuleIndex() const {
  return CParser::RuleParamList;
}


std::any CParser::ParamListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitParamList(this);
  else
    return visitor->visitChildren(this);
}

CParser::ParamListContext* CParser::paramList() {
  ParamListContext *_localctx = _tracker.createInstance<ParamListContext>(_ctx, getState());
  enterRule(_localctx, 20, CParser::RuleParamList);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(214);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CParser::INT_KW:
      case CParser::CHAR_KW:
      case CParser::SHORT_KW:
      case CParser::LONG_KW:
      case CParser::FLOAT_KW:
      case CParser::DOUBLE_KW:
      case CParser::SIGNED_KW:
      case CParser::UNSIGNED_KW:
      case CParser::CONST_KW:
      case CParser::REGISTER_KW:
      case CParser::STRUCT_KW:
      case CParser::ENUM_KW: {
        enterOuterAlt(_localctx, 1);
        setState(205);
        param();
        setState(210);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == CParser::COMMA) {
          setState(206);
          match(CParser::COMMA);
          setState(207);
          param();
          setState(212);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
        break;
      }

      case CParser::VOID_KW: {
        enterOuterAlt(_localctx, 2);
        setState(213);
        match(CParser::VOID_KW);
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

//----------------- ParamContext ------------------------------------------------------------------

CParser::ParamContext::ParamContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

CParser::TypeSpecContext* CParser::ParamContext::typeSpec() {
  return getRuleContext<CParser::TypeSpecContext>(0);
}

tree::TerminalNode* CParser::ParamContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::ParamContext::REGISTER_KW() {
  return getToken(CParser::REGISTER_KW, 0);
}

tree::TerminalNode* CParser::ParamContext::CONST_KW() {
  return getToken(CParser::CONST_KW, 0);
}


size_t CParser::ParamContext::getRuleIndex() const {
  return CParser::RuleParam;
}


std::any CParser::ParamContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitParam(this);
  else
    return visitor->visitChildren(this);
}

CParser::ParamContext* CParser::param() {
  ParamContext *_localctx = _tracker.createInstance<ParamContext>(_ctx, getState());
  enterRule(_localctx, 22, CParser::RuleParam);
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
    setState(217);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CParser::REGISTER_KW) {
      setState(216);
      antlrcpp::downCast<ParamContext *>(_localctx)->reg = match(CParser::REGISTER_KW);
    }
    setState(220);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == CParser::CONST_KW) {
      setState(219);
      antlrcpp::downCast<ParamContext *>(_localctx)->cst = match(CParser::CONST_KW);
    }
    setState(222);
    typeSpec();
    setState(223);
    match(CParser::IDENT);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- GlobalDeclContext ------------------------------------------------------------------

CParser::GlobalDeclContext::GlobalDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t CParser::GlobalDeclContext::getRuleIndex() const {
  return CParser::RuleGlobalDecl;
}

void CParser::GlobalDeclContext::copyFrom(GlobalDeclContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- GlobalDeclGroupContext ------------------------------------------------------------------

CParser::TypeSpecContext* CParser::GlobalDeclGroupContext::typeSpec() {
  return getRuleContext<CParser::TypeSpecContext>(0);
}

tree::TerminalNode* CParser::GlobalDeclGroupContext::SEMI() {
  return getToken(CParser::SEMI, 0);
}

std::vector<CParser::GlobalDeclaratorContext *> CParser::GlobalDeclGroupContext::globalDeclarator() {
  return getRuleContexts<CParser::GlobalDeclaratorContext>();
}

CParser::GlobalDeclaratorContext* CParser::GlobalDeclGroupContext::globalDeclarator(size_t i) {
  return getRuleContext<CParser::GlobalDeclaratorContext>(i);
}

std::vector<tree::TerminalNode *> CParser::GlobalDeclGroupContext::COMMA() {
  return getTokens(CParser::COMMA);
}

tree::TerminalNode* CParser::GlobalDeclGroupContext::COMMA(size_t i) {
  return getToken(CParser::COMMA, i);
}

tree::TerminalNode* CParser::GlobalDeclGroupContext::STATIC_KW() {
  return getToken(CParser::STATIC_KW, 0);
}

tree::TerminalNode* CParser::GlobalDeclGroupContext::CONST_KW() {
  return getToken(CParser::CONST_KW, 0);
}

CParser::GlobalDeclGroupContext::GlobalDeclGroupContext(GlobalDeclContext *ctx) { copyFrom(ctx); }


std::any CParser::GlobalDeclGroupContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitGlobalDeclGroup(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ExternDeclGroupContext ------------------------------------------------------------------

tree::TerminalNode* CParser::ExternDeclGroupContext::EXTERN_KW() {
  return getToken(CParser::EXTERN_KW, 0);
}

CParser::TypeSpecContext* CParser::ExternDeclGroupContext::typeSpec() {
  return getRuleContext<CParser::TypeSpecContext>(0);
}

tree::TerminalNode* CParser::ExternDeclGroupContext::SEMI() {
  return getToken(CParser::SEMI, 0);
}

std::vector<CParser::ExternDeclaratorContext *> CParser::ExternDeclGroupContext::externDeclarator() {
  return getRuleContexts<CParser::ExternDeclaratorContext>();
}

CParser::ExternDeclaratorContext* CParser::ExternDeclGroupContext::externDeclarator(size_t i) {
  return getRuleContext<CParser::ExternDeclaratorContext>(i);
}

std::vector<tree::TerminalNode *> CParser::ExternDeclGroupContext::COMMA() {
  return getTokens(CParser::COMMA);
}

tree::TerminalNode* CParser::ExternDeclGroupContext::COMMA(size_t i) {
  return getToken(CParser::COMMA, i);
}

tree::TerminalNode* CParser::ExternDeclGroupContext::CONST_KW() {
  return getToken(CParser::CONST_KW, 0);
}

CParser::ExternDeclGroupContext::ExternDeclGroupContext(GlobalDeclContext *ctx) { copyFrom(ctx); }


std::any CParser::ExternDeclGroupContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitExternDeclGroup(this);
  else
    return visitor->visitChildren(this);
}
CParser::GlobalDeclContext* CParser::globalDecl() {
  GlobalDeclContext *_localctx = _tracker.createInstance<GlobalDeclContext>(_ctx, getState());
  enterRule(_localctx, 24, CParser::RuleGlobalDecl);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(257);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CParser::EXTERN_KW: {
        _localctx = _tracker.createInstance<CParser::ExternDeclGroupContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(225);
        match(CParser::EXTERN_KW);
        setState(227);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CParser::CONST_KW) {
          setState(226);
          antlrcpp::downCast<ExternDeclGroupContext *>(_localctx)->cst = match(CParser::CONST_KW);
        }
        setState(229);
        typeSpec();
        setState(230);
        antlrcpp::downCast<ExternDeclGroupContext *>(_localctx)->externDeclaratorContext = externDeclarator();
        antlrcpp::downCast<ExternDeclGroupContext *>(_localctx)->decl.push_back(antlrcpp::downCast<ExternDeclGroupContext *>(_localctx)->externDeclaratorContext);
        setState(235);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == CParser::COMMA) {
          setState(231);
          match(CParser::COMMA);
          setState(232);
          antlrcpp::downCast<ExternDeclGroupContext *>(_localctx)->externDeclaratorContext = externDeclarator();
          antlrcpp::downCast<ExternDeclGroupContext *>(_localctx)->decl.push_back(antlrcpp::downCast<ExternDeclGroupContext *>(_localctx)->externDeclaratorContext);
          setState(237);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
        setState(238);
        match(CParser::SEMI);
        break;
      }

      case CParser::INT_KW:
      case CParser::CHAR_KW:
      case CParser::SHORT_KW:
      case CParser::LONG_KW:
      case CParser::FLOAT_KW:
      case CParser::DOUBLE_KW:
      case CParser::SIGNED_KW:
      case CParser::UNSIGNED_KW:
      case CParser::STATIC_KW:
      case CParser::CONST_KW:
      case CParser::STRUCT_KW:
      case CParser::ENUM_KW: {
        _localctx = _tracker.createInstance<CParser::GlobalDeclGroupContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(241);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CParser::STATIC_KW) {
          setState(240);
          antlrcpp::downCast<GlobalDeclGroupContext *>(_localctx)->stat = match(CParser::STATIC_KW);
        }
        setState(244);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CParser::CONST_KW) {
          setState(243);
          antlrcpp::downCast<GlobalDeclGroupContext *>(_localctx)->cst = match(CParser::CONST_KW);
        }
        setState(246);
        typeSpec();
        setState(247);
        antlrcpp::downCast<GlobalDeclGroupContext *>(_localctx)->globalDeclaratorContext = globalDeclarator();
        antlrcpp::downCast<GlobalDeclGroupContext *>(_localctx)->decl.push_back(antlrcpp::downCast<GlobalDeclGroupContext *>(_localctx)->globalDeclaratorContext);
        setState(252);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == CParser::COMMA) {
          setState(248);
          match(CParser::COMMA);
          setState(249);
          antlrcpp::downCast<GlobalDeclGroupContext *>(_localctx)->globalDeclaratorContext = globalDeclarator();
          antlrcpp::downCast<GlobalDeclGroupContext *>(_localctx)->decl.push_back(antlrcpp::downCast<GlobalDeclGroupContext *>(_localctx)->globalDeclaratorContext);
          setState(254);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
        setState(255);
        match(CParser::SEMI);
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

//----------------- ExternDeclaratorContext ------------------------------------------------------------------

CParser::ExternDeclaratorContext::ExternDeclaratorContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t CParser::ExternDeclaratorContext::getRuleIndex() const {
  return CParser::RuleExternDeclarator;
}

void CParser::ExternDeclaratorContext::copyFrom(ExternDeclaratorContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ExternScalarDeclaratorContext ------------------------------------------------------------------

tree::TerminalNode* CParser::ExternScalarDeclaratorContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

CParser::ExternScalarDeclaratorContext::ExternScalarDeclaratorContext(ExternDeclaratorContext *ctx) { copyFrom(ctx); }


std::any CParser::ExternScalarDeclaratorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitExternScalarDeclarator(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ExternArrayDeclaratorContext ------------------------------------------------------------------

tree::TerminalNode* CParser::ExternArrayDeclaratorContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

std::vector<tree::TerminalNode *> CParser::ExternArrayDeclaratorContext::LBRACK() {
  return getTokens(CParser::LBRACK);
}

tree::TerminalNode* CParser::ExternArrayDeclaratorContext::LBRACK(size_t i) {
  return getToken(CParser::LBRACK, i);
}

std::vector<tree::TerminalNode *> CParser::ExternArrayDeclaratorContext::RBRACK() {
  return getTokens(CParser::RBRACK);
}

tree::TerminalNode* CParser::ExternArrayDeclaratorContext::RBRACK(size_t i) {
  return getToken(CParser::RBRACK, i);
}

std::vector<tree::TerminalNode *> CParser::ExternArrayDeclaratorContext::NUMBER() {
  return getTokens(CParser::NUMBER);
}

tree::TerminalNode* CParser::ExternArrayDeclaratorContext::NUMBER(size_t i) {
  return getToken(CParser::NUMBER, i);
}

CParser::ExternArrayDeclaratorContext::ExternArrayDeclaratorContext(ExternDeclaratorContext *ctx) { copyFrom(ctx); }


std::any CParser::ExternArrayDeclaratorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitExternArrayDeclarator(this);
  else
    return visitor->visitChildren(this);
}
CParser::ExternDeclaratorContext* CParser::externDeclarator() {
  ExternDeclaratorContext *_localctx = _tracker.createInstance<ExternDeclaratorContext>(_ctx, getState());
  enterRule(_localctx, 26, CParser::RuleExternDeclarator);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(268);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 39, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<CParser::ExternArrayDeclaratorContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(259);
      match(CParser::IDENT);
      setState(263); 
      _errHandler->sync(this);
      _la = _input->LA(1);
      do {
        setState(260);
        match(CParser::LBRACK);
        setState(261);
        antlrcpp::downCast<ExternArrayDeclaratorContext *>(_localctx)->numberToken = match(CParser::NUMBER);
        antlrcpp::downCast<ExternArrayDeclaratorContext *>(_localctx)->dims.push_back(antlrcpp::downCast<ExternArrayDeclaratorContext *>(_localctx)->numberToken);
        setState(262);
        match(CParser::RBRACK);
        setState(265); 
        _errHandler->sync(this);
        _la = _input->LA(1);
      } while (_la == CParser::LBRACK);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<CParser::ExternScalarDeclaratorContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(267);
      match(CParser::IDENT);
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

//----------------- GlobalDeclaratorContext ------------------------------------------------------------------

CParser::GlobalDeclaratorContext::GlobalDeclaratorContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t CParser::GlobalDeclaratorContext::getRuleIndex() const {
  return CParser::RuleGlobalDeclarator;
}

void CParser::GlobalDeclaratorContext::copyFrom(GlobalDeclaratorContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- GlobalScalarDeclaratorContext ------------------------------------------------------------------

std::vector<tree::TerminalNode *> CParser::GlobalScalarDeclaratorContext::IDENT() {
  return getTokens(CParser::IDENT);
}

tree::TerminalNode* CParser::GlobalScalarDeclaratorContext::IDENT(size_t i) {
  return getToken(CParser::IDENT, i);
}

tree::TerminalNode* CParser::GlobalScalarDeclaratorContext::ASSIGN() {
  return getToken(CParser::ASSIGN, 0);
}

tree::TerminalNode* CParser::GlobalScalarDeclaratorContext::AT() {
  return getToken(CParser::AT, 0);
}

std::vector<tree::TerminalNode *> CParser::GlobalScalarDeclaratorContext::NUMBER() {
  return getTokens(CParser::NUMBER);
}

tree::TerminalNode* CParser::GlobalScalarDeclaratorContext::NUMBER(size_t i) {
  return getToken(CParser::NUMBER, i);
}

tree::TerminalNode* CParser::GlobalScalarDeclaratorContext::FLOAT_NUMBER() {
  return getToken(CParser::FLOAT_NUMBER, 0);
}

tree::TerminalNode* CParser::GlobalScalarDeclaratorContext::MINUS() {
  return getToken(CParser::MINUS, 0);
}

CParser::GlobalScalarDeclaratorContext::GlobalScalarDeclaratorContext(GlobalDeclaratorContext *ctx) { copyFrom(ctx); }


std::any CParser::GlobalScalarDeclaratorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitGlobalScalarDeclarator(this);
  else
    return visitor->visitChildren(this);
}
//----------------- GlobalArrayDeclaratorContext ------------------------------------------------------------------

tree::TerminalNode* CParser::GlobalArrayDeclaratorContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

std::vector<tree::TerminalNode *> CParser::GlobalArrayDeclaratorContext::LBRACK() {
  return getTokens(CParser::LBRACK);
}

tree::TerminalNode* CParser::GlobalArrayDeclaratorContext::LBRACK(size_t i) {
  return getToken(CParser::LBRACK, i);
}

std::vector<tree::TerminalNode *> CParser::GlobalArrayDeclaratorContext::RBRACK() {
  return getTokens(CParser::RBRACK);
}

tree::TerminalNode* CParser::GlobalArrayDeclaratorContext::RBRACK(size_t i) {
  return getToken(CParser::RBRACK, i);
}

tree::TerminalNode* CParser::GlobalArrayDeclaratorContext::ASSIGN() {
  return getToken(CParser::ASSIGN, 0);
}

CParser::ArrayInitContext* CParser::GlobalArrayDeclaratorContext::arrayInit() {
  return getRuleContext<CParser::ArrayInitContext>(0);
}

tree::TerminalNode* CParser::GlobalArrayDeclaratorContext::AT() {
  return getToken(CParser::AT, 0);
}

std::vector<tree::TerminalNode *> CParser::GlobalArrayDeclaratorContext::NUMBER() {
  return getTokens(CParser::NUMBER);
}

tree::TerminalNode* CParser::GlobalArrayDeclaratorContext::NUMBER(size_t i) {
  return getToken(CParser::NUMBER, i);
}

CParser::GlobalArrayDeclaratorContext::GlobalArrayDeclaratorContext(GlobalDeclaratorContext *ctx) { copyFrom(ctx); }


std::any CParser::GlobalArrayDeclaratorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitGlobalArrayDeclarator(this);
  else
    return visitor->visitChildren(this);
}
CParser::GlobalDeclaratorContext* CParser::globalDeclarator() {
  GlobalDeclaratorContext *_localctx = _tracker.createInstance<GlobalDeclaratorContext>(_ctx, getState());
  enterRule(_localctx, 28, CParser::RuleGlobalDeclarator);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(301);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 47, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<CParser::GlobalArrayDeclaratorContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(270);
      match(CParser::IDENT);
      setState(274); 
      _errHandler->sync(this);
      _la = _input->LA(1);
      do {
        setState(271);
        match(CParser::LBRACK);
        setState(272);
        antlrcpp::downCast<GlobalArrayDeclaratorContext *>(_localctx)->numberToken = match(CParser::NUMBER);
        antlrcpp::downCast<GlobalArrayDeclaratorContext *>(_localctx)->dims.push_back(antlrcpp::downCast<GlobalArrayDeclaratorContext *>(_localctx)->numberToken);
        setState(273);
        match(CParser::RBRACK);
        setState(276); 
        _errHandler->sync(this);
        _la = _input->LA(1);
      } while (_la == CParser::LBRACK);
      setState(280);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::ASSIGN) {
        setState(278);
        match(CParser::ASSIGN);
        setState(279);
        arrayInit();
      }
      setState(284);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::AT) {
        setState(282);
        match(CParser::AT);
        setState(283);
        antlrcpp::downCast<GlobalArrayDeclaratorContext *>(_localctx)->addr = match(CParser::NUMBER);
      }
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<CParser::GlobalScalarDeclaratorContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(286);
      match(CParser::IDENT);
      setState(295);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::ASSIGN) {
        setState(287);
        match(CParser::ASSIGN);
        setState(293);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case CParser::MINUS:
          case CParser::NUMBER:
          case CParser::FLOAT_NUMBER: {
            setState(289);
            _errHandler->sync(this);

            _la = _input->LA(1);
            if (_la == CParser::MINUS) {
              setState(288);
              antlrcpp::downCast<GlobalScalarDeclaratorContext *>(_localctx)->sign = match(CParser::MINUS);
            }
            setState(291);
            antlrcpp::downCast<GlobalScalarDeclaratorContext *>(_localctx)->val = _input->LT(1);
            _la = _input->LA(1);
            if (!(_la == CParser::NUMBER

            || _la == CParser::FLOAT_NUMBER)) {
              antlrcpp::downCast<GlobalScalarDeclaratorContext *>(_localctx)->val = _errHandler->recoverInline(this);
            }
            else {
              _errHandler->reportMatch(this);
              consume();
            }
            break;
          }

          case CParser::IDENT: {
            setState(292);
            antlrcpp::downCast<GlobalScalarDeclaratorContext *>(_localctx)->identVal = match(CParser::IDENT);
            break;
          }

        default:
          throw NoViableAltException(this);
        }
      }
      setState(299);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::AT) {
        setState(297);
        match(CParser::AT);
        setState(298);
        antlrcpp::downCast<GlobalScalarDeclaratorContext *>(_localctx)->addr = match(CParser::NUMBER);
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

//----------------- ArrayInitContext ------------------------------------------------------------------

CParser::ArrayInitContext::ArrayInitContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CParser::ArrayInitContext::LBRACE() {
  return getToken(CParser::LBRACE, 0);
}

tree::TerminalNode* CParser::ArrayInitContext::RBRACE() {
  return getToken(CParser::RBRACE, 0);
}

std::vector<CParser::ArrayInitElemContext *> CParser::ArrayInitContext::arrayInitElem() {
  return getRuleContexts<CParser::ArrayInitElemContext>();
}

CParser::ArrayInitElemContext* CParser::ArrayInitContext::arrayInitElem(size_t i) {
  return getRuleContext<CParser::ArrayInitElemContext>(i);
}

std::vector<tree::TerminalNode *> CParser::ArrayInitContext::COMMA() {
  return getTokens(CParser::COMMA);
}

tree::TerminalNode* CParser::ArrayInitContext::COMMA(size_t i) {
  return getToken(CParser::COMMA, i);
}


size_t CParser::ArrayInitContext::getRuleIndex() const {
  return CParser::RuleArrayInit;
}


std::any CParser::ArrayInitContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitArrayInit(this);
  else
    return visitor->visitChildren(this);
}

CParser::ArrayInitContext* CParser::arrayInit() {
  ArrayInitContext *_localctx = _tracker.createInstance<ArrayInitContext>(_ctx, getState());
  enterRule(_localctx, 30, CParser::RuleArrayInit);
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
    setState(303);
    match(CParser::LBRACE);
    setState(304);
    antlrcpp::downCast<ArrayInitContext *>(_localctx)->arrayInitElemContext = arrayInitElem();
    antlrcpp::downCast<ArrayInitContext *>(_localctx)->elem.push_back(antlrcpp::downCast<ArrayInitContext *>(_localctx)->arrayInitElemContext);
    setState(309);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CParser::COMMA) {
      setState(305);
      match(CParser::COMMA);
      setState(306);
      antlrcpp::downCast<ArrayInitContext *>(_localctx)->arrayInitElemContext = arrayInitElem();
      antlrcpp::downCast<ArrayInitContext *>(_localctx)->elem.push_back(antlrcpp::downCast<ArrayInitContext *>(_localctx)->arrayInitElemContext);
      setState(311);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(312);
    match(CParser::RBRACE);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ArrayInitElemContext ------------------------------------------------------------------

CParser::ArrayInitElemContext::ArrayInitElemContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t CParser::ArrayInitElemContext::getRuleIndex() const {
  return CParser::RuleArrayInitElem;
}

void CParser::ArrayInitElemContext::copyFrom(ArrayInitElemContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- SymArrayElemContext ------------------------------------------------------------------

tree::TerminalNode* CParser::SymArrayElemContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

CParser::SymArrayElemContext::SymArrayElemContext(ArrayInitElemContext *ctx) { copyFrom(ctx); }


std::any CParser::SymArrayElemContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitSymArrayElem(this);
  else
    return visitor->visitChildren(this);
}
//----------------- NumArrayElemContext ------------------------------------------------------------------

tree::TerminalNode* CParser::NumArrayElemContext::NUMBER() {
  return getToken(CParser::NUMBER, 0);
}

tree::TerminalNode* CParser::NumArrayElemContext::FLOAT_NUMBER() {
  return getToken(CParser::FLOAT_NUMBER, 0);
}

tree::TerminalNode* CParser::NumArrayElemContext::MINUS() {
  return getToken(CParser::MINUS, 0);
}

CParser::NumArrayElemContext::NumArrayElemContext(ArrayInitElemContext *ctx) { copyFrom(ctx); }


std::any CParser::NumArrayElemContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitNumArrayElem(this);
  else
    return visitor->visitChildren(this);
}
CParser::ArrayInitElemContext* CParser::arrayInitElem() {
  ArrayInitElemContext *_localctx = _tracker.createInstance<ArrayInitElemContext>(_ctx, getState());
  enterRule(_localctx, 32, CParser::RuleArrayInitElem);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(319);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CParser::MINUS:
      case CParser::NUMBER:
      case CParser::FLOAT_NUMBER: {
        _localctx = _tracker.createInstance<CParser::NumArrayElemContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(315);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CParser::MINUS) {
          setState(314);
          antlrcpp::downCast<NumArrayElemContext *>(_localctx)->sign = match(CParser::MINUS);
        }
        setState(317);
        _la = _input->LA(1);
        if (!(_la == CParser::NUMBER

        || _la == CParser::FLOAT_NUMBER)) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        break;
      }

      case CParser::IDENT: {
        _localctx = _tracker.createInstance<CParser::SymArrayElemContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(318);
        match(CParser::IDENT);
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

//----------------- BlockContext ------------------------------------------------------------------

CParser::BlockContext::BlockContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CParser::BlockContext::LBRACE() {
  return getToken(CParser::LBRACE, 0);
}

tree::TerminalNode* CParser::BlockContext::RBRACE() {
  return getToken(CParser::RBRACE, 0);
}

std::vector<CParser::StatementContext *> CParser::BlockContext::statement() {
  return getRuleContexts<CParser::StatementContext>();
}

CParser::StatementContext* CParser::BlockContext::statement(size_t i) {
  return getRuleContext<CParser::StatementContext>(i);
}


size_t CParser::BlockContext::getRuleIndex() const {
  return CParser::RuleBlock;
}


std::any CParser::BlockContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitBlock(this);
  else
    return visitor->visitChildren(this);
}

CParser::BlockContext* CParser::block() {
  BlockContext *_localctx = _tracker.createInstance<BlockContext>(_ctx, getState());
  enterRule(_localctx, 34, CParser::RuleBlock);
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
    setState(321);
    match(CParser::LBRACE);
    setState(325);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 8560402296318) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 68)) & 18371) != 0)) {
      setState(322);
      statement();
      setState(327);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(328);
    match(CParser::RBRACE);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StatementContext ------------------------------------------------------------------

CParser::StatementContext::StatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t CParser::StatementContext::getRuleIndex() const {
  return CParser::RuleStatement;
}

void CParser::StatementContext::copyFrom(StatementContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ForStmtContext ------------------------------------------------------------------

tree::TerminalNode* CParser::ForStmtContext::FOR_KW() {
  return getToken(CParser::FOR_KW, 0);
}

tree::TerminalNode* CParser::ForStmtContext::LPAREN() {
  return getToken(CParser::LPAREN, 0);
}

std::vector<tree::TerminalNode *> CParser::ForStmtContext::SEMI() {
  return getTokens(CParser::SEMI);
}

tree::TerminalNode* CParser::ForStmtContext::SEMI(size_t i) {
  return getToken(CParser::SEMI, i);
}

tree::TerminalNode* CParser::ForStmtContext::RPAREN() {
  return getToken(CParser::RPAREN, 0);
}

CParser::StatementContext* CParser::ForStmtContext::statement() {
  return getRuleContext<CParser::StatementContext>(0);
}

std::vector<CParser::SimpleStmtContext *> CParser::ForStmtContext::simpleStmt() {
  return getRuleContexts<CParser::SimpleStmtContext>();
}

CParser::SimpleStmtContext* CParser::ForStmtContext::simpleStmt(size_t i) {
  return getRuleContext<CParser::SimpleStmtContext>(i);
}

CParser::ExprContext* CParser::ForStmtContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

CParser::ForStmtContext::ForStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any CParser::ForStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitForStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AsmStmtContext ------------------------------------------------------------------

tree::TerminalNode* CParser::AsmStmtContext::ASM_BEGIN() {
  return getToken(CParser::ASM_BEGIN, 0);
}

tree::TerminalNode* CParser::AsmStmtContext::ASM_END() {
  return getToken(CParser::ASM_END, 0);
}

tree::TerminalNode* CParser::AsmStmtContext::ASM_TEXT() {
  return getToken(CParser::ASM_TEXT, 0);
}

CParser::AsmStmtContext::AsmStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any CParser::AsmStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitAsmStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- WhileStmtContext ------------------------------------------------------------------

tree::TerminalNode* CParser::WhileStmtContext::WHILE_KW() {
  return getToken(CParser::WHILE_KW, 0);
}

tree::TerminalNode* CParser::WhileStmtContext::LPAREN() {
  return getToken(CParser::LPAREN, 0);
}

CParser::ExprContext* CParser::WhileStmtContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

tree::TerminalNode* CParser::WhileStmtContext::RPAREN() {
  return getToken(CParser::RPAREN, 0);
}

CParser::StatementContext* CParser::WhileStmtContext::statement() {
  return getRuleContext<CParser::StatementContext>(0);
}

CParser::WhileStmtContext::WhileStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any CParser::WhileStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitWhileStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- SimpleStmtWrapperContext ------------------------------------------------------------------

CParser::SimpleStmtContext* CParser::SimpleStmtWrapperContext::simpleStmt() {
  return getRuleContext<CParser::SimpleStmtContext>(0);
}

tree::TerminalNode* CParser::SimpleStmtWrapperContext::SEMI() {
  return getToken(CParser::SEMI, 0);
}

CParser::SimpleStmtWrapperContext::SimpleStmtWrapperContext(StatementContext *ctx) { copyFrom(ctx); }


std::any CParser::SimpleStmtWrapperContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitSimpleStmtWrapper(this);
  else
    return visitor->visitChildren(this);
}
//----------------- GotoStmtContext ------------------------------------------------------------------

tree::TerminalNode* CParser::GotoStmtContext::GOTO_KW() {
  return getToken(CParser::GOTO_KW, 0);
}

tree::TerminalNode* CParser::GotoStmtContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::GotoStmtContext::SEMI() {
  return getToken(CParser::SEMI, 0);
}

CParser::GotoStmtContext::GotoStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any CParser::GotoStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitGotoStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- EmptyStmtContext ------------------------------------------------------------------

tree::TerminalNode* CParser::EmptyStmtContext::SEMI() {
  return getToken(CParser::SEMI, 0);
}

CParser::EmptyStmtContext::EmptyStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any CParser::EmptyStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitEmptyStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ReturnStmtContext ------------------------------------------------------------------

tree::TerminalNode* CParser::ReturnStmtContext::RETURN_KW() {
  return getToken(CParser::RETURN_KW, 0);
}

tree::TerminalNode* CParser::ReturnStmtContext::SEMI() {
  return getToken(CParser::SEMI, 0);
}

CParser::ExprContext* CParser::ReturnStmtContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

CParser::ReturnStmtContext::ReturnStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any CParser::ReturnStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitReturnStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- SwitchStmtContext ------------------------------------------------------------------

tree::TerminalNode* CParser::SwitchStmtContext::SWITCH_KW() {
  return getToken(CParser::SWITCH_KW, 0);
}

tree::TerminalNode* CParser::SwitchStmtContext::LPAREN() {
  return getToken(CParser::LPAREN, 0);
}

CParser::ExprContext* CParser::SwitchStmtContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

tree::TerminalNode* CParser::SwitchStmtContext::RPAREN() {
  return getToken(CParser::RPAREN, 0);
}

tree::TerminalNode* CParser::SwitchStmtContext::LBRACE() {
  return getToken(CParser::LBRACE, 0);
}

tree::TerminalNode* CParser::SwitchStmtContext::RBRACE() {
  return getToken(CParser::RBRACE, 0);
}

std::vector<CParser::SwitchCaseContext *> CParser::SwitchStmtContext::switchCase() {
  return getRuleContexts<CParser::SwitchCaseContext>();
}

CParser::SwitchCaseContext* CParser::SwitchStmtContext::switchCase(size_t i) {
  return getRuleContext<CParser::SwitchCaseContext>(i);
}

CParser::SwitchStmtContext::SwitchStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any CParser::SwitchStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitSwitchStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- IfStmtContext ------------------------------------------------------------------

tree::TerminalNode* CParser::IfStmtContext::IF_KW() {
  return getToken(CParser::IF_KW, 0);
}

tree::TerminalNode* CParser::IfStmtContext::LPAREN() {
  return getToken(CParser::LPAREN, 0);
}

CParser::ExprContext* CParser::IfStmtContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

tree::TerminalNode* CParser::IfStmtContext::RPAREN() {
  return getToken(CParser::RPAREN, 0);
}

std::vector<CParser::StatementContext *> CParser::IfStmtContext::statement() {
  return getRuleContexts<CParser::StatementContext>();
}

CParser::StatementContext* CParser::IfStmtContext::statement(size_t i) {
  return getRuleContext<CParser::StatementContext>(i);
}

tree::TerminalNode* CParser::IfStmtContext::ELSE_KW() {
  return getToken(CParser::ELSE_KW, 0);
}

CParser::IfStmtContext::IfStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any CParser::IfStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitIfStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- BlockStmtContext ------------------------------------------------------------------

CParser::BlockContext* CParser::BlockStmtContext::block() {
  return getRuleContext<CParser::BlockContext>(0);
}

CParser::BlockStmtContext::BlockStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any CParser::BlockStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitBlockStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- BreakStmtContext ------------------------------------------------------------------

tree::TerminalNode* CParser::BreakStmtContext::BREAK_KW() {
  return getToken(CParser::BREAK_KW, 0);
}

tree::TerminalNode* CParser::BreakStmtContext::SEMI() {
  return getToken(CParser::SEMI, 0);
}

CParser::BreakStmtContext::BreakStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any CParser::BreakStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitBreakStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- LabelStmtContext ------------------------------------------------------------------

tree::TerminalNode* CParser::LabelStmtContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::LabelStmtContext::COLON() {
  return getToken(CParser::COLON, 0);
}

CParser::StatementContext* CParser::LabelStmtContext::statement() {
  return getRuleContext<CParser::StatementContext>(0);
}

CParser::LabelStmtContext::LabelStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any CParser::LabelStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitLabelStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ContinueStmtContext ------------------------------------------------------------------

tree::TerminalNode* CParser::ContinueStmtContext::CONTINUE_KW() {
  return getToken(CParser::CONTINUE_KW, 0);
}

tree::TerminalNode* CParser::ContinueStmtContext::SEMI() {
  return getToken(CParser::SEMI, 0);
}

CParser::ContinueStmtContext::ContinueStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any CParser::ContinueStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitContinueStmt(this);
  else
    return visitor->visitChildren(this);
}
CParser::StatementContext* CParser::statement() {
  StatementContext *_localctx = _tracker.createInstance<StatementContext>(_ctx, getState());
  enterRule(_localctx, 36, CParser::RuleStatement);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(398);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 59, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<CParser::BlockStmtContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(330);
      block();
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<CParser::IfStmtContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(331);
      match(CParser::IF_KW);
      setState(332);
      match(CParser::LPAREN);
      setState(333);
      expr();
      setState(334);
      match(CParser::RPAREN);
      setState(335);
      antlrcpp::downCast<IfStmtContext *>(_localctx)->thenS = statement();
      setState(338);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 52, _ctx)) {
      case 1: {
        setState(336);
        match(CParser::ELSE_KW);
        setState(337);
        antlrcpp::downCast<IfStmtContext *>(_localctx)->elseS = statement();
        break;
      }

      default:
        break;
      }
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<CParser::WhileStmtContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(340);
      match(CParser::WHILE_KW);
      setState(341);
      match(CParser::LPAREN);
      setState(342);
      expr();
      setState(343);
      match(CParser::RPAREN);
      setState(344);
      statement();
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<CParser::ForStmtContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(346);
      match(CParser::FOR_KW);
      setState(347);
      match(CParser::LPAREN);
      setState(349);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8521743599102) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 68)) & 1987) != 0)) {
        setState(348);
        antlrcpp::downCast<ForStmtContext *>(_localctx)->init = simpleStmt();
      }
      setState(351);
      match(CParser::SEMI);
      setState(353);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8521483551744) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 68)) & 1987) != 0)) {
        setState(352);
        antlrcpp::downCast<ForStmtContext *>(_localctx)->cond = expr();
      }
      setState(355);
      match(CParser::SEMI);
      setState(357);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8521743599102) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 68)) & 1987) != 0)) {
        setState(356);
        antlrcpp::downCast<ForStmtContext *>(_localctx)->post = simpleStmt();
      }
      setState(359);
      match(CParser::RPAREN);
      setState(360);
      statement();
      break;
    }

    case 5: {
      _localctx = _tracker.createInstance<CParser::SwitchStmtContext>(_localctx);
      enterOuterAlt(_localctx, 5);
      setState(361);
      match(CParser::SWITCH_KW);
      setState(362);
      match(CParser::LPAREN);
      setState(363);
      expr();
      setState(364);
      match(CParser::RPAREN);
      setState(365);
      match(CParser::LBRACE);
      setState(369);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == CParser::CASE_KW

      || _la == CParser::DEFAULT_KW) {
        setState(366);
        switchCase();
        setState(371);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(372);
      match(CParser::RBRACE);
      break;
    }

    case 6: {
      _localctx = _tracker.createInstance<CParser::ReturnStmtContext>(_localctx);
      enterOuterAlt(_localctx, 6);
      setState(374);
      match(CParser::RETURN_KW);
      setState(376);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8521483551744) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 68)) & 1987) != 0)) {
        setState(375);
        expr();
      }
      setState(378);
      match(CParser::SEMI);
      break;
    }

    case 7: {
      _localctx = _tracker.createInstance<CParser::BreakStmtContext>(_localctx);
      enterOuterAlt(_localctx, 7);
      setState(379);
      match(CParser::BREAK_KW);
      setState(380);
      match(CParser::SEMI);
      break;
    }

    case 8: {
      _localctx = _tracker.createInstance<CParser::ContinueStmtContext>(_localctx);
      enterOuterAlt(_localctx, 8);
      setState(381);
      match(CParser::CONTINUE_KW);
      setState(382);
      match(CParser::SEMI);
      break;
    }

    case 9: {
      _localctx = _tracker.createInstance<CParser::GotoStmtContext>(_localctx);
      enterOuterAlt(_localctx, 9);
      setState(383);
      match(CParser::GOTO_KW);
      setState(384);
      match(CParser::IDENT);
      setState(385);
      match(CParser::SEMI);
      break;
    }

    case 10: {
      _localctx = _tracker.createInstance<CParser::LabelStmtContext>(_localctx);
      enterOuterAlt(_localctx, 10);
      setState(386);
      match(CParser::IDENT);
      setState(387);
      match(CParser::COLON);
      setState(388);
      statement();
      break;
    }

    case 11: {
      _localctx = _tracker.createInstance<CParser::AsmStmtContext>(_localctx);
      enterOuterAlt(_localctx, 11);
      setState(389);
      match(CParser::ASM_BEGIN);
      setState(391);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::ASM_TEXT) {
        setState(390);
        antlrcpp::downCast<AsmStmtContext *>(_localctx)->text = match(CParser::ASM_TEXT);
      }
      setState(393);
      match(CParser::ASM_END);
      break;
    }

    case 12: {
      _localctx = _tracker.createInstance<CParser::SimpleStmtWrapperContext>(_localctx);
      enterOuterAlt(_localctx, 12);
      setState(394);
      simpleStmt();
      setState(395);
      match(CParser::SEMI);
      break;
    }

    case 13: {
      _localctx = _tracker.createInstance<CParser::EmptyStmtContext>(_localctx);
      enterOuterAlt(_localctx, 13);
      setState(397);
      match(CParser::SEMI);
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

//----------------- SimpleStmtContext ------------------------------------------------------------------

CParser::SimpleStmtContext::SimpleStmtContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t CParser::SimpleStmtContext::getRuleIndex() const {
  return CParser::RuleSimpleStmt;
}

void CParser::SimpleStmtContext::copyFrom(SimpleStmtContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- StaticLocalDeclGroupContext ------------------------------------------------------------------

tree::TerminalNode* CParser::StaticLocalDeclGroupContext::STATIC_KW() {
  return getToken(CParser::STATIC_KW, 0);
}

CParser::TypeSpecContext* CParser::StaticLocalDeclGroupContext::typeSpec() {
  return getRuleContext<CParser::TypeSpecContext>(0);
}

std::vector<CParser::StaticLocalDeclaratorContext *> CParser::StaticLocalDeclGroupContext::staticLocalDeclarator() {
  return getRuleContexts<CParser::StaticLocalDeclaratorContext>();
}

CParser::StaticLocalDeclaratorContext* CParser::StaticLocalDeclGroupContext::staticLocalDeclarator(size_t i) {
  return getRuleContext<CParser::StaticLocalDeclaratorContext>(i);
}

std::vector<tree::TerminalNode *> CParser::StaticLocalDeclGroupContext::COMMA() {
  return getTokens(CParser::COMMA);
}

tree::TerminalNode* CParser::StaticLocalDeclGroupContext::COMMA(size_t i) {
  return getToken(CParser::COMMA, i);
}

tree::TerminalNode* CParser::StaticLocalDeclGroupContext::CONST_KW() {
  return getToken(CParser::CONST_KW, 0);
}

CParser::StaticLocalDeclGroupContext::StaticLocalDeclGroupContext(SimpleStmtContext *ctx) { copyFrom(ctx); }


std::any CParser::StaticLocalDeclGroupContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitStaticLocalDeclGroup(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ExprStmtContext ------------------------------------------------------------------

CParser::ExprContext* CParser::ExprStmtContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

CParser::ExprStmtContext::ExprStmtContext(SimpleStmtContext *ctx) { copyFrom(ctx); }


std::any CParser::ExprStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitExprStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- LocalDeclGroupContext ------------------------------------------------------------------

CParser::TypeSpecContext* CParser::LocalDeclGroupContext::typeSpec() {
  return getRuleContext<CParser::TypeSpecContext>(0);
}

std::vector<CParser::LocalDeclaratorContext *> CParser::LocalDeclGroupContext::localDeclarator() {
  return getRuleContexts<CParser::LocalDeclaratorContext>();
}

CParser::LocalDeclaratorContext* CParser::LocalDeclGroupContext::localDeclarator(size_t i) {
  return getRuleContext<CParser::LocalDeclaratorContext>(i);
}

std::vector<tree::TerminalNode *> CParser::LocalDeclGroupContext::COMMA() {
  return getTokens(CParser::COMMA);
}

tree::TerminalNode* CParser::LocalDeclGroupContext::COMMA(size_t i) {
  return getToken(CParser::COMMA, i);
}

tree::TerminalNode* CParser::LocalDeclGroupContext::REGISTER_KW() {
  return getToken(CParser::REGISTER_KW, 0);
}

tree::TerminalNode* CParser::LocalDeclGroupContext::CONST_KW() {
  return getToken(CParser::CONST_KW, 0);
}

CParser::LocalDeclGroupContext::LocalDeclGroupContext(SimpleStmtContext *ctx) { copyFrom(ctx); }


std::any CParser::LocalDeclGroupContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitLocalDeclGroup(this);
  else
    return visitor->visitChildren(this);
}
CParser::SimpleStmtContext* CParser::simpleStmt() {
  SimpleStmtContext *_localctx = _tracker.createInstance<SimpleStmtContext>(_ctx, getState());
  enterRule(_localctx, 38, CParser::RuleSimpleStmt);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(429);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CParser::INT_KW:
      case CParser::CHAR_KW:
      case CParser::SHORT_KW:
      case CParser::LONG_KW:
      case CParser::FLOAT_KW:
      case CParser::DOUBLE_KW:
      case CParser::SIGNED_KW:
      case CParser::UNSIGNED_KW:
      case CParser::CONST_KW:
      case CParser::REGISTER_KW:
      case CParser::STRUCT_KW:
      case CParser::ENUM_KW: {
        _localctx = _tracker.createInstance<CParser::LocalDeclGroupContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(401);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CParser::REGISTER_KW) {
          setState(400);
          antlrcpp::downCast<LocalDeclGroupContext *>(_localctx)->reg = match(CParser::REGISTER_KW);
        }
        setState(404);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CParser::CONST_KW) {
          setState(403);
          antlrcpp::downCast<LocalDeclGroupContext *>(_localctx)->cst = match(CParser::CONST_KW);
        }
        setState(406);
        typeSpec();
        setState(407);
        antlrcpp::downCast<LocalDeclGroupContext *>(_localctx)->localDeclaratorContext = localDeclarator();
        antlrcpp::downCast<LocalDeclGroupContext *>(_localctx)->decl.push_back(antlrcpp::downCast<LocalDeclGroupContext *>(_localctx)->localDeclaratorContext);
        setState(412);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == CParser::COMMA) {
          setState(408);
          match(CParser::COMMA);
          setState(409);
          antlrcpp::downCast<LocalDeclGroupContext *>(_localctx)->localDeclaratorContext = localDeclarator();
          antlrcpp::downCast<LocalDeclGroupContext *>(_localctx)->decl.push_back(antlrcpp::downCast<LocalDeclGroupContext *>(_localctx)->localDeclaratorContext);
          setState(414);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
        break;
      }

      case CParser::STATIC_KW: {
        _localctx = _tracker.createInstance<CParser::StaticLocalDeclGroupContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(415);
        match(CParser::STATIC_KW);
        setState(417);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == CParser::CONST_KW) {
          setState(416);
          antlrcpp::downCast<StaticLocalDeclGroupContext *>(_localctx)->cst = match(CParser::CONST_KW);
        }
        setState(419);
        typeSpec();
        setState(420);
        antlrcpp::downCast<StaticLocalDeclGroupContext *>(_localctx)->staticLocalDeclaratorContext = staticLocalDeclarator();
        antlrcpp::downCast<StaticLocalDeclGroupContext *>(_localctx)->decl.push_back(antlrcpp::downCast<StaticLocalDeclGroupContext *>(_localctx)->staticLocalDeclaratorContext);
        setState(425);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == CParser::COMMA) {
          setState(421);
          match(CParser::COMMA);
          setState(422);
          antlrcpp::downCast<StaticLocalDeclGroupContext *>(_localctx)->staticLocalDeclaratorContext = staticLocalDeclarator();
          antlrcpp::downCast<StaticLocalDeclGroupContext *>(_localctx)->decl.push_back(antlrcpp::downCast<StaticLocalDeclGroupContext *>(_localctx)->staticLocalDeclaratorContext);
          setState(427);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
        break;
      }

      case CParser::SIZEOF_KW:
      case CParser::LPAREN:
      case CParser::STAR:
      case CParser::AMP:
      case CParser::MINUS:
      case CParser::BANG:
      case CParser::TILDE:
      case CParser::INC:
      case CParser::DEC:
      case CParser::IDENT:
      case CParser::NUMBER:
      case CParser::FLOAT_NUMBER:
      case CParser::CHARLIT:
      case CParser::STRINGLIT: {
        _localctx = _tracker.createInstance<CParser::ExprStmtContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(428);
        expr();
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

//----------------- LocalDeclaratorContext ------------------------------------------------------------------

CParser::LocalDeclaratorContext::LocalDeclaratorContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t CParser::LocalDeclaratorContext::getRuleIndex() const {
  return CParser::RuleLocalDeclarator;
}

void CParser::LocalDeclaratorContext::copyFrom(LocalDeclaratorContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- LocalArrayDeclaratorContext ------------------------------------------------------------------

tree::TerminalNode* CParser::LocalArrayDeclaratorContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

std::vector<tree::TerminalNode *> CParser::LocalArrayDeclaratorContext::LBRACK() {
  return getTokens(CParser::LBRACK);
}

tree::TerminalNode* CParser::LocalArrayDeclaratorContext::LBRACK(size_t i) {
  return getToken(CParser::LBRACK, i);
}

std::vector<tree::TerminalNode *> CParser::LocalArrayDeclaratorContext::RBRACK() {
  return getTokens(CParser::RBRACK);
}

tree::TerminalNode* CParser::LocalArrayDeclaratorContext::RBRACK(size_t i) {
  return getToken(CParser::RBRACK, i);
}

tree::TerminalNode* CParser::LocalArrayDeclaratorContext::ASSIGN() {
  return getToken(CParser::ASSIGN, 0);
}

CParser::ArrayInitContext* CParser::LocalArrayDeclaratorContext::arrayInit() {
  return getRuleContext<CParser::ArrayInitContext>(0);
}

std::vector<tree::TerminalNode *> CParser::LocalArrayDeclaratorContext::NUMBER() {
  return getTokens(CParser::NUMBER);
}

tree::TerminalNode* CParser::LocalArrayDeclaratorContext::NUMBER(size_t i) {
  return getToken(CParser::NUMBER, i);
}

CParser::LocalArrayDeclaratorContext::LocalArrayDeclaratorContext(LocalDeclaratorContext *ctx) { copyFrom(ctx); }


std::any CParser::LocalArrayDeclaratorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitLocalArrayDeclarator(this);
  else
    return visitor->visitChildren(this);
}
//----------------- LocalScalarDeclaratorContext ------------------------------------------------------------------

tree::TerminalNode* CParser::LocalScalarDeclaratorContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::LocalScalarDeclaratorContext::ASSIGN() {
  return getToken(CParser::ASSIGN, 0);
}

CParser::ExprContext* CParser::LocalScalarDeclaratorContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

CParser::LocalScalarDeclaratorContext::LocalScalarDeclaratorContext(LocalDeclaratorContext *ctx) { copyFrom(ctx); }


std::any CParser::LocalScalarDeclaratorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitLocalScalarDeclarator(this);
  else
    return visitor->visitChildren(this);
}
CParser::LocalDeclaratorContext* CParser::localDeclarator() {
  LocalDeclaratorContext *_localctx = _tracker.createInstance<LocalDeclaratorContext>(_ctx, getState());
  enterRule(_localctx, 40, CParser::RuleLocalDeclarator);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(448);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 69, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<CParser::LocalArrayDeclaratorContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(431);
      match(CParser::IDENT);
      setState(435); 
      _errHandler->sync(this);
      _la = _input->LA(1);
      do {
        setState(432);
        match(CParser::LBRACK);
        setState(433);
        antlrcpp::downCast<LocalArrayDeclaratorContext *>(_localctx)->numberToken = match(CParser::NUMBER);
        antlrcpp::downCast<LocalArrayDeclaratorContext *>(_localctx)->dims.push_back(antlrcpp::downCast<LocalArrayDeclaratorContext *>(_localctx)->numberToken);
        setState(434);
        match(CParser::RBRACK);
        setState(437); 
        _errHandler->sync(this);
        _la = _input->LA(1);
      } while (_la == CParser::LBRACK);
      setState(441);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::ASSIGN) {
        setState(439);
        match(CParser::ASSIGN);
        setState(440);
        arrayInit();
      }
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<CParser::LocalScalarDeclaratorContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(443);
      match(CParser::IDENT);
      setState(446);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::ASSIGN) {
        setState(444);
        match(CParser::ASSIGN);
        setState(445);
        expr();
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

//----------------- StaticLocalDeclaratorContext ------------------------------------------------------------------

CParser::StaticLocalDeclaratorContext::StaticLocalDeclaratorContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t CParser::StaticLocalDeclaratorContext::getRuleIndex() const {
  return CParser::RuleStaticLocalDeclarator;
}

void CParser::StaticLocalDeclaratorContext::copyFrom(StaticLocalDeclaratorContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- StaticLocalArrayDeclaratorContext ------------------------------------------------------------------

tree::TerminalNode* CParser::StaticLocalArrayDeclaratorContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

std::vector<tree::TerminalNode *> CParser::StaticLocalArrayDeclaratorContext::LBRACK() {
  return getTokens(CParser::LBRACK);
}

tree::TerminalNode* CParser::StaticLocalArrayDeclaratorContext::LBRACK(size_t i) {
  return getToken(CParser::LBRACK, i);
}

std::vector<tree::TerminalNode *> CParser::StaticLocalArrayDeclaratorContext::RBRACK() {
  return getTokens(CParser::RBRACK);
}

tree::TerminalNode* CParser::StaticLocalArrayDeclaratorContext::RBRACK(size_t i) {
  return getToken(CParser::RBRACK, i);
}

tree::TerminalNode* CParser::StaticLocalArrayDeclaratorContext::ASSIGN() {
  return getToken(CParser::ASSIGN, 0);
}

CParser::ArrayInitContext* CParser::StaticLocalArrayDeclaratorContext::arrayInit() {
  return getRuleContext<CParser::ArrayInitContext>(0);
}

std::vector<tree::TerminalNode *> CParser::StaticLocalArrayDeclaratorContext::NUMBER() {
  return getTokens(CParser::NUMBER);
}

tree::TerminalNode* CParser::StaticLocalArrayDeclaratorContext::NUMBER(size_t i) {
  return getToken(CParser::NUMBER, i);
}

CParser::StaticLocalArrayDeclaratorContext::StaticLocalArrayDeclaratorContext(StaticLocalDeclaratorContext *ctx) { copyFrom(ctx); }


std::any CParser::StaticLocalArrayDeclaratorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitStaticLocalArrayDeclarator(this);
  else
    return visitor->visitChildren(this);
}
//----------------- StaticLocalScalarDeclaratorContext ------------------------------------------------------------------

std::vector<tree::TerminalNode *> CParser::StaticLocalScalarDeclaratorContext::IDENT() {
  return getTokens(CParser::IDENT);
}

tree::TerminalNode* CParser::StaticLocalScalarDeclaratorContext::IDENT(size_t i) {
  return getToken(CParser::IDENT, i);
}

tree::TerminalNode* CParser::StaticLocalScalarDeclaratorContext::ASSIGN() {
  return getToken(CParser::ASSIGN, 0);
}

tree::TerminalNode* CParser::StaticLocalScalarDeclaratorContext::NUMBER() {
  return getToken(CParser::NUMBER, 0);
}

tree::TerminalNode* CParser::StaticLocalScalarDeclaratorContext::MINUS() {
  return getToken(CParser::MINUS, 0);
}

CParser::StaticLocalScalarDeclaratorContext::StaticLocalScalarDeclaratorContext(StaticLocalDeclaratorContext *ctx) { copyFrom(ctx); }


std::any CParser::StaticLocalScalarDeclaratorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitStaticLocalScalarDeclarator(this);
  else
    return visitor->visitChildren(this);
}
CParser::StaticLocalDeclaratorContext* CParser::staticLocalDeclarator() {
  StaticLocalDeclaratorContext *_localctx = _tracker.createInstance<StaticLocalDeclaratorContext>(_ctx, getState());
  enterRule(_localctx, 42, CParser::RuleStaticLocalDeclarator);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(473);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 75, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<CParser::StaticLocalArrayDeclaratorContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(450);
      match(CParser::IDENT);
      setState(454); 
      _errHandler->sync(this);
      _la = _input->LA(1);
      do {
        setState(451);
        match(CParser::LBRACK);
        setState(452);
        antlrcpp::downCast<StaticLocalArrayDeclaratorContext *>(_localctx)->numberToken = match(CParser::NUMBER);
        antlrcpp::downCast<StaticLocalArrayDeclaratorContext *>(_localctx)->dims.push_back(antlrcpp::downCast<StaticLocalArrayDeclaratorContext *>(_localctx)->numberToken);
        setState(453);
        match(CParser::RBRACK);
        setState(456); 
        _errHandler->sync(this);
        _la = _input->LA(1);
      } while (_la == CParser::LBRACK);
      setState(460);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::ASSIGN) {
        setState(458);
        match(CParser::ASSIGN);
        setState(459);
        arrayInit();
      }
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<CParser::StaticLocalScalarDeclaratorContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(462);
      match(CParser::IDENT);
      setState(471);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::ASSIGN) {
        setState(463);
        match(CParser::ASSIGN);
        setState(469);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case CParser::MINUS:
          case CParser::NUMBER: {
            setState(465);
            _errHandler->sync(this);

            _la = _input->LA(1);
            if (_la == CParser::MINUS) {
              setState(464);
              antlrcpp::downCast<StaticLocalScalarDeclaratorContext *>(_localctx)->sign = match(CParser::MINUS);
            }
            setState(467);
            antlrcpp::downCast<StaticLocalScalarDeclaratorContext *>(_localctx)->val = match(CParser::NUMBER);
            break;
          }

          case CParser::IDENT: {
            setState(468);
            antlrcpp::downCast<StaticLocalScalarDeclaratorContext *>(_localctx)->identVal = match(CParser::IDENT);
            break;
          }

        default:
          throw NoViableAltException(this);
        }
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

//----------------- SwitchCaseContext ------------------------------------------------------------------

CParser::SwitchCaseContext::SwitchCaseContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t CParser::SwitchCaseContext::getRuleIndex() const {
  return CParser::RuleSwitchCase;
}

void CParser::SwitchCaseContext::copyFrom(SwitchCaseContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- DefaultLabelContext ------------------------------------------------------------------

tree::TerminalNode* CParser::DefaultLabelContext::DEFAULT_KW() {
  return getToken(CParser::DEFAULT_KW, 0);
}

tree::TerminalNode* CParser::DefaultLabelContext::COLON() {
  return getToken(CParser::COLON, 0);
}

std::vector<CParser::StatementContext *> CParser::DefaultLabelContext::statement() {
  return getRuleContexts<CParser::StatementContext>();
}

CParser::StatementContext* CParser::DefaultLabelContext::statement(size_t i) {
  return getRuleContext<CParser::StatementContext>(i);
}

CParser::DefaultLabelContext::DefaultLabelContext(SwitchCaseContext *ctx) { copyFrom(ctx); }


std::any CParser::DefaultLabelContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitDefaultLabel(this);
  else
    return visitor->visitChildren(this);
}
//----------------- CaseLabelContext ------------------------------------------------------------------

tree::TerminalNode* CParser::CaseLabelContext::CASE_KW() {
  return getToken(CParser::CASE_KW, 0);
}

tree::TerminalNode* CParser::CaseLabelContext::COLON() {
  return getToken(CParser::COLON, 0);
}

tree::TerminalNode* CParser::CaseLabelContext::NUMBER() {
  return getToken(CParser::NUMBER, 0);
}

tree::TerminalNode* CParser::CaseLabelContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

std::vector<CParser::StatementContext *> CParser::CaseLabelContext::statement() {
  return getRuleContexts<CParser::StatementContext>();
}

CParser::StatementContext* CParser::CaseLabelContext::statement(size_t i) {
  return getRuleContext<CParser::StatementContext>(i);
}

tree::TerminalNode* CParser::CaseLabelContext::MINUS() {
  return getToken(CParser::MINUS, 0);
}

CParser::CaseLabelContext::CaseLabelContext(SwitchCaseContext *ctx) { copyFrom(ctx); }


std::any CParser::CaseLabelContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitCaseLabel(this);
  else
    return visitor->visitChildren(this);
}
CParser::SwitchCaseContext* CParser::switchCase() {
  SwitchCaseContext *_localctx = _tracker.createInstance<SwitchCaseContext>(_ctx, getState());
  enterRule(_localctx, 44, CParser::RuleSwitchCase);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(498);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case CParser::CASE_KW: {
        _localctx = _tracker.createInstance<CParser::CaseLabelContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(475);
        match(CParser::CASE_KW);
        setState(481);
        _errHandler->sync(this);
        switch (_input->LA(1)) {
          case CParser::MINUS:
          case CParser::NUMBER: {
            setState(477);
            _errHandler->sync(this);

            _la = _input->LA(1);
            if (_la == CParser::MINUS) {
              setState(476);
              antlrcpp::downCast<CaseLabelContext *>(_localctx)->sign = match(CParser::MINUS);
            }
            setState(479);
            antlrcpp::downCast<CaseLabelContext *>(_localctx)->val = match(CParser::NUMBER);
            break;
          }

          case CParser::IDENT: {
            setState(480);
            antlrcpp::downCast<CaseLabelContext *>(_localctx)->identVal = match(CParser::IDENT);
            break;
          }

        default:
          throw NoViableAltException(this);
        }
        setState(483);
        match(CParser::COLON);
        setState(487);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 8560402296318) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
          ((1ULL << (_la - 68)) & 18371) != 0)) {
          setState(484);
          statement();
          setState(489);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
        break;
      }

      case CParser::DEFAULT_KW: {
        _localctx = _tracker.createInstance<CParser::DefaultLabelContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(490);
        match(CParser::DEFAULT_KW);
        setState(491);
        match(CParser::COLON);
        setState(495);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 8560402296318) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
          ((1ULL << (_la - 68)) & 18371) != 0)) {
          setState(492);
          statement();
          setState(497);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
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

//----------------- AssignOpContext ------------------------------------------------------------------

CParser::AssignOpContext::AssignOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* CParser::AssignOpContext::ASSIGN() {
  return getToken(CParser::ASSIGN, 0);
}

tree::TerminalNode* CParser::AssignOpContext::PLUSEQ() {
  return getToken(CParser::PLUSEQ, 0);
}

tree::TerminalNode* CParser::AssignOpContext::MINUSEQ() {
  return getToken(CParser::MINUSEQ, 0);
}

tree::TerminalNode* CParser::AssignOpContext::STAREQ() {
  return getToken(CParser::STAREQ, 0);
}

tree::TerminalNode* CParser::AssignOpContext::SLASHEQ() {
  return getToken(CParser::SLASHEQ, 0);
}

tree::TerminalNode* CParser::AssignOpContext::PERCENTEQ() {
  return getToken(CParser::PERCENTEQ, 0);
}

tree::TerminalNode* CParser::AssignOpContext::AMPEQ() {
  return getToken(CParser::AMPEQ, 0);
}

tree::TerminalNode* CParser::AssignOpContext::CARETEQ() {
  return getToken(CParser::CARETEQ, 0);
}

tree::TerminalNode* CParser::AssignOpContext::PIPEEQ() {
  return getToken(CParser::PIPEEQ, 0);
}

tree::TerminalNode* CParser::AssignOpContext::SHLEQ() {
  return getToken(CParser::SHLEQ, 0);
}

tree::TerminalNode* CParser::AssignOpContext::SHREQ() {
  return getToken(CParser::SHREQ, 0);
}


size_t CParser::AssignOpContext::getRuleIndex() const {
  return CParser::RuleAssignOp;
}


std::any CParser::AssignOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitAssignOp(this);
  else
    return visitor->visitChildren(this);
}

CParser::AssignOpContext* CParser::assignOp() {
  AssignOpContext *_localctx = _tracker.createInstance<AssignOpContext>(_ctx, getState());
  enterRule(_localctx, 46, CParser::RuleAssignOp);
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
    setState(500);
    _la = _input->LA(1);
    if (!(((((_la - 37) & ~ 0x3fULL) == 0) &&
      ((1ULL << (_la - 37)) & 2145386497) != 0))) {
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

//----------------- ExprContext ------------------------------------------------------------------

CParser::ExprContext::ExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t CParser::ExprContext::getRuleIndex() const {
  return CParser::RuleExpr;
}

void CParser::ExprContext::copyFrom(ExprContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- AssignMemberExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::AssignMemberExprContext::DOT() {
  return getToken(CParser::DOT, 0);
}

CParser::CondExprContext* CParser::AssignMemberExprContext::condExpr() {
  return getRuleContext<CParser::CondExprContext>(0);
}

tree::TerminalNode* CParser::AssignMemberExprContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

CParser::AssignOpContext* CParser::AssignMemberExprContext::assignOp() {
  return getRuleContext<CParser::AssignOpContext>(0);
}

std::vector<CParser::ExprContext *> CParser::AssignMemberExprContext::expr() {
  return getRuleContexts<CParser::ExprContext>();
}

CParser::ExprContext* CParser::AssignMemberExprContext::expr(size_t i) {
  return getRuleContext<CParser::ExprContext>(i);
}

tree::TerminalNode* CParser::AssignMemberExprContext::LBRACK() {
  return getToken(CParser::LBRACK, 0);
}

tree::TerminalNode* CParser::AssignMemberExprContext::RBRACK() {
  return getToken(CParser::RBRACK, 0);
}

CParser::AssignMemberExprContext::AssignMemberExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any CParser::AssignMemberExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitAssignMemberExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ExprCondContext ------------------------------------------------------------------

CParser::CondExprContext* CParser::ExprCondContext::condExpr() {
  return getRuleContext<CParser::CondExprContext>(0);
}

CParser::ExprCondContext::ExprCondContext(ExprContext *ctx) { copyFrom(ctx); }


std::any CParser::ExprCondContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitExprCond(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AssignIndexExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::AssignIndexExprContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

CParser::AssignOpContext* CParser::AssignIndexExprContext::assignOp() {
  return getRuleContext<CParser::AssignOpContext>(0);
}

std::vector<CParser::ExprContext *> CParser::AssignIndexExprContext::expr() {
  return getRuleContexts<CParser::ExprContext>();
}

CParser::ExprContext* CParser::AssignIndexExprContext::expr(size_t i) {
  return getRuleContext<CParser::ExprContext>(i);
}

std::vector<tree::TerminalNode *> CParser::AssignIndexExprContext::LBRACK() {
  return getTokens(CParser::LBRACK);
}

tree::TerminalNode* CParser::AssignIndexExprContext::LBRACK(size_t i) {
  return getToken(CParser::LBRACK, i);
}

std::vector<tree::TerminalNode *> CParser::AssignIndexExprContext::RBRACK() {
  return getTokens(CParser::RBRACK);
}

tree::TerminalNode* CParser::AssignIndexExprContext::RBRACK(size_t i) {
  return getToken(CParser::RBRACK, i);
}

CParser::AssignIndexExprContext::AssignIndexExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any CParser::AssignIndexExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitAssignIndexExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AssignMemberPtrExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::AssignMemberPtrExprContext::ARROW() {
  return getToken(CParser::ARROW, 0);
}

CParser::CondExprContext* CParser::AssignMemberPtrExprContext::condExpr() {
  return getRuleContext<CParser::CondExprContext>(0);
}

tree::TerminalNode* CParser::AssignMemberPtrExprContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

CParser::AssignOpContext* CParser::AssignMemberPtrExprContext::assignOp() {
  return getRuleContext<CParser::AssignOpContext>(0);
}

std::vector<CParser::ExprContext *> CParser::AssignMemberPtrExprContext::expr() {
  return getRuleContexts<CParser::ExprContext>();
}

CParser::ExprContext* CParser::AssignMemberPtrExprContext::expr(size_t i) {
  return getRuleContext<CParser::ExprContext>(i);
}

tree::TerminalNode* CParser::AssignMemberPtrExprContext::LBRACK() {
  return getToken(CParser::LBRACK, 0);
}

tree::TerminalNode* CParser::AssignMemberPtrExprContext::RBRACK() {
  return getToken(CParser::RBRACK, 0);
}

CParser::AssignMemberPtrExprContext::AssignMemberPtrExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any CParser::AssignMemberPtrExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitAssignMemberPtrExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AssignDerefExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::AssignDerefExprContext::STAR() {
  return getToken(CParser::STAR, 0);
}

CParser::CondExprContext* CParser::AssignDerefExprContext::condExpr() {
  return getRuleContext<CParser::CondExprContext>(0);
}

CParser::AssignOpContext* CParser::AssignDerefExprContext::assignOp() {
  return getRuleContext<CParser::AssignOpContext>(0);
}

CParser::ExprContext* CParser::AssignDerefExprContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

CParser::AssignDerefExprContext::AssignDerefExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any CParser::AssignDerefExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitAssignDerefExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AssignExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::AssignExprContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

CParser::AssignOpContext* CParser::AssignExprContext::assignOp() {
  return getRuleContext<CParser::AssignOpContext>(0);
}

CParser::ExprContext* CParser::AssignExprContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

CParser::AssignExprContext::AssignExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any CParser::AssignExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitAssignExpr(this);
  else
    return visitor->visitChildren(this);
}
CParser::ExprContext* CParser::expr() {
  ExprContext *_localctx = _tracker.createInstance<ExprContext>(_ctx, getState());
  enterRule(_localctx, 48, CParser::RuleExpr);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(548);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 84, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<CParser::AssignDerefExprContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(502);
      match(CParser::STAR);
      setState(503);
      antlrcpp::downCast<AssignDerefExprContext *>(_localctx)->ptr = condExpr(0);
      setState(504);
      antlrcpp::downCast<AssignDerefExprContext *>(_localctx)->op = assignOp();
      setState(505);
      antlrcpp::downCast<AssignDerefExprContext *>(_localctx)->val = expr();
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<CParser::AssignIndexExprContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(507);
      match(CParser::IDENT);
      setState(512); 
      _errHandler->sync(this);
      _la = _input->LA(1);
      do {
        setState(508);
        match(CParser::LBRACK);
        setState(509);
        antlrcpp::downCast<AssignIndexExprContext *>(_localctx)->exprContext = expr();
        antlrcpp::downCast<AssignIndexExprContext *>(_localctx)->idx.push_back(antlrcpp::downCast<AssignIndexExprContext *>(_localctx)->exprContext);
        setState(510);
        match(CParser::RBRACK);
        setState(514); 
        _errHandler->sync(this);
        _la = _input->LA(1);
      } while (_la == CParser::LBRACK);
      setState(516);
      antlrcpp::downCast<AssignIndexExprContext *>(_localctx)->op = assignOp();
      setState(517);
      antlrcpp::downCast<AssignIndexExprContext *>(_localctx)->val = expr();
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<CParser::AssignExprContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(519);
      match(CParser::IDENT);
      setState(520);
      antlrcpp::downCast<AssignExprContext *>(_localctx)->op = assignOp();
      setState(521);
      antlrcpp::downCast<AssignExprContext *>(_localctx)->val = expr();
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<CParser::AssignMemberExprContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(523);
      antlrcpp::downCast<AssignMemberExprContext *>(_localctx)->base = condExpr(0);
      setState(524);
      match(CParser::DOT);
      setState(525);
      antlrcpp::downCast<AssignMemberExprContext *>(_localctx)->name = match(CParser::IDENT);
      setState(530);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::LBRACK) {
        setState(526);
        match(CParser::LBRACK);
        setState(527);
        antlrcpp::downCast<AssignMemberExprContext *>(_localctx)->idx = expr();
        setState(528);
        match(CParser::RBRACK);
      }
      setState(532);
      antlrcpp::downCast<AssignMemberExprContext *>(_localctx)->op = assignOp();
      setState(533);
      antlrcpp::downCast<AssignMemberExprContext *>(_localctx)->val = expr();
      break;
    }

    case 5: {
      _localctx = _tracker.createInstance<CParser::AssignMemberPtrExprContext>(_localctx);
      enterOuterAlt(_localctx, 5);
      setState(535);
      antlrcpp::downCast<AssignMemberPtrExprContext *>(_localctx)->base = condExpr(0);
      setState(536);
      match(CParser::ARROW);
      setState(537);
      antlrcpp::downCast<AssignMemberPtrExprContext *>(_localctx)->name = match(CParser::IDENT);
      setState(542);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == CParser::LBRACK) {
        setState(538);
        match(CParser::LBRACK);
        setState(539);
        antlrcpp::downCast<AssignMemberPtrExprContext *>(_localctx)->idx = expr();
        setState(540);
        match(CParser::RBRACK);
      }
      setState(544);
      antlrcpp::downCast<AssignMemberPtrExprContext *>(_localctx)->op = assignOp();
      setState(545);
      antlrcpp::downCast<AssignMemberPtrExprContext *>(_localctx)->val = expr();
      break;
    }

    case 6: {
      _localctx = _tracker.createInstance<CParser::ExprCondContext>(_localctx);
      enterOuterAlt(_localctx, 6);
      setState(547);
      condExpr(0);
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

//----------------- CondExprContext ------------------------------------------------------------------

CParser::CondExprContext::CondExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t CParser::CondExprContext::getRuleIndex() const {
  return CParser::RuleCondExpr;
}

void CParser::CondExprContext::copyFrom(CondExprContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- SizeofExprExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::SizeofExprExprContext::SIZEOF_KW() {
  return getToken(CParser::SIZEOF_KW, 0);
}

tree::TerminalNode* CParser::SizeofExprExprContext::LPAREN() {
  return getToken(CParser::LPAREN, 0);
}

CParser::ExprContext* CParser::SizeofExprExprContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

tree::TerminalNode* CParser::SizeofExprExprContext::RPAREN() {
  return getToken(CParser::RPAREN, 0);
}

CParser::SizeofExprExprContext::SizeofExprExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::SizeofExprExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitSizeofExprExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PreIncDecDerefExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::PreIncDecDerefExprContext::STAR() {
  return getToken(CParser::STAR, 0);
}

CParser::CondExprContext* CParser::PreIncDecDerefExprContext::condExpr() {
  return getRuleContext<CParser::CondExprContext>(0);
}

tree::TerminalNode* CParser::PreIncDecDerefExprContext::INC() {
  return getToken(CParser::INC, 0);
}

tree::TerminalNode* CParser::PreIncDecDerefExprContext::DEC() {
  return getToken(CParser::DEC, 0);
}

CParser::PreIncDecDerefExprContext::PreIncDecDerefExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::PreIncDecDerefExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitPreIncDecDerefExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- CastExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::CastExprContext::LPAREN() {
  return getToken(CParser::LPAREN, 0);
}

CParser::TypeSpecContext* CParser::CastExprContext::typeSpec() {
  return getRuleContext<CParser::TypeSpecContext>(0);
}

tree::TerminalNode* CParser::CastExprContext::RPAREN() {
  return getToken(CParser::RPAREN, 0);
}

CParser::CondExprContext* CParser::CastExprContext::condExpr() {
  return getRuleContext<CParser::CondExprContext>(0);
}

CParser::CastExprContext::CastExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::CastExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitCastExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PostIncDecVarExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::PostIncDecVarExprContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::PostIncDecVarExprContext::INC() {
  return getToken(CParser::INC, 0);
}

tree::TerminalNode* CParser::PostIncDecVarExprContext::DEC() {
  return getToken(CParser::DEC, 0);
}

CParser::PostIncDecVarExprContext::PostIncDecVarExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::PostIncDecVarExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitPostIncDecVarExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- StringLitExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::StringLitExprContext::STRINGLIT() {
  return getToken(CParser::STRINGLIT, 0);
}

CParser::StringLitExprContext::StringLitExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::StringLitExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitStringLitExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PreIncDecIndexExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::PreIncDecIndexExprContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::PreIncDecIndexExprContext::INC() {
  return getToken(CParser::INC, 0);
}

tree::TerminalNode* CParser::PreIncDecIndexExprContext::DEC() {
  return getToken(CParser::DEC, 0);
}

std::vector<tree::TerminalNode *> CParser::PreIncDecIndexExprContext::LBRACK() {
  return getTokens(CParser::LBRACK);
}

tree::TerminalNode* CParser::PreIncDecIndexExprContext::LBRACK(size_t i) {
  return getToken(CParser::LBRACK, i);
}

std::vector<tree::TerminalNode *> CParser::PreIncDecIndexExprContext::RBRACK() {
  return getTokens(CParser::RBRACK);
}

tree::TerminalNode* CParser::PreIncDecIndexExprContext::RBRACK(size_t i) {
  return getToken(CParser::RBRACK, i);
}

std::vector<CParser::ExprContext *> CParser::PreIncDecIndexExprContext::expr() {
  return getRuleContexts<CParser::ExprContext>();
}

CParser::ExprContext* CParser::PreIncDecIndexExprContext::expr(size_t i) {
  return getRuleContext<CParser::ExprContext>(i);
}

CParser::PreIncDecIndexExprContext::PreIncDecIndexExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::PreIncDecIndexExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitPreIncDecIndexExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ParenExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::ParenExprContext::LPAREN() {
  return getToken(CParser::LPAREN, 0);
}

CParser::ExprContext* CParser::ParenExprContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

tree::TerminalNode* CParser::ParenExprContext::RPAREN() {
  return getToken(CParser::RPAREN, 0);
}

CParser::ParenExprContext::ParenExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::ParenExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitParenExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- BitXorExprContext ------------------------------------------------------------------

std::vector<CParser::CondExprContext *> CParser::BitXorExprContext::condExpr() {
  return getRuleContexts<CParser::CondExprContext>();
}

CParser::CondExprContext* CParser::BitXorExprContext::condExpr(size_t i) {
  return getRuleContext<CParser::CondExprContext>(i);
}

tree::TerminalNode* CParser::BitXorExprContext::CARET() {
  return getToken(CParser::CARET, 0);
}

CParser::BitXorExprContext::BitXorExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::BitXorExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitBitXorExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PostIncDecDerefExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::PostIncDecDerefExprContext::STAR() {
  return getToken(CParser::STAR, 0);
}

CParser::CondExprContext* CParser::PostIncDecDerefExprContext::condExpr() {
  return getRuleContext<CParser::CondExprContext>(0);
}

tree::TerminalNode* CParser::PostIncDecDerefExprContext::INC() {
  return getToken(CParser::INC, 0);
}

tree::TerminalNode* CParser::PostIncDecDerefExprContext::DEC() {
  return getToken(CParser::DEC, 0);
}

CParser::PostIncDecDerefExprContext::PostIncDecDerefExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::PostIncDecDerefExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitPostIncDecDerefExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- UnaryExprContext ------------------------------------------------------------------

CParser::CondExprContext* CParser::UnaryExprContext::condExpr() {
  return getRuleContext<CParser::CondExprContext>(0);
}

tree::TerminalNode* CParser::UnaryExprContext::MINUS() {
  return getToken(CParser::MINUS, 0);
}

tree::TerminalNode* CParser::UnaryExprContext::BANG() {
  return getToken(CParser::BANG, 0);
}

tree::TerminalNode* CParser::UnaryExprContext::TILDE() {
  return getToken(CParser::TILDE, 0);
}

CParser::UnaryExprContext::UnaryExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::UnaryExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitUnaryExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PostIncDecIndexExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::PostIncDecIndexExprContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::PostIncDecIndexExprContext::INC() {
  return getToken(CParser::INC, 0);
}

tree::TerminalNode* CParser::PostIncDecIndexExprContext::DEC() {
  return getToken(CParser::DEC, 0);
}

std::vector<tree::TerminalNode *> CParser::PostIncDecIndexExprContext::LBRACK() {
  return getTokens(CParser::LBRACK);
}

tree::TerminalNode* CParser::PostIncDecIndexExprContext::LBRACK(size_t i) {
  return getToken(CParser::LBRACK, i);
}

std::vector<tree::TerminalNode *> CParser::PostIncDecIndexExprContext::RBRACK() {
  return getTokens(CParser::RBRACK);
}

tree::TerminalNode* CParser::PostIncDecIndexExprContext::RBRACK(size_t i) {
  return getToken(CParser::RBRACK, i);
}

std::vector<CParser::ExprContext *> CParser::PostIncDecIndexExprContext::expr() {
  return getRuleContexts<CParser::ExprContext>();
}

CParser::ExprContext* CParser::PostIncDecIndexExprContext::expr(size_t i) {
  return getRuleContext<CParser::ExprContext>(i);
}

CParser::PostIncDecIndexExprContext::PostIncDecIndexExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::PostIncDecIndexExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitPostIncDecIndexExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- TernaryExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::TernaryExprContext::QUESTION() {
  return getToken(CParser::QUESTION, 0);
}

tree::TerminalNode* CParser::TernaryExprContext::COLON() {
  return getToken(CParser::COLON, 0);
}

std::vector<CParser::CondExprContext *> CParser::TernaryExprContext::condExpr() {
  return getRuleContexts<CParser::CondExprContext>();
}

CParser::CondExprContext* CParser::TernaryExprContext::condExpr(size_t i) {
  return getRuleContext<CParser::CondExprContext>(i);
}

CParser::ExprContext* CParser::TernaryExprContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

CParser::TernaryExprContext::TernaryExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::TernaryExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitTernaryExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- MulExprContext ------------------------------------------------------------------

std::vector<CParser::CondExprContext *> CParser::MulExprContext::condExpr() {
  return getRuleContexts<CParser::CondExprContext>();
}

CParser::CondExprContext* CParser::MulExprContext::condExpr(size_t i) {
  return getRuleContext<CParser::CondExprContext>(i);
}

tree::TerminalNode* CParser::MulExprContext::STAR() {
  return getToken(CParser::STAR, 0);
}

tree::TerminalNode* CParser::MulExprContext::SLASH() {
  return getToken(CParser::SLASH, 0);
}

tree::TerminalNode* CParser::MulExprContext::PERCENT() {
  return getToken(CParser::PERCENT, 0);
}

CParser::MulExprContext::MulExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::MulExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitMulExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- MemberAccessExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::MemberAccessExprContext::DOT() {
  return getToken(CParser::DOT, 0);
}

CParser::CondExprContext* CParser::MemberAccessExprContext::condExpr() {
  return getRuleContext<CParser::CondExprContext>(0);
}

tree::TerminalNode* CParser::MemberAccessExprContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::MemberAccessExprContext::LBRACK() {
  return getToken(CParser::LBRACK, 0);
}

tree::TerminalNode* CParser::MemberAccessExprContext::RBRACK() {
  return getToken(CParser::RBRACK, 0);
}

CParser::ExprContext* CParser::MemberAccessExprContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

CParser::MemberAccessExprContext::MemberAccessExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::MemberAccessExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitMemberAccessExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- CallExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::CallExprContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::CallExprContext::LPAREN() {
  return getToken(CParser::LPAREN, 0);
}

tree::TerminalNode* CParser::CallExprContext::RPAREN() {
  return getToken(CParser::RPAREN, 0);
}

CParser::ArgListContext* CParser::CallExprContext::argList() {
  return getRuleContext<CParser::ArgListContext>(0);
}

CParser::CallExprContext::CallExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::CallExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitCallExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- RelExprContext ------------------------------------------------------------------

std::vector<CParser::CondExprContext *> CParser::RelExprContext::condExpr() {
  return getRuleContexts<CParser::CondExprContext>();
}

CParser::CondExprContext* CParser::RelExprContext::condExpr(size_t i) {
  return getRuleContext<CParser::CondExprContext>(i);
}

tree::TerminalNode* CParser::RelExprContext::LT() {
  return getToken(CParser::LT, 0);
}

tree::TerminalNode* CParser::RelExprContext::LE() {
  return getToken(CParser::LE, 0);
}

tree::TerminalNode* CParser::RelExprContext::GT() {
  return getToken(CParser::GT, 0);
}

tree::TerminalNode* CParser::RelExprContext::GE() {
  return getToken(CParser::GE, 0);
}

CParser::RelExprContext::RelExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::RelExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitRelExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- BitAndExprContext ------------------------------------------------------------------

std::vector<CParser::CondExprContext *> CParser::BitAndExprContext::condExpr() {
  return getRuleContexts<CParser::CondExprContext>();
}

CParser::CondExprContext* CParser::BitAndExprContext::condExpr(size_t i) {
  return getRuleContext<CParser::CondExprContext>(i);
}

tree::TerminalNode* CParser::BitAndExprContext::AMP() {
  return getToken(CParser::AMP, 0);
}

CParser::BitAndExprContext::BitAndExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::BitAndExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitBitAndExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- FloatNumExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::FloatNumExprContext::FLOAT_NUMBER() {
  return getToken(CParser::FLOAT_NUMBER, 0);
}

CParser::FloatNumExprContext::FloatNumExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::FloatNumExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitFloatNumExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- CharExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::CharExprContext::CHARLIT() {
  return getToken(CParser::CHARLIT, 0);
}

CParser::CharExprContext::CharExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::CharExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitCharExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- SizeofTypeExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::SizeofTypeExprContext::SIZEOF_KW() {
  return getToken(CParser::SIZEOF_KW, 0);
}

tree::TerminalNode* CParser::SizeofTypeExprContext::LPAREN() {
  return getToken(CParser::LPAREN, 0);
}

CParser::TypeSpecContext* CParser::SizeofTypeExprContext::typeSpec() {
  return getRuleContext<CParser::TypeSpecContext>(0);
}

tree::TerminalNode* CParser::SizeofTypeExprContext::RPAREN() {
  return getToken(CParser::RPAREN, 0);
}

CParser::SizeofTypeExprContext::SizeofTypeExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::SizeofTypeExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitSizeofTypeExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AddrOfExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::AddrOfExprContext::AMP() {
  return getToken(CParser::AMP, 0);
}

CParser::CondExprContext* CParser::AddrOfExprContext::condExpr() {
  return getRuleContext<CParser::CondExprContext>(0);
}

CParser::AddrOfExprContext::AddrOfExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::AddrOfExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitAddrOfExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- OrExprContext ------------------------------------------------------------------

std::vector<CParser::CondExprContext *> CParser::OrExprContext::condExpr() {
  return getRuleContexts<CParser::CondExprContext>();
}

CParser::CondExprContext* CParser::OrExprContext::condExpr(size_t i) {
  return getRuleContext<CParser::CondExprContext>(i);
}

tree::TerminalNode* CParser::OrExprContext::OROR() {
  return getToken(CParser::OROR, 0);
}

CParser::OrExprContext::OrExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::OrExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitOrExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- MemberAccessPtrExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::MemberAccessPtrExprContext::ARROW() {
  return getToken(CParser::ARROW, 0);
}

CParser::CondExprContext* CParser::MemberAccessPtrExprContext::condExpr() {
  return getRuleContext<CParser::CondExprContext>(0);
}

tree::TerminalNode* CParser::MemberAccessPtrExprContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::MemberAccessPtrExprContext::LBRACK() {
  return getToken(CParser::LBRACK, 0);
}

tree::TerminalNode* CParser::MemberAccessPtrExprContext::RBRACK() {
  return getToken(CParser::RBRACK, 0);
}

CParser::ExprContext* CParser::MemberAccessPtrExprContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

CParser::MemberAccessPtrExprContext::MemberAccessPtrExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::MemberAccessPtrExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitMemberAccessPtrExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- CallPtrExprContext ------------------------------------------------------------------

std::vector<tree::TerminalNode *> CParser::CallPtrExprContext::LPAREN() {
  return getTokens(CParser::LPAREN);
}

tree::TerminalNode* CParser::CallPtrExprContext::LPAREN(size_t i) {
  return getToken(CParser::LPAREN, i);
}

CParser::ExprContext* CParser::CallPtrExprContext::expr() {
  return getRuleContext<CParser::ExprContext>(0);
}

std::vector<tree::TerminalNode *> CParser::CallPtrExprContext::RPAREN() {
  return getTokens(CParser::RPAREN);
}

tree::TerminalNode* CParser::CallPtrExprContext::RPAREN(size_t i) {
  return getToken(CParser::RPAREN, i);
}

CParser::ArgListContext* CParser::CallPtrExprContext::argList() {
  return getRuleContext<CParser::ArgListContext>(0);
}

CParser::CallPtrExprContext::CallPtrExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::CallPtrExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitCallPtrExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- NumExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::NumExprContext::NUMBER() {
  return getToken(CParser::NUMBER, 0);
}

CParser::NumExprContext::NumExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::NumExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitNumExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ShiftExprContext ------------------------------------------------------------------

std::vector<CParser::CondExprContext *> CParser::ShiftExprContext::condExpr() {
  return getRuleContexts<CParser::CondExprContext>();
}

CParser::CondExprContext* CParser::ShiftExprContext::condExpr(size_t i) {
  return getRuleContext<CParser::CondExprContext>(i);
}

tree::TerminalNode* CParser::ShiftExprContext::SHL() {
  return getToken(CParser::SHL, 0);
}

tree::TerminalNode* CParser::ShiftExprContext::SHR() {
  return getToken(CParser::SHR, 0);
}

CParser::ShiftExprContext::ShiftExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::ShiftExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitShiftExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- IndexExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::IndexExprContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

std::vector<tree::TerminalNode *> CParser::IndexExprContext::LBRACK() {
  return getTokens(CParser::LBRACK);
}

tree::TerminalNode* CParser::IndexExprContext::LBRACK(size_t i) {
  return getToken(CParser::LBRACK, i);
}

std::vector<tree::TerminalNode *> CParser::IndexExprContext::RBRACK() {
  return getTokens(CParser::RBRACK);
}

tree::TerminalNode* CParser::IndexExprContext::RBRACK(size_t i) {
  return getToken(CParser::RBRACK, i);
}

std::vector<CParser::ExprContext *> CParser::IndexExprContext::expr() {
  return getRuleContexts<CParser::ExprContext>();
}

CParser::ExprContext* CParser::IndexExprContext::expr(size_t i) {
  return getRuleContext<CParser::ExprContext>(i);
}

CParser::IndexExprContext::IndexExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::IndexExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitIndexExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PreIncDecVarExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::PreIncDecVarExprContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

tree::TerminalNode* CParser::PreIncDecVarExprContext::INC() {
  return getToken(CParser::INC, 0);
}

tree::TerminalNode* CParser::PreIncDecVarExprContext::DEC() {
  return getToken(CParser::DEC, 0);
}

CParser::PreIncDecVarExprContext::PreIncDecVarExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::PreIncDecVarExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitPreIncDecVarExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- VarExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::VarExprContext::IDENT() {
  return getToken(CParser::IDENT, 0);
}

CParser::VarExprContext::VarExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::VarExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitVarExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- EqExprContext ------------------------------------------------------------------

std::vector<CParser::CondExprContext *> CParser::EqExprContext::condExpr() {
  return getRuleContexts<CParser::CondExprContext>();
}

CParser::CondExprContext* CParser::EqExprContext::condExpr(size_t i) {
  return getRuleContext<CParser::CondExprContext>(i);
}

tree::TerminalNode* CParser::EqExprContext::EQ() {
  return getToken(CParser::EQ, 0);
}

tree::TerminalNode* CParser::EqExprContext::NE() {
  return getToken(CParser::NE, 0);
}

CParser::EqExprContext::EqExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::EqExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitEqExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- BitOrExprContext ------------------------------------------------------------------

std::vector<CParser::CondExprContext *> CParser::BitOrExprContext::condExpr() {
  return getRuleContexts<CParser::CondExprContext>();
}

CParser::CondExprContext* CParser::BitOrExprContext::condExpr(size_t i) {
  return getRuleContext<CParser::CondExprContext>(i);
}

tree::TerminalNode* CParser::BitOrExprContext::PIPE() {
  return getToken(CParser::PIPE, 0);
}

CParser::BitOrExprContext::BitOrExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::BitOrExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitBitOrExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AddExprContext ------------------------------------------------------------------

std::vector<CParser::CondExprContext *> CParser::AddExprContext::condExpr() {
  return getRuleContexts<CParser::CondExprContext>();
}

CParser::CondExprContext* CParser::AddExprContext::condExpr(size_t i) {
  return getRuleContext<CParser::CondExprContext>(i);
}

tree::TerminalNode* CParser::AddExprContext::PLUS() {
  return getToken(CParser::PLUS, 0);
}

tree::TerminalNode* CParser::AddExprContext::MINUS() {
  return getToken(CParser::MINUS, 0);
}

CParser::AddExprContext::AddExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::AddExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitAddExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- DerefExprContext ------------------------------------------------------------------

tree::TerminalNode* CParser::DerefExprContext::STAR() {
  return getToken(CParser::STAR, 0);
}

CParser::CondExprContext* CParser::DerefExprContext::condExpr() {
  return getRuleContext<CParser::CondExprContext>(0);
}

CParser::DerefExprContext::DerefExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::DerefExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitDerefExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AndExprContext ------------------------------------------------------------------

std::vector<CParser::CondExprContext *> CParser::AndExprContext::condExpr() {
  return getRuleContexts<CParser::CondExprContext>();
}

CParser::CondExprContext* CParser::AndExprContext::condExpr(size_t i) {
  return getRuleContext<CParser::CondExprContext>(i);
}

tree::TerminalNode* CParser::AndExprContext::ANDAND() {
  return getToken(CParser::ANDAND, 0);
}

CParser::AndExprContext::AndExprContext(CondExprContext *ctx) { copyFrom(ctx); }


std::any CParser::AndExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitAndExpr(this);
  else
    return visitor->visitChildren(this);
}

CParser::CondExprContext* CParser::condExpr() {
   return condExpr(0);
}

CParser::CondExprContext* CParser::condExpr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  CParser::CondExprContext *_localctx = _tracker.createInstance<CondExprContext>(_ctx, parentState);
  CParser::CondExprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 50;
  enterRecursionRule(_localctx, 50, CParser::RuleCondExpr, precedence);

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
    setState(637);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 90, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<PostIncDecVarExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;

      setState(551);
      match(CParser::IDENT);
      setState(552);
      antlrcpp::downCast<PostIncDecVarExprContext *>(_localctx)->op = _input->LT(1);
      _la = _input->LA(1);
      if (!(_la == CParser::INC

      || _la == CParser::DEC)) {
        antlrcpp::downCast<PostIncDecVarExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<PostIncDecIndexExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(553);
      match(CParser::IDENT);
      setState(558); 
      _errHandler->sync(this);
      _la = _input->LA(1);
      do {
        setState(554);
        match(CParser::LBRACK);
        setState(555);
        antlrcpp::downCast<PostIncDecIndexExprContext *>(_localctx)->exprContext = expr();
        antlrcpp::downCast<PostIncDecIndexExprContext *>(_localctx)->idx.push_back(antlrcpp::downCast<PostIncDecIndexExprContext *>(_localctx)->exprContext);
        setState(556);
        match(CParser::RBRACK);
        setState(560); 
        _errHandler->sync(this);
        _la = _input->LA(1);
      } while (_la == CParser::LBRACK);
      setState(562);
      antlrcpp::downCast<PostIncDecIndexExprContext *>(_localctx)->op = _input->LT(1);
      _la = _input->LA(1);
      if (!(_la == CParser::INC

      || _la == CParser::DEC)) {
        antlrcpp::downCast<PostIncDecIndexExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<PostIncDecDerefExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(564);
      match(CParser::STAR);
      setState(565);
      antlrcpp::downCast<PostIncDecDerefExprContext *>(_localctx)->ptr = condExpr(0);
      setState(566);
      antlrcpp::downCast<PostIncDecDerefExprContext *>(_localctx)->op = _input->LT(1);
      _la = _input->LA(1);
      if (!(_la == CParser::INC

      || _la == CParser::DEC)) {
        antlrcpp::downCast<PostIncDecDerefExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<CallExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(568);
      match(CParser::IDENT);
      setState(569);
      match(CParser::LPAREN);
      setState(571);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8521483551744) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 68)) & 1987) != 0)) {
        setState(570);
        argList();
      }
      setState(573);
      match(CParser::RPAREN);
      break;
    }

    case 5: {
      _localctx = _tracker.createInstance<CallPtrExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(574);
      match(CParser::LPAREN);
      setState(575);
      expr();
      setState(576);
      match(CParser::RPAREN);
      setState(577);
      match(CParser::LPAREN);
      setState(579);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if ((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 8521483551744) != 0) || ((((_la - 68) & ~ 0x3fULL) == 0) &&
        ((1ULL << (_la - 68)) & 1987) != 0)) {
        setState(578);
        argList();
      }
      setState(581);
      match(CParser::RPAREN);
      break;
    }

    case 6: {
      _localctx = _tracker.createInstance<IndexExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(583);
      match(CParser::IDENT);
      setState(588); 
      _errHandler->sync(this);
      alt = 1;
      do {
        switch (alt) {
          case 1: {
                setState(584);
                match(CParser::LBRACK);
                setState(585);
                antlrcpp::downCast<IndexExprContext *>(_localctx)->exprContext = expr();
                antlrcpp::downCast<IndexExprContext *>(_localctx)->idx.push_back(antlrcpp::downCast<IndexExprContext *>(_localctx)->exprContext);
                setState(586);
                match(CParser::RBRACK);
                break;
              }

        default:
          throw NoViableAltException(this);
        }
        setState(590); 
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 88, _ctx);
      } while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER);
      break;
    }

    case 7: {
      _localctx = _tracker.createInstance<VarExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(592);
      match(CParser::IDENT);
      break;
    }

    case 8: {
      _localctx = _tracker.createInstance<NumExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(593);
      match(CParser::NUMBER);
      break;
    }

    case 9: {
      _localctx = _tracker.createInstance<FloatNumExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(594);
      match(CParser::FLOAT_NUMBER);
      break;
    }

    case 10: {
      _localctx = _tracker.createInstance<CharExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(595);
      match(CParser::CHARLIT);
      break;
    }

    case 11: {
      _localctx = _tracker.createInstance<StringLitExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(596);
      match(CParser::STRINGLIT);
      break;
    }

    case 12: {
      _localctx = _tracker.createInstance<ParenExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(597);
      match(CParser::LPAREN);
      setState(598);
      expr();
      setState(599);
      match(CParser::RPAREN);
      break;
    }

    case 13: {
      _localctx = _tracker.createInstance<CastExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(601);
      match(CParser::LPAREN);
      setState(602);
      typeSpec();
      setState(603);
      match(CParser::RPAREN);
      setState(604);
      condExpr(22);
      break;
    }

    case 14: {
      _localctx = _tracker.createInstance<SizeofTypeExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(606);
      match(CParser::SIZEOF_KW);
      setState(607);
      match(CParser::LPAREN);
      setState(608);
      typeSpec();
      setState(609);
      match(CParser::RPAREN);
      break;
    }

    case 15: {
      _localctx = _tracker.createInstance<SizeofExprExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(611);
      match(CParser::SIZEOF_KW);
      setState(612);
      match(CParser::LPAREN);
      setState(613);
      expr();
      setState(614);
      match(CParser::RPAREN);
      break;
    }

    case 16: {
      _localctx = _tracker.createInstance<DerefExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(616);
      match(CParser::STAR);
      setState(617);
      condExpr(19);
      break;
    }

    case 17: {
      _localctx = _tracker.createInstance<AddrOfExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(618);
      match(CParser::AMP);
      setState(619);
      condExpr(18);
      break;
    }

    case 18: {
      _localctx = _tracker.createInstance<UnaryExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(620);
      antlrcpp::downCast<UnaryExprContext *>(_localctx)->op = _input->LT(1);
      _la = _input->LA(1);
      if (!((((_la & ~ 0x3fULL) == 0) &&
        ((1ULL << _la) & 7696581394432) != 0))) {
        antlrcpp::downCast<UnaryExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(621);
      condExpr(15);
      break;
    }

    case 19: {
      _localctx = _tracker.createInstance<PreIncDecVarExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(622);
      antlrcpp::downCast<PreIncDecVarExprContext *>(_localctx)->op = _input->LT(1);
      _la = _input->LA(1);
      if (!(_la == CParser::INC

      || _la == CParser::DEC)) {
        antlrcpp::downCast<PreIncDecVarExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(623);
      match(CParser::IDENT);
      break;
    }

    case 20: {
      _localctx = _tracker.createInstance<PreIncDecIndexExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(624);
      antlrcpp::downCast<PreIncDecIndexExprContext *>(_localctx)->op = _input->LT(1);
      _la = _input->LA(1);
      if (!(_la == CParser::INC

      || _la == CParser::DEC)) {
        antlrcpp::downCast<PreIncDecIndexExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(625);
      match(CParser::IDENT);
      setState(630); 
      _errHandler->sync(this);
      alt = 1;
      do {
        switch (alt) {
          case 1: {
                setState(626);
                match(CParser::LBRACK);
                setState(627);
                antlrcpp::downCast<PreIncDecIndexExprContext *>(_localctx)->exprContext = expr();
                antlrcpp::downCast<PreIncDecIndexExprContext *>(_localctx)->idx.push_back(antlrcpp::downCast<PreIncDecIndexExprContext *>(_localctx)->exprContext);
                setState(628);
                match(CParser::RBRACK);
                break;
              }

        default:
          throw NoViableAltException(this);
        }
        setState(632); 
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 89, _ctx);
      } while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER);
      break;
    }

    case 21: {
      _localctx = _tracker.createInstance<PreIncDecDerefExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(634);
      antlrcpp::downCast<PreIncDecDerefExprContext *>(_localctx)->op = _input->LT(1);
      _la = _input->LA(1);
      if (!(_la == CParser::INC

      || _la == CParser::DEC)) {
        antlrcpp::downCast<PreIncDecDerefExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(635);
      match(CParser::STAR);
      setState(636);
      antlrcpp::downCast<PreIncDecDerefExprContext *>(_localctx)->ptr = condExpr(12);
      break;
    }

    default:
      break;
    }
    _ctx->stop = _input->LT(-1);
    setState(695);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 94, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(693);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 93, _ctx)) {
        case 1: {
          auto newContext = _tracker.createInstance<MulExprContext>(_tracker.createInstance<CondExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleCondExpr);
          setState(639);

          if (!(precpred(_ctx, 11))) throw FailedPredicateException(this, "precpred(_ctx, 11)");
          setState(640);
          antlrcpp::downCast<MulExprContext *>(_localctx)->op = _input->LT(1);
          _la = _input->LA(1);
          if (!((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & 26663156973568) != 0))) {
            antlrcpp::downCast<MulExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
          }
          else {
            _errHandler->reportMatch(this);
            consume();
          }
          setState(641);
          condExpr(12);
          break;
        }

        case 2: {
          auto newContext = _tracker.createInstance<AddExprContext>(_tracker.createInstance<CondExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleCondExpr);
          setState(642);

          if (!(precpred(_ctx, 10))) throw FailedPredicateException(this, "precpred(_ctx, 10)");
          setState(643);
          antlrcpp::downCast<AddExprContext *>(_localctx)->op = _input->LT(1);
          _la = _input->LA(1);
          if (!(_la == CParser::MINUS

          || _la == CParser::PLUS)) {
            antlrcpp::downCast<AddExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
          }
          else {
            _errHandler->reportMatch(this);
            consume();
          }
          setState(644);
          condExpr(11);
          break;
        }

        case 3: {
          auto newContext = _tracker.createInstance<ShiftExprContext>(_tracker.createInstance<CondExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleCondExpr);
          setState(645);

          if (!(precpred(_ctx, 9))) throw FailedPredicateException(this, "precpred(_ctx, 9)");
          setState(646);
          antlrcpp::downCast<ShiftExprContext *>(_localctx)->op = _input->LT(1);
          _la = _input->LA(1);
          if (!(_la == CParser::SHL

          || _la == CParser::SHR)) {
            antlrcpp::downCast<ShiftExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
          }
          else {
            _errHandler->reportMatch(this);
            consume();
          }
          setState(647);
          condExpr(10);
          break;
        }

        case 4: {
          auto newContext = _tracker.createInstance<RelExprContext>(_tracker.createInstance<CondExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleCondExpr);
          setState(648);

          if (!(precpred(_ctx, 8))) throw FailedPredicateException(this, "precpred(_ctx, 8)");
          setState(649);
          antlrcpp::downCast<RelExprContext *>(_localctx)->op = _input->LT(1);
          _la = _input->LA(1);
          if (!((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & 4222124650659840) != 0))) {
            antlrcpp::downCast<RelExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
          }
          else {
            _errHandler->reportMatch(this);
            consume();
          }
          setState(650);
          condExpr(9);
          break;
        }

        case 5: {
          auto newContext = _tracker.createInstance<EqExprContext>(_tracker.createInstance<CondExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleCondExpr);
          setState(651);

          if (!(precpred(_ctx, 7))) throw FailedPredicateException(this, "precpred(_ctx, 7)");
          setState(652);
          antlrcpp::downCast<EqExprContext *>(_localctx)->op = _input->LT(1);
          _la = _input->LA(1);
          if (!(_la == CParser::EQ

          || _la == CParser::NE)) {
            antlrcpp::downCast<EqExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
          }
          else {
            _errHandler->reportMatch(this);
            consume();
          }
          setState(653);
          condExpr(8);
          break;
        }

        case 6: {
          auto newContext = _tracker.createInstance<BitAndExprContext>(_tracker.createInstance<CondExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleCondExpr);
          setState(654);

          if (!(precpred(_ctx, 6))) throw FailedPredicateException(this, "precpred(_ctx, 6)");
          setState(655);
          match(CParser::AMP);
          setState(656);
          condExpr(7);
          break;
        }

        case 7: {
          auto newContext = _tracker.createInstance<BitXorExprContext>(_tracker.createInstance<CondExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleCondExpr);
          setState(657);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(658);
          match(CParser::CARET);
          setState(659);
          condExpr(6);
          break;
        }

        case 8: {
          auto newContext = _tracker.createInstance<BitOrExprContext>(_tracker.createInstance<CondExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleCondExpr);
          setState(660);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(661);
          match(CParser::PIPE);
          setState(662);
          condExpr(5);
          break;
        }

        case 9: {
          auto newContext = _tracker.createInstance<AndExprContext>(_tracker.createInstance<CondExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleCondExpr);
          setState(663);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(664);
          match(CParser::ANDAND);
          setState(665);
          condExpr(4);
          break;
        }

        case 10: {
          auto newContext = _tracker.createInstance<OrExprContext>(_tracker.createInstance<CondExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleCondExpr);
          setState(666);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(667);
          match(CParser::OROR);
          setState(668);
          condExpr(3);
          break;
        }

        case 11: {
          auto newContext = _tracker.createInstance<TernaryExprContext>(_tracker.createInstance<CondExprContext>(parentContext, parentState));
          _localctx = newContext;
          newContext->cond = previousContext;
          pushNewRecursionContext(newContext, startState, RuleCondExpr);
          setState(669);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(670);
          match(CParser::QUESTION);
          setState(671);
          antlrcpp::downCast<TernaryExprContext *>(_localctx)->then = expr();
          setState(672);
          match(CParser::COLON);
          setState(673);
          antlrcpp::downCast<TernaryExprContext *>(_localctx)->els = condExpr(1);
          break;
        }

        case 12: {
          auto newContext = _tracker.createInstance<MemberAccessExprContext>(_tracker.createInstance<CondExprContext>(parentContext, parentState));
          _localctx = newContext;
          newContext->base = previousContext;
          pushNewRecursionContext(newContext, startState, RuleCondExpr);
          setState(675);

          if (!(precpred(_ctx, 17))) throw FailedPredicateException(this, "precpred(_ctx, 17)");
          setState(676);
          match(CParser::DOT);
          setState(677);
          antlrcpp::downCast<MemberAccessExprContext *>(_localctx)->name = match(CParser::IDENT);
          setState(682);
          _errHandler->sync(this);

          switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 91, _ctx)) {
          case 1: {
            setState(678);
            match(CParser::LBRACK);
            setState(679);
            antlrcpp::downCast<MemberAccessExprContext *>(_localctx)->idx = expr();
            setState(680);
            match(CParser::RBRACK);
            break;
          }

          default:
            break;
          }
          break;
        }

        case 13: {
          auto newContext = _tracker.createInstance<MemberAccessPtrExprContext>(_tracker.createInstance<CondExprContext>(parentContext, parentState));
          _localctx = newContext;
          newContext->base = previousContext;
          pushNewRecursionContext(newContext, startState, RuleCondExpr);
          setState(684);

          if (!(precpred(_ctx, 16))) throw FailedPredicateException(this, "precpred(_ctx, 16)");
          setState(685);
          match(CParser::ARROW);
          setState(686);
          antlrcpp::downCast<MemberAccessPtrExprContext *>(_localctx)->name = match(CParser::IDENT);
          setState(691);
          _errHandler->sync(this);

          switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 92, _ctx)) {
          case 1: {
            setState(687);
            match(CParser::LBRACK);
            setState(688);
            antlrcpp::downCast<MemberAccessPtrExprContext *>(_localctx)->idx = expr();
            setState(689);
            match(CParser::RBRACK);
            break;
          }

          default:
            break;
          }
          break;
        }

        default:
          break;
        } 
      }
      setState(697);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 94, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

//----------------- ArgListContext ------------------------------------------------------------------

CParser::ArgListContext::ArgListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<CParser::ExprContext *> CParser::ArgListContext::expr() {
  return getRuleContexts<CParser::ExprContext>();
}

CParser::ExprContext* CParser::ArgListContext::expr(size_t i) {
  return getRuleContext<CParser::ExprContext>(i);
}

std::vector<tree::TerminalNode *> CParser::ArgListContext::COMMA() {
  return getTokens(CParser::COMMA);
}

tree::TerminalNode* CParser::ArgListContext::COMMA(size_t i) {
  return getToken(CParser::COMMA, i);
}


size_t CParser::ArgListContext::getRuleIndex() const {
  return CParser::RuleArgList;
}


std::any CParser::ArgListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<CParserVisitor*>(visitor))
    return parserVisitor->visitArgList(this);
  else
    return visitor->visitChildren(this);
}

CParser::ArgListContext* CParser::argList() {
  ArgListContext *_localctx = _tracker.createInstance<ArgListContext>(_ctx, getState());
  enterRule(_localctx, 52, CParser::RuleArgList);
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
    setState(698);
    expr();
    setState(703);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == CParser::COMMA) {
      setState(699);
      match(CParser::COMMA);
      setState(700);
      expr();
      setState(705);
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

bool CParser::sempred(RuleContext *context, size_t ruleIndex, size_t predicateIndex) {
  switch (ruleIndex) {
    case 25: return condExprSempred(antlrcpp::downCast<CondExprContext *>(context), predicateIndex);

  default:
    break;
  }
  return true;
}

bool CParser::condExprSempred(CondExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 0: return precpred(_ctx, 11);
    case 1: return precpred(_ctx, 10);
    case 2: return precpred(_ctx, 9);
    case 3: return precpred(_ctx, 8);
    case 4: return precpred(_ctx, 7);
    case 5: return precpred(_ctx, 6);
    case 6: return precpred(_ctx, 5);
    case 7: return precpred(_ctx, 4);
    case 8: return precpred(_ctx, 3);
    case 9: return precpred(_ctx, 2);
    case 10: return precpred(_ctx, 1);
    case 11: return precpred(_ctx, 17);
    case 12: return precpred(_ctx, 16);

  default:
    break;
  }
  return true;
}

void CParser::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  cparserParserInitialize();
#else
  ::antlr4::internal::call_once(cparserParserOnceFlag, cparserParserInitialize);
#endif
}
