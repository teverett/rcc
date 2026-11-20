
// Generated from /Users/tom/projects/pcc/grammar/basic/BasicParser.g4 by ANTLR 4.13.2


#include "BasicParserVisitor.h"

#include "BasicParser.h"


using namespace antlrcpp;

using namespace antlr4;

namespace {

struct BasicParserStaticData final {
  BasicParserStaticData(std::vector<std::string> ruleNames,
                        std::vector<std::string> literalNames,
                        std::vector<std::string> symbolicNames)
      : ruleNames(std::move(ruleNames)), literalNames(std::move(literalNames)),
        symbolicNames(std::move(symbolicNames)),
        vocabulary(this->literalNames, this->symbolicNames) {}

  BasicParserStaticData(const BasicParserStaticData&) = delete;
  BasicParserStaticData(BasicParserStaticData&&) = delete;
  BasicParserStaticData& operator=(const BasicParserStaticData&) = delete;
  BasicParserStaticData& operator=(BasicParserStaticData&&) = delete;

  std::vector<antlr4::dfa::DFA> decisionToDFA;
  antlr4::atn::PredictionContextCache sharedContextCache;
  const std::vector<std::string> ruleNames;
  const std::vector<std::string> literalNames;
  const std::vector<std::string> symbolicNames;
  const antlr4::dfa::Vocabulary vocabulary;
  antlr4::atn::SerializedATNView serializedATN;
  std::unique_ptr<antlr4::atn::ATN> atn;
};

::antlr4::internal::OnceFlag basicparserParserOnceFlag;
#if ANTLR4_USE_THREAD_LOCAL_CACHE
static thread_local
#endif
std::unique_ptr<BasicParserStaticData> basicparserParserStaticData = nullptr;

void basicparserParserInitialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  if (basicparserParserStaticData != nullptr) {
    return;
  }
#else
  assert(basicparserParserStaticData == nullptr);
#endif
  auto staticData = std::make_unique<BasicParserStaticData>(
    std::vector<std::string>{
      "program", "line", "statement", "printList", "expr"
    },
    std::vector<std::string>{
      "", "'LET'", "'PRINT'", "'INPUT'", "'IF'", "'THEN'", "'ELSE'", "'FOR'", 
      "'TO'", "'STEP'", "'NEXT'", "'GOTO'", "'GOSUB'", "'RETURN'", "'DIM'", 
      "'END'", "'AND'", "'OR'", "'NOT'", "'('", "')'", "','", "';'", "':'", 
      "'+'", "'-'", "'*'", "'/'", "'<='", "'>='", "'<>'", "'<'", "'>'", 
      "'='"
    },
    std::vector<std::string>{
      "", "LET_KW", "PRINT_KW", "INPUT_KW", "IF_KW", "THEN_KW", "ELSE_KW", 
      "FOR_KW", "TO_KW", "STEP_KW", "NEXT_KW", "GOTO_KW", "GOSUB_KW", "RETURN_KW", 
      "DIM_KW", "END_KW", "AND_KW", "OR_KW", "NOT_KW", "LPAREN", "RPAREN", 
      "COMMA", "SEMI", "COLON", "PLUS", "MINUS", "STAR", "SLASH", "LE", 
      "GE", "NE", "LT", "GT", "EQ", "IDENT", "NUMBER", "STRINGLIT", "REM_COMMENT", 
      "LINE_COMMENT", "WS", "NEWLINE"
    }
  );
  static const int32_t serializedATNSegment[] = {
  	4,1,40,147,2,0,7,0,2,1,7,1,2,2,7,2,2,3,7,3,2,4,7,4,1,0,5,0,12,8,0,10,
  	0,12,0,15,9,0,1,0,5,0,18,8,0,10,0,12,0,21,9,0,1,0,1,0,1,1,1,1,1,1,1,1,
  	5,1,29,8,1,10,1,12,1,32,9,1,1,1,1,1,1,1,3,1,37,8,1,1,2,3,2,40,8,2,1,2,
  	1,2,1,2,1,2,1,2,3,2,47,8,2,1,2,1,2,1,2,1,2,3,2,53,8,2,1,2,1,2,1,2,3,2,
  	58,8,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,3,2,67,8,2,1,2,1,2,1,2,1,2,1,2,1,2,
  	1,2,1,2,3,2,77,8,2,1,2,1,2,3,2,81,8,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,1,2,
  	1,2,1,2,1,2,1,2,3,2,95,8,2,1,3,1,3,1,3,5,3,100,8,3,10,3,12,3,103,9,3,
  	1,3,3,3,106,8,3,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,
  	4,1,4,1,4,1,4,3,4,125,8,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,1,4,
  	1,4,1,4,1,4,1,4,5,4,142,8,4,10,4,12,4,145,9,4,1,4,0,1,8,5,0,2,4,6,8,0,
  	4,1,0,21,22,1,0,26,27,1,0,24,25,1,0,28,33,176,0,13,1,0,0,0,2,36,1,0,0,
  	0,4,94,1,0,0,0,6,96,1,0,0,0,8,124,1,0,0,0,10,12,5,40,0,0,11,10,1,0,0,
  	0,12,15,1,0,0,0,13,11,1,0,0,0,13,14,1,0,0,0,14,19,1,0,0,0,15,13,1,0,0,
  	0,16,18,3,2,1,0,17,16,1,0,0,0,18,21,1,0,0,0,19,17,1,0,0,0,19,20,1,0,0,
  	0,20,22,1,0,0,0,21,19,1,0,0,0,22,23,5,0,0,1,23,1,1,0,0,0,24,25,5,35,0,
  	0,25,30,3,4,2,0,26,27,5,23,0,0,27,29,3,4,2,0,28,26,1,0,0,0,29,32,1,0,
  	0,0,30,28,1,0,0,0,30,31,1,0,0,0,31,33,1,0,0,0,32,30,1,0,0,0,33,34,5,40,
  	0,0,34,37,1,0,0,0,35,37,5,40,0,0,36,24,1,0,0,0,36,35,1,0,0,0,37,3,1,0,
  	0,0,38,40,5,1,0,0,39,38,1,0,0,0,39,40,1,0,0,0,40,41,1,0,0,0,41,46,5,34,
  	0,0,42,43,5,19,0,0,43,44,3,8,4,0,44,45,5,20,0,0,45,47,1,0,0,0,46,42,1,
  	0,0,0,46,47,1,0,0,0,47,48,1,0,0,0,48,49,5,33,0,0,49,95,3,8,4,0,50,52,
  	5,2,0,0,51,53,3,6,3,0,52,51,1,0,0,0,52,53,1,0,0,0,53,95,1,0,0,0,54,57,
  	5,3,0,0,55,56,5,36,0,0,56,58,5,21,0,0,57,55,1,0,0,0,57,58,1,0,0,0,58,
  	59,1,0,0,0,59,95,5,34,0,0,60,61,5,4,0,0,61,62,3,8,4,0,62,63,5,5,0,0,63,
  	66,3,4,2,0,64,65,5,6,0,0,65,67,3,4,2,0,66,64,1,0,0,0,66,67,1,0,0,0,67,
  	95,1,0,0,0,68,69,5,7,0,0,69,70,5,34,0,0,70,71,5,33,0,0,71,72,3,8,4,0,
  	72,73,5,8,0,0,73,76,3,8,4,0,74,75,5,9,0,0,75,77,3,8,4,0,76,74,1,0,0,0,
  	76,77,1,0,0,0,77,95,1,0,0,0,78,80,5,10,0,0,79,81,5,34,0,0,80,79,1,0,0,
  	0,80,81,1,0,0,0,81,95,1,0,0,0,82,83,5,11,0,0,83,95,5,35,0,0,84,85,5,12,
  	0,0,85,95,5,35,0,0,86,95,5,13,0,0,87,88,5,14,0,0,88,89,5,34,0,0,89,90,
  	5,19,0,0,90,91,5,35,0,0,91,95,5,20,0,0,92,95,5,15,0,0,93,95,5,35,0,0,
  	94,39,1,0,0,0,94,50,1,0,0,0,94,54,1,0,0,0,94,60,1,0,0,0,94,68,1,0,0,0,
  	94,78,1,0,0,0,94,82,1,0,0,0,94,84,1,0,0,0,94,86,1,0,0,0,94,87,1,0,0,0,
  	94,92,1,0,0,0,94,93,1,0,0,0,95,5,1,0,0,0,96,101,3,8,4,0,97,98,7,0,0,0,
  	98,100,3,8,4,0,99,97,1,0,0,0,100,103,1,0,0,0,101,99,1,0,0,0,101,102,1,
  	0,0,0,102,105,1,0,0,0,103,101,1,0,0,0,104,106,7,0,0,0,105,104,1,0,0,0,
  	105,106,1,0,0,0,106,7,1,0,0,0,107,108,6,4,-1,0,108,109,5,34,0,0,109,110,
  	5,19,0,0,110,111,3,8,4,0,111,112,5,20,0,0,112,125,1,0,0,0,113,125,5,34,
  	0,0,114,125,5,35,0,0,115,125,5,36,0,0,116,117,5,19,0,0,117,118,3,8,4,
  	0,118,119,5,20,0,0,119,125,1,0,0,0,120,121,5,25,0,0,121,125,3,8,4,7,122,
  	123,5,18,0,0,123,125,3,8,4,6,124,107,1,0,0,0,124,113,1,0,0,0,124,114,
  	1,0,0,0,124,115,1,0,0,0,124,116,1,0,0,0,124,120,1,0,0,0,124,122,1,0,0,
  	0,125,143,1,0,0,0,126,127,10,5,0,0,127,128,7,1,0,0,128,142,3,8,4,6,129,
  	130,10,4,0,0,130,131,7,2,0,0,131,142,3,8,4,5,132,133,10,3,0,0,133,134,
  	7,3,0,0,134,142,3,8,4,4,135,136,10,2,0,0,136,137,5,16,0,0,137,142,3,8,
  	4,3,138,139,10,1,0,0,139,140,5,17,0,0,140,142,3,8,4,2,141,126,1,0,0,0,
  	141,129,1,0,0,0,141,132,1,0,0,0,141,135,1,0,0,0,141,138,1,0,0,0,142,145,
  	1,0,0,0,143,141,1,0,0,0,143,144,1,0,0,0,144,9,1,0,0,0,145,143,1,0,0,0,
  	17,13,19,30,36,39,46,52,57,66,76,80,94,101,105,124,141,143
  };
  staticData->serializedATN = antlr4::atn::SerializedATNView(serializedATNSegment, sizeof(serializedATNSegment) / sizeof(serializedATNSegment[0]));

