lexer grammar PascalLexer;

// ---------------------------------------------------------------------
// Lexer half of rcc's Pascal-subset grammar (see PascalParser.g4 for
// the parser half and ../../docs/pascal-language.md for exactly what
// v1 of this frontend accepts). Keywords are lowercase, case-sensitive
// ('begin', not 'Begin'/'BEGIN') - the smallest rule that avoids every
// keyword needing a case-insensitive fragment, same "smallest useful
// surface" philosophy ../c/CLexer.g4 and ../basic/BasicLexer.g4 each
// apply to their own subset (BASIC picked uppercase-only for the same
// reason; Pascal source in the wild skews lowercase, so that's the
// single case this grammar accepts).
// ---------------------------------------------------------------------

PROGRAM_KW  : 'program' ;
VAR_KW      : 'var' ;
CONST_KW    : 'const' ;
TYPE_KW     : 'type' ;
ARRAY_KW    : 'array' ;
OF_KW       : 'of' ;
RECORD_KW   : 'record' ;
SET_KW      : 'set' ;
PROCEDURE_KW: 'procedure' ;
FUNCTION_KW : 'function' ;
BEGIN_KW    : 'begin' ;
END_KW      : 'end' ;
IF_KW       : 'if' ;
THEN_KW     : 'then' ;
ELSE_KW     : 'else' ;
WHILE_KW    : 'while' ;
DO_KW       : 'do' ;
REPEAT_KW   : 'repeat' ;
UNTIL_KW    : 'until' ;
FOR_KW      : 'for' ;
TO_KW       : 'to' ;
DOWNTO_KW   : 'downto' ;
CASE_KW     : 'case' ;
NOT_KW      : 'not' ;
AND_KW      : 'and' ;
OR_KW       : 'or' ;
IN_KW       : 'in' ;
DIV_KW      : 'div' ;
MOD_KW      : 'mod' ;
INTEGER_KW  : 'integer' ;
REAL_KW     : 'real' ;
CHAR_KW     : 'char' ;
BOOLEAN_KW  : 'boolean' ;
TRUE_KW     : 'true' ;
FALSE_KW    : 'false' ;

ASSIGN  : ':=' ;
DOTDOT  : '..' ;
LE      : '<=' ;
GE      : '>=' ;
NE      : '<>' ;
SEMI    : ';' ;
COLON   : ':' ;
COMMA   : ',' ;
DOT     : '.' ;
LPAREN  : '(' ;
RPAREN  : ')' ;
LBRACK  : '[' ;
RBRACK  : ']' ;
EQ      : '=' ;
LT      : '<' ;
GT      : '>' ;
PLUS    : '+' ;
MINUS   : '-' ;
STAR    : '*' ;
SLASH   : '/' ;
// Doubles as both the pointer-type suffix (T^, see "pointerType" in
// PascalParser.g4) and the dereference postfix on a designator
// (p^) - same single token, disambiguated by grammar position, not
// by the lexer.
CARET   : '^' ;

IDENT : [A-Za-z_][A-Za-z0-9_]* ;

// A real literal needs a decimal point immediately followed by a
// digit, so `1..5` (a subrange bound, see "rangeSpec" in
// PascalParser.g4) lexes as INTLIT DOTDOT INTLIT, never as a stray
// REALLIT eating the first '.'.
REALLIT : [0-9]+ '.' [0-9]+ ( [eE] [+-]? [0-9]+ )?
        | [0-9]+ [eE] [+-]? [0-9]+
        ;
INTLIT  : [0-9]+ ;

// Serves as both Pascal's string literal and its char literal (a
// single-character STRINGLIT) - see ../../docs/pascal-language.md;
// there's no separate CHARLIT token the way CLexer.g4 has one, since
// Pascal spells both the same way. A literal quote inside one is a
// doubled quote ('it''s'), the standard Pascal escape.
STRINGLIT : '\'' ( '\'\'' | ~['\r\n] )* '\'' ;

// Pascal has two unrelated comment spellings in the wild; both are
// accepted and simply skipped, with no lexer mode needed (unlike
// CLexer.g4's own asm{} block) since neither has anything inside it
// this grammar ever needs to see.
BRACE_COMMENT : '{' ~[}]* '}' -> skip ;
PAREN_COMMENT : '(*' .*? '*)' -> skip ;

WS : [ \t\r\n]+ -> skip ;
