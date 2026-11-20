
// Generated from /Users/tom/projects/pcc/grammar/pascal/PascalParser.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"
#include "PascalParser.h"



/**
 * This class defines an abstract visitor for a parse tree
 * produced by PascalParser.
 */
class  PascalParserVisitor : public antlr4::tree::AbstractParseTreeVisitor {
public:

  /**
   * Visit parse trees produced by PascalParser.
   */
    virtual std::any visitProgram(PascalParser::ProgramContext *context) = 0;

    virtual std::any visitBlock(PascalParser::BlockContext *context) = 0;

    virtual std::any visitDeclPart(PascalParser::DeclPartContext *context) = 0;

    virtual std::any visitConstDecl(PascalParser::ConstDeclContext *context) = 0;

    virtual std::any visitTypeDecl(PascalParser::TypeDeclContext *context) = 0;

    virtual std::any visitVarDecl(PascalParser::VarDeclContext *context) = 0;

    virtual std::any visitIdentList(PascalParser::IdentListContext *context) = 0;

    virtual std::any visitIntegerType(PascalParser::IntegerTypeContext *context) = 0;

    virtual std::any visitRealType(PascalParser::RealTypeContext *context) = 0;

    virtual std::any visitCharType(PascalParser::CharTypeContext *context) = 0;

    virtual std::any visitBooleanType(PascalParser::BooleanTypeContext *context) = 0;

    virtual std::any visitPointerType(PascalParser::PointerTypeContext *context) = 0;

    virtual std::any visitArrayType(PascalParser::ArrayTypeContext *context) = 0;

    virtual std::any visitSetType(PascalParser::SetTypeContext *context) = 0;

    virtual std::any visitRecordType(PascalParser::RecordTypeContext *context) = 0;

    virtual std::any visitEnumType(PascalParser::EnumTypeContext *context) = 0;

    virtual std::any visitSubrangeType(PascalParser::SubrangeTypeContext *context) = 0;

    virtual std::any visitNamedType(PascalParser::NamedTypeContext *context) = 0;

    virtual std::any visitRangeSpec(PascalParser::RangeSpecContext *context) = 0;

    virtual std::any visitFieldList(PascalParser::FieldListContext *context) = 0;

    virtual std::any visitProcDecl(PascalParser::ProcDeclContext *context) = 0;

    virtual std::any visitFuncDecl(PascalParser::FuncDeclContext *context) = 0;

    virtual std::any visitFormalParams(PascalParser::FormalParamsContext *context) = 0;

    virtual std::any visitFormalParamGroup(PascalParser::FormalParamGroupContext *context) = 0;

    virtual std::any visitCompoundStmt(PascalParser::CompoundStmtContext *context) = 0;

    virtual std::any visitCompoundStatement(PascalParser::CompoundStatementContext *context) = 0;

    virtual std::any visitAssignStmt(PascalParser::AssignStmtContext *context) = 0;

    virtual std::any visitCallStmt(PascalParser::CallStmtContext *context) = 0;

    virtual std::any visitIfStmt(PascalParser::IfStmtContext *context) = 0;

    virtual std::any visitWhileStmt(PascalParser::WhileStmtContext *context) = 0;

    virtual std::any visitRepeatStmt(PascalParser::RepeatStmtContext *context) = 0;

    virtual std::any visitForStmt(PascalParser::ForStmtContext *context) = 0;

    virtual std::any visitCaseStmt(PascalParser::CaseStmtContext *context) = 0;

    virtual std::any visitEmptyStmt(PascalParser::EmptyStmtContext *context) = 0;

    virtual std::any visitCaseBranch(PascalParser::CaseBranchContext *context) = 0;

    virtual std::any visitDesignator(PascalParser::DesignatorContext *context) = 0;

    virtual std::any visitExprList(PascalParser::ExprListContext *context) = 0;

    virtual std::any visitExpr(PascalParser::ExprContext *context) = 0;

    virtual std::any visitRelOp(PascalParser::RelOpContext *context) = 0;

    virtual std::any visitSimpleExpr(PascalParser::SimpleExprContext *context) = 0;

    virtual std::any visitAddOp(PascalParser::AddOpContext *context) = 0;

    virtual std::any visitTerm(PascalParser::TermContext *context) = 0;

    virtual std::any visitMulOp(PascalParser::MulOpContext *context) = 0;

    virtual std::any visitDesignatorFactor(PascalParser::DesignatorFactorContext *context) = 0;

    virtual std::any visitIntFactor(PascalParser::IntFactorContext *context) = 0;

    virtual std::any visitRealFactor(PascalParser::RealFactorContext *context) = 0;

    virtual std::any visitStringFactor(PascalParser::StringFactorContext *context) = 0;

    virtual std::any visitTrueFactor(PascalParser::TrueFactorContext *context) = 0;

    virtual std::any visitFalseFactor(PascalParser::FalseFactorContext *context) = 0;

    virtual std::any visitParenFactor(PascalParser::ParenFactorContext *context) = 0;

    virtual std::any visitNotFactor(PascalParser::NotFactorContext *context) = 0;

    virtual std::any visitSetFactor(PascalParser::SetFactorContext *context) = 0;

    virtual std::any visitAddrOfFactor(PascalParser::AddrOfFactorContext *context) = 0;

    virtual std::any visitSetElement(PascalParser::SetElementContext *context) = 0;

    virtual std::any visitConstExpr(PascalParser::ConstExprContext *context) = 0;


};

