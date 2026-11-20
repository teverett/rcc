
// Generated from /Users/tom/projects/pcc/grammar/basic/BasicParser.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"
#include "BasicParserVisitor.h"


/**
 * This class provides an empty implementation of BasicParserVisitor, which can be
 * extended to create a visitor which only needs to handle a subset of the available methods.
 */
class  BasicParserBaseVisitor : public BasicParserVisitor {
public:

  virtual std::any visitProgram(BasicParser::ProgramContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLine(BasicParser::LineContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLetStmt(BasicParser::LetStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrintStmt(BasicParser::PrintStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitInputStmt(BasicParser::InputStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIfStmt(BasicParser::IfStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitForStmt(BasicParser::ForStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNextStmt(BasicParser::NextStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGotoStmt(BasicParser::GotoStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGosubStmt(BasicParser::GosubStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitReturnStmt(BasicParser::ReturnStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDimStmt(BasicParser::DimStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEndStmt(BasicParser::EndStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitGotoShorthandStmt(BasicParser::GotoShorthandStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrintList(BasicParser::PrintListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIndexExpr(BasicParser::IndexExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStringExpr(BasicParser::StringExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitVarExpr(BasicParser::VarExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNotExpr(BasicParser::NotExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAddExpr(BasicParser::AddExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNegExpr(BasicParser::NegExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMulExpr(BasicParser::MulExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitOrExpr(BasicParser::OrExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRelExpr(BasicParser::RelExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNumExpr(BasicParser::NumExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParenExpr(BasicParser::ParenExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAndExpr(BasicParser::AndExprContext *ctx) override {
    return visitChildren(ctx);
  }


};