  antlr4::atn::ATNDeserializer deserializer;
  staticData->atn = deserializer.deserialize(staticData->serializedATN);

  const size_t count = staticData->atn->getNumberOfDecisions();
  staticData->decisionToDFA.reserve(count);
  for (size_t i = 0; i < count; i++) { 
    staticData->decisionToDFA.emplace_back(staticData->atn->getDecisionState(i), i);
  }
  basicparserParserStaticData = std::move(staticData);
}

}

BasicParser::BasicParser(TokenStream *input) : BasicParser(input, antlr4::atn::ParserATNSimulatorOptions()) {}

BasicParser::BasicParser(TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options) : Parser(input) {
  BasicParser::initialize();
  _interpreter = new atn::ParserATNSimulator(this, *basicparserParserStaticData->atn, basicparserParserStaticData->decisionToDFA, basicparserParserStaticData->sharedContextCache, options);
}

BasicParser::~BasicParser() {
  delete _interpreter;
}

const atn::ATN& BasicParser::getATN() const {
  return *basicparserParserStaticData->atn;
}

std::string BasicParser::getGrammarFileName() const {
  return "BasicParser.g4";
}

const std::vector<std::string>& BasicParser::getRuleNames() const {
  return basicparserParserStaticData->ruleNames;
}

const dfa::Vocabulary& BasicParser::getVocabulary() const {
  return basicparserParserStaticData->vocabulary;
}

