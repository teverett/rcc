
// Generated from /Users/tom/projects/pcc/grammar/pascal/PascalLexer.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  PascalLexer : public antlr4::Lexer {
public:
  enum {
    PROGRAM_KW = 1, VAR_KW = 2, CONST_KW = 3, TYPE_KW = 4, ARRAY_KW = 5, 
    OF_KW = 6, RECORD_KW = 7, SET_KW = 8, PROCEDURE_KW = 9, FUNCTION_KW = 10, 
    BEGIN_KW = 11, END_KW = 12, IF_KW = 13, THEN_KW = 14, ELSE_KW = 15, 
    WHILE_KW = 16, DO_KW = 17, REPEAT_KW = 18, UNTIL_KW = 19, FOR_KW = 20, 
    TO_KW = 21, DOWNTO_KW = 22, CASE_KW = 23, NOT_KW = 24, AND_KW = 25, 
    OR_KW = 26, IN_KW = 27, DIV_KW = 28, MOD_KW = 29, INTEGER_KW = 30, REAL_KW = 31, 
    CHAR_KW = 32, BOOLEAN_KW = 33, TRUE_KW = 34, FALSE_KW = 35, ASSIGN = 36, 
    DOTDOT = 37, LE = 38, GE = 39, NE = 40, SEMI = 41, COLON = 42, COMMA = 43, 
    DOT = 44, LPAREN = 45, RPAREN = 46, LBRACK = 47, RBRACK = 48, EQ = 49, 
    LT = 50, GT = 51, PLUS = 52, MINUS = 53, STAR = 54, SLASH = 55, CARET = 56, 
    IDENT = 57, REALLIT = 58, INTLIT = 59, STRINGLIT = 60, BRACE_COMMENT = 61, 
    PAREN_COMMENT = 62, WS = 63
  };

  explicit PascalLexer(antlr4::CharStream *input);

  ~PascalLexer() override;


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

