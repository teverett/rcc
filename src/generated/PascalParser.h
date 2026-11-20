
// Generated from /Users/tom/projects/pcc/grammar/pascal/PascalParser.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"




class  PascalParser : public antlr4::Parser {
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

  enum {
    RuleProgram = 0, RuleBlock = 1, RuleDeclPart = 2, RuleConstDecl = 3, 
    RuleTypeDecl = 4, RuleVarDecl = 5, RuleIdentList = 6, RuleTypeSpec = 7, 
    RuleRangeSpec = 8, RuleFieldList = 9, RuleProcDecl = 10, RuleFuncDecl = 11, 
    RuleFormalParams = 12, RuleFormalParamGroup = 13, RuleCompoundStmt = 14, 
    RuleStatement = 15, RuleCaseBranch = 16, RuleDesignator = 17, RuleExprList = 18, 
    RuleExpr = 19, RuleRelOp = 20, RuleSimpleExpr = 21, RuleAddOp = 22, 
    RuleTerm = 23, RuleMulOp = 24, RuleFactor = 25, RuleSetElement = 26, 
    RuleConstExpr = 27
  };

  explicit PascalParser(antlr4::TokenStream *input);

  PascalParser(antlr4::TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options);

  ~PascalParser() override;

  std::string getGrammarFileName() const override;

  const antlr4::atn::ATN& getATN() const override;

  const std::vector<std::string>& getRuleNames() const override;

  const antlr4::dfa::Vocabulary& getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;


  class ProgramContext;
  class BlockContext;
  class DeclPartContext;
  class ConstDeclContext;
  class TypeDeclContext;
  class VarDeclContext;
  class IdentListContext;
  class TypeSpecContext;
  class RangeSpecContext;
  class FieldListContext;
  class ProcDeclContext;
  class FuncDeclContext;
  class FormalParamsContext;
  class FormalParamGroupContext;
  class CompoundStmtContext;
  class StatementContext;
  class CaseBranchContext;
  class DesignatorContext;
  class ExprListContext;
  class ExprContext;
  class RelOpContext;
  class SimpleExprContext;
  class AddOpContext;
  class TermContext;
  class MulOpContext;
  class FactorContext;
  class SetElementContext;
  class ConstExprContext; 

  class  ProgramContext : public antlr4::ParserRuleContext {
  public:
    ProgramContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PROGRAM_KW();
    antlr4::tree::TerminalNode *IDENT();
    antlr4::tree::TerminalNode *SEMI();
    BlockContext *block();
    antlr4::tree::TerminalNode *DOT();
    antlr4::tree::TerminalNode *EOF();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ProgramContext* program();