antlr4::atn::SerializedATNView BasicParser::getSerializedATN() const {
  return basicparserParserStaticData->serializedATN;
}


//----------------- ProgramContext ------------------------------------------------------------------

BasicParser::ProgramContext::ProgramContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* BasicParser::ProgramContext::EOF() {
  return getToken(BasicParser::EOF, 0);
}

std::vector<tree::TerminalNode *> BasicParser::ProgramContext::NEWLINE() {
  return getTokens(BasicParser::NEWLINE);
}

tree::TerminalNode* BasicParser::ProgramContext::NEWLINE(size_t i) {
  return getToken(BasicParser::NEWLINE, i);
}

std::vector<BasicParser::LineContext *> BasicParser::ProgramContext::line() {
  return getRuleContexts<BasicParser::LineContext>();
}

BasicParser::LineContext* BasicParser::ProgramContext::line(size_t i) {
  return getRuleContext<BasicParser::LineContext>(i);
}


size_t BasicParser::ProgramContext::getRuleIndex() const {
  return BasicParser::RuleProgram;
}


std::any BasicParser::ProgramContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitProgram(this);
  else
    return visitor->visitChildren(this);
}

BasicParser::ProgramContext* BasicParser::program() {
  ProgramContext *_localctx = _tracker.createInstance<ProgramContext>(_ctx, getState());
  enterRule(_localctx, 0, BasicParser::RuleProgram);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(13);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 0, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(10);
        match(BasicParser::NEWLINE); 
      }
      setState(15);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 0, _ctx);
    }
    setState(19);
    _errHandler->sync(this);
    _la = _input->LA(1);
    while (_la == BasicParser::NUMBER

    || _la == BasicParser::NEWLINE) {
      setState(16);
      line();
      setState(21);
      _errHandler->sync(this);
      _la = _input->LA(1);
    }
    setState(22);
    match(BasicParser::EOF);
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- LineContext ------------------------------------------------------------------

BasicParser::LineContext::LineContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

tree::TerminalNode* BasicParser::LineContext::NUMBER() {
  return getToken(BasicParser::NUMBER, 0);
}

std::vector<BasicParser::StatementContext *> BasicParser::LineContext::statement() {
  return getRuleContexts<BasicParser::StatementContext>();
}

BasicParser::StatementContext* BasicParser::LineContext::statement(size_t i) {
  return getRuleContext<BasicParser::StatementContext>(i);
}

tree::TerminalNode* BasicParser::LineContext::NEWLINE() {
  return getToken(BasicParser::NEWLINE, 0);
}

std::vector<tree::TerminalNode *> BasicParser::LineContext::COLON() {
  return getTokens(BasicParser::COLON);
}

tree::TerminalNode* BasicParser::LineContext::COLON(size_t i) {
  return getToken(BasicParser::COLON, i);
}


size_t BasicParser::LineContext::getRuleIndex() const {
  return BasicParser::RuleLine;
}


std::any BasicParser::LineContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitLine(this);
  else
    return visitor->visitChildren(this);
}

