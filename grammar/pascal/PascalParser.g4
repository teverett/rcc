parser grammar PascalParser;

options { tokenVocab = PascalLexer; }

// ---------------------------------------------------------------------
// Parser half of rcc's Pascal-subset grammar (see PascalLexer.g4 for
// the lexer half, and ../../docs/pascal-language.md for exactly
// what's in and out of v1's scope). A from-scratch grammar sized for
// that v1 subset, not a full ISO-7185 grammar - same approach
// CParser.g4/BasicParser.g4 each take for their own subset.
//
// Deliberately absent, per docs/pascal-language.md: variant records
// (`case tag: T of ...` as a record field), procedural types
// (a procedure/function parameter, or any indirect call), and nested
// procedure/function declarations (procDecl/funcDecl only ever nest
// inside `program`'s own top-level block, never inside another
// procDecl/funcDecl's block - enforced by BuildPascalAst later, since
// this grammar alone can't rule out a deeper nesting without
// duplicating the whole block rule at every level).
// ---------------------------------------------------------------------

program
    : PROGRAM_KW IDENT SEMI block DOT EOF
    ;

// A block's declaration parts may appear in any order and any number
// of times (real Pascal fixes `const`/`type`/`var`/routines to one
// canonical order with at most one of each; this subset is looser on
// purpose, since nothing downstream needs the stricter shape).
block
    : declPart* compoundStmt
    ;

declPart
    : constDecl
    | typeDecl
    | varDecl
    | procDecl
    | funcDecl
    ;

constDecl
    : CONST_KW (IDENT EQ constExpr SEMI)+
    ;

typeDecl
    : TYPE_KW (IDENT EQ typeSpec SEMI)+
    ;

varDecl
    : VAR_KW (identList COLON typeSpec SEMI)+
    ;

identList
    : IDENT (COMMA IDENT)*
    ;

typeSpec
    : INTEGER_KW                                                 # integerType
    | REAL_KW                                                    # realType
    | CHAR_KW                                                     # charType
    | BOOLEAN_KW                                                  # booleanType
    | CARET typeSpec                                              # pointerType
    | ARRAY_KW LBRACK rangeSpec (COMMA rangeSpec)* RBRACK OF_KW typeSpec   # arrayType
    | SET_KW OF_KW typeSpec                                       # setType
    | RECORD_KW fieldList END_KW                                  # recordType
    | LPAREN IDENT (COMMA IDENT)* RPAREN                          # enumType
    | constExpr DOTDOT constExpr                                  # subrangeType
    // A previously `type`-declared name - checked last since every
    // keyword-led alternative above already claims its own first
    // token; only a bare identifier falls through to here.
    | IDENT                                                       # namedType
    ;

rangeSpec
    : constExpr DOTDOT constExpr
    ;

// Real Pascal allows an empty `record end`; the trailing `;` is
// optional on the last field, same as a C struct's own member list.
fieldList
    : (identList COLON typeSpec SEMI)* (identList COLON typeSpec)?
    ;

procDecl
    : PROCEDURE_KW IDENT formalParams? SEMI block SEMI
    ;

funcDecl
    : FUNCTION_KW IDENT formalParams? COLON typeSpec SEMI block SEMI
    ;

formalParams
    : LPAREN formalParamGroup (SEMI formalParamGroup)* RPAREN
    ;

// A leading `var` marks the whole group pass-by-reference - see
// docs/pascal-language.md's "var parameters".
formalParamGroup
    : VAR_KW? identList COLON typeSpec
    ;

compoundStmt
    : BEGIN_KW statement (SEMI statement)* END_KW
    ;

// The trailing empty alternative is what makes a bare `;` (two
// statements separated by a redundant semicolon, or one right before
// `end`) legal, same as real Pascal.
statement
    : compoundStmt                                                        # compoundStatement
    | designator ASSIGN expr                                              # assignStmt
    | designator                                                          # callStmt
    | IF_KW expr THEN_KW statement (ELSE_KW statement)?                   # ifStmt
    | WHILE_KW expr DO_KW statement                                       # whileStmt
    | REPEAT_KW statement (SEMI statement)* UNTIL_KW expr                 # repeatStmt
    | FOR_KW IDENT ASSIGN expr dir=(TO_KW | DOWNTO_KW) expr DO_KW statement # forStmt
    | CASE_KW expr OF_KW caseBranch (SEMI caseBranch)* (SEMI ELSE_KW statement)? SEMI? END_KW  # caseStmt
    |                                                                      # emptyStmt
    ;

caseBranch
    : constExpr (COMMA constExpr)* COLON statement
    ;

// A variable, array index, field access, pointer deref, or a
// procedure/function call - one rule covers all of them (both as an
// assignment target and as a statement/expression on its own), same
// "one designator rule" real Pascal grammars use, rather than
// splitting lvalue vs. call vs. plain-variable into separate rules
// that would just duplicate this suffix chain three times.
designator
    : IDENT (LPAREN exprList? RPAREN | LBRACK exprList RBRACK | DOT IDENT | CARET)*
    ;

exprList
    : expr (COMMA expr)*
    ;

// Precedence chain, loosest first - mirrors ISO 7185's own
// expression/simple_expression/term/factor split exactly (not an
// rcc-specific choice, unlike CParser.g4's condExpr chain), since
// that's the one piece of standard Pascal grammar shape worth keeping
// as-is rather than reinventing.
expr
    : simpleExpr (relOp simpleExpr)?
    ;

relOp
    : EQ | NE | LT | LE | GT | GE | IN_KW
    ;

simpleExpr
    : sign=(PLUS | MINUS)? term (addOp term)*
    ;

addOp
    : PLUS | MINUS | OR_KW
    ;

term
    : factor (mulOp factor)*
    ;

mulOp
    : STAR | SLASH | DIV_KW | MOD_KW | AND_KW
    ;

factor
    : designator                 # designatorFactor
    | INTLIT                     # intFactor
    | REALLIT                    # realFactor
    | STRINGLIT                  # stringFactor
    | TRUE_KW                    # trueFactor
    | FALSE_KW                   # falseFactor
    | LPAREN expr RPAREN         # parenFactor
    | NOT_KW factor               # notFactor
    // Set constructor (`[1, 3, 5..9]`) - see docs/pascal-language.md's
    // "Sets". Reuses rangeSpec for a sub-range element so `5..9` is
    // legal here without a second copy of that production.
    | LBRACK (setElement (COMMA setElement)*)? RBRACK   # setFactor
    // Address-of (`@x`, `@arr[i]`) - the only way this frontend gives a
    // pointer variable something to point at; see docs/pascal-language.md.
    | AT designator              # addrOfFactor
    ;

setElement
    : expr (DOTDOT expr)?
    ;

// A compile-time constant: an optionally signed number, a previously
// declared const/enum-member name, or a string/char literal - never a
// general expression, same restriction a C enum member's value or
// array size already has (see docs/language.md's "Enums").
constExpr
    : sign=(PLUS | MINUS)? (INTLIT | REALLIT | IDENT)
    | STRINGLIT
    ;
