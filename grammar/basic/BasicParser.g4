parser grammar BasicParser;

options { tokenVocab = BasicLexer; }

// ---------------------------------------------------------------------
// Parser half of rcc's BASIC-subset grammar (see BasicLexer.g4 for the
// lexer half, and ../../docs/basic-language.md for exactly what's in
// and out of scope). A from-scratch grammar sized for that subset, not
// a full/legacy BASIC grammar - same approach CParser.g4 takes for its
// own C subset.
// ---------------------------------------------------------------------

program
    : NEWLINE* line* EOF
    ;

// Every real line starts with its own line number, which doubles as
// the GOTO/GOSUB target label - see ../../docs/basic-language.md's
// "Line numbers, labels, and GOTO/GOSUB". A blank line (bare NEWLINE,
// no number) is allowed between numbered lines.
line
    : NUMBER statement (COLON statement)* NEWLINE
    | NEWLINE
    ;

// thenStmt/elseStmt below reuse this same rule (minus gotoShorthandStmt's
// own ambiguity with a line's leading NUMBER - IF's 'THEN'/'ELSE' keyword
// already consumed before either is reached, so there's no conflict with
// `line`'s own leading NUMBER).
statement
    : 'LET'? target=IDENT ('(' idx+=expr (',' idx+=expr)* ')')? '=' val=expr      # letStmt
    | 'PRINT' printList?                                                          # printStmt
    | 'INPUT' (prompt=STRINGLIT ',')? target=IDENT                                # inputStmt
    | 'IF' cond=expr 'THEN' thenStmt=statement ('ELSE' elseStmt=statement)?       # ifStmt
    | 'FOR' loopVar=IDENT '=' from=expr 'TO' to=expr ('STEP' step=expr)?          # forStmt
    | 'NEXT' IDENT?                                                               # nextStmt
    | 'GOTO' target=NUMBER                                                       # gotoStmt
    | 'GOSUB' target=NUMBER                                                       # gosubStmt
    | 'RETURN'                                                                    # returnStmt
    | 'DIM' name=IDENT '(' dims+=NUMBER (',' dims+=NUMBER)* ')'                   # dimStmt
    | 'END'                                                                       # endStmt
    // 'IF X THEN 100' (no explicit GOTO) is the standard BASIC shorthand
    // for 'IF X THEN GOTO 100' - a bare line number, reachable only
    // through thenStmt/elseStmt above (and chained IF/THEN/ELSE), not as
    // a line's own leading statement (that position is NUMBER already,
    // consumed by `line` itself before `statement` is ever entered).
    | target=NUMBER                                                               # gotoShorthandStmt
    ;

// item/sep lengths differ by exactly one unless trailingSep is present
// (BuildBasicAst zips them back together): ',' inserts the same
// tab-stop spacing real BASIC's zone printing uses, ';' inserts none,
// and a trailing separator of either kind suppresses the newline -
// see ../../docs/basic-language.md's "PRINT" wording.
printList
    : item+=expr (sep+=(',' | ';') item+=expr)* trailingSep=(',' | ';')?
    ;

// Precedence chain, tightest first - same left-to-right convention
// CParser.g4's own condExpr uses. '+' is shared between arithmetic add
// and string concatenation (disambiguated by operand type when this is
// lowered, not here - same "grammar only admits the syntax" approach
// CParser.g4 takes throughout).
expr
    : target=IDENT '(' idx+=expr (',' idx+=expr)* ')'   # indexExpr
    | IDENT                                             # varExpr
    | NUMBER                                            # numExpr
    | STRINGLIT                                         # stringExpr
    | '(' expr ')'                                      # parenExpr
    | '-' expr                                          # negExpr
    | 'NOT' expr                                        # notExpr
    | expr op=('*'|'/') expr                            # mulExpr
    | expr op=('+'|'-') expr                            # addExpr
    | expr op=('='|'<>'|'<'|'<='|'>'|'>=') expr         # relExpr
    | expr op='AND' expr                                # andExpr
    | expr op='OR' expr                                 # orExpr
    ;