BasicParser::LineContext* BasicParser::line() {
  LineContext *_localctx = _tracker.createInstance<LineContext>(_ctx, getState());
  enterRule(_localctx, 2, BasicParser::RuleLine);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(36);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case BasicParser::NUMBER: {
        enterOuterAlt(_localctx, 1);
        setState(24);
        match(BasicParser::NUMBER);
        setState(25);
        statement();
        setState(30);
        _errHandler->sync(this);
        _la = _input->LA(1);
        while (_la == BasicParser::COLON) {
          setState(26);
          match(BasicParser::COLON);
          setState(27);
          statement();
          setState(32);
          _errHandler->sync(this);
          _la = _input->LA(1);
        }
        setState(33);
        match(BasicParser::NEWLINE);
        break;
      }

      case BasicParser::NEWLINE: {
        enterOuterAlt(_localctx, 2);
        setState(35);
        match(BasicParser::NEWLINE);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- StatementContext ------------------------------------------------------------------

BasicParser::StatementContext::StatementContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t BasicParser::StatementContext::getRuleIndex() const {
  return BasicParser::RuleStatement;
}

void BasicParser::StatementContext::copyFrom(StatementContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- EndStmtContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::EndStmtContext::END_KW() {
  return getToken(BasicParser::END_KW, 0);
}

BasicParser::EndStmtContext::EndStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any BasicParser::EndStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitEndStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- PrintStmtContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::PrintStmtContext::PRINT_KW() {
  return getToken(BasicParser::PRINT_KW, 0);
}

BasicParser::PrintListContext* BasicParser::PrintStmtContext::printList() {
  return getRuleContext<BasicParser::PrintListContext>(0);
}

BasicParser::PrintStmtContext::PrintStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any BasicParser::PrintStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitPrintStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ForStmtContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::ForStmtContext::FOR_KW() {
  return getToken(BasicParser::FOR_KW, 0);
}

tree::TerminalNode* BasicParser::ForStmtContext::EQ() {
  return getToken(BasicParser::EQ, 0);
}

tree::TerminalNode* BasicParser::ForStmtContext::TO_KW() {
  return getToken(BasicParser::TO_KW, 0);
}

tree::TerminalNode* BasicParser::ForStmtContext::IDENT() {
  return getToken(BasicParser::IDENT, 0);
}

std::vector<BasicParser::ExprContext *> BasicParser::ForStmtContext::expr() {
  return getRuleContexts<BasicParser::ExprContext>();
}

BasicParser::ExprContext* BasicParser::ForStmtContext::expr(size_t i) {
  return getRuleContext<BasicParser::ExprContext>(i);
}

tree::TerminalNode* BasicParser::ForStmtContext::STEP_KW() {
  return getToken(BasicParser::STEP_KW, 0);
}

BasicParser::ForStmtContext::ForStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any BasicParser::ForStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitForStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- DimStmtContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::DimStmtContext::DIM_KW() {
  return getToken(BasicParser::DIM_KW, 0);
}

tree::TerminalNode* BasicParser::DimStmtContext::LPAREN() {
  return getToken(BasicParser::LPAREN, 0);
}

tree::TerminalNode* BasicParser::DimStmtContext::RPAREN() {
  return getToken(BasicParser::RPAREN, 0);
}

tree::TerminalNode* BasicParser::DimStmtContext::IDENT() {
  return getToken(BasicParser::IDENT, 0);
}

tree::TerminalNode* BasicParser::DimStmtContext::NUMBER() {
  return getToken(BasicParser::NUMBER, 0);
}

BasicParser::DimStmtContext::DimStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any BasicParser::DimStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitDimStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- IfStmtContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::IfStmtContext::IF_KW() {
  return getToken(BasicParser::IF_KW, 0);
}

tree::TerminalNode* BasicParser::IfStmtContext::THEN_KW() {
  return getToken(BasicParser::THEN_KW, 0);
}

BasicParser::ExprContext* BasicParser::IfStmtContext::expr() {
  return getRuleContext<BasicParser::ExprContext>(0);
}

std::vector<BasicParser::StatementContext *> BasicParser::IfStmtContext::statement() {
  return getRuleContexts<BasicParser::StatementContext>();
}

BasicParser::StatementContext* BasicParser::IfStmtContext::statement(size_t i) {
  return getRuleContext<BasicParser::StatementContext>(i);
}

tree::TerminalNode* BasicParser::IfStmtContext::ELSE_KW() {
  return getToken(BasicParser::ELSE_KW, 0);
}

BasicParser::IfStmtContext::IfStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any BasicParser::IfStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitIfStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- LetStmtContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::LetStmtContext::EQ() {
  return getToken(BasicParser::EQ, 0);
}

tree::TerminalNode* BasicParser::LetStmtContext::IDENT() {
  return getToken(BasicParser::IDENT, 0);
}

std::vector<BasicParser::ExprContext *> BasicParser::LetStmtContext::expr() {
  return getRuleContexts<BasicParser::ExprContext>();
}

BasicParser::ExprContext* BasicParser::LetStmtContext::expr(size_t i) {
  return getRuleContext<BasicParser::ExprContext>(i);
}

tree::TerminalNode* BasicParser::LetStmtContext::LET_KW() {
  return getToken(BasicParser::LET_KW, 0);
}

tree::TerminalNode* BasicParser::LetStmtContext::LPAREN() {
  return getToken(BasicParser::LPAREN, 0);
}

tree::TerminalNode* BasicParser::LetStmtContext::RPAREN() {
  return getToken(BasicParser::RPAREN, 0);
}

BasicParser::LetStmtContext::LetStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any BasicParser::LetStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitLetStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- GotoShorthandStmtContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::GotoShorthandStmtContext::NUMBER() {
  return getToken(BasicParser::NUMBER, 0);
}

BasicParser::GotoShorthandStmtContext::GotoShorthandStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any BasicParser::GotoShorthandStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitGotoShorthandStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- GotoStmtContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::GotoStmtContext::GOTO_KW() {
  return getToken(BasicParser::GOTO_KW, 0);
}

tree::TerminalNode* BasicParser::GotoStmtContext::NUMBER() {
  return getToken(BasicParser::NUMBER, 0);
}

BasicParser::GotoStmtContext::GotoStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any BasicParser::GotoStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitGotoStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- GosubStmtContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::GosubStmtContext::GOSUB_KW() {
  return getToken(BasicParser::GOSUB_KW, 0);
}

tree::TerminalNode* BasicParser::GosubStmtContext::NUMBER() {
  return getToken(BasicParser::NUMBER, 0);
}

BasicParser::GosubStmtContext::GosubStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any BasicParser::GosubStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitGosubStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ReturnStmtContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::ReturnStmtContext::RETURN_KW() {
  return getToken(BasicParser::RETURN_KW, 0);
}

BasicParser::ReturnStmtContext::ReturnStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any BasicParser::ReturnStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitReturnStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- NextStmtContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::NextStmtContext::NEXT_KW() {
  return getToken(BasicParser::NEXT_KW, 0);
}

tree::TerminalNode* BasicParser::NextStmtContext::IDENT() {
  return getToken(BasicParser::IDENT, 0);
}

BasicParser::NextStmtContext::NextStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any BasicParser::NextStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitNextStmt(this);
  else
    return visitor->visitChildren(this);
}
//----------------- InputStmtContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::InputStmtContext::INPUT_KW() {
  return getToken(BasicParser::INPUT_KW, 0);
}

tree::TerminalNode* BasicParser::InputStmtContext::IDENT() {
  return getToken(BasicParser::IDENT, 0);
}

tree::TerminalNode* BasicParser::InputStmtContext::COMMA() {
  return getToken(BasicParser::COMMA, 0);
}

tree::TerminalNode* BasicParser::InputStmtContext::STRINGLIT() {
  return getToken(BasicParser::STRINGLIT, 0);
}

BasicParser::InputStmtContext::InputStmtContext(StatementContext *ctx) { copyFrom(ctx); }


std::any BasicParser::InputStmtContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitInputStmt(this);
  else
    return visitor->visitChildren(this);
}
BasicParser::StatementContext* BasicParser::statement() {
  StatementContext *_localctx = _tracker.createInstance<StatementContext>(_ctx, getState());
  enterRule(_localctx, 4, BasicParser::RuleStatement);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    setState(94);
    _errHandler->sync(this);
    switch (_input->LA(1)) {
      case BasicParser::LET_KW:
      case BasicParser::IDENT: {
        _localctx = _tracker.createInstance<BasicParser::LetStmtContext>(_localctx);
        enterOuterAlt(_localctx, 1);
        setState(39);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == BasicParser::LET_KW) {
          setState(38);
          match(BasicParser::LET_KW);
        }
        setState(41);
        antlrcpp::downCast<LetStmtContext *>(_localctx)->target = match(BasicParser::IDENT);
        setState(46);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == BasicParser::LPAREN) {
          setState(42);
          match(BasicParser::LPAREN);
          setState(43);
          antlrcpp::downCast<LetStmtContext *>(_localctx)->idx = expr(0);
          setState(44);
          match(BasicParser::RPAREN);
        }
        setState(48);
        match(BasicParser::EQ);
        setState(49);
        antlrcpp::downCast<LetStmtContext *>(_localctx)->val = expr(0);
        break;
      }

      case BasicParser::PRINT_KW: {
        _localctx = _tracker.createInstance<BasicParser::PrintStmtContext>(_localctx);
        enterOuterAlt(_localctx, 2);
        setState(50);
        match(BasicParser::PRINT_KW);
        setState(52);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if ((((_la & ~ 0x3fULL) == 0) &&
          ((1ULL << _la) & 120293425152) != 0)) {
          setState(51);
          printList();
        }
        break;
      }

      case BasicParser::INPUT_KW: {
        _localctx = _tracker.createInstance<BasicParser::InputStmtContext>(_localctx);
        enterOuterAlt(_localctx, 3);
        setState(54);
        match(BasicParser::INPUT_KW);
        setState(57);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == BasicParser::STRINGLIT) {
          setState(55);
          antlrcpp::downCast<InputStmtContext *>(_localctx)->prompt = match(BasicParser::STRINGLIT);
          setState(56);
          match(BasicParser::COMMA);
        }
        setState(59);
        antlrcpp::downCast<InputStmtContext *>(_localctx)->target = match(BasicParser::IDENT);
        break;
      }

      case BasicParser::IF_KW: {
        _localctx = _tracker.createInstance<BasicParser::IfStmtContext>(_localctx);
        enterOuterAlt(_localctx, 4);
        setState(60);
        match(BasicParser::IF_KW);
        setState(61);
        antlrcpp::downCast<IfStmtContext *>(_localctx)->cond = expr(0);
        setState(62);
        match(BasicParser::THEN_KW);
        setState(63);
        antlrcpp::downCast<IfStmtContext *>(_localctx)->thenStmt = statement();
        setState(66);
        _errHandler->sync(this);

        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 8, _ctx)) {
        case 1: {
          setState(64);
          match(BasicParser::ELSE_KW);
          setState(65);
          antlrcpp::downCast<IfStmtContext *>(_localctx)->elseStmt = statement();
          break;
        }

        default:
          break;
        }
        break;
      }

      case BasicParser::FOR_KW: {
        _localctx = _tracker.createInstance<BasicParser::ForStmtContext>(_localctx);
        enterOuterAlt(_localctx, 5);
        setState(68);
        match(BasicParser::FOR_KW);
        setState(69);
        antlrcpp::downCast<ForStmtContext *>(_localctx)->loopVar = match(BasicParser::IDENT);
        setState(70);
        match(BasicParser::EQ);
        setState(71);
        antlrcpp::downCast<ForStmtContext *>(_localctx)->from = expr(0);
        setState(72);
        match(BasicParser::TO_KW);
        setState(73);
        antlrcpp::downCast<ForStmtContext *>(_localctx)->to = expr(0);
        setState(76);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == BasicParser::STEP_KW) {
          setState(74);
          match(BasicParser::STEP_KW);
          setState(75);
          antlrcpp::downCast<ForStmtContext *>(_localctx)->step = expr(0);
        }
        break;
      }

      case BasicParser::NEXT_KW: {
        _localctx = _tracker.createInstance<BasicParser::NextStmtContext>(_localctx);
        enterOuterAlt(_localctx, 6);
        setState(78);
        match(BasicParser::NEXT_KW);
        setState(80);
        _errHandler->sync(this);

        _la = _input->LA(1);
        if (_la == BasicParser::IDENT) {
          setState(79);
          match(BasicParser::IDENT);
        }
        break;
      }

      case BasicParser::GOTO_KW: {
        _localctx = _tracker.createInstance<BasicParser::GotoStmtContext>(_localctx);
        enterOuterAlt(_localctx, 7);
        setState(82);
        match(BasicParser::GOTO_KW);
        setState(83);
        antlrcpp::downCast<GotoStmtContext *>(_localctx)->target = match(BasicParser::NUMBER);
        break;
      }

      case BasicParser::GOSUB_KW: {
        _localctx = _tracker.createInstance<BasicParser::GosubStmtContext>(_localctx);
        enterOuterAlt(_localctx, 8);
        setState(84);
        match(BasicParser::GOSUB_KW);
        setState(85);
        antlrcpp::downCast<GosubStmtContext *>(_localctx)->target = match(BasicParser::NUMBER);
        break;
      }

      case BasicParser::RETURN_KW: {
        _localctx = _tracker.createInstance<BasicParser::ReturnStmtContext>(_localctx);
        enterOuterAlt(_localctx, 9);
        setState(86);
        match(BasicParser::RETURN_KW);
        break;
      }

      case BasicParser::DIM_KW: {
        _localctx = _tracker.createInstance<BasicParser::DimStmtContext>(_localctx);
        enterOuterAlt(_localctx, 10);
        setState(87);
        match(BasicParser::DIM_KW);
        setState(88);
        antlrcpp::downCast<DimStmtContext *>(_localctx)->name = match(BasicParser::IDENT);
        setState(89);
        match(BasicParser::LPAREN);
        setState(90);
        antlrcpp::downCast<DimStmtContext *>(_localctx)->size = match(BasicParser::NUMBER);
        setState(91);
        match(BasicParser::RPAREN);
        break;
      }

      case BasicParser::END_KW: {
        _localctx = _tracker.createInstance<BasicParser::EndStmtContext>(_localctx);
        enterOuterAlt(_localctx, 11);
        setState(92);
        match(BasicParser::END_KW);
        break;
      }

      case BasicParser::NUMBER: {
        _localctx = _tracker.createInstance<BasicParser::GotoShorthandStmtContext>(_localctx);
        enterOuterAlt(_localctx, 12);
        setState(93);
        antlrcpp::downCast<GotoShorthandStmtContext *>(_localctx)->target = match(BasicParser::NUMBER);
        break;
      }

    default:
      throw NoViableAltException(this);
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- PrintListContext ------------------------------------------------------------------