  class  BlockContext : public antlr4::ParserRuleContext {
  public:
    BlockContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    CompoundStmtContext *compoundStmt();
    std::vector<DeclPartContext *> declPart();
    DeclPartContext* declPart(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  BlockContext* block();

  class  DeclPartContext : public antlr4::ParserRuleContext {
  public:
    DeclPartContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ConstDeclContext *constDecl();
    TypeDeclContext *typeDecl();
    VarDeclContext *varDecl();
    ProcDeclContext *procDecl();
    FuncDeclContext *funcDecl();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DeclPartContext* declPart();

  class  ConstDeclContext : public antlr4::ParserRuleContext {
  public:
    ConstDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *CONST_KW();
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    std::vector<antlr4::tree::TerminalNode *> EQ();
    antlr4::tree::TerminalNode* EQ(size_t i);
    std::vector<ConstExprContext *> constExpr();
    ConstExprContext* constExpr(size_t i);
    std::vector<antlr4::tree::TerminalNode *> SEMI();
    antlr4::tree::TerminalNode* SEMI(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ConstDeclContext* constDecl();

  class  TypeDeclContext : public antlr4::ParserRuleContext {
  public:
    TypeDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *TYPE_KW();
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    std::vector<antlr4::tree::TerminalNode *> EQ();
    antlr4::tree::TerminalNode* EQ(size_t i);
    std::vector<TypeSpecContext *> typeSpec();
    TypeSpecContext* typeSpec(size_t i);
    std::vector<antlr4::tree::TerminalNode *> SEMI();
    antlr4::tree::TerminalNode* SEMI(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TypeDeclContext* typeDecl();

  class  VarDeclContext : public antlr4::ParserRuleContext {
  public:
    VarDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *VAR_KW();
    std::vector<IdentListContext *> identList();
    IdentListContext* identList(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COLON();
    antlr4::tree::TerminalNode* COLON(size_t i);
    std::vector<TypeSpecContext *> typeSpec();
    TypeSpecContext* typeSpec(size_t i);
    std::vector<antlr4::tree::TerminalNode *> SEMI();
    antlr4::tree::TerminalNode* SEMI(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  VarDeclContext* varDecl();

  class  IdentListContext : public antlr4::ParserRuleContext {
  public:
    IdentListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  IdentListContext* identList();

  class  TypeSpecContext : public antlr4::ParserRuleContext {
  public:
    TypeSpecContext(antlr4::ParserRuleContext *parent, size_t invokingState);
   
    TypeSpecContext() = default;
    void copyFrom(TypeSpecContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;

   
  };

  class  CharTypeContext : public TypeSpecContext {
  public:
    CharTypeContext(TypeSpecContext *ctx);

    antlr4::tree::TerminalNode *CHAR_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  NamedTypeContext : public TypeSpecContext {
  public:
    NamedTypeContext(TypeSpecContext *ctx);

    antlr4::tree::TerminalNode *IDENT();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  ArrayTypeContext : public TypeSpecContext {
  public:
    ArrayTypeContext(TypeSpecContext *ctx);

    antlr4::tree::TerminalNode *ARRAY_KW();
    antlr4::tree::TerminalNode *LBRACK();
    std::vector<RangeSpecContext *> rangeSpec();
    RangeSpecContext* rangeSpec(size_t i);
    antlr4::tree::TerminalNode *RBRACK();
    antlr4::tree::TerminalNode *OF_KW();
    TypeSpecContext *typeSpec();
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  RealTypeContext : public TypeSpecContext {
  public:
    RealTypeContext(TypeSpecContext *ctx);

    antlr4::tree::TerminalNode *REAL_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  BooleanTypeContext : public TypeSpecContext {
  public:
    BooleanTypeContext(TypeSpecContext *ctx);

    antlr4::tree::TerminalNode *BOOLEAN_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  PointerTypeContext : public TypeSpecContext {
  public:
    PointerTypeContext(TypeSpecContext *ctx);

    antlr4::tree::TerminalNode *CARET();
    TypeSpecContext *typeSpec();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  SubrangeTypeContext : public TypeSpecContext {
  public:
    SubrangeTypeContext(TypeSpecContext *ctx);

    std::vector<ConstExprContext *> constExpr();
    ConstExprContext* constExpr(size_t i);
    antlr4::tree::TerminalNode *DOTDOT();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  IntegerTypeContext : public TypeSpecContext {
  public:
    IntegerTypeContext(TypeSpecContext *ctx);

    antlr4::tree::TerminalNode *INTEGER_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  RecordTypeContext : public TypeSpecContext {
  public:
    RecordTypeContext(TypeSpecContext *ctx);

    antlr4::tree::TerminalNode *RECORD_KW();
    FieldListContext *fieldList();
    antlr4::tree::TerminalNode *END_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  EnumTypeContext : public TypeSpecContext {
  public:
    EnumTypeContext(TypeSpecContext *ctx);

    antlr4::tree::TerminalNode *LPAREN();
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    antlr4::tree::TerminalNode *RPAREN();
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  SetTypeContext : public TypeSpecContext {
  public:
    SetTypeContext(TypeSpecContext *ctx);

    antlr4::tree::TerminalNode *SET_KW();
    antlr4::tree::TerminalNode *OF_KW();
    TypeSpecContext *typeSpec();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  TypeSpecContext* typeSpec();

  class  RangeSpecContext : public antlr4::ParserRuleContext {
  public:
    RangeSpecContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ConstExprContext *> constExpr();
    ConstExprContext* constExpr(size_t i);
    antlr4::tree::TerminalNode *DOTDOT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RangeSpecContext* rangeSpec();

  class  FieldListContext : public antlr4::ParserRuleContext {
  public:
    FieldListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<IdentListContext *> identList();
    IdentListContext* identList(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COLON();
    antlr4::tree::TerminalNode* COLON(size_t i);
    std::vector<TypeSpecContext *> typeSpec();
    TypeSpecContext* typeSpec(size_t i);
    std::vector<antlr4::tree::TerminalNode *> SEMI();
    antlr4::tree::TerminalNode* SEMI(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FieldListContext* fieldList();

  class  ProcDeclContext : public antlr4::ParserRuleContext {
  public:
    ProcDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PROCEDURE_KW();
    antlr4::tree::TerminalNode *IDENT();
    std::vector<antlr4::tree::TerminalNode *> SEMI();
    antlr4::tree::TerminalNode* SEMI(size_t i);
    BlockContext *block();
    FormalParamsContext *formalParams();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ProcDeclContext* procDecl();

  class  FuncDeclContext : public antlr4::ParserRuleContext {
  public:
    FuncDeclContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *FUNCTION_KW();
    antlr4::tree::TerminalNode *IDENT();
    antlr4::tree::TerminalNode *COLON();
    TypeSpecContext *typeSpec();
    std::vector<antlr4::tree::TerminalNode *> SEMI();
    antlr4::tree::TerminalNode* SEMI(size_t i);
    BlockContext *block();
    FormalParamsContext *formalParams();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FuncDeclContext* funcDecl();

  class  FormalParamsContext : public antlr4::ParserRuleContext {
  public:
    FormalParamsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LPAREN();
    std::vector<FormalParamGroupContext *> formalParamGroup();
    FormalParamGroupContext* formalParamGroup(size_t i);
    antlr4::tree::TerminalNode *RPAREN();
    std::vector<antlr4::tree::TerminalNode *> SEMI();
    antlr4::tree::TerminalNode* SEMI(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FormalParamsContext* formalParams();

  class  FormalParamGroupContext : public antlr4::ParserRuleContext {
  public:
    FormalParamGroupContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    IdentListContext *identList();
    antlr4::tree::TerminalNode *COLON();
    TypeSpecContext *typeSpec();
    antlr4::tree::TerminalNode *VAR_KW();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  FormalParamGroupContext* formalParamGroup();

  class  CompoundStmtContext : public antlr4::ParserRuleContext {
  public:
    CompoundStmtContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *BEGIN_KW();
    std::vector<StatementContext *> statement();
    StatementContext* statement(size_t i);
    antlr4::tree::TerminalNode *END_KW();
    std::vector<antlr4::tree::TerminalNode *> SEMI();
    antlr4::tree::TerminalNode* SEMI(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CompoundStmtContext* compoundStmt();

  class  StatementContext : public antlr4::ParserRuleContext {
  public:
    StatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
   
    StatementContext() = default;
    void copyFrom(StatementContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;

   
  };

  class  ForStmtContext : public StatementContext {
  public:
    ForStmtContext(StatementContext *ctx);

    antlr4::Token *dir = nullptr;
    antlr4::tree::TerminalNode *FOR_KW();
    antlr4::tree::TerminalNode *IDENT();
    antlr4::tree::TerminalNode *ASSIGN();
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    antlr4::tree::TerminalNode *DO_KW();
    StatementContext *statement();
    antlr4::tree::TerminalNode *TO_KW();
    antlr4::tree::TerminalNode *DOWNTO_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  CallStmtContext : public StatementContext {
  public:
    CallStmtContext(StatementContext *ctx);

    DesignatorContext *designator();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  WhileStmtContext : public StatementContext {
  public:
    WhileStmtContext(StatementContext *ctx);

    antlr4::tree::TerminalNode *WHILE_KW();
    ExprContext *expr();
    antlr4::tree::TerminalNode *DO_KW();
    StatementContext *statement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  IfStmtContext : public StatementContext {
  public:
    IfStmtContext(StatementContext *ctx);

    antlr4::tree::TerminalNode *IF_KW();
    ExprContext *expr();
    antlr4::tree::TerminalNode *THEN_KW();
    std::vector<StatementContext *> statement();
    StatementContext* statement(size_t i);
    antlr4::tree::TerminalNode *ELSE_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  CompoundStatementContext : public StatementContext {
  public:
    CompoundStatementContext(StatementContext *ctx);

    CompoundStmtContext *compoundStmt();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  AssignStmtContext : public StatementContext {
  public:
    AssignStmtContext(StatementContext *ctx);

    DesignatorContext *designator();
    antlr4::tree::TerminalNode *ASSIGN();
    ExprContext *expr();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  EmptyStmtContext : public StatementContext {
  public:
    EmptyStmtContext(StatementContext *ctx);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  CaseStmtContext : public StatementContext {
  public:
    CaseStmtContext(StatementContext *ctx);

    antlr4::tree::TerminalNode *CASE_KW();
    ExprContext *expr();
    antlr4::tree::TerminalNode *OF_KW();
    std::vector<CaseBranchContext *> caseBranch();
    CaseBranchContext* caseBranch(size_t i);
    antlr4::tree::TerminalNode *END_KW();
    std::vector<antlr4::tree::TerminalNode *> SEMI();
    antlr4::tree::TerminalNode* SEMI(size_t i);
    antlr4::tree::TerminalNode *ELSE_KW();
    StatementContext *statement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  RepeatStmtContext : public StatementContext {
  public:
    RepeatStmtContext(StatementContext *ctx);

    antlr4::tree::TerminalNode *REPEAT_KW();
    std::vector<StatementContext *> statement();
    StatementContext* statement(size_t i);
    antlr4::tree::TerminalNode *UNTIL_KW();
    ExprContext *expr();
    std::vector<antlr4::tree::TerminalNode *> SEMI();
    antlr4::tree::TerminalNode* SEMI(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  StatementContext* statement();

  class  CaseBranchContext : public antlr4::ParserRuleContext {
  public:
    CaseBranchContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ConstExprContext *> constExpr();
    ConstExprContext* constExpr(size_t i);
    antlr4::tree::TerminalNode *COLON();
    StatementContext *statement();
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  CaseBranchContext* caseBranch();

  class  DesignatorContext : public antlr4::ParserRuleContext {
  public:
    DesignatorContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> IDENT();
    antlr4::tree::TerminalNode* IDENT(size_t i);
    std::vector<antlr4::tree::TerminalNode *> LPAREN();
    antlr4::tree::TerminalNode* LPAREN(size_t i);
    std::vector<antlr4::tree::TerminalNode *> RPAREN();
    antlr4::tree::TerminalNode* RPAREN(size_t i);
    std::vector<antlr4::tree::TerminalNode *> LBRACK();
    antlr4::tree::TerminalNode* LBRACK(size_t i);
    std::vector<ExprListContext *> exprList();
    ExprListContext* exprList(size_t i);
    std::vector<antlr4::tree::TerminalNode *> RBRACK();
    antlr4::tree::TerminalNode* RBRACK(size_t i);
    std::vector<antlr4::tree::TerminalNode *> DOT();
    antlr4::tree::TerminalNode* DOT(size_t i);
    std::vector<antlr4::tree::TerminalNode *> CARET();
    antlr4::tree::TerminalNode* CARET(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  DesignatorContext* designator();

  class  ExprListContext : public antlr4::ParserRuleContext {
  public:
    ExprListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExprListContext* exprList();

  class  ExprContext : public antlr4::ParserRuleContext {
  public:
    ExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<SimpleExprContext *> simpleExpr();
    SimpleExprContext* simpleExpr(size_t i);
    RelOpContext *relOp();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ExprContext* expr();

  class  RelOpContext : public antlr4::ParserRuleContext {
  public:
    RelOpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EQ();
    antlr4::tree::TerminalNode *NE();
    antlr4::tree::TerminalNode *LT();
    antlr4::tree::TerminalNode *LE();
    antlr4::tree::TerminalNode *GT();
    antlr4::tree::TerminalNode *GE();
    antlr4::tree::TerminalNode *IN_KW();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  RelOpContext* relOp();

  class  SimpleExprContext : public antlr4::ParserRuleContext {
  public:
    antlr4::Token *sign = nullptr;
    SimpleExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<TermContext *> term();
    TermContext* term(size_t i);
    std::vector<AddOpContext *> addOp();
    AddOpContext* addOp(size_t i);
    antlr4::tree::TerminalNode *PLUS();
    antlr4::tree::TerminalNode *MINUS();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SimpleExprContext* simpleExpr();

  class  AddOpContext : public antlr4::ParserRuleContext {
  public:
    AddOpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *PLUS();
    antlr4::tree::TerminalNode *MINUS();
    antlr4::tree::TerminalNode *OR_KW();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  AddOpContext* addOp();

  class  TermContext : public antlr4::ParserRuleContext {
  public:
    TermContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<FactorContext *> factor();
    FactorContext* factor(size_t i);
    std::vector<MulOpContext *> mulOp();
    MulOpContext* mulOp(size_t i);


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  TermContext* term();

  class  MulOpContext : public antlr4::ParserRuleContext {
  public:
    MulOpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *STAR();
    antlr4::tree::TerminalNode *SLASH();
    antlr4::tree::TerminalNode *DIV_KW();
    antlr4::tree::TerminalNode *MOD_KW();
    antlr4::tree::TerminalNode *AND_KW();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  MulOpContext* mulOp();

  class  FactorContext : public antlr4::ParserRuleContext {
  public:
    FactorContext(antlr4::ParserRuleContext *parent, size_t invokingState);
   
    FactorContext() = default;
    void copyFrom(FactorContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;

   
  };

  class  IntFactorContext : public FactorContext {
  public:
    IntFactorContext(FactorContext *ctx);

    antlr4::tree::TerminalNode *INTLIT();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  StringFactorContext : public FactorContext {
  public:
    StringFactorContext(FactorContext *ctx);

    antlr4::tree::TerminalNode *STRINGLIT();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  ParenFactorContext : public FactorContext {
  public:
    ParenFactorContext(FactorContext *ctx);

    antlr4::tree::TerminalNode *LPAREN();
    ExprContext *expr();
    antlr4::tree::TerminalNode *RPAREN();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  SetFactorContext : public FactorContext {
  public:
    SetFactorContext(FactorContext *ctx);

    antlr4::tree::TerminalNode *LBRACK();
    antlr4::tree::TerminalNode *RBRACK();
    std::vector<SetElementContext *> setElement();
    SetElementContext* setElement(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode* COMMA(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  DesignatorFactorContext : public FactorContext {
  public:
    DesignatorFactorContext(FactorContext *ctx);

    DesignatorContext *designator();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  TrueFactorContext : public FactorContext {
  public:
    TrueFactorContext(FactorContext *ctx);

    antlr4::tree::TerminalNode *TRUE_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  FalseFactorContext : public FactorContext {
  public:
    FalseFactorContext(FactorContext *ctx);

    antlr4::tree::TerminalNode *FALSE_KW();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  NotFactorContext : public FactorContext {
  public:
    NotFactorContext(FactorContext *ctx);

    antlr4::tree::TerminalNode *NOT_KW();
    FactorContext *factor();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class  RealFactorContext : public FactorContext {
  public:
    RealFactorContext(FactorContext *ctx);

    antlr4::tree::TerminalNode *REALLIT();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  FactorContext* factor();

  class  SetElementContext : public antlr4::ParserRuleContext {
  public:
    SetElementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ExprContext *> expr();
    ExprContext* expr(size_t i);
    antlr4::tree::TerminalNode *DOTDOT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  SetElementContext* setElement();

  class  ConstExprContext : public antlr4::ParserRuleContext {
  public:
    antlr4::Token *sign = nullptr;
    ConstExprContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *INTLIT();
    antlr4::tree::TerminalNode *REALLIT();
    antlr4::tree::TerminalNode *IDENT();
    antlr4::tree::TerminalNode *PLUS();
    antlr4::tree::TerminalNode *MINUS();
    antlr4::tree::TerminalNode *STRINGLIT();


    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
   
  };

  ConstExprContext* constExpr();


  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};

