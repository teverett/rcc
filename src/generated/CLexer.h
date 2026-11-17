
// Generated from /Users/tom/projects/pcc/grammar/CLexer.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  CLexer : public antlr4::Lexer {
public:
  enum {
    INT_KW = 1, CHAR_KW = 2, SHORT_KW = 3, LONG_KW = 4, FLOAT_KW = 5, DOUBLE_KW = 6, 
    SIGNED_KW = 7, UNSIGNED_KW = 8, VOID_KW = 9, SIZEOF_KW = 10, IF_KW = 11, 
    ELSE_KW = 12, WHILE_KW = 13, FOR_KW = 14, SWITCH_KW = 15, CASE_KW = 16, 
    DEFAULT_KW = 17, RETURN_KW = 18, BREAK_KW = 19, CONTINUE_KW = 20, GOTO_KW = 21, 
    EXTERN_KW = 22, STATIC_KW = 23, CONST_KW = 24, REGISTER_KW = 25, STRUCT_KW = 26, 
    ENUM_KW = 27, LPAREN = 28, RPAREN = 29, LBRACK = 30, RBRACK = 31, LBRACE = 32, 
    RBRACE = 33, AT = 34, SEMI = 35, COMMA = 36, ASSIGN = 37, STAR = 38, 
    AMP = 39, MINUS = 40, BANG = 41, TILDE = 42, SLASH = 43, PERCENT = 44, 
    PLUS = 45, SHL = 46, SHR = 47, LE = 48, GE = 49, LT = 50, GT = 51, EQ = 52, 
    NE = 53, CARET = 54, PIPE = 55, ANDAND = 56, OROR = 57, PLUSEQ = 58, 
    MINUSEQ = 59, STAREQ = 60, SLASHEQ = 61, PERCENTEQ = 62, AMPEQ = 63, 
    CARETEQ = 64, PIPEEQ = 65, SHLEQ = 66, SHREQ = 67, INC = 68, DEC = 69, 
    QUESTION = 70, COLON = 71, DOT = 72, ARROW = 73, IDENT = 74, NUMBER = 75, 
    FLOAT_NUMBER = 76, CHARLIT = 77, STRINGLIT = 78, LINE_COMMENT = 79, 
    BLOCK_COMMENT = 80, WS = 81, ASM_BEGIN = 82, ASM_TEXT = 83, ASM_END = 84
  };

  enum {
    ASM_MODE = 1
  };

  explicit CLexer(antlr4::CharStream *input);

  ~CLexer() override;


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