BasicParser::PrintListContext::PrintListContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}

std::vector<BasicParser::ExprContext *> BasicParser::PrintListContext::expr() {
  return getRuleContexts<BasicParser::ExprContext>();
}

BasicParser::ExprContext* BasicParser::PrintListContext::expr(size_t i) {
  return getRuleContext<BasicParser::ExprContext>(i);
}

std::vector<tree::TerminalNode *> BasicParser::PrintListContext::COMMA() {
  return getTokens(BasicParser::COMMA);
}

tree::TerminalNode* BasicParser::PrintListContext::COMMA(size_t i) {
  return getToken(BasicParser::COMMA, i);
}

std::vector<tree::TerminalNode *> BasicParser::PrintListContext::SEMI() {
  return getTokens(BasicParser::SEMI);
}

tree::TerminalNode* BasicParser::PrintListContext::SEMI(size_t i) {
  return getToken(BasicParser::SEMI, i);
}


size_t BasicParser::PrintListContext::getRuleIndex() const {
  return BasicParser::RulePrintList;
}


std::any BasicParser::PrintListContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitPrintList(this);
  else
    return visitor->visitChildren(this);
}

BasicParser::PrintListContext* BasicParser::printList() {
  PrintListContext *_localctx = _tracker.createInstance<PrintListContext>(_ctx, getState());
  enterRule(_localctx, 6, BasicParser::RulePrintList);
  size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    exitRule();
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(96);
    antlrcpp::downCast<PrintListContext *>(_localctx)->exprContext = expr(0);
    antlrcpp::downCast<PrintListContext *>(_localctx)->item.push_back(antlrcpp::downCast<PrintListContext *>(_localctx)->exprContext);
    setState(101);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 12, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        setState(97);
        antlrcpp::downCast<PrintListContext *>(_localctx)->_tset323 = _input->LT(1);
        _la = _input->LA(1);
        if (!(_la == BasicParser::COMMA

        || _la == BasicParser::SEMI)) {
          antlrcpp::downCast<PrintListContext *>(_localctx)->_tset323 = _errHandler->recoverInline(this);
        }
        else {
          _errHandler->reportMatch(this);
          consume();
        }
        antlrcpp::downCast<PrintListContext *>(_localctx)->sep.push_back(antlrcpp::downCast<PrintListContext *>(_localctx)->_tset323);
        setState(98);
        antlrcpp::downCast<PrintListContext *>(_localctx)->exprContext = expr(0);
        antlrcpp::downCast<PrintListContext *>(_localctx)->item.push_back(antlrcpp::downCast<PrintListContext *>(_localctx)->exprContext); 
      }
      setState(103);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 12, _ctx);
    }
    setState(105);
    _errHandler->sync(this);

    _la = _input->LA(1);
    if (_la == BasicParser::COMMA

    || _la == BasicParser::SEMI) {
      setState(104);
      antlrcpp::downCast<PrintListContext *>(_localctx)->trailingSep = _input->LT(1);
      _la = _input->LA(1);
      if (!(_la == BasicParser::COMMA

      || _la == BasicParser::SEMI)) {
        antlrcpp::downCast<PrintListContext *>(_localctx)->trailingSep = _errHandler->recoverInline(this);
      }
      else {
        _errHandler->reportMatch(this);
        consume();
      }
    }
   
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }

  return _localctx;
}

