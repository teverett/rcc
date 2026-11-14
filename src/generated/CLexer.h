
// Generated from /Users/tom/projects/pcc/grammar/CLexer.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  CLexer : public antlr4::Lexer {
public:
  enum {
    INT_KW = 1, CHAR_KW = 2, SHORT_KW = 3, LONG_KW = 4, SIGNED_KW = 5, UNSIGNED_KW = 6, 
    VOID_KW = 7, SIZEOF_KW = 8, IF_KW = 9, ELSE_KW = 10, WHILE_KW = 11, 
    FOR_KW = 12, SWITCH_KW = 13, CASE_KW = 14, DEFAULT_KW = 15, RETURN_KW = 16, 
    BREAK_KW = 17, CONTINUE_KW = 18, GOTO_KW = 19, EXTERN_KW = 20, STATIC_KW = 21, 
    CONST_KW = 22, REGISTER_KW = 23, STRUCT_KW = 24, ENUM_KW = 25, LPAREN = 26, 
    RPAREN = 27, LBRACK = 28, RBRACK = 29, LBRACE = 30, RBRACE = 31, AT = 32, 
    SEMI = 33, COMMA = 34, ASSIGN = 35, STAR = 36, AMP = 37, MINUS = 38, 
    BANG = 39, TILDE = 40, SLASH = 41, PERCENT = 42, PLUS = 43, SHL = 44, 
    SHR = 45, LE = 46, GE = 47, LT = 48, GT = 49, EQ = 50, NE = 51, CARET = 52, 
    PIPE = 53, ANDAND = 54, OROR = 55, PLUSEQ = 56, MINUSEQ = 57, STAREQ = 58, 
    SLASHEQ = 59, PERCENTEQ = 60, AMPEQ = 61, CARETEQ = 62, PIPEEQ = 63, 
    SHLEQ = 64, SHREQ = 65, INC = 66, DEC = 67, QUESTION = 68, COLON = 69, 
    DOT = 70, ARROW = 71, IDENT = 72, NUMBER = 73, CHARLIT = 74, STRINGLIT = 75, 
    LINE_COMMENT = 76, BLOCK_COMMENT = 77, WS = 78, ASM_BEGIN = 79, ASM_TEXT = 80, 
    ASM_END = 81
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

