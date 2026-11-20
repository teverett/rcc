
// Generated from /Users/tom/projects/pcc/grammar/basic/BasicParser.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  BasicParser : public antlr4::Parser {
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

  enum {
    RuleProgram = 0, RuleLine = 1, RuleStatement = 2, RulePrintList = 3, 
    RuleExpr = 4
  };

  explicit BasicParser(antlr4::TokenStream *input);

  BasicParser(antlr4::TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options);

  ~BasicParser() override;

  std::string getGrammarFileName() const override;

  const antlr4::atn::ATN& getATN() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;


  class ProgramContext;
  class LineContext;
  class StatementContext;
  class PrintListContext;
  class ExprContext; 

  class  ProgramContext : public antlr4::ParserRuleContext {
  public:
    ProgramContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EOF();
    std::vector<antlr4::tree::TerminalNode *> NEWLINE();
    antlr4::tree::TerminalNode* NEWLINE(size_t i);
    std::vector<LineContext *> line();
    LineContext* line(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ProgramContext* program();

  class  LineContext : public antlr4::ParserRuleContext {
  public:
    LineContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *NUMBER();
    std::vector<StatementContext *> statement();
    StatementContext* statement(size_t i);
    antlr4::tree::TerminalNode *NEWLINE();
    std::vector<antlr4::tree::TerminalNode *> COLON();
    antlr4::tree::TerminalNode* COLON(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  LineContext* line();

  class  StatementContext : public antlr4::ParserRuleContext {
  public:
    StatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
   
    StatementContext() = default;
    void copyFrom(StatementContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;

   
  };

  class  EndStmtContext : public StatementContext {
  public:
    EndStmtContext(StatementContext *ctx);

    antlr4::tree::TerminalNode *END_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  PrintStmtContext : public StatementContext {
  public:
    PrintStmtContext(StatementContext *ctx);

    antlr4::tree::TerminalNode *PRINT_KW();
    PrintListContext *printList();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  ForStmtContext : public StatementContext {
  public:
    ForStmtContext(StatementContext *ctx);

    antlr4::Token *loopVar = nullptr;
    BasicParser::ExprContext *from = nullptr;
    BasicParser::ExprContext *to = nullptr;
    BasicParser::ExprContext *step = nullptr;
    antlr4::tree::TerminalNode *FOR_KW();
    antlr4::tree::TerminalNode *EQ();
    antlr4::tree::TerminalNode *TO_KW();
    antlr4::tree::TerminalNode *IDENT();
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    antlr4::tree::TerminalNode *STEP_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  DimStmtContext : public StatementContext {
  public:
    DimStmtContext(StatementContext *ctx);

    antlr4::Token *name = nullptr;
    antlr4::Token *numberToken = nullptr;
    std::vector<antlr4::Token *> dims;
    antlr4::tree::TerminalNode *DIM_KW();
    antlr4::tree::TerminalNode *LPAREN();
    antlr4::tree::TerminalNode *RPAREN();
    antlr4::tree::TerminalNode *IDENT();
    std::vector<antlr4::tree::TerminalNode *> NUMBER();
    antlr4::tree::TerminalNode* NUMBER(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  IfStmtContext : public StatementContext {
  public:
    IfStmtContext(StatementContext *ctx);

    BasicParser::ExprContext *cond = nullptr;
    BasicParser::StatementContext *thenStmt = nullptr;
    BasicParser::StatementContext *elseStmt = nullptr;
    antlr4::tree::TerminalNode *IF_KW();
    antlr4::tree::TerminalNode *THEN_KW();
    ExprContext *expr();
    std::vector<StatementContext *> statement();
    StatementContext* statement(size_t i);
    antlr4::tree::TerminalNode *ELSE_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  LetStmtContext : public StatementContext {
  public:
    LetStmtContext(StatementContext *ctx);

    antlr4::Token *target = nullptr;
    BasicParser::ExprContext *exprContext = nullptr;
    std::vector<ExprContext *> idx;
    BasicParser::ExprContext *val = nullptr;
    antlr4::tree::TerminalNode *EQ();
    antlr4::tree::TerminalNode *IDENT();
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    antlr4::tree::TerminalNode *LET_KW();
    antlr4::tree::TerminalNode *LPAREN();
    antlr4::tree::TerminalNode *RPAREN();
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  GotoShorthandStmtContext : public StatementContext {
  public:
    GotoShorthandStmtContext(StatementContext *ctx);

    antlr4::Token *target = nullptr;
    antlr4::tree::TerminalNode *NUMBER();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  GotoStmtContext : public StatementContext {
  public:
    GotoStmtContext(StatementContext *ctx);

    antlr4::Token *target = nullptr;
    antlr4::tree::TerminalNode *GOTO_KW();
    antlr4::tree::TerminalNode *NUMBER();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  GosubStmtContext : public StatementContext {
  public:
    GosubStmtContext(StatementContext *ctx);

    antlr4::Token *target = nullptr;
    antlr4::tree::TerminalNode *GOSUB_KW();
    antlr4::tree::TerminalNode *NUMBER();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  ReturnStmtContext : public StatementContext {
  public:
    ReturnStmtContext(StatementContext *ctx);

    antlr4::tree::TerminalNode *RETURN_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  NextStmtContext : public StatementContext {
  public:
    NextStmtContext(StatementContext *ctx);

    antlr4::tree::TerminalNode *NEXT_KW();
    antlr4::tree::TerminalNode *IDENT();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  InputStmtContext : public StatementContext {
  public:
    InputStmtContext(StatementContext *ctx);

    antlr4::Token *prompt = nullptr;
    antlr4::Token *target = nullptr;
    antlr4::tree::TerminalNode *INPUT_KW();
    antlr4::tree::TerminalNode *IDENT();
    antlr4::tree::TerminalNode *COMMA();
    antlr4::tree::TerminalNode *STRINGLIT();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  StatementContext* statement();

  class  PrintListContext : public antlr4::ParserRuleContext {
  public:
    BasicParser::ExprContext *exprContext = nullptr;
    std::vector<ExprContext *> item;
    antlr4::Token *s21 = nullptr;
    std::vector<antlr4::Token *> sep;
    antlr4::Token *s22 = nullptr;
    antlr4::Token *_tset341 = nullptr;
    antlr4::Token *trailingSep = nullptr;
    PrintListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);
    std::vector<antlr4::tree::TerminalNode *> SEMI();
    antlr4::tree::TerminalNode* SEMI(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  PrintListContext* printList();

  class  ExprContext : public antlr4::ParserRuleContext {
  public:
    ExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
   
    ExprContext() = default;
    void copyFrom(ExprContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;

   
  };

  class  IndexExprContext : public ExprContext {
  public:
    IndexExprContext(ExprContext *ctx);

    antlr4::Token *target = nullptr;
    BasicParser::ExprContext *exprContext = nullptr;
    std::vector<ExprContext *> idx;
    antlr4::tree::TerminalNode *LPAREN();
    antlr4::tree::TerminalNode *RPAREN();
    antlr4::tree::TerminalNode *IDENT();
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  StringExprContext : public ExprContext {
  public:
    StringExprContext(ExprContext *ctx);

    antlr4::tree::TerminalNode *STRINGLIT();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  VarExprContext : public ExprContext {
  public:
    VarExprContext(ExprContext *ctx);

    antlr4::tree::TerminalNode *IDENT();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  NotExprContext : public ExprContext {
  public:
    NotExprContext(ExprContext *ctx);

    antlr4::tree::TerminalNode *NOT_KW();
    ExprContext *expr();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  AddExprContext : public ExprContext {
  public:
    AddExprContext(ExprContext *ctx);

    antlr4::Token *op = nullptr;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    antlr4::tree::TerminalNode *PLUS();
    antlr4::tree::TerminalNode *MINUS();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  NegExprContext : public ExprContext {
  public:
    NegExprContext(ExprContext *ctx);

    antlr4::tree::TerminalNode *MINUS();
    ExprContext *expr();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  MulExprContext : public ExprContext {
  public:
    MulExprContext(ExprContext *ctx);

    antlr4::Token *op = nullptr;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    antlr4::tree::TerminalNode *STAR();
    antlr4::tree::TerminalNode *SLASH();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  OrExprContext : public ExprContext {
  public:
    OrExprContext(ExprContext *ctx);

    antlr4::Token *op = nullptr;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    antlr4::tree::TerminalNode *OR_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  RelExprContext : public ExprContext {
  public:
    RelExprContext(ExprContext *ctx);

    antlr4::Token *op = nullptr;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    antlr4::tree::TerminalNode *EQ();
    antlr4::tree::TerminalNode *NE();
    antlr4::tree::TerminalNode *LT();
    antlr4::tree::TerminalNode *LE();
    antlr4::tree::TerminalNode *GT();
    antlr4::tree::TerminalNode *GE();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  NumExprContext : public ExprContext {
  public:
    NumExprContext(ExprContext *ctx);

    antlr4::tree::TerminalNode *NUMBER();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  ParenExprContext : public ExprContext {
  public:
    ParenExprContext(ExprContext *ctx);

    antlr4::tree::TerminalNode *LPAREN();
    ExprContext *expr();
    antlr4::tree::TerminalNode *RPAREN();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  AndExprContext : public ExprContext {
  public:
    AndExprContext(ExprContext *ctx);

    antlr4::Token *op = nullptr;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    antlr4::tree::TerminalNode *AND_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ExprContext* expr();
  ExprContext* expr(int precedence);

  bool sempred(antlr4::RuleContext *_localctx, size_t ruleIndex, size_t predicateIndex) override;

  bool exprSempred(ExprContext *_localctx, size_t predicateIndex);

  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