//----------------- ExprContext ------------------------------------------------------------------

BasicParser::ExprContext::ExprContext(ParserRuleContext *parent, size_t invokingState)
  : ParserRuleContext(parent, invokingState) {
}


size_t BasicParser::ExprContext::getRuleIndex() const {
  return BasicParser::RuleExpr;
}

void BasicParser::ExprContext::copyFrom(ExprContext *ctx) {
  ParserRuleContext::copyFrom(ctx);
}

//----------------- IndexExprContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::IndexExprContext::LPAREN() {
  return getToken(BasicParser::LPAREN, 0);
}

tree::TerminalNode* BasicParser::IndexExprContext::RPAREN() {
  return getToken(BasicParser::RPAREN, 0);
}

tree::TerminalNode* BasicParser::IndexExprContext::IDENT() {
  return getToken(BasicParser::IDENT, 0);
}

BasicParser::ExprContext* BasicParser::IndexExprContext::expr() {
  return getRuleContext<BasicParser::ExprContext>(0);
}

BasicParser::IndexExprContext::IndexExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any BasicParser::IndexExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitIndexExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- StringExprContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::StringExprContext::STRINGLIT() {
  return getToken(BasicParser::STRINGLIT, 0);
}

BasicParser::StringExprContext::StringExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any BasicParser::StringExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitStringExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- VarExprContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::VarExprContext::IDENT() {
  return getToken(BasicParser::IDENT, 0);
}

BasicParser::VarExprContext::VarExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any BasicParser::VarExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitVarExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- NotExprContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::NotExprContext::NOT_KW() {
  return getToken(BasicParser::NOT_KW, 0);
}

BasicParser::ExprContext* BasicParser::NotExprContext::expr() {
  return getRuleContext<BasicParser::ExprContext>(0);
}

BasicParser::NotExprContext::NotExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any BasicParser::NotExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitNotExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AddExprContext ------------------------------------------------------------------

std::vector<BasicParser::ExprContext *> BasicParser::AddExprContext::expr() {
  return getRuleContexts<BasicParser::ExprContext>();
}

BasicParser::ExprContext* BasicParser::AddExprContext::expr(size_t i) {
  return getRuleContext<BasicParser::ExprContext>(i);
}

tree::TerminalNode* BasicParser::AddExprContext::PLUS() {
  return getToken(BasicParser::PLUS, 0);
}

tree::TerminalNode* BasicParser::AddExprContext::MINUS() {
  return getToken(BasicParser::MINUS, 0);
}

