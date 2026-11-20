
// Generated from /Users/tom/projects/pcc/grammar/basic/BasicLexer.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  BasicLexer : public antlr4::Lexer {
public:
  enum {
    LET_KW = 1, PRINT_KW = 2, INPUT_KW = 3, IF_KW = 4, THEN_KW = 5, ELSE_KW = 6, 
    FOR_KW = 7, TO_KW = 8, STEP_KW = 9, NEXT_KW = 10, GOTO_KW = 11, GOSUB_KW = 12, 
    RETURN_KW = 13, DIM_KW = 14, END_KW = 15, AND_KW = 16, OR_KW = 17, NOT_KW = 18, 
    LPAREN = 19, RPAREN = 20, COMMA = 21, SEMI = 22, COLON = 23, PLUS = 24, 
    MINUS = 25, STAR = 26, SLASH = 27, LE = 28, GE = 29, NE = 30, LT = 31, 
    GT = 32, EQ = 33, IDENT = 34, NUMBER = 35, STRINGLIT = 36, REM_COMMENT = 37, 
    LINE_COMMENT = 38, WS = 39, NEWLINE = 40
  };

  explicit BasicLexer(antlr4::CharStream *input);

  ~BasicLexer() override;


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

