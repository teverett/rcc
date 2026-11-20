
// Generated from /Users/tom/projects/pcc/grammar/pascal/PascalParser.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"
#include "PascalParserVisitor.h"


/**
 * This class provides an empty implementation of PascalParserVisitor, which can be
 * extended to create a visitor which only needs to handle a subset of the available methods.
 */
class  PascalParserBaseVisitor : public PascalParserVisitor {
public:

  virtual std::any visitProgram(PascalParser::ProgramContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBlock(PascalParser::BlockContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDeclPart(PascalParser::DeclPartContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstDecl(PascalParser::ConstDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeDecl(PascalParser::TypeDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitVarDecl(PascalParser::VarDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIdentList(PascalParser::IdentListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIntegerType(PascalParser::IntegerTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRealType(PascalParser::RealTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCharType(PascalParser::CharTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBooleanType(PascalParser::BooleanTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPointerType(PascalParser::PointerTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitArrayType(PascalParser::ArrayTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSetType(PascalParser::SetTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRecordType(PascalParser::RecordTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEnumType(PascalParser::EnumTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSubrangeType(PascalParser::SubrangeTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNamedType(PascalParser::NamedTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRangeSpec(PascalParser::RangeSpecContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFieldList(PascalParser::FieldListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitProcDecl(PascalParser::ProcDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFuncDecl(PascalParser::FuncDeclContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFormalParams(PascalParser::FormalParamsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFormalParamGroup(PascalParser::FormalParamGroupContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCompoundStmt(PascalParser::CompoundStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCompoundStatement(PascalParser::CompoundStatementContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAssignStmt(PascalParser::AssignStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCallStmt(PascalParser::CallStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIfStmt(PascalParser::IfStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWhileStmt(PascalParser::WhileStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRepeatStmt(PascalParser::RepeatStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitForStmt(PascalParser::ForStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCaseStmt(PascalParser::CaseStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEmptyStmt(PascalParser::EmptyStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitCaseBranch(PascalParser::CaseBranchContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDesignator(PascalParser::DesignatorContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExprList(PascalParser::ExprListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExpr(PascalParser::ExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRelOp(PascalParser::RelOpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSimpleExpr(PascalParser::SimpleExprContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAddOp(PascalParser::AddOpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTerm(PascalParser::TermContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMulOp(PascalParser::MulOpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDesignatorFactor(PascalParser::DesignatorFactorContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIntFactor(PascalParser::IntFactorContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitRealFactor(PascalParser::RealFactorContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitStringFactor(PascalParser::StringFactorContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTrueFactor(PascalParser::TrueFactorContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFalseFactor(PascalParser::FalseFactorContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParenFactor(PascalParser::ParenFactorContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNotFactor(PascalParser::NotFactorContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSetFactor(PascalParser::SetFactorContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAddrOfFactor(PascalParser::AddrOfFactorContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSetElement(PascalParser::SetElementContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConstExpr(PascalParser::ConstExprContext *ctx) override {
    return visitChildren(ctx);
  }


};

