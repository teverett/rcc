
// Generated from /Users/tom/projects/pcc/grammar/pascal/PascalParser.g4 by ANTLR 4.13.2


#include "PascalParserVisitor.h"

#include "PascalParser.h"


using namespace antlrcpp;

using namespace antlr4;

namespace {

struct PascalParserStaticData final {
  PascalParserStaticData(std::vector<std::string> ruleNames,
                        std::vector<std::string> literalNames,
                        std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  PascalParserStaticData(const PascalParserStaticData&) = delete;
  PascalParserStaticData(PascalParserStaticData&&) = delete;
  PascalParserStaticData& operator=(const PascalParserStaticData&) = delete;
  PascalParserStaticData& operator=(PascalParserStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag pascalparserParserOnceFlag;
#if ANTLR4_USE_THREAD_LOCAL_CACHE
static thread_local
#endif
std::unique_ptr<PascalParserStaticData> pascalparserParserStaticData = nullptr;

void pascalparserParserInitialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  if (pascalparserParserStaticData != nullptr) {
    return;
  }
#else
  assert(pascalparserParserStaticData == nullptr);
#endif
  auto staticData = std::make_unique<PascalParserStaticData>(
    std::vector<std::string>{
      "program", "block", "declPart", "constDecl", "typeDecl", "varDecl", 
      "identList", "typeSpec", "rangeSpec", "fieldList", "procDecl", "funcDecl", 
      "formalParams", "formalParamGroup", "compoundStmt", "statement", "caseBranch", 
      "designator", "exprList", "expr", "relOp", "simpleExpr", "addOp", 
      "term", "mulOp", "factor", "setElement", "constExpr"
    },
    std::vector<std::string>{
      "", "'program'", "'var'", "'const'", "'type'", "'array'", "'of'", 
      "'record'", "'set'", "'procedure'", "'function'", "'begin'", "'end'", 
      "'if'", "'then'", "'else'", "'while'", "'do'", "'repeat'", "'until'", 
      "'for'", "'to'", "'downto'", "'case'", "'not'", "'and'", "'or'", "'in'", 
      "'div'", "'mod'", "'integer'", "'real'", "'char'", "'boolean'", "'true'", 
      "'false'", "':='", "'..'", "'<='", "'>='", "'<>'", "';'", "':'", "','", 
      "'.'", "'('", "')'", "'['", "']'", "'='", "'<'", "'>'", "'+'", "'-'", 
      "'*'", "'/'", "'^'"
    },
    std::vector<std::string>{
      "", "PROGRAM_KW", "VAR_KW", "CONST_KW", "TYPE_KW", "ARRAY_KW", "OF_KW", 
      "RECORD_KW", "SET_KW", "PROCEDURE_KW", "FUNCTION_KW", "BEGIN_KW", 
      "END_KW", "IF_KW", "THEN_KW", "ELSE_KW", "WHILE_KW", "DO_KW", "REPEAT_KW", 
      "UNTIL_KW", "FOR_KW", "TO_KW", "DOWNTO_KW", "CASE_KW", "NOT_KW", "AND_KW", 
      "OR_KW", "IN_KW", "DIV_KW", "MOD_KW", "INTEGER_KW", "REAL_KW", "CHAR_KW", 
      "BOOLEAN_KW", "TRUE_KW", "FALSE_KW", "ASSIGN", "DOTDOT", "LE", "GE", 
      "NE", "SEMI", "COLON", "COMMA", "DOT", "LPAREN", "RPAREN", "LBRACK", 
      "RBRACK", "EQ", "LT", "GT", "PLUS", "MINUS", "STAR", "SLASH", "CARET", 
      "IDENT", "REALLIT", "INTLIT", "STRINGLIT", "BRACE_COMMENT", "PAREN_COMMENT", 
      "WS"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,63,402,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,2,5,7,5,2,6,7,6,2,
  	7,7,7,2,8,7,8,2,9,7,9,2,10,7,10,2,11,7,11,2,12,7,12,2,13,7,13,2,14,7,
  	14,2,15,7,15,2,16,7,16,2,17,7,17,2,18,7,18,2,19,7,19,2,20,7,20,2,21,7,
  	21,2,22,7,22,2,23,7,23,2,24,7,24,2,25,7,25,2,26,7,26,2,27,7,27,1,0,1,
  	0,1,0,1,0,1,0,1,0,1,0,1,1,5,1,65,8,1,10,1,12,1,68,9,1,1,1,1,1,1,2,1,2,
  	1,2,1,2,1,2,3,2,77,8,2,1,3,1,3,1,3,1,3,1,3,1,3,4,3,85,8,3,11,3,12,3,86,
  	1,4,1,4,1,4,1,4,1,4,1,4,4,4,95,8,4,11,4,12,4,96,1,5,1,5,1,5,1,5,1,5,1,
  	5,4,5,105,8,5,11,5,12,5,106,1,6,1,6,1,6,5,6,112,8,6,10,6,12,6,115,9,6,
  	1,7,1,7,1,7,1,7,1,7,1,7,1,7,1,7,1,7,1,7,1,7,5,7,128,8,7,10,7,12,7,131,
  	9,7,1,7,1,7,1,7,1,7,1,7,1,7,1,7,1,7,1,7,1,7,1,7,1,7,1,7,1,7,1,7,5,7,148,
  	8,7,10,7,12,7,151,9,7,1,7,1,7,1,7,1,7,1,7,1,7,3,7,159,8,7,1,8,1,8,1,8,
  	1,8,1,9,1,9,1,9,1,9,1,9,5,9,170,8,9,10,9,12,9,173,9,9,1,9,1,9,1,9,1,9,
  	3,9,179,8,9,1,10,1,10,1,10,3,10,184,8,10,1,10,1,10,1,10,1,10,1,11,1,11,
  	1,11,3,11,193,8,11,1,11,1,11,1,11,1,11,1,11,1,11,1,12,1,12,1,12,1,12,
  	5,12,205,8,12,10,12,12,12,208,9,12,1,12,1,12,1,13,3,13,213,8,13,1,13,
  	1,13,1,13,1,13,1,14,1,14,1,14,1,14,5,14,223,8,14,10,14,12,14,226,9,14,
  	1,14,1,14,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,
  	3,15,242,8,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,5,15,253,8,
  	15,10,15,12,15,256,9,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,
  	1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,1,15,5,15,276,8,15,10,15,12,15,
  	279,9,15,1,15,1,15,1,15,3,15,284,8,15,1,15,3,15,287,8,15,1,15,1,15,1,
  	15,3,15,292,8,15,1,16,1,16,1,16,5,16,297,8,16,10,16,12,16,300,9,16,1,
  	16,1,16,1,16,1,17,1,17,1,17,3,17,308,8,17,1,17,1,17,1,17,1,17,1,17,1,
  	17,1,17,1,17,5,17,318,8,17,10,17,12,17,321,9,17,1,18,1,18,1,18,5,18,326,
  	8,18,10,18,12,18,329,9,18,1,19,1,19,1,19,1,19,3,19,335,8,19,1,20,1,20,
  	1,21,3,21,340,8,21,1,21,1,21,1,21,1,21,5,21,346,8,21,10,21,12,21,349,
  	9,21,1,22,1,22,1,23,1,23,1,23,1,23,5,23,357,8,23,10,23,12,23,360,9,23,
  	1,24,1,24,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,1,25,
  	1,25,1,25,1,25,1,25,5,25,380,8,25,10,25,12,25,383,9,25,3,25,385,8,25,
  	1,25,3,25,388,8,25,1,26,1,26,1,26,3,26,393,8,26,1,27,3,27,396,8,27,1,
  	27,1,27,3,27,400,8,27,1,27,0,0,28,0,2,4,6,8,10,12,14,16,18,20,22,24,26,
  	28,30,32,34,36,38,40,42,44,46,48,50,52,54,0,6,1,0,21,22,3,0,27,27,38,
  	40,49,51,1,0,52,53,2,0,26,26,52,53,3,0,25,25,28,29,54,55,1,0,57,59,438,
  	0,56,1,0,0,0,2,66,1,0,0,0,4,76,1,0,0,0,6,78,1,0,0,0,8,88,1,0,0,0,10,98,
  	1,0,0,0,12,108,1,0,0,0,14,158,1,0,0,0,16,160,1,0,0,0,18,171,1,0,0,0,20,
  	180,1,0,0,0,22,189,1,0,0,0,24,200,1,0,0,0,26,212,1,0,0,0,28,218,1,0,0,
  	0,30,291,1,0,0,0,32,293,1,0,0,0,34,304,1,0,0,0,36,322,1,0,0,0,38,330,
  	1,0,0,0,40,336,1,0,0,0,42,339,1,0,0,0,44,350,1,0,0,0,46,352,1,0,0,0,48,
  	361,1,0,0,0,50,387,1,0,0,0,52,389,1,0,0,0,54,399,1,0,0,0,56,57,5,1,0,
  	0,57,58,5,57,0,0,58,59,5,41,0,0,59,60,3,2,1,0,60,61,5,44,0,0,61,62,5,
  	0,0,1,62,1,1,0,0,0,63,65,3,4,2,0,64,63,1,0,0,0,65,68,1,0,0,0,66,64,1,
  	0,0,0,66,67,1,0,0,0,67,69,1,0,0,0,68,66,1,0,0,0,69,70,3,28,14,0,70,3,
  	1,0,0,0,71,77,3,6,3,0,72,77,3,8,4,0,73,77,3,10,5,0,74,77,3,20,10,0,75,
  	77,3,22,11,0,76,71,1,0,0,0,76,72,1,0,0,0,76,73,1,0,0,0,76,74,1,0,0,0,
  	76,75,1,0,0,0,77,5,1,0,0,0,78,84,5,3,0,0,79,80,5,57,0,0,80,81,5,49,0,
  	0,81,82,3,54,27,0,82,83,5,41,0,0,83,85,1,0,0,0,84,79,1,0,0,0,85,86,1,
  	0,0,0,86,84,1,0,0,0,86,87,1,0,0,0,87,7,1,0,0,0,88,94,5,4,0,0,89,90,5,
  	57,0,0,90,91,5,49,0,0,91,92,3,14,7,0,92,93,5,41,0,0,93,95,1,0,0,0,94,
  	89,1,0,0,0,95,96,1,0,0,0,96,94,1,0,0,0,96,97,1,0,0,0,97,9,1,0,0,0,98,
  	104,5,2,0,0,99,100,3,12,6,0,100,101,5,42,0,0,101,102,3,14,7,0,102,103,
  	5,41,0,0,103,105,1,0,0,0,104,99,1,0,0,0,105,106,1,0,0,0,106,104,1,0,0,
  	0,106,107,1,0,0,0,107,11,1,0,0,0,108,113,5,57,0,0,109,110,5,43,0,0,110,
  	112,5,57,0,0,111,109,1,0,0,0,112,115,1,0,0,0,113,111,1,0,0,0,113,114,
  	1,0,0,0,114,13,1,0,0,0,115,113,1,0,0,0,116,159,5,30,0,0,117,159,5,31,
  	0,0,118,159,5,32,0,0,119,159,5,33,0,0,120,121,5,56,0,0,121,159,3,14,7,
  	0,122,123,5,5,0,0,123,124,5,47,0,0,124,129,3,16,8,0,125,126,5,43,0,0,
  	126,128,3,16,8,0,127,125,1,0,0,0,128,131,1,0,0,0,129,127,1,0,0,0,129,
  	130,1,0,0,0,130,132,1,0,0,0,131,129,1,0,0,0,132,133,5,48,0,0,133,134,
  	5,6,0,0,134,135,3,14,7,0,135,159,1,0,0,0,136,137,5,8,0,0,137,138,5,6,
  	0,0,138,159,3,14,7,0,139,140,5,7,0,0,140,141,3,18,9,0,141,142,5,12,0,
  	0,142,159,1,0,0,0,143,144,5,45,0,0,144,149,5,57,0,0,145,146,5,43,0,0,
  	146,148,5,57,0,0,147,145,1,0,0,0,148,151,1,0,0,0,149,147,1,0,0,0,149,
  	150,1,0,0,0,150,152,1,0,0,0,151,149,1,0,0,0,152,159,5,46,0,0,153,154,
  	3,54,27,0,154,155,5,37,0,0,155,156,3,54,27,0,156,159,1,0,0,0,157,159,
  	5,57,0,0,158,116,1,0,0,0,158,117,1,0,0,0,158,118,1,0,0,0,158,119,1,0,
  	0,0,158,120,1,0,0,0,158,122,1,0,0,0,158,136,1,0,0,0,158,139,1,0,0,0,158,
  	143,1,0,0,0,158,153,1,0,0,0,158,157,1,0,0,0,159,15,1,0,0,0,160,161,3,
  	54,27,0,161,162,5,37,0,0,162,163,3,54,27,0,163,17,1,0,0,0,164,165,3,12,
  	6,0,165,166,5,42,0,0,166,167,3,14,7,0,167,168,5,41,0,0,168,170,1,0,0,
  	0,169,164,1,0,0,0,170,173,1,0,0,0,171,169,1,0,0,0,171,172,1,0,0,0,172,
  	178,1,0,0,0,173,171,1,0,0,0,174,175,3,12,6,0,175,176,5,42,0,0,176,177,
  	3,14,7,0,177,179,1,0,0,0,178,174,1,0,0,0,178,179,1,0,0,0,179,19,1,0,0,
  	0,180,181,5,9,0,0,181,183,5,57,0,0,182,184,3,24,12,0,183,182,1,0,0,0,
  	183,184,1,0,0,0,184,185,1,0,0,0,185,186,5,41,0,0,186,187,3,2,1,0,187,
  	188,5,41,0,0,188,21,1,0,0,0,189,190,5,10,0,0,190,192,5,57,0,0,191,193,
  	3,24,12,0,192,191,1,0,0,0,192,193,1,0,0,0,193,194,1,0,0,0,194,195,5,42,
  	0,0,195,196,3,14,7,0,196,197,5,41,0,0,197,198,3,2,1,0,198,199,5,41,0,
  	0,199,23,1,0,0,0,200,201,5,45,0,0,201,206,3,26,13,0,202,203,5,41,0,0,
  	203,205,3,26,13,0,204,202,1,0,0,0,205,208,1,0,0,0,206,204,1,0,0,0,206,
  	207,1,0,0,0,207,209,1,0,0,0,208,206,1,0,0,0,209,210,5,46,0,0,210,25,1,
  	0,0,0,211,213,5,2,0,0,212,211,1,0,0,0,212,213,1,0,0,0,213,214,1,0,0,0,
  	214,215,3,12,6,0,215,216,5,42,0,0,216,217,3,14,7,0,217,27,1,0,0,0,218,
  	219,5,11,0,0,219,224,3,30,15,0,220,221,5,41,0,0,221,223,3,30,15,0,222,
  	220,1,0,0,0,223,226,1,0,0,0,224,222,1,0,0,0,224,225,1,0,0,0,225,227,1,
  	0,0,0,226,224,1,0,0,0,227,228,5,12,0,0,228,29,1,0,0,0,229,292,3,28,14,
  	0,230,231,3,34,17,0,231,232,5,36,0,0,232,233,3,38,19,0,233,292,1,0,0,
  	0,234,292,3,34,17,0,235,236,5,13,0,0,236,237,3,38,19,0,237,238,5,14,0,
  	0,238,241,3,30,15,0,239,240,5,15,0,0,240,242,3,30,15,0,241,239,1,0,0,
  	0,241,242,1,0,0,0,242,292,1,0,0,0,243,244,5,16,0,0,244,245,3,38,19,0,
  	245,246,5,17,0,0,246,247,3,30,15,0,247,292,1,0,0,0,248,249,5,18,0,0,249,
  	254,3,30,15,0,250,251,5,41,0,0,251,253,3,30,15,0,252,250,1,0,0,0,253,
  	256,1,0,0,0,254,252,1,0,0,0,254,255,1,0,0,0,255,257,1,0,0,0,256,254,1,
  	0,0,0,257,258,5,19,0,0,258,259,3,38,19,0,259,292,1,0,0,0,260,261,5,20,
  	0,0,261,262,5,57,0,0,262,263,5,36,0,0,263,264,3,38,19,0,264,265,7,0,0,
  	0,265,266,3,38,19,0,266,267,5,17,0,0,267,268,3,30,15,0,268,292,1,0,0,
  	0,269,270,5,23,0,0,270,271,3,38,19,0,271,272,5,6,0,0,272,277,3,32,16,
  	0,273,274,5,41,0,0,274,276,3,32,16,0,275,273,1,0,0,0,276,279,1,0,0,0,
  	277,275,1,0,0,0,277,278,1,0,0,0,278,283,1,0,0,0,279,277,1,0,0,0,280,281,
  	5,41,0,0,281,282,5,15,0,0,282,284,3,30,15,0,283,280,1,0,0,0,283,284,1,
  	0,0,0,284,286,1,0,0,0,285,287,5,41,0,0,286,285,1,0,0,0,286,287,1,0,0,
  	0,287,288,1,0,0,0,288,289,5,12,0,0,289,292,1,0,0,0,290,292,1,0,0,0,291,
  	229,1,0,0,0,291,230,1,0,0,0,291,234,1,0,0,0,291,235,1,0,0,0,291,243,1,
  	0,0,0,291,248,1,0,0,0,291,260,1,0,0,0,291,269,1,0,0,0,291,290,1,0,0,0,
  	292,31,1,0,0,0,293,298,3,54,27,0,294,295,5,43,0,0,295,297,3,54,27,0,296,
  	294,1,0,0,0,297,300,1,0,0,0,298,296,1,0,0,0,298,299,1,0,0,0,299,301,1,
  	0,0,0,300,298,1,0,0,0,301,302,5,42,0,0,302,303,3,30,15,0,303,33,1,0,0,
  	0,304,319,5,57,0,0,305,307,5,45,0,0,306,308,3,36,18,0,307,306,1,0,0,0,
  	307,308,1,0,0,0,308,309,1,0,0,0,309,318,5,46,0,0,310,311,5,47,0,0,311,
  	312,3,36,18,0,312,313,5,48,0,0,313,318,1,0,0,0,314,315,5,44,0,0,315,318,
  	5,57,0,0,316,318,5,56,0,0,317,305,1,0,0,0,317,310,1,0,0,0,317,314,1,0,
  	0,0,317,316,1,0,0,0,318,321,1,0,0,0,319,317,1,0,0,0,319,320,1,0,0,0,320,
  	35,1,0,0,0,321,319,1,0,0,0,322,327,3,38,19,0,323,324,5,43,0,0,324,326,
  	3,38,19,0,325,323,1,0,0,0,326,329,1,0,0,0,327,325,1,0,0,0,327,328,1,0,
  	0,0,328,37,1,0,0,0,329,327,1,0,0,0,330,334,3,42,21,0,331,332,3,40,20,
  	0,332,333,3,42,21,0,333,335,1,0,0,0,334,331,1,0,0,0,334,335,1,0,0,0,335,
  	39,1,0,0,0,336,337,7,1,0,0,337,41,1,0,0,0,338,340,7,2,0,0,339,338,1,0,
  	0,0,339,340,1,0,0,0,340,341,1,0,0,0,341,347,3,46,23,0,342,343,3,44,22,
  	0,343,344,3,46,23,0,344,346,1,0,0,0,345,342,1,0,0,0,346,349,1,0,0,0,347,
  	345,1,0,0,0,347,348,1,0,0,0,348,43,1,0,0,0,349,347,1,0,0,0,350,351,7,
  	3,0,0,351,45,1,0,0,0,352,358,3,50,25,0,353,354,3,48,24,0,354,355,3,50,
  	25,0,355,357,1,0,0,0,356,353,1,0,0,0,357,360,1,0,0,0,358,356,1,0,0,0,
  	358,359,1,0,0,0,359,47,1,0,0,0,360,358,1,0,0,0,361,362,7,4,0,0,362,49,
  	1,0,0,0,363,388,3,34,17,0,364,388,5,59,0,0,365,388,5,58,0,0,366,388,5,
  	60,0,0,367,388,5,34,0,0,368,388,5,35,0,0,369,370,5,45,0,0,370,371,3,38,
  	19,0,371,372,5,46,0,0,372,388,1,0,0,0,373,374,5,24,0,0,374,388,3,50,25,
  	0,375,384,5,47,0,0,376,381,3,52,26,0,377,378,5,43,0,0,378,380,3,52,26,
  	0,379,377,1,0,0,0,380,383,1,0,0,0,381,379,1,0,0,0,381,382,1,0,0,0,382,
  	385,1,0,0,0,383,381,1,0,0,0,384,376,1,0,0,0,384,385,1,0,0,0,385,386,1,
  	0,0,0,386,388,5,48,0,0,387,363,1,0,0,0,387,364,1,0,0,0,387,365,1,0,0,
  	0,387,366,1,0,0,0,387,367,1,0,0,0,387,368,1,0,0,0,387,369,1,0,0,0,387,
  	373,1,0,0,0,387,375,1,0,0,0,388,51,1,0,0,0,389,392,3,38,19,0,390,391,
  	5,37,0,0,391,393,3,38,19,0,392,390,1,0,0,0,392,393,1,0,0,0,393,53,1,0,
  	0,0,394,396,7,2,0,0,395,394,1,0,0,0,395,396,1,0,0,0,396,397,1,0,0,0,397,
  	400,7,5,0,0,398,400,5,60,0,0,399,395,1,0,0,0,399,398,1,0,0,0,400,55,1,
  	0,0,0,37,66,76,86,96,106,113,129,149,158,171,178,183,192,206,212,224,
  	241,254,277,283,286,291,298,307,317,319,327,334,339,347,358,381,384,387,
  	392,395,399
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  pascalparserParserStaticData = std::move(staticData);
}

}

PascalParser::PascalParser(TokenStream *input) : PascalParser(input, antlr4::atn::ParserATNSimulatorOptions()) {}

PascalParser::PascalParser(TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options) : Parser(input) {
  PascalParser::initialize();
  _interpreter = new atn::ParserATNSimulator(this, *pascalparserParserStaticData->atn, pascalparserParserStaticData->decisionToDFA, pascalparserParserStaticData->sharedContextCache, options);
}

PascalParser::~PascalParser() {
  delete _interpreter;
}

const atn::ATN& PascalParser::getATN() const {
  return *pascalparserParserStaticData->atn;
}

std::string PascalParser::getGrammarFileName() const {
  return "PascalParser.g4";
}

const std::vector<std::string>& PascalParser::getRuleNames() const {
  return pascalparserParserStaticData->ruleNames;
}

const dfa::Vocabulary& PascalParser::getVocabulary() const {
  return pascalparserParserStaticData->vocabulary;
}

antlr4::atn::SerializedATNView PascalParser::getSerializedATN() const {
  return pascalparserParserStaticData->serializedATN;
}


//----------------- ProgramContext ------------------------------------------------------------------

PascalParser::ProgramContext::ProgramContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PascalParser::ProgramContext::PROGRAM_KW() {
  return getToken(PascalParser::PROGRAM_KW, 0);
}

tree::TerminalNode* PascalParser::ProgramContext::IDENT() {
  return getToken(PascalParser::IDENT, 0);
}

tree::TerminalNode* PascalParser::ProgramContext::SEMI() {
  return getToken(PascalParser::SEMI, 0);
}

PascalParser::BlockContext* PascalParser::ProgramContext::block() {
  return getRuleContext<PascalParser::BlockContext>(0);
}

tree::TerminalNode* PascalParser::ProgramContext::DOT() {
  return getToken(PascalParser::DOT, 0);
}

tree::TerminalNode* PascalParser::ProgramContext::EOF() {
  return getToken(PascalParser::EOF, 0);
}


size_t PascalParser::ProgramContext::getRuleIndex() const {
  return PascalParser::RuleProgram;
}


std::any PascalParser::ProgramContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitProgram(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::ProgramContext* PascalParser::program() {
  ProgramContext *_localctx = _tracker.createInstance<ProgramContext>(_ctx, getState());
  enterRule(_localctx, 0, PascalParser::RuleProgram);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(56);
    match(PascalParser::PROGRAM_KW);
    setState(57);
    match(PascalParser::IDENT);
    setState(58);
    match(PascalParser::SEMI);
    setState(59);
    block();
    setState(60);
    match(PascalParser::DOT);
    setState(61);
    match(PascalParser::EOF);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- BlockContext ------------------------------------------------------------------

PascalParser::BlockContext::BlockContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

PascalParser::CompoundStmtContext* PascalParser::BlockContext::compoundStmt() {
  return getRuleContext<PascalParser::CompoundStmtContext>(0);
}

std::vector<PascalParser::DeclPartContext *> PascalParser::BlockContext::declPart() {
  return getRuleContexts<PascalParser::DeclPartContext>();
}

PascalParser::DeclPartContext* PascalParser::BlockContext::declPart(size_t i) {
  return getRuleContext<PascalParser::DeclPartContext>(i);
}


size_t PascalParser::BlockContext::getRuleIndex() const {
  return PascalParser::RuleBlock;
}


std::any PascalParser::BlockContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitBlock(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::BlockContext* PascalParser::block() {
  BlockContext *_localctx = _tracker.createInstance<BlockContext>(_ctx, getState());
  enterRule(_localctx, 2, PascalParser::RuleBlock);
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
      ((1ULL << _la) & 1564) != 0)) {
      setState(63);
      declPart();
      setState(68);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(69);
    compoundStmt();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- DeclPartContext ------------------------------------------------------------------

PascalParser::DeclPartContext::DeclPartContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

PascalParser::ConstDeclContext* PascalParser::DeclPartContext::constDecl() {
  return getRuleContext<PascalParser::ConstDeclContext>(0);
}

PascalParser::TypeDeclContext* PascalParser::DeclPartContext::typeDecl() {
  return getRuleContext<PascalParser::TypeDeclContext>(0);
}

PascalParser::VarDeclContext* PascalParser::DeclPartContext::varDecl() {
  return getRuleContext<PascalParser::VarDeclContext>(0);
}

PascalParser::ProcDeclContext* PascalParser::DeclPartContext::procDecl() {
  return getRuleContext<PascalParser::ProcDeclContext>(0);
}

PascalParser::FuncDeclContext* PascalParser::DeclPartContext::funcDecl() {
  return getRuleContext<PascalParser::FuncDeclContext>(0);
}


size_t PascalParser::DeclPartContext::getRuleIndex() const {
  return PascalParser::RuleDeclPart;
}


std::any PascalParser::DeclPartContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitDeclPart(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::DeclPartContext* PascalParser::declPart() {
  DeclPartContext *_localctx = _tracker.createInstance<DeclPartContext>(_ctx, getState());
  enterRule(_localctx, 4, PascalParser::RuleDeclPart);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(76);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case PascalParser::CONST_KW: {
        enterOuterAlt(_localctx, 1);
        setState(71);
        constDecl();
        break;
      }

      case PascalParser::TYPE_KW: {
        enterOuterAlt(_localctx, 2);
        setState(72);
        typeDecl();
        break;
      }

      case PascalParser::VAR_KW: {
        enterOuterAlt(_localctx, 3);
        setState(73);
        varDecl();
        break;
      }

      case PascalParser::PROCEDURE_KW: {
        enterOuterAlt(_localctx, 4);
        setState(74);
        procDecl();
        break;
      }

      case PascalParser::FUNCTION_KW: {
        enterOuterAlt(_localctx, 5);
        setState(75);
        funcDecl();
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

PascalParser::ConstDeclContext::ConstDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PascalParser::ConstDeclContext::CONST_KW() {
  return getToken(PascalParser::CONST_KW, 0);
}

std::vector<tree::TerminalNode *> PascalParser::ConstDeclContext::IDENT() {
  return getTokens(PascalParser::IDENT);
}

tree::TerminalNode* PascalParser::ConstDeclContext::IDENT(size_t i) {
  return getToken(PascalParser::IDENT, i);
}

std::vector<tree::TerminalNode *> PascalParser::ConstDeclContext::EQ() {
  return getTokens(PascalParser::EQ);
}

tree::TerminalNode* PascalParser::ConstDeclContext::EQ(size_t i) {
  return getToken(PascalParser::EQ, i);
}

std::vector<PascalParser::ConstExprContext *> PascalParser::ConstDeclContext::constExpr() {
  return getRuleContexts<PascalParser::ConstExprContext>();
}

PascalParser::ConstExprContext* PascalParser::ConstDeclContext::constExpr(size_t i) {
  return getRuleContext<PascalParser::ConstExprContext>(i);
}

std::vector<tree::TerminalNode *> PascalParser::ConstDeclContext::SEMI() {
  return getTokens(PascalParser::SEMI);
}

tree::TerminalNode* PascalParser::ConstDeclContext::SEMI(size_t i) {
  return getToken(PascalParser::SEMI, i);
}


size_t PascalParser::ConstDeclContext::getRuleIndex() const {
  return PascalParser::RuleConstDecl;
}


std::any PascalParser::ConstDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitConstDecl(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::ConstDeclContext* PascalParser::constDecl() {
  ConstDeclContext *_localctx = _tracker.createInstance<ConstDeclContext>(_ctx, getState());
  enterRule(_localctx, 6, PascalParser::RuleConstDecl);
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
    setState(78);
    match(PascalParser::CONST_KW);
    setState(84); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(79);
      match(PascalParser::IDENT);
      setState(80);
      match(PascalParser::EQ);
      setState(81);
      constExpr();
      setState(82);
      match(PascalParser::SEMI);
      setState(86); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == PascalParser::IDENT);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- TypeDeclContext ------------------------------------------------------------------

PascalParser::TypeDeclContext::TypeDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PascalParser::TypeDeclContext::TYPE_KW() {
  return getToken(PascalParser::TYPE_KW, 0);
}

std::vector<tree::TerminalNode *> PascalParser::TypeDeclContext::IDENT() {
  return getTokens(PascalParser::IDENT);
}

tree::TerminalNode* PascalParser::TypeDeclContext::IDENT(size_t i) {
  return getToken(PascalParser::IDENT, i);
}

std::vector<tree::TerminalNode *> PascalParser::TypeDeclContext::EQ() {
  return getTokens(PascalParser::EQ);
}

tree::TerminalNode* PascalParser::TypeDeclContext::EQ(size_t i) {
  return getToken(PascalParser::EQ, i);
}

std::vector<PascalParser::TypeSpecContext *> PascalParser::TypeDeclContext::typeSpec() {
  return getRuleContexts<PascalParser::TypeSpecContext>();
}

PascalParser::TypeSpecContext* PascalParser::TypeDeclContext::typeSpec(size_t i) {
  return getRuleContext<PascalParser::TypeSpecContext>(i);
}

std::vector<tree::TerminalNode *> PascalParser::TypeDeclContext::SEMI() {
  return getTokens(PascalParser::SEMI);
}

tree::TerminalNode* PascalParser::TypeDeclContext::SEMI(size_t i) {
  return getToken(PascalParser::SEMI, i);
}


size_t PascalParser::TypeDeclContext::getRuleIndex() const {
  return PascalParser::RuleTypeDecl;
}


std::any PascalParser::TypeDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitTypeDecl(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::TypeDeclContext* PascalParser::typeDecl() {
  TypeDeclContext *_localctx = _tracker.createInstance<TypeDeclContext>(_ctx, getState());
  enterRule(_localctx, 8, PascalParser::RuleTypeDecl);
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
    setState(88);
    match(PascalParser::TYPE_KW);
    setState(94); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(89);
      match(PascalParser::IDENT);
      setState(90);
      match(PascalParser::EQ);
      setState(91);
      typeSpec();
      setState(92);
      match(PascalParser::SEMI);
      setState(96); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == PascalParser::IDENT);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- VarDeclContext ------------------------------------------------------------------

PascalParser::VarDeclContext::VarDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PascalParser::VarDeclContext::VAR_KW() {
  return getToken(PascalParser::VAR_KW, 0);
}

std::vector<PascalParser::IdentListContext *> PascalParser::VarDeclContext::identList() {
  return getRuleContexts<PascalParser::IdentListContext>();
}

PascalParser::IdentListContext* PascalParser::VarDeclContext::identList(size_t i) {
  return getRuleContext<PascalParser::IdentListContext>(i);
}

std::vector<tree::TerminalNode *> PascalParser::VarDeclContext::COLON() {
  return getTokens(PascalParser::COLON);
}

tree::TerminalNode* PascalParser::VarDeclContext::COLON(size_t i) {
  return getToken(PascalParser::COLON, i);
}

std::vector<PascalParser::TypeSpecContext *> PascalParser::VarDeclContext::typeSpec() {
  return getRuleContexts<PascalParser::TypeSpecContext>();
}

PascalParser::TypeSpecContext* PascalParser::VarDeclContext::typeSpec(size_t i) {
  return getRuleContext<PascalParser::TypeSpecContext>(i);
}

std::vector<tree::TerminalNode *> PascalParser::VarDeclContext::SEMI() {
  return getTokens(PascalParser::SEMI);
}

tree::TerminalNode* PascalParser::VarDeclContext::SEMI(size_t i) {
  return getToken(PascalParser::SEMI, i);
}


size_t PascalParser::VarDeclContext::getRuleIndex() const {
  return PascalParser::RuleVarDecl;
}


std::any PascalParser::VarDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitVarDecl(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::VarDeclContext* PascalParser::varDecl() {
  VarDeclContext *_localctx = _tracker.createInstance<VarDeclContext>(_ctx, getState());
  enterRule(_localctx, 10, PascalParser::RuleVarDecl);
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
    setState(98);
    match(PascalParser::VAR_KW);
    setState(104); 
    _errHandler->sync(this);
    _la = _input->LA(1);
    do {
      setState(99);
      identList();
      setState(100);
      match(PascalParser::COLON);
      setState(101);
      typeSpec();
      setState(102);
      match(PascalParser::SEMI);
      setState(106); 
      _errHandler->sync(this);
      _la = _input->LA(1);
    } while (_la == PascalParser::IDENT);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- IdentListContext ------------------------------------------------------------------

PascalParser::IdentListContext::IdentListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> PascalParser::IdentListContext::IDENT() {
  return getTokens(PascalParser::IDENT);
}

tree::TerminalNode* PascalParser::IdentListContext::IDENT(size_t i) {
  return getToken(PascalParser::IDENT, i);
}

std::vector<tree::TerminalNode *> PascalParser::IdentListContext::COMMA() {
  return getTokens(PascalParser::COMMA);
}

tree::TerminalNode* PascalParser::IdentListContext::COMMA(size_t i) {
  return getToken(PascalParser::COMMA, i);
}


size_t PascalParser::IdentListContext::getRuleIndex() const {
  return PascalParser::RuleIdentList;
}


std::any PascalParser::IdentListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitIdentList(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::IdentListContext* PascalParser::identList() {
  IdentListContext *_localctx = _tracker.createInstance<IdentListContext>(_ctx, getState());
  enterRule(_localctx, 12, PascalParser::RuleIdentList);
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
    setState(108);
    match(PascalParser::IDENT);
    setState(113);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PascalParser::COMMA) {
      setState(109);
      match(PascalParser::COMMA);
      setState(110);
      match(PascalParser::IDENT);
      setState(115);
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

//----------------- TypeSpecContext ------------------------------------------------------------------

PascalParser::TypeSpecContext::TypeSpecContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t PascalParser::TypeSpecContext::getRuleIndex() const {
  return PascalParser::RuleTypeSpec;
}

void PascalParser::TypeSpecContext::copyFrom(TypeSpecContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- CharTypeContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::CharTypeContext::CHAR_KW() {
  return getToken(PascalParser::CHAR_KW, 0);
}

PascalParser::CharTypeContext::CharTypeContext(TypeSpecContext *ctx) { copyFrom(ctx); }


std::any PascalParser::CharTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitCharType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- NamedTypeContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::NamedTypeContext::IDENT() {
  return getToken(PascalParser::IDENT, 0);
}

PascalParser::NamedTypeContext::NamedTypeContext(TypeSpecContext *ctx) { copyFrom(ctx); }


std::any PascalParser::NamedTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitNamedType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ArrayTypeContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::ArrayTypeContext::ARRAY_KW() {
  return getToken(PascalParser::ARRAY_KW, 0);
}

tree::TerminalNode* PascalParser::ArrayTypeContext::LBRACK() {
  return getToken(PascalParser::LBRACK, 0);
}

std::vector<PascalParser::RangeSpecContext *> PascalParser::ArrayTypeContext::rangeSpec() {
  return getRuleContexts<PascalParser::RangeSpecContext>();
}

PascalParser::RangeSpecContext* PascalParser::ArrayTypeContext::rangeSpec(size_t i) {
  return getRuleContext<PascalParser::RangeSpecContext>(i);
}

tree::TerminalNode* PascalParser::ArrayTypeContext::RBRACK() {
  return getToken(PascalParser::RBRACK, 0);
}

tree::TerminalNode* PascalParser::ArrayTypeContext::OF_KW() {
  return getToken(PascalParser::OF_KW, 0);
}

PascalParser::TypeSpecContext* PascalParser::ArrayTypeContext::typeSpec() {
  return getRuleContext<PascalParser::TypeSpecContext>(0);
}

std::vector<tree::TerminalNode *> PascalParser::ArrayTypeContext::COMMA() {
  return getTokens(PascalParser::COMMA);
}

tree::TerminalNode* PascalParser::ArrayTypeContext::COMMA(size_t i) {
  return getToken(PascalParser::COMMA, i);
}

PascalParser::ArrayTypeContext::ArrayTypeContext(TypeSpecContext *ctx) { copyFrom(ctx); }


std::any PascalParser::ArrayTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitArrayType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- RealTypeContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::RealTypeContext::REAL_KW() {
  return getToken(PascalParser::REAL_KW, 0);
}

PascalParser::RealTypeContext::RealTypeContext(TypeSpecContext *ctx) { copyFrom(ctx); }


std::any PascalParser::RealTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitRealType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- BooleanTypeContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::BooleanTypeContext::BOOLEAN_KW() {
  return getToken(PascalParser::BOOLEAN_KW, 0);
}

PascalParser::BooleanTypeContext::BooleanTypeContext(TypeSpecContext *ctx) { copyFrom(ctx); }


std::any PascalParser::BooleanTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitBooleanType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PointerTypeContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::PointerTypeContext::CARET() {
  return getToken(PascalParser::CARET, 0);
}

PascalParser::TypeSpecContext* PascalParser::PointerTypeContext::typeSpec() {
  return getRuleContext<PascalParser::TypeSpecContext>(0);
}

PascalParser::PointerTypeContext::PointerTypeContext(TypeSpecContext *ctx) { copyFrom(ctx); }


std::any PascalParser::PointerTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitPointerType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- SubrangeTypeContext ------------------------------------------------------------------

std::vector<PascalParser::ConstExprContext *> PascalParser::SubrangeTypeContext::constExpr() {
  return getRuleContexts<PascalParser::ConstExprContext>();
}

PascalParser::ConstExprContext* PascalParser::SubrangeTypeContext::constExpr(size_t i) {
  return getRuleContext<PascalParser::ConstExprContext>(i);
}

tree::TerminalNode* PascalParser::SubrangeTypeContext::DOTDOT() {
  return getToken(PascalParser::DOTDOT, 0);
}

PascalParser::SubrangeTypeContext::SubrangeTypeContext(TypeSpecContext *ctx) { copyFrom(ctx); }


std::any PascalParser::SubrangeTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitSubrangeType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- IntegerTypeContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::IntegerTypeContext::INTEGER_KW() {
  return getToken(PascalParser::INTEGER_KW, 0);
}

PascalParser::IntegerTypeContext::IntegerTypeContext(TypeSpecContext *ctx) { copyFrom(ctx); }


std::any PascalParser::IntegerTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitIntegerType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- RecordTypeContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::RecordTypeContext::RECORD_KW() {
  return getToken(PascalParser::RECORD_KW, 0);
}

PascalParser::FieldListContext* PascalParser::RecordTypeContext::fieldList() {
  return getRuleContext<PascalParser::FieldListContext>(0);
}

tree::TerminalNode* PascalParser::RecordTypeContext::END_KW() {
  return getToken(PascalParser::END_KW, 0);
}

PascalParser::RecordTypeContext::RecordTypeContext(TypeSpecContext *ctx) { copyFrom(ctx); }


std::any PascalParser::RecordTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitRecordType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- EnumTypeContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::EnumTypeContext::LPAREN() {
  return getToken(PascalParser::LPAREN, 0);
}

std::vector<tree::TerminalNode *> PascalParser::EnumTypeContext::IDENT() {
  return getTokens(PascalParser::IDENT);
}

tree::TerminalNode* PascalParser::EnumTypeContext::IDENT(size_t i) {
  return getToken(PascalParser::IDENT, i);
}

tree::TerminalNode* PascalParser::EnumTypeContext::RPAREN() {
  return getToken(PascalParser::RPAREN, 0);
}

std::vector<tree::TerminalNode *> PascalParser::EnumTypeContext::COMMA() {
  return getTokens(PascalParser::COMMA);
}

tree::TerminalNode* PascalParser::EnumTypeContext::COMMA(size_t i) {
  return getToken(PascalParser::COMMA, i);
}

PascalParser::EnumTypeContext::EnumTypeContext(TypeSpecContext *ctx) { copyFrom(ctx); }


std::any PascalParser::EnumTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitEnumType(this);
  else
    return visitor->visitChildren(this);
}
//----------------- SetTypeContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::SetTypeContext::SET_KW() {
  return getToken(PascalParser::SET_KW, 0);
}

tree::TerminalNode* PascalParser::SetTypeContext::OF_KW() {
  return getToken(PascalParser::OF_KW, 0);
}

PascalParser::TypeSpecContext* PascalParser::SetTypeContext::typeSpec() {
  return getRuleContext<PascalParser::TypeSpecContext>(0);
}

PascalParser::SetTypeContext::SetTypeContext(TypeSpecContext *ctx) { copyFrom(ctx); }


std::any PascalParser::SetTypeContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitSetType(this);
  else
    return visitor->visitChildren(this);
}
PascalParser::TypeSpecContext* PascalParser::typeSpec() {
  TypeSpecContext *_localctx = _tracker.createInstance<TypeSpecContext>(_ctx, getState());
  enterRule(_localctx, 14, PascalParser::RuleTypeSpec);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(158);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 8, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<PascalParser::IntegerTypeContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(116);
      match(PascalParser::INTEGER_KW);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<PascalParser::RealTypeContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(117);
      match(PascalParser::REAL_KW);
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<PascalParser::CharTypeContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(118);
      match(PascalParser::CHAR_KW);
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<PascalParser::BooleanTypeContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(119);
      match(PascalParser::BOOLEAN_KW);
      break;
    }

    case 5: {
      _localctx = _tracker.createInstance<PascalParser::PointerTypeContext>(_localctx);
      enterOuterAlt(_localctx, 5);
      setState(120);
      match(PascalParser::CARET);
      setState(121);
      typeSpec();
      break;
    }

    case 6: {
      _localctx = _tracker.createInstance<PascalParser::ArrayTypeContext>(_localctx);
      enterOuterAlt(_localctx, 6);
      setState(122);
      match(PascalParser::ARRAY_KW);
      setState(123);
      match(PascalParser::LBRACK);
      setState(124);
      rangeSpec();
      setState(129);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == PascalParser::COMMA) {
        setState(125);
        match(PascalParser::COMMA);
        setState(126);
        rangeSpec();
        setState(131);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(132);
      match(PascalParser::RBRACK);
      setState(133);
      match(PascalParser::OF_KW);
      setState(134);
      typeSpec();
      break;
    }

    case 7: {
      _localctx = _tracker.createInstance<PascalParser::SetTypeContext>(_localctx);
      enterOuterAlt(_localctx, 7);
      setState(136);
      match(PascalParser::SET_KW);
      setState(137);
      match(PascalParser::OF_KW);
      setState(138);
      typeSpec();
      break;
    }

    case 8: {
      _localctx = _tracker.createInstance<PascalParser::RecordTypeContext>(_localctx);
      enterOuterAlt(_localctx, 8);
      setState(139);
      match(PascalParser::RECORD_KW);
      setState(140);
      fieldList();
      setState(141);
      match(PascalParser::END_KW);
      break;
    }

    case 9: {
      _localctx = _tracker.createInstance<PascalParser::EnumTypeContext>(_localctx);
      enterOuterAlt(_localctx, 9);
      setState(143);
      match(PascalParser::LPAREN);
      setState(144);
      match(PascalParser::IDENT);
      setState(149);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == PascalParser::COMMA) {
        setState(145);
        match(PascalParser::COMMA);
        setState(146);
        match(PascalParser::IDENT);
        setState(151);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(152);
      match(PascalParser::RPAREN);
      break;
    }

    case 10: {
      _localctx = _tracker.createInstance<PascalParser::SubrangeTypeContext>(_localctx);
      enterOuterAlt(_localctx, 10);
      setState(153);
      constExpr();
      setState(154);
      match(PascalParser::DOTDOT);
      setState(155);
      constExpr();
      break;
    }

    case 11: {
      _localctx = _tracker.createInstance<PascalParser::NamedTypeContext>(_localctx);
      enterOuterAlt(_localctx, 11);
      setState(157);
      match(PascalParser::IDENT);
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

//----------------- RangeSpecContext ------------------------------------------------------------------

PascalParser::RangeSpecContext::RangeSpecContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<PascalParser::ConstExprContext *> PascalParser::RangeSpecContext::constExpr() {
  return getRuleContexts<PascalParser::ConstExprContext>();
}

PascalParser::ConstExprContext* PascalParser::RangeSpecContext::constExpr(size_t i) {
  return getRuleContext<PascalParser::ConstExprContext>(i);
}

tree::TerminalNode* PascalParser::RangeSpecContext::DOTDOT() {
  return getToken(PascalParser::DOTDOT, 0);
}


size_t PascalParser::RangeSpecContext::getRuleIndex() const {
  return PascalParser::RuleRangeSpec;
}


std::any PascalParser::RangeSpecContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitRangeSpec(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::RangeSpecContext* PascalParser::rangeSpec() {
  RangeSpecContext *_localctx = _tracker.createInstance<RangeSpecContext>(_ctx, getState());
  enterRule(_localctx, 16, PascalParser::RuleRangeSpec);

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    enterOuterAlt(_localctx, 1);
    setState(160);
    constExpr();
    setState(161);
    match(PascalParser::DOTDOT);
    setState(162);
    constExpr();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FieldListContext ------------------------------------------------------------------

PascalParser::FieldListContext::FieldListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<PascalParser::IdentListContext *> PascalParser::FieldListContext::identList() {
  return getRuleContexts<PascalParser::IdentListContext>();
}

PascalParser::IdentListContext* PascalParser::FieldListContext::identList(size_t i) {
  return getRuleContext<PascalParser::IdentListContext>(i);
}

std::vector<tree::TerminalNode *> PascalParser::FieldListContext::COLON() {
  return getTokens(PascalParser::COLON);
}

tree::TerminalNode* PascalParser::FieldListContext::COLON(size_t i) {
  return getToken(PascalParser::COLON, i);
}

std::vector<PascalParser::TypeSpecContext *> PascalParser::FieldListContext::typeSpec() {
  return getRuleContexts<PascalParser::TypeSpecContext>();
}

PascalParser::TypeSpecContext* PascalParser::FieldListContext::typeSpec(size_t i) {
  return getRuleContext<PascalParser::TypeSpecContext>(i);
}

std::vector<tree::TerminalNode *> PascalParser::FieldListContext::SEMI() {
  return getTokens(PascalParser::SEMI);
}

tree::TerminalNode* PascalParser::FieldListContext::SEMI(size_t i) {
  return getToken(PascalParser::SEMI, i);
}


size_t PascalParser::FieldListContext::getRuleIndex() const {
  return PascalParser::RuleFieldList;
}


std::any PascalParser::FieldListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitFieldList(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::FieldListContext* PascalParser::fieldList() {
  FieldListContext *_localctx = _tracker.createInstance<FieldListContext>(_ctx, getState());
  enterRule(_localctx, 18, PascalParser::RuleFieldList);
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
    setState(171);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 9, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(164);
        identList();
        setState(165);
        match(PascalParser::COLON);
        setState(166);
        typeSpec();
        setState(167);
        match(PascalParser::SEMI); 
      }
      setState(173);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 9, _ctx);
    }
    setState(178);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PascalParser::IDENT) {
      setState(174);
      identList();
      setState(175);
      match(PascalParser::COLON);
      setState(176);
      typeSpec();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ProcDeclContext ------------------------------------------------------------------

PascalParser::ProcDeclContext::ProcDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PascalParser::ProcDeclContext::PROCEDURE_KW() {
  return getToken(PascalParser::PROCEDURE_KW, 0);
}

tree::TerminalNode* PascalParser::ProcDeclContext::IDENT() {
  return getToken(PascalParser::IDENT, 0);
}

std::vector<tree::TerminalNode *> PascalParser::ProcDeclContext::SEMI() {
  return getTokens(PascalParser::SEMI);
}

tree::TerminalNode* PascalParser::ProcDeclContext::SEMI(size_t i) {
  return getToken(PascalParser::SEMI, i);
}

PascalParser::BlockContext* PascalParser::ProcDeclContext::block() {
  return getRuleContext<PascalParser::BlockContext>(0);
}

PascalParser::FormalParamsContext* PascalParser::ProcDeclContext::formalParams() {
  return getRuleContext<PascalParser::FormalParamsContext>(0);
}


size_t PascalParser::ProcDeclContext::getRuleIndex() const {
  return PascalParser::RuleProcDecl;
}


std::any PascalParser::ProcDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitProcDecl(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::ProcDeclContext* PascalParser::procDecl() {
  ProcDeclContext *_localctx = _tracker.createInstance<ProcDeclContext>(_ctx, getState());
  enterRule(_localctx, 20, PascalParser::RuleProcDecl);
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
    setState(180);
    match(PascalParser::PROCEDURE_KW);
    setState(181);
    match(PascalParser::IDENT);
    setState(183);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PascalParser::LPAREN) {
      setState(182);
      formalParams();
    }
    setState(185);
    match(PascalParser::SEMI);
    setState(186);
    block();
    setState(187);
    match(PascalParser::SEMI);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FuncDeclContext ------------------------------------------------------------------

PascalParser::FuncDeclContext::FuncDeclContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PascalParser::FuncDeclContext::FUNCTION_KW() {
  return getToken(PascalParser::FUNCTION_KW, 0);
}

tree::TerminalNode* PascalParser::FuncDeclContext::IDENT() {
  return getToken(PascalParser::IDENT, 0);
}

tree::TerminalNode* PascalParser::FuncDeclContext::COLON() {
  return getToken(PascalParser::COLON, 0);
}

PascalParser::TypeSpecContext* PascalParser::FuncDeclContext::typeSpec() {
  return getRuleContext<PascalParser::TypeSpecContext>(0);
}

std::vector<tree::TerminalNode *> PascalParser::FuncDeclContext::SEMI() {
  return getTokens(PascalParser::SEMI);
}

tree::TerminalNode* PascalParser::FuncDeclContext::SEMI(size_t i) {
  return getToken(PascalParser::SEMI, i);
}

PascalParser::BlockContext* PascalParser::FuncDeclContext::block() {
  return getRuleContext<PascalParser::BlockContext>(0);
}

PascalParser::FormalParamsContext* PascalParser::FuncDeclContext::formalParams() {
  return getRuleContext<PascalParser::FormalParamsContext>(0);
}


size_t PascalParser::FuncDeclContext::getRuleIndex() const {
  return PascalParser::RuleFuncDecl;
}


std::any PascalParser::FuncDeclContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitFuncDecl(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::FuncDeclContext* PascalParser::funcDecl() {
  FuncDeclContext *_localctx = _tracker.createInstance<FuncDeclContext>(_ctx, getState());
  enterRule(_localctx, 22, PascalParser::RuleFuncDecl);
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
    setState(189);
    match(PascalParser::FUNCTION_KW);
    setState(190);
    match(PascalParser::IDENT);
    setState(192);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PascalParser::LPAREN) {
      setState(191);
      formalParams();
    }
    setState(194);
    match(PascalParser::COLON);
    setState(195);
    typeSpec();
    setState(196);
    match(PascalParser::SEMI);
    setState(197);
    block();
    setState(198);
    match(PascalParser::SEMI);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FormalParamsContext ------------------------------------------------------------------

PascalParser::FormalParamsContext::FormalParamsContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PascalParser::FormalParamsContext::LPAREN() {
  return getToken(PascalParser::LPAREN, 0);
}

std::vector<PascalParser::FormalParamGroupContext *> PascalParser::FormalParamsContext::formalParamGroup() {
  return getRuleContexts<PascalParser::FormalParamGroupContext>();
}

PascalParser::FormalParamGroupContext* PascalParser::FormalParamsContext::formalParamGroup(size_t i) {
  return getRuleContext<PascalParser::FormalParamGroupContext>(i);
}

tree::TerminalNode* PascalParser::FormalParamsContext::RPAREN() {
  return getToken(PascalParser::RPAREN, 0);
}

std::vector<tree::TerminalNode *> PascalParser::FormalParamsContext::SEMI() {
  return getTokens(PascalParser::SEMI);
}

tree::TerminalNode* PascalParser::FormalParamsContext::SEMI(size_t i) {
  return getToken(PascalParser::SEMI, i);
}


size_t PascalParser::FormalParamsContext::getRuleIndex() const {
  return PascalParser::RuleFormalParams;
}


std::any PascalParser::FormalParamsContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitFormalParams(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::FormalParamsContext* PascalParser::formalParams() {
  FormalParamsContext *_localctx = _tracker.createInstance<FormalParamsContext>(_ctx, getState());
  enterRule(_localctx, 24, PascalParser::RuleFormalParams);
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
    setState(200);
    match(PascalParser::LPAREN);
    setState(201);
    formalParamGroup();
    setState(206);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PascalParser::SEMI) {
      setState(202);
      match(PascalParser::SEMI);
      setState(203);
      formalParamGroup();
      setState(208);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(209);
    match(PascalParser::RPAREN);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- FormalParamGroupContext ------------------------------------------------------------------

PascalParser::FormalParamGroupContext::FormalParamGroupContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

PascalParser::IdentListContext* PascalParser::FormalParamGroupContext::identList() {
  return getRuleContext<PascalParser::IdentListContext>(0);
}

tree::TerminalNode* PascalParser::FormalParamGroupContext::COLON() {
  return getToken(PascalParser::COLON, 0);
}

PascalParser::TypeSpecContext* PascalParser::FormalParamGroupContext::typeSpec() {
  return getRuleContext<PascalParser::TypeSpecContext>(0);
}

tree::TerminalNode* PascalParser::FormalParamGroupContext::VAR_KW() {
  return getToken(PascalParser::VAR_KW, 0);
}


size_t PascalParser::FormalParamGroupContext::getRuleIndex() const {
  return PascalParser::RuleFormalParamGroup;
}


std::any PascalParser::FormalParamGroupContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitFormalParamGroup(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::FormalParamGroupContext* PascalParser::formalParamGroup() {
  FormalParamGroupContext *_localctx = _tracker.createInstance<FormalParamGroupContext>(_ctx, getState());
  enterRule(_localctx, 26, PascalParser::RuleFormalParamGroup);
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
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PascalParser::VAR_KW) {
      setState(211);
      match(PascalParser::VAR_KW);
    }
    setState(214);
    identList();
    setState(215);
    match(PascalParser::COLON);
    setState(216);
    typeSpec();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- CompoundStmtContext ------------------------------------------------------------------

PascalParser::CompoundStmtContext::CompoundStmtContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PascalParser::CompoundStmtContext::BEGIN_KW() {
  return getToken(PascalParser::BEGIN_KW, 0);
}

std::vector<PascalParser::StatementContext *> PascalParser::CompoundStmtContext::statement() {
  return getRuleContexts<PascalParser::StatementContext>();
}

PascalParser::StatementContext* PascalParser::CompoundStmtContext::statement(size_t i) {
  return getRuleContext<PascalParser::StatementContext>(i);
}

tree::TerminalNode* PascalParser::CompoundStmtContext::END_KW() {
  return getToken(PascalParser::END_KW, 0);
}

std::vector<tree::TerminalNode *> PascalParser::CompoundStmtContext::SEMI() {
  return getTokens(PascalParser::SEMI);
}

tree::TerminalNode* PascalParser::CompoundStmtContext::SEMI(size_t i) {
  return getToken(PascalParser::SEMI, i);
}


size_t PascalParser::CompoundStmtContext::getRuleIndex() const {
  return PascalParser::RuleCompoundStmt;
}


std::any PascalParser::CompoundStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitCompoundStmt(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::CompoundStmtContext* PascalParser::compoundStmt() {
  CompoundStmtContext *_localctx = _tracker.createInstance<CompoundStmtContext>(_ctx, getState());
  enterRule(_localctx, 28, PascalParser::RuleCompoundStmt);
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
    setState(218);
    match(PascalParser::BEGIN_KW);
    setState(219);
    statement();
    setState(224);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PascalParser::SEMI) {
      setState(220);
      match(PascalParser::SEMI);
      setState(221);
      statement();
      setState(226);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(227);
    match(PascalParser::END_KW);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StatementContext ------------------------------------------------------------------

PascalParser::StatementContext::StatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t PascalParser::StatementContext::getRuleIndex() const {
  return PascalParser::RuleStatement;
}

void PascalParser::StatementContext::copyFrom(StatementContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- ForStmtContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::ForStmtContext::FOR_KW() {
  return getToken(PascalParser::FOR_KW, 0);
}

tree::TerminalNode* PascalParser::ForStmtContext::IDENT() {
  return getToken(PascalParser::IDENT, 0);
}

tree::TerminalNode* PascalParser::ForStmtContext::ASSIGN() {
  return getToken(PascalParser::ASSIGN, 0);
}

std::vector<PascalParser::ExprContext *> PascalParser::ForStmtContext::expr() {
  return getRuleContexts<PascalParser::ExprContext>();
}

PascalParser::ExprContext* PascalParser::ForStmtContext::expr(size_t i) {
  return getRuleContext<PascalParser::ExprContext>(i);
}

tree::TerminalNode* PascalParser::ForStmtContext::DO_KW() {
  return getToken(PascalParser::DO_KW, 0);
}

PascalParser::StatementContext* PascalParser::ForStmtContext::statement() {
  return getRuleContext<PascalParser::StatementContext>(0);
}

tree::TerminalNode* PascalParser::ForStmtContext::TO_KW() {
  return getToken(PascalParser::TO_KW, 0);
}

tree::TerminalNode* PascalParser::ForStmtContext::DOWNTO_KW() {
  return getToken(PascalParser::DOWNTO_KW, 0);
}

PascalParser::ForStmtContext::ForStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any PascalParser::ForStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitForStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- CallStmtContext ------------------------------------------------------------------

PascalParser::DesignatorContext* PascalParser::CallStmtContext::designator() {
  return getRuleContext<PascalParser::DesignatorContext>(0);
}

PascalParser::CallStmtContext::CallStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any PascalParser::CallStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitCallStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- WhileStmtContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::WhileStmtContext::WHILE_KW() {
  return getToken(PascalParser::WHILE_KW, 0);
}

PascalParser::ExprContext* PascalParser::WhileStmtContext::expr() {
  return getRuleContext<PascalParser::ExprContext>(0);
}

tree::TerminalNode* PascalParser::WhileStmtContext::DO_KW() {
  return getToken(PascalParser::DO_KW, 0);
}

PascalParser::StatementContext* PascalParser::WhileStmtContext::statement() {
  return getRuleContext<PascalParser::StatementContext>(0);
}

PascalParser::WhileStmtContext::WhileStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any PascalParser::WhileStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitWhileStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- IfStmtContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::IfStmtContext::IF_KW() {
  return getToken(PascalParser::IF_KW, 0);
}

PascalParser::ExprContext* PascalParser::IfStmtContext::expr() {
  return getRuleContext<PascalParser::ExprContext>(0);
}

tree::TerminalNode* PascalParser::IfStmtContext::THEN_KW() {
  return getToken(PascalParser::THEN_KW, 0);
}

std::vector<PascalParser::StatementContext *> PascalParser::IfStmtContext::statement() {
  return getRuleContexts<PascalParser::StatementContext>();
}

PascalParser::StatementContext* PascalParser::IfStmtContext::statement(size_t i) {
  return getRuleContext<PascalParser::StatementContext>(i);
}

tree::TerminalNode* PascalParser::IfStmtContext::ELSE_KW() {
  return getToken(PascalParser::ELSE_KW, 0);
}

PascalParser::IfStmtContext::IfStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any PascalParser::IfStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitIfStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- CompoundStatementContext ------------------------------------------------------------------

PascalParser::CompoundStmtContext* PascalParser::CompoundStatementContext::compoundStmt() {
  return getRuleContext<PascalParser::CompoundStmtContext>(0);
}

PascalParser::CompoundStatementContext::CompoundStatementContext(StatementContext *ctx) { copyFrom(ctx); }


std::any PascalParser::CompoundStatementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitCompoundStatement(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AssignStmtContext ------------------------------------------------------------------

PascalParser::DesignatorContext* PascalParser::AssignStmtContext::designator() {
  return getRuleContext<PascalParser::DesignatorContext>(0);
}

tree::TerminalNode* PascalParser::AssignStmtContext::ASSIGN() {
  return getToken(PascalParser::ASSIGN, 0);
}

PascalParser::ExprContext* PascalParser::AssignStmtContext::expr() {
  return getRuleContext<PascalParser::ExprContext>(0);
}

PascalParser::AssignStmtContext::AssignStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any PascalParser::AssignStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitAssignStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- EmptyStmtContext ------------------------------------------------------------------

PascalParser::EmptyStmtContext::EmptyStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any PascalParser::EmptyStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitEmptyStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- CaseStmtContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::CaseStmtContext::CASE_KW() {
  return getToken(PascalParser::CASE_KW, 0);
}

PascalParser::ExprContext* PascalParser::CaseStmtContext::expr() {
  return getRuleContext<PascalParser::ExprContext>(0);
}

tree::TerminalNode* PascalParser::CaseStmtContext::OF_KW() {
  return getToken(PascalParser::OF_KW, 0);
}

std::vector<PascalParser::CaseBranchContext *> PascalParser::CaseStmtContext::caseBranch() {
  return getRuleContexts<PascalParser::CaseBranchContext>();
}

PascalParser::CaseBranchContext* PascalParser::CaseStmtContext::caseBranch(size_t i) {
  return getRuleContext<PascalParser::CaseBranchContext>(i);
}

tree::TerminalNode* PascalParser::CaseStmtContext::END_KW() {
  return getToken(PascalParser::END_KW, 0);
}

std::vector<tree::TerminalNode *> PascalParser::CaseStmtContext::SEMI() {
  return getTokens(PascalParser::SEMI);
}

tree::TerminalNode* PascalParser::CaseStmtContext::SEMI(size_t i) {
  return getToken(PascalParser::SEMI, i);
}

tree::TerminalNode* PascalParser::CaseStmtContext::ELSE_KW() {
  return getToken(PascalParser::ELSE_KW, 0);
}

PascalParser::StatementContext* PascalParser::CaseStmtContext::statement() {
  return getRuleContext<PascalParser::StatementContext>(0);
}

PascalParser::CaseStmtContext::CaseStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any PascalParser::CaseStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitCaseStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- RepeatStmtContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::RepeatStmtContext::REPEAT_KW() {
  return getToken(PascalParser::REPEAT_KW, 0);
}

std::vector<PascalParser::StatementContext *> PascalParser::RepeatStmtContext::statement() {
  return getRuleContexts<PascalParser::StatementContext>();
}

PascalParser::StatementContext* PascalParser::RepeatStmtContext::statement(size_t i) {
  return getRuleContext<PascalParser::StatementContext>(i);
}

tree::TerminalNode* PascalParser::RepeatStmtContext::UNTIL_KW() {
  return getToken(PascalParser::UNTIL_KW, 0);
}

PascalParser::ExprContext* PascalParser::RepeatStmtContext::expr() {
  return getRuleContext<PascalParser::ExprContext>(0);
}

std::vector<tree::TerminalNode *> PascalParser::RepeatStmtContext::SEMI() {
  return getTokens(PascalParser::SEMI);
}

tree::TerminalNode* PascalParser::RepeatStmtContext::SEMI(size_t i) {
  return getToken(PascalParser::SEMI, i);
}

PascalParser::RepeatStmtContext::RepeatStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any PascalParser::RepeatStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitRepeatStmt(this);
  else
    return visitor->visitChildren(this);
}
PascalParser::StatementContext* PascalParser::statement() {
  StatementContext *_localctx = _tracker.createInstance<StatementContext>(_ctx, getState());
  enterRule(_localctx, 30, PascalParser::RuleStatement);
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
    setState(291);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 21, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<PascalParser::CompoundStatementContext>(_localctx);
      enterOuterAlt(_localctx, 1);
      setState(229);
      compoundStmt();
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<PascalParser::AssignStmtContext>(_localctx);
      enterOuterAlt(_localctx, 2);
      setState(230);
      designator();
      setState(231);
      match(PascalParser::ASSIGN);
      setState(232);
      expr();
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<PascalParser::CallStmtContext>(_localctx);
      enterOuterAlt(_localctx, 3);
      setState(234);
      designator();
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<PascalParser::IfStmtContext>(_localctx);
      enterOuterAlt(_localctx, 4);
      setState(235);
      match(PascalParser::IF_KW);
      setState(236);
      expr();
      setState(237);
      match(PascalParser::THEN_KW);
      setState(238);
      statement();
      setState(241);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 16, _ctx)) {
      case 1: {
        setState(239);
        match(PascalParser::ELSE_KW);
        setState(240);
        statement();
        break;
      }

      default:
        break;
      }
      break;
    }

    case 5: {
      _localctx = _tracker.createInstance<PascalParser::WhileStmtContext>(_localctx);
      enterOuterAlt(_localctx, 5);
      setState(243);
      match(PascalParser::WHILE_KW);
      setState(244);
      expr();
      setState(245);
      match(PascalParser::DO_KW);
      setState(246);
      statement();
      break;
    }

    case 6: {
      _localctx = _tracker.createInstance<PascalParser::RepeatStmtContext>(_localctx);
      enterOuterAlt(_localctx, 6);
      setState(248);
      match(PascalParser::REPEAT_KW);
      setState(249);
      statement();
      setState(254);
      _errHandler->sync(this);
      _la = _input->LA(1);
      while (_la == PascalParser::SEMI) {
        setState(250);
        match(PascalParser::SEMI);
        setState(251);
        statement();
        setState(256);
        _errHandler->sync(this);
        _la = _input->LA(1);
      }
      setState(257);
      match(PascalParser::UNTIL_KW);
      setState(258);
      expr();
      break;
    }

    case 7: {
      _localctx = _tracker.createInstance<PascalParser::ForStmtContext>(_localctx);
      enterOuterAlt(_localctx, 7);
      setState(260);
      match(PascalParser::FOR_KW);
      setState(261);
      match(PascalParser::IDENT);
      setState(262);
      match(PascalParser::ASSIGN);
      setState(263);
      expr();
      setState(264);
      antlrcpp::downCast<ForStmtContext *>(_localctx)->dir = _input->LT(1);
      _la = _input->LA(1);
      if (!(_la == PascalParser::TO_KW

      || _la == PascalParser::DOWNTO_KW)) {
        antlrcpp::downCast<ForStmtContext *>(_localctx)->dir = _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
      setState(265);
      expr();
      setState(266);
      match(PascalParser::DO_KW);
      setState(267);
      statement();
      break;
    }

    case 8: {
      _localctx = _tracker.createInstance<PascalParser::CaseStmtContext>(_localctx);
      enterOuterAlt(_localctx, 8);
      setState(269);
      match(PascalParser::CASE_KW);
      setState(270);
      expr();
      setState(271);
      match(PascalParser::OF_KW);
      setState(272);
      caseBranch();
      setState(277);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 18, _ctx);
      while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
        if (alt == 1) {
          setState(273);
          match(PascalParser::SEMI);
          setState(274);
          caseBranch(); 
        }
        setState(279);
        _errHandler->sync(this);
        alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 18, _ctx);
      }
      setState(283);
      _errHandler->sync(this);

      switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 19, _ctx)) {
      case 1: {
        setState(280);
        match(PascalParser::SEMI);
        setState(281);
        match(PascalParser::ELSE_KW);
        setState(282);
        statement();
        break;
      }

      default:
        break;
      }
      setState(286);
      _errHandler->sync(this);

      _la = _input->LA(1);
      if (_la == PascalParser::SEMI) {
        setState(285);
        match(PascalParser::SEMI);
      }
      setState(288);
      match(PascalParser::END_KW);
      break;
    }

    case 9: {
      _localctx = _tracker.createInstance<PascalParser::EmptyStmtContext>(_localctx);
      enterOuterAlt(_localctx, 9);

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

//----------------- CaseBranchContext ------------------------------------------------------------------

PascalParser::CaseBranchContext::CaseBranchContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<PascalParser::ConstExprContext *> PascalParser::CaseBranchContext::constExpr() {
  return getRuleContexts<PascalParser::ConstExprContext>();
}

PascalParser::ConstExprContext* PascalParser::CaseBranchContext::constExpr(size_t i) {
  return getRuleContext<PascalParser::ConstExprContext>(i);
}

tree::TerminalNode* PascalParser::CaseBranchContext::COLON() {
  return getToken(PascalParser::COLON, 0);
}

PascalParser::StatementContext* PascalParser::CaseBranchContext::statement() {
  return getRuleContext<PascalParser::StatementContext>(0);
}

std::vector<tree::TerminalNode *> PascalParser::CaseBranchContext::COMMA() {
  return getTokens(PascalParser::COMMA);
}

tree::TerminalNode* PascalParser::CaseBranchContext::COMMA(size_t i) {
  return getToken(PascalParser::COMMA, i);
}


size_t PascalParser::CaseBranchContext::getRuleIndex() const {
  return PascalParser::RuleCaseBranch;
}


std::any PascalParser::CaseBranchContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitCaseBranch(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::CaseBranchContext* PascalParser::caseBranch() {
  CaseBranchContext *_localctx = _tracker.createInstance<CaseBranchContext>(_ctx, getState());
  enterRule(_localctx, 32, PascalParser::RuleCaseBranch);
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
    setState(293);
    constExpr();
    setState(298);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PascalParser::COMMA) {
      setState(294);
      match(PascalParser::COMMA);
      setState(295);
      constExpr();
      setState(300);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(301);
    match(PascalParser::COLON);
    setState(302);
    statement();
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- DesignatorContext ------------------------------------------------------------------

PascalParser::DesignatorContext::DesignatorContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<tree::TerminalNode *> PascalParser::DesignatorContext::IDENT() {
  return getTokens(PascalParser::IDENT);
}

tree::TerminalNode* PascalParser::DesignatorContext::IDENT(size_t i) {
  return getToken(PascalParser::IDENT, i);
}

std::vector<tree::TerminalNode *> PascalParser::DesignatorContext::LPAREN() {
  return getTokens(PascalParser::LPAREN);
}

tree::TerminalNode* PascalParser::DesignatorContext::LPAREN(size_t i) {
  return getToken(PascalParser::LPAREN, i);
}

std::vector<tree::TerminalNode *> PascalParser::DesignatorContext::RPAREN() {
  return getTokens(PascalParser::RPAREN);
}

tree::TerminalNode* PascalParser::DesignatorContext::RPAREN(size_t i) {
  return getToken(PascalParser::RPAREN, i);
}

std::vector<tree::TerminalNode *> PascalParser::DesignatorContext::LBRACK() {
  return getTokens(PascalParser::LBRACK);
}

tree::TerminalNode* PascalParser::DesignatorContext::LBRACK(size_t i) {
  return getToken(PascalParser::LBRACK, i);
}

std::vector<PascalParser::ExprListContext *> PascalParser::DesignatorContext::exprList() {
  return getRuleContexts<PascalParser::ExprListContext>();
}

PascalParser::ExprListContext* PascalParser::DesignatorContext::exprList(size_t i) {
  return getRuleContext<PascalParser::ExprListContext>(i);
}

std::vector<tree::TerminalNode *> PascalParser::DesignatorContext::RBRACK() {
  return getTokens(PascalParser::RBRACK);
}

tree::TerminalNode* PascalParser::DesignatorContext::RBRACK(size_t i) {
  return getToken(PascalParser::RBRACK, i);
}

std::vector<tree::TerminalNode *> PascalParser::DesignatorContext::DOT() {
  return getTokens(PascalParser::DOT);
}

tree::TerminalNode* PascalParser::DesignatorContext::DOT(size_t i) {
  return getToken(PascalParser::DOT, i);
}

std::vector<tree::TerminalNode *> PascalParser::DesignatorContext::CARET() {
  return getTokens(PascalParser::CARET);
}

tree::TerminalNode* PascalParser::DesignatorContext::CARET(size_t i) {
  return getToken(PascalParser::CARET, i);
}


size_t PascalParser::DesignatorContext::getRuleIndex() const {
  return PascalParser::RuleDesignator;
}


std::any PascalParser::DesignatorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitDesignator(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::DesignatorContext* PascalParser::designator() {
  DesignatorContext *_localctx = _tracker.createInstance<DesignatorContext>(_ctx, getState());
  enterRule(_localctx, 34, PascalParser::RuleDesignator);
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
    match(PascalParser::IDENT);
    setState(319);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 72251108084416512) != 0)) {
      setState(317);
      _errHandler->sync(this);
      switch (_input->LA(1)) {
        case PascalParser::LPAREN: {
          setState(305);
          match(PascalParser::LPAREN);
          setState(307);
          _errHandler->sync(this);

          _la = _input->LA(1);
          if ((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & 2175414593436778496) != 0)) {
            setState(306);
            exprList();
          }
          setState(309);
          match(PascalParser::RPAREN);
          break;
        }

        case PascalParser::LBRACK: {
          setState(310);
          match(PascalParser::LBRACK);
          setState(311);
          exprList();
          setState(312);
          match(PascalParser::RBRACK);
          break;
        }

        case PascalParser::DOT: {
          setState(314);
          match(PascalParser::DOT);
          setState(315);
          match(PascalParser::IDENT);
          break;
        }

        case PascalParser::CARET: {
          setState(316);
          match(PascalParser::CARET);
          break;
        }

      default:
        throw NoViableAltException(this);
      }
      setState(321);
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

//----------------- ExprListContext ------------------------------------------------------------------

PascalParser::ExprListContext::ExprListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<PascalParser::ExprContext *> PascalParser::ExprListContext::expr() {
  return getRuleContexts<PascalParser::ExprContext>();
}

PascalParser::ExprContext* PascalParser::ExprListContext::expr(size_t i) {
  return getRuleContext<PascalParser::ExprContext>(i);
}

std::vector<tree::TerminalNode *> PascalParser::ExprListContext::COMMA() {
  return getTokens(PascalParser::COMMA);
}

tree::TerminalNode* PascalParser::ExprListContext::COMMA(size_t i) {
  return getToken(PascalParser::COMMA, i);
}


size_t PascalParser::ExprListContext::getRuleIndex() const {
  return PascalParser::RuleExprList;
}


std::any PascalParser::ExprListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitExprList(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::ExprListContext* PascalParser::exprList() {
  ExprListContext *_localctx = _tracker.createInstance<ExprListContext>(_ctx, getState());
  enterRule(_localctx, 36, PascalParser::RuleExprList);
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
    setState(322);
    expr();
    setState(327);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == PascalParser::COMMA) {
      setState(323);
      match(PascalParser::COMMA);
      setState(324);
      expr();
      setState(329);
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

//----------------- ExprContext ------------------------------------------------------------------

PascalParser::ExprContext::ExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<PascalParser::SimpleExprContext *> PascalParser::ExprContext::simpleExpr() {
  return getRuleContexts<PascalParser::SimpleExprContext>();
}

PascalParser::SimpleExprContext* PascalParser::ExprContext::simpleExpr(size_t i) {
  return getRuleContext<PascalParser::SimpleExprContext>(i);
}

PascalParser::RelOpContext* PascalParser::ExprContext::relOp() {
  return getRuleContext<PascalParser::RelOpContext>(0);
}


size_t PascalParser::ExprContext::getRuleIndex() const {
  return PascalParser::RuleExpr;
}


std::any PascalParser::ExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitExpr(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::ExprContext* PascalParser::expr() {
  ExprContext *_localctx = _tracker.createInstance<ExprContext>(_ctx, getState());
  enterRule(_localctx, 38, PascalParser::RuleExpr);
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
    setState(330);
    simpleExpr();
    setState(334);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 3942573953515520) != 0)) {
      setState(331);
      relOp();
      setState(332);
      simpleExpr();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- RelOpContext ------------------------------------------------------------------

PascalParser::RelOpContext::RelOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PascalParser::RelOpContext::EQ() {
  return getToken(PascalParser::EQ, 0);
}

tree::TerminalNode* PascalParser::RelOpContext::NE() {
  return getToken(PascalParser::NE, 0);
}

tree::TerminalNode* PascalParser::RelOpContext::LT() {
  return getToken(PascalParser::LT, 0);
}

tree::TerminalNode* PascalParser::RelOpContext::LE() {
  return getToken(PascalParser::LE, 0);
}

tree::TerminalNode* PascalParser::RelOpContext::GT() {
  return getToken(PascalParser::GT, 0);
}

tree::TerminalNode* PascalParser::RelOpContext::GE() {
  return getToken(PascalParser::GE, 0);
}

tree::TerminalNode* PascalParser::RelOpContext::IN_KW() {
  return getToken(PascalParser::IN_KW, 0);
}


size_t PascalParser::RelOpContext::getRuleIndex() const {
  return PascalParser::RuleRelOp;
}


std::any PascalParser::RelOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitRelOp(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::RelOpContext* PascalParser::relOp() {
  RelOpContext *_localctx = _tracker.createInstance<RelOpContext>(_ctx, getState());
  enterRule(_localctx, 40, PascalParser::RuleRelOp);
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
    setState(336);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 3942573953515520) != 0))) {
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

//----------------- SimpleExprContext ------------------------------------------------------------------

PascalParser::SimpleExprContext::SimpleExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<PascalParser::TermContext *> PascalParser::SimpleExprContext::term() {
  return getRuleContexts<PascalParser::TermContext>();
}

PascalParser::TermContext* PascalParser::SimpleExprContext::term(size_t i) {
  return getRuleContext<PascalParser::TermContext>(i);
}

std::vector<PascalParser::AddOpContext *> PascalParser::SimpleExprContext::addOp() {
  return getRuleContexts<PascalParser::AddOpContext>();
}

PascalParser::AddOpContext* PascalParser::SimpleExprContext::addOp(size_t i) {
  return getRuleContext<PascalParser::AddOpContext>(i);
}

tree::TerminalNode* PascalParser::SimpleExprContext::PLUS() {
  return getToken(PascalParser::PLUS, 0);
}

tree::TerminalNode* PascalParser::SimpleExprContext::MINUS() {
  return getToken(PascalParser::MINUS, 0);
}


size_t PascalParser::SimpleExprContext::getRuleIndex() const {
  return PascalParser::RuleSimpleExpr;
}


std::any PascalParser::SimpleExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitSimpleExpr(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::SimpleExprContext* PascalParser::simpleExpr() {
  SimpleExprContext *_localctx = _tracker.createInstance<SimpleExprContext>(_ctx, getState());
  enterRule(_localctx, 42, PascalParser::RuleSimpleExpr);
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
    setState(339);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PascalParser::PLUS

    || _la == PascalParser::MINUS) {
      setState(338);
      antlrcpp::downCast<SimpleExprContext *>(_localctx)->sign = _input->LT(1);
      _la = _input->LA(1);
      if (!(_la == PascalParser::PLUS

      || _la == PascalParser::MINUS)) {
        antlrcpp::downCast<SimpleExprContext *>(_localctx)->sign = _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
    }
    setState(341);
    term();
    setState(347);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 13510798949220352) != 0)) {
      setState(342);
      addOp();
      setState(343);
      term();
      setState(349);
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

//----------------- AddOpContext ------------------------------------------------------------------

PascalParser::AddOpContext::AddOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PascalParser::AddOpContext::PLUS() {
  return getToken(PascalParser::PLUS, 0);
}

tree::TerminalNode* PascalParser::AddOpContext::MINUS() {
  return getToken(PascalParser::MINUS, 0);
}

tree::TerminalNode* PascalParser::AddOpContext::OR_KW() {
  return getToken(PascalParser::OR_KW, 0);
}


size_t PascalParser::AddOpContext::getRuleIndex() const {
  return PascalParser::RuleAddOp;
}


std::any PascalParser::AddOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitAddOp(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::AddOpContext* PascalParser::addOp() {
  AddOpContext *_localctx = _tracker.createInstance<AddOpContext>(_ctx, getState());
  enterRule(_localctx, 44, PascalParser::RuleAddOp);
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
    setState(350);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 13510798949220352) != 0))) {
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

//----------------- TermContext ------------------------------------------------------------------

PascalParser::TermContext::TermContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<PascalParser::FactorContext *> PascalParser::TermContext::factor() {
  return getRuleContexts<PascalParser::FactorContext>();
}

PascalParser::FactorContext* PascalParser::TermContext::factor(size_t i) {
  return getRuleContext<PascalParser::FactorContext>(i);
}

std::vector<PascalParser::MulOpContext *> PascalParser::TermContext::mulOp() {
  return getRuleContexts<PascalParser::MulOpContext>();
}

PascalParser::MulOpContext* PascalParser::TermContext::mulOp(size_t i) {
  return getRuleContext<PascalParser::MulOpContext>(i);
}


size_t PascalParser::TermContext::getRuleIndex() const {
  return PascalParser::RuleTerm;
}


std::any PascalParser::TermContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitTerm(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::TermContext* PascalParser::term() {
  TermContext *_localctx = _tracker.createInstance<TermContext>(_ctx, getState());
  enterRule(_localctx, 46, PascalParser::RuleTerm);
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
    setState(352);
    factor();
    setState(358);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while ((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 54043196367306752) != 0)) {
      setState(353);
      mulOp();
      setState(354);
      factor();
      setState(360);
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

//----------------- MulOpContext ------------------------------------------------------------------

PascalParser::MulOpContext::MulOpContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PascalParser::MulOpContext::STAR() {
  return getToken(PascalParser::STAR, 0);
}

tree::TerminalNode* PascalParser::MulOpContext::SLASH() {
  return getToken(PascalParser::SLASH, 0);
}

tree::TerminalNode* PascalParser::MulOpContext::DIV_KW() {
  return getToken(PascalParser::DIV_KW, 0);
}

tree::TerminalNode* PascalParser::MulOpContext::MOD_KW() {
  return getToken(PascalParser::MOD_KW, 0);
}

tree::TerminalNode* PascalParser::MulOpContext::AND_KW() {
  return getToken(PascalParser::AND_KW, 0);
}


size_t PascalParser::MulOpContext::getRuleIndex() const {
  return PascalParser::RuleMulOp;
}


std::any PascalParser::MulOpContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitMulOp(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::MulOpContext* PascalParser::mulOp() {
  MulOpContext *_localctx = _tracker.createInstance<MulOpContext>(_ctx, getState());
  enterRule(_localctx, 48, PascalParser::RuleMulOp);
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
    setState(361);
    _la = _input->LA(1);
    if (!((((_la & ~ 0x3fULL) == 0) &&
      ((1ULL << _la) & 54043196367306752) != 0))) {
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

//----------------- FactorContext ------------------------------------------------------------------

PascalParser::FactorContext::FactorContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t PascalParser::FactorContext::getRuleIndex() const {
  return PascalParser::RuleFactor;
}

void PascalParser::FactorContext::copyFrom(FactorContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- IntFactorContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::IntFactorContext::INTLIT() {
  return getToken(PascalParser::INTLIT, 0);
}

PascalParser::IntFactorContext::IntFactorContext(FactorContext *ctx) { copyFrom(ctx); }


std::any PascalParser::IntFactorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitIntFactor(this);
  else
    return visitor->visitChildren(this);
}
//----------------- StringFactorContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::StringFactorContext::STRINGLIT() {
  return getToken(PascalParser::STRINGLIT, 0);
}

PascalParser::StringFactorContext::StringFactorContext(FactorContext *ctx) { copyFrom(ctx); }


std::any PascalParser::StringFactorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitStringFactor(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ParenFactorContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::ParenFactorContext::LPAREN() {
  return getToken(PascalParser::LPAREN, 0);
}

PascalParser::ExprContext* PascalParser::ParenFactorContext::expr() {
  return getRuleContext<PascalParser::ExprContext>(0);
}

tree::TerminalNode* PascalParser::ParenFactorContext::RPAREN() {
  return getToken(PascalParser::RPAREN, 0);
}

PascalParser::ParenFactorContext::ParenFactorContext(FactorContext *ctx) { copyFrom(ctx); }


std::any PascalParser::ParenFactorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitParenFactor(this);
  else
    return visitor->visitChildren(this);
}
//----------------- SetFactorContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::SetFactorContext::LBRACK() {
  return getToken(PascalParser::LBRACK, 0);
}

tree::TerminalNode* PascalParser::SetFactorContext::RBRACK() {
  return getToken(PascalParser::RBRACK, 0);
}

std::vector<PascalParser::SetElementContext *> PascalParser::SetFactorContext::setElement() {
  return getRuleContexts<PascalParser::SetElementContext>();
}

PascalParser::SetElementContext* PascalParser::SetFactorContext::setElement(size_t i) {
  return getRuleContext<PascalParser::SetElementContext>(i);
}

std::vector<tree::TerminalNode *> PascalParser::SetFactorContext::COMMA() {
  return getTokens(PascalParser::COMMA);
}

tree::TerminalNode* PascalParser::SetFactorContext::COMMA(size_t i) {
  return getToken(PascalParser::COMMA, i);
}

PascalParser::SetFactorContext::SetFactorContext(FactorContext *ctx) { copyFrom(ctx); }


std::any PascalParser::SetFactorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitSetFactor(this);
  else
    return visitor->visitChildren(this);
}
//----------------- DesignatorFactorContext ------------------------------------------------------------------

PascalParser::DesignatorContext* PascalParser::DesignatorFactorContext::designator() {
  return getRuleContext<PascalParser::DesignatorContext>(0);
}

PascalParser::DesignatorFactorContext::DesignatorFactorContext(FactorContext *ctx) { copyFrom(ctx); }


std::any PascalParser::DesignatorFactorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitDesignatorFactor(this);
  else
    return visitor->visitChildren(this);
}
//----------------- TrueFactorContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::TrueFactorContext::TRUE_KW() {
  return getToken(PascalParser::TRUE_KW, 0);
}

PascalParser::TrueFactorContext::TrueFactorContext(FactorContext *ctx) { copyFrom(ctx); }


std::any PascalParser::TrueFactorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitTrueFactor(this);
  else
    return visitor->visitChildren(this);
}
//----------------- FalseFactorContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::FalseFactorContext::FALSE_KW() {
  return getToken(PascalParser::FALSE_KW, 0);
}

PascalParser::FalseFactorContext::FalseFactorContext(FactorContext *ctx) { copyFrom(ctx); }


std::any PascalParser::FalseFactorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitFalseFactor(this);
  else
    return visitor->visitChildren(this);
}
//----------------- NotFactorContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::NotFactorContext::NOT_KW() {
  return getToken(PascalParser::NOT_KW, 0);
}

PascalParser::FactorContext* PascalParser::NotFactorContext::factor() {
  return getRuleContext<PascalParser::FactorContext>(0);
}

PascalParser::NotFactorContext::NotFactorContext(FactorContext *ctx) { copyFrom(ctx); }


std::any PascalParser::NotFactorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitNotFactor(this);
  else
    return visitor->visitChildren(this);
}
//----------------- RealFactorContext ------------------------------------------------------------------

tree::TerminalNode* PascalParser::RealFactorContext::REALLIT() {
  return getToken(PascalParser::REALLIT, 0);
}

PascalParser::RealFactorContext::RealFactorContext(FactorContext *ctx) { copyFrom(ctx); }


std::any PascalParser::RealFactorContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitRealFactor(this);
  else
    return visitor->visitChildren(this);
}
PascalParser::FactorContext* PascalParser::factor() {
  FactorContext *_localctx = _tracker.createInstance<FactorContext>(_ctx, getState());
  enterRule(_localctx, 50, PascalParser::RuleFactor);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(387);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case PascalParser::IDENT: {
        _localctx = _tracker.createInstance<PascalParser::DesignatorFactorContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(363);
        designator();
        break;
      }

      case PascalParser::INTLIT: {
        _localctx = _tracker.createInstance<PascalParser::IntFactorContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(364);
        match(PascalParser::INTLIT);
        break;
      }

      case PascalParser::REALLIT: {
        _localctx = _tracker.createInstance<PascalParser::RealFactorContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(365);
        match(PascalParser::REALLIT);
        break;
      }

      case PascalParser::STRINGLIT: {
        _localctx = _tracker.createInstance<PascalParser::StringFactorContext>(_localctx);
        enterOuterAlt(_localctx, 4);
        setState(366);
        match(PascalParser::STRINGLIT);
        break;
      }

      case PascalParser::TRUE_KW: {
        _localctx = _tracker.createInstance<PascalParser::TrueFactorContext>(_localctx);
        enterOuterAlt(_localctx, 5);
        setState(367);
        match(PascalParser::TRUE_KW);
        break;
      }

      case PascalParser::FALSE_KW: {
        _localctx = _tracker.createInstance<PascalParser::FalseFactorContext>(_localctx);
        enterOuterAlt(_localctx, 6);
        setState(368);
        match(PascalParser::FALSE_KW);
        break;
      }

      case PascalParser::LPAREN: {
        _localctx = _tracker.createInstance<PascalParser::ParenFactorContext>(_localctx);
        enterOuterAlt(_localctx, 7);
        setState(369);
        match(PascalParser::LPAREN);
        setState(370);
        expr();
        setState(371);
        match(PascalParser::RPAREN);
        break;
      }

      case PascalParser::NOT_KW: {
        _localctx = _tracker.createInstance<PascalParser::NotFactorContext>(_localctx);
        enterOuterAlt(_localctx, 8);
        setState(373);
        match(PascalParser::NOT_KW);
        setState(374);
        factor();
        break;
      }

      case PascalParser::LBRACK: {
        _localctx = _tracker.createInstance<PascalParser::SetFactorContext>(_localctx);
        enterOuterAlt(_localctx, 9);
        setState(375);
        match(PascalParser::LBRACK);
        setState(384);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 2175414593436778496) != 0)) {
          setState(376);
          setElement();
          setState(381);
          _errHandler->sync(this);
          _la = _input->LA(1);
          while (_la == PascalParser::COMMA) {
            setState(377);
            match(PascalParser::COMMA);
            setState(378);
            setElement();
            setState(383);
            _errHandler->sync(this);
            _la = _input->LA(1);
          }
        }
        setState(386);
        match(PascalParser::RBRACK);
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

//----------------- SetElementContext ------------------------------------------------------------------

PascalParser::SetElementContext::SetElementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<PascalParser::ExprContext *> PascalParser::SetElementContext::expr() {
  return getRuleContexts<PascalParser::ExprContext>();
}

PascalParser::ExprContext* PascalParser::SetElementContext::expr(size_t i) {
  return getRuleContext<PascalParser::ExprContext>(i);
}

tree::TerminalNode* PascalParser::SetElementContext::DOTDOT() {
  return getToken(PascalParser::DOTDOT, 0);
}


size_t PascalParser::SetElementContext::getRuleIndex() const {
  return PascalParser::RuleSetElement;
}


std::any PascalParser::SetElementContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitSetElement(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::SetElementContext* PascalParser::setElement() {
  SetElementContext *_localctx = _tracker.createInstance<SetElementContext>(_ctx, getState());
  enterRule(_localctx, 52, PascalParser::RuleSetElement);
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
    setState(389);
    expr();
    setState(392);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == PascalParser::DOTDOT) {
      setState(390);
      match(PascalParser::DOTDOT);
      setState(391);
      expr();
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ConstExprContext ------------------------------------------------------------------

PascalParser::ConstExprContext::ConstExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* PascalParser::ConstExprContext::INTLIT() {
  return getToken(PascalParser::INTLIT, 0);
}

tree::TerminalNode* PascalParser::ConstExprContext::REALLIT() {
  return getToken(PascalParser::REALLIT, 0);
}

tree::TerminalNode* PascalParser::ConstExprContext::IDENT() {
  return getToken(PascalParser::IDENT, 0);
}

tree::TerminalNode* PascalParser::ConstExprContext::PLUS() {
  return getToken(PascalParser::PLUS, 0);
}

tree::TerminalNode* PascalParser::ConstExprContext::MINUS() {
  return getToken(PascalParser::MINUS, 0);
}

tree::TerminalNode* PascalParser::ConstExprContext::STRINGLIT() {
  return getToken(PascalParser::STRINGLIT, 0);
}


size_t PascalParser::ConstExprContext::getRuleIndex() const {
  return PascalParser::RuleConstExpr;
}


std::any PascalParser::ConstExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<PascalParserVisitor*>(visitor))
    return parserVisitor->visitConstExpr(this);
  else
    return visitor->visitChildren(this);
}

PascalParser::ConstExprContext* PascalParser::constExpr() {
  ConstExprContext *_localctx = _tracker.createInstance<ConstExprContext>(_ctx, getState());
  enterRule(_localctx, 54, PascalParser::RuleConstExpr);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(399);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case PascalParser::PLUS:
      case PascalParser::MINUS:
      case PascalParser::IDENT:
      case PascalParser::REALLIT:
      case PascalParser::INTLIT: {
        enterOuterAlt(_localctx, 1);
        setState(395);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == PascalParser::PLUS

        || _la == PascalParser::MINUS) {
          setState(394);
          antlrcpp::downCast<ConstExprContext *>(_localctx)->sign = _input->LT(1);
          _la = _input->LA(1);
          if (!(_la == PascalParser::PLUS

          || _la == PascalParser::MINUS)) {
            antlrcpp::downCast<ConstExprContext *>(_localctx)->sign = _errHandler->recoverInline(this);
          }
          else {
            _errHandler->reportMatch(this);
            consume();
          }
        }
        setState(397);
        _la = _input->LA(1);
        if (!((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 1008806316530991104) != 0))) {
        _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        break;
      }

      case PascalParser::STRINGLIT: {
        enterOuterAlt(_localctx, 2);
        setState(398);
        match(PascalParser::STRINGLIT);
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

void PascalParser::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  pascalparserParserInitialize();
#else
  ::antlr4::internal::call_once(pascalparserParserOnceFlag, pascalparserParserInitialize);
#endif
}
