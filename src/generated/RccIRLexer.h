
// Generated from /Users/tom/projects/pcc/grammar/rccir/RccIRLexer.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  RccIRLexer : public antlr4::Lexer {
public:
  enum {
    GLOBALS_HEADER = 1, FUNCTIONS_HEADER = 2, INT_KW = 3, CHAR_KW = 4, SHORT_KW = 5, 
    LONG_KW = 6, FLOAT_KW = 7, DOUBLE_KW = 8, UNSIGNED_KW = 9, VOID_KW = 10, 
    STRUCT_KW = 11, LOCAL_KW = 12, IF_KW = 13, ELSE_KW = 14, WHILE_KW = 15, 
    RETURN_KW = 16, BREAK_KW = 17, CONTINUE_KW = 18, GOTO_KW = 19, POST_KW = 20, 
    OLDVAL_KW = 21, LPAREN = 22, RPAREN = 23, LBRACK = 24, RBRACK = 25, 
    LBRACE = 26, RBRACE = 27, SEMI = 28, COMMA = 29, ASSIGN = 30, HASH = 31, 
    STAR = 32, AMP = 33, MINUS = 34, BANG = 35, TILDE = 36, SLASH = 37, 
    PERCENT = 38, PLUS = 39, SHL = 40, SHR = 41, LE = 42, GE = 43, LT = 44, 
    GT = 45, EQ = 46, NE = 47, CARET = 48, PIPE = 49, ANDAND = 50, OROR = 51, 
    PLUSEQ = 52, MINUSEQ = 53, STAREQ = 54, SLASHEQ = 55, PERCENTEQ = 56, 
    AMPEQ = 57, CARETEQ = 58, PIPEEQ = 59, SHLEQ = 60, SHREQ = 61, QUESTION = 62, 
    COLON = 63, IDENT = 64, NUMBER = 65, FLOAT_NUMBER = 66, WS = 67, ASM_BEGIN = 68, 
    ASM_TEXT = 69, ASM_END = 70
  };

  enum {
    ASM_MODE = 1
  };

  explicit RccIRLexer(antlr4::CharStream *input);

  ~RccIRLexer() override;


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

