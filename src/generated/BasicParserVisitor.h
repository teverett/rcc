
// Generated from /Users/tom/projects/pcc/grammar/basic/BasicParser.g4 by ANTLR 4.13.2

#pragma once


#include "antlr4-runtime.h"
#include "BasicParser.h"



/**
 * This class defines an abstract visitor for a parse tree
 * produced by BasicParser.
 */
class  BasicParserVisitor : public antlr4::tree::AbstractParseTreeVisitor {
public:

  /**
   * Visit parse trees produced by BasicParser.
   */
    virtual std::any visitProgram(BasicParser::ProgramContext *context) = 0;

    virtual std::any visitLine(BasicParser::LineContext *context) = 0;

    virtual std::any visitLetStmt(BasicParser::LetStmtContext *context) = 0;

    virtual std::any visitPrintStmt(BasicParser::PrintStmtContext *context) = 0;

    virtual std::any visitInputStmt(BasicParser::InputStmtContext *context) = 0;

    virtual std::any visitIfStmt(BasicParser::IfStmtContext *context) = 0;

    virtual std::any visitForStmt(BasicParser::ForStmtContext *context) = 0;

    virtual std::any visitNextStmt(BasicParser::NextStmtContext *context) = 0;

    virtual std::any visitGotoStmt(BasicParser::GotoStmtContext *context) = 0;

    virtual std::any visitGosubStmt(BasicParser::GosubStmtContext *context) = 0;

    virtual std::any visitReturnStmt(BasicParser::ReturnStmtContext *context) = 0;

    virtual std::any visitDimStmt(BasicParser::DimStmtContext *context) = 0;

    virtual std::any visitEndStmt(BasicParser::EndStmtContext *context) = 0;

    virtual std::any visitGotoShorthandStmt(BasicParser::GotoShorthandStmtContext *context) = 0;

    virtual std::any visitPrintList(BasicParser::PrintListContext *context) = 0;

    virtual std::any visitIndexExpr(BasicParser::IndexExprContext *context) = 0;

    virtual std::any visitStringExpr(BasicParser::StringExprContext *context) = 0;

    virtual std::any visitVarExpr(BasicParser::VarExprContext *context) = 0;

    virtual std::any visitNotExpr(BasicParser::NotExprContext *context) = 0;

    virtual std::any visitAddExpr(BasicParser::AddExprContext *context) = 0;

    virtual std::any visitNegExpr(BasicParser::NegExprContext *context) = 0;

    virtual std::any visitMulExpr(BasicParser::MulExprContext *context) = 0;

    virtual std::any visitOrExpr(BasicParser::OrExprContext *context) = 0;

    virtual std::any visitRelExpr(BasicParser::RelExprContext *context) = 0;

    virtual std::any visitNumExpr(BasicParser::NumExprContext *context) = 0;

    virtual std::any visitParenExpr(BasicParser::ParenExprContext *context) = 0;

    virtual std::any visitAndExpr(BasicParser::AndExprContext *context) = 0;


};