BasicParser::AddExprContext::AddExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any BasicParser::AddExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitAddExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- NegExprContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::NegExprContext::MINUS() {
  return getToken(BasicParser::MINUS, 0);
}

BasicParser::ExprContext* BasicParser::NegExprContext::expr() {
  return getRuleContext<BasicParser::ExprContext>(0);
}

BasicParser::NegExprContext::NegExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any BasicParser::NegExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitNegExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- MulExprContext ------------------------------------------------------------------

std::vector<BasicParser::ExprContext *> BasicParser::MulExprContext::expr() {
  return getRuleContexts<BasicParser::ExprContext>();
}

BasicParser::ExprContext* BasicParser::MulExprContext::expr(size_t i) {
  return getRuleContext<BasicParser::ExprContext>(i);
}

tree::TerminalNode* BasicParser::MulExprContext::STAR() {
  return getToken(BasicParser::STAR, 0);
}

tree::TerminalNode* BasicParser::MulExprContext::SLASH() {
  return getToken(BasicParser::SLASH, 0);
}

BasicParser::MulExprContext::MulExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any BasicParser::MulExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitMulExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- OrExprContext ------------------------------------------------------------------

std::vector<BasicParser::ExprContext *> BasicParser::OrExprContext::expr() {
  return getRuleContexts<BasicParser::ExprContext>();
}

BasicParser::ExprContext* BasicParser::OrExprContext::expr(size_t i) {
  return getRuleContext<BasicParser::ExprContext>(i);
}

tree::TerminalNode* BasicParser::OrExprContext::OR_KW() {
  return getToken(BasicParser::OR_KW, 0);
}

BasicParser::OrExprContext::OrExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any BasicParser::OrExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitOrExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- RelExprContext ------------------------------------------------------------------

std::vector<BasicParser::ExprContext *> BasicParser::RelExprContext::expr() {
  return getRuleContexts<BasicParser::ExprContext>();
}

BasicParser::ExprContext* BasicParser::RelExprContext::expr(size_t i) {
  return getRuleContext<BasicParser::ExprContext>(i);
}

tree::TerminalNode* BasicParser::RelExprContext::EQ() {
  return getToken(BasicParser::EQ, 0);
}

tree::TerminalNode* BasicParser::RelExprContext::NE() {
  return getToken(BasicParser::NE, 0);
}

tree::TerminalNode* BasicParser::RelExprContext::LT() {
  return getToken(BasicParser::LT, 0);
}

tree::TerminalNode* BasicParser::RelExprContext::LE() {
  return getToken(BasicParser::LE, 0);
}

tree::TerminalNode* BasicParser::RelExprContext::GT() {
  return getToken(BasicParser::GT, 0);
}

tree::TerminalNode* BasicParser::RelExprContext::GE() {
  return getToken(BasicParser::GE, 0);
}

BasicParser::RelExprContext::RelExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any BasicParser::RelExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitRelExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- NumExprContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::NumExprContext::NUMBER() {
  return getToken(BasicParser::NUMBER, 0);
}

BasicParser::NumExprContext::NumExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any BasicParser::NumExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitNumExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- ParenExprContext ------------------------------------------------------------------

tree::TerminalNode* BasicParser::ParenExprContext::LPAREN() {
  return getToken(BasicParser::LPAREN, 0);
}

BasicParser::ExprContext* BasicParser::ParenExprContext::expr() {
  return getRuleContext<BasicParser::ExprContext>(0);
}

tree::TerminalNode* BasicParser::ParenExprContext::RPAREN() {
  return getToken(BasicParser::RPAREN, 0);
}

BasicParser::ParenExprContext::ParenExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any BasicParser::ParenExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitParenExpr(this);
  else
    return visitor->visitChildren(this);
}
//----------------- AndExprContext ------------------------------------------------------------------

std::vector<BasicParser::ExprContext *> BasicParser::AndExprContext::expr() {
  return getRuleContexts<BasicParser::ExprContext>();
}

BasicParser::ExprContext* BasicParser::AndExprContext::expr(size_t i) {
  return getRuleContext<BasicParser::ExprContext>(i);
}

tree::TerminalNode* BasicParser::AndExprContext::AND_KW() {
  return getToken(BasicParser::AND_KW, 0);
}

BasicParser::AndExprContext::AndExprContext(ExprContext *ctx) { copyFrom(ctx); }


std::any BasicParser::AndExprContext::accept(tree::ParseTreeVisitor *visitor) {
  if (auto parserVisitor = dynamic_cast<BasicParserVisitor*>(visitor))
    return parserVisitor->visitAndExpr(this);
  else
    return visitor->visitChildren(this);
}

BasicParser::ExprContext* BasicParser::expr() {
   return expr(0);
}

BasicParser::ExprContext* BasicParser::expr(int precedence) {
  ParserRuleContext *parentContext = _ctx;
  size_t parentState = getState();
  BasicParser::ExprContext *_localctx = _tracker.createInstance<ExprContext>(_ctx, parentState);
  BasicParser::ExprContext *previousContext = _localctx;
  (void)previousContext; // Silence compiler, in case the context is not used by generated code.
  size_t startState = 8;
  enterRecursionRule(_localctx, 8, BasicParser::RuleExpr, precedence);

    size_t _la = 0;

#if __cplusplus > 201703L
  auto onExit = finally([=, this] {
#else
  auto onExit = finally([=] {
#endif
    unrollRecursionContexts(parentContext);
  });
  try {
    size_t alt;
    enterOuterAlt(_localctx, 1);
    setState(124);
    _errHandler->sync(this);
    switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 14, _ctx)) {
    case 1: {
      _localctx = _tracker.createInstance<IndexExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;

      setState(108);
      antlrcpp::downCast<IndexExprContext *>(_localctx)->target = match(BasicParser::IDENT);
      setState(109);
      match(BasicParser::LPAREN);
      setState(110);
      antlrcpp::downCast<IndexExprContext *>(_localctx)->idx = expr(0);
      setState(111);
      match(BasicParser::RPAREN);
      break;
    }

    case 2: {
      _localctx = _tracker.createInstance<VarExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(113);
      match(BasicParser::IDENT);
      break;
    }

    case 3: {
      _localctx = _tracker.createInstance<NumExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(114);
      match(BasicParser::NUMBER);
      break;
    }

    case 4: {
      _localctx = _tracker.createInstance<StringExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(115);
      match(BasicParser::STRINGLIT);
      break;
    }

    case 5: {
      _localctx = _tracker.createInstance<ParenExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(116);
      match(BasicParser::LPAREN);
      setState(117);
      expr(0);
      setState(118);
      match(BasicParser::RPAREN);
      break;
    }

    case 6: {
      _localctx = _tracker.createInstance<NegExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(120);
      match(BasicParser::MINUS);
      setState(121);
      expr(7);
      break;
    }

    case 7: {
      _localctx = _tracker.createInstance<NotExprContext>(_localctx);
      _ctx = _localctx;
      previousContext = _localctx;
      setState(122);
      match(BasicParser::NOT_KW);
      setState(123);
      expr(6);
      break;
    }

    default:
      break;
    }
    _ctx->stop = _input->LT(-1);
    setState(143);
    _errHandler->sync(this);
    alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 16, _ctx);
    while (alt != 2 && alt != atn::ATN::INVALID_ALT_NUMBER) {
      if (alt == 1) {
        if (!_parseListeners.empty())
          triggerExitRuleEvent();
        previousContext = _localctx;
        setState(141);
        _errHandler->sync(this);
        switch (getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 15, _ctx)) {
        case 1: {
          auto newContext = _tracker.createInstance<MulExprContext>(_tracker.createInstance<ExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleExpr);
          setState(126);

          if (!(precpred(_ctx, 5))) throw FailedPredicateException(this, "precpred(_ctx, 5)");
          setState(127);
          antlrcpp::downCast<MulExprContext *>(_localctx)->op = _input->LT(1);
          _la = _input->LA(1);
          if (!(_la == BasicParser::STAR

          || _la == BasicParser::SLASH)) {
            antlrcpp::downCast<MulExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
          }
          else {
            _errHandler->reportMatch(this);
            consume();
          }
          setState(128);
          expr(6);
          break;
        }

        case 2: {
          auto newContext = _tracker.createInstance<AddExprContext>(_tracker.createInstance<ExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleExpr);
          setState(129);

          if (!(precpred(_ctx, 4))) throw FailedPredicateException(this, "precpred(_ctx, 4)");
          setState(130);
          antlrcpp::downCast<AddExprContext *>(_localctx)->op = _input->LT(1);
          _la = _input->LA(1);
          if (!(_la == BasicParser::PLUS

          || _la == BasicParser::MINUS)) {
            antlrcpp::downCast<AddExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
          }
          else {
            _errHandler->reportMatch(this);
            consume();
          }
          setState(131);
          expr(5);
          break;
        }

        case 3: {
          auto newContext = _tracker.createInstance<RelExprContext>(_tracker.createInstance<ExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleExpr);
          setState(132);

          if (!(precpred(_ctx, 3))) throw FailedPredicateException(this, "precpred(_ctx, 3)");
          setState(133);
          antlrcpp::downCast<RelExprContext *>(_localctx)->op = _input->LT(1);
          _la = _input->LA(1);
          if (!((((_la & ~ 0x3fULL) == 0) &&
            ((1ULL << _la) & 16911433728) != 0))) {
            antlrcpp::downCast<RelExprContext *>(_localctx)->op = _errHandler->recoverInline(this);
          }
          else {
            _errHandler->reportMatch(this);
            consume();
          }
          setState(134);
          expr(4);
          break;
        }

        case 4: {
          auto newContext = _tracker.createInstance<AndExprContext>(_tracker.createInstance<ExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleExpr);
          setState(135);

          if (!(precpred(_ctx, 2))) throw FailedPredicateException(this, "precpred(_ctx, 2)");
          setState(136);
          antlrcpp::downCast<AndExprContext *>(_localctx)->op = match(BasicParser::AND_KW);
          setState(137);
          expr(3);
          break;
        }

        case 5: {
          auto newContext = _tracker.createInstance<OrExprContext>(_tracker.createInstance<ExprContext>(parentContext, parentState));
          _localctx = newContext;
          pushNewRecursionContext(newContext, startState, RuleExpr);
          setState(138);

          if (!(precpred(_ctx, 1))) throw FailedPredicateException(this, "precpred(_ctx, 1)");
          setState(139);
          antlrcpp::downCast<OrExprContext *>(_localctx)->op = match(BasicParser::OR_KW);
          setState(140);
          expr(2);
          break;
        }

        default:
          break;
        } 
      }
      setState(145);
      _errHandler->sync(this);
      alt = getInterpreter<atn::ParserATNSimulator>()->adaptivePredict(_input, 16, _ctx);
    }
  }
  catch (RecognitionException &e) {
    _errHandler->reportError(this, e);
    _localctx->exception = std::current_exception();
    _errHandler->recover(this, _localctx->exception);
  }
  return _localctx;
}

bool BasicParser::sempred(RuleContext *context, size_t ruleIndex, size_t predicateIndex) {
  switch (ruleIndex) {
    case 4: return exprSempred(antlrcpp::downCast<ExprContext *>(context), predicateIndex);

  default:
    break;
  }
  return true;
}

bool BasicParser::exprSempred(ExprContext *_localctx, size_t predicateIndex) {
  switch (predicateIndex) {
    case 0: return precpred(_ctx, 5);
    case 1: return precpred(_ctx, 4);
    case 2: return precpred(_ctx, 3);
    case 3: return precpred(_ctx, 2);
    case 4: return precpred(_ctx, 1);

  default:
    break;
  }
  return true;
}

void BasicParser::initialize() {
#if ANTLR4_USE_THREAD_LOCAL_CACHE
  basicparserParserInitialize();
#else
  ::antlr4::internal::call_once(basicparserParserOnceFlag, basicparserParserInitialize);
#endif
}
