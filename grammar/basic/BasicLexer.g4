lexer grammar BasicLexer;

// ---------------------------------------------------------------------
// Lexer half of rcc's BASIC-subset grammar (see BasicParser.g4 for the
// parser half and ../../docs/basic-language.md for what the language
// actually is). Keywords are case-sensitive, uppercase-only ('PRINT',
// not 'print'/'Print') - the smallest rule that avoids every keyword
// needing a case-insensitive fragment, matching this subset's general
// "smallest useful surface" approach (see ../c/CLexer.g4's own header
// comment for the same philosophy applied to the C frontend).
// ---------------------------------------------------------------------

LET_KW    : 'LET' ;
PRINT_KW  : 'PRINT' ;
INPUT_KW  : 'INPUT' ;
IF_KW     : 'IF' ;
THEN_KW   : 'THEN' ;
ELSE_KW   : 'ELSE' ;
FOR_KW    : 'FOR' ;
TO_KW     : 'TO' ;
STEP_KW   : 'STEP' ;
NEXT_KW   : 'NEXT' ;
GOTO_KW   : 'GOTO' ;
GOSUB_KW  : 'GOSUB' ;
RETURN_KW : 'RETURN' ;
DIM_KW    : 'DIM' ;
END_KW    : 'END' ;
AND_KW    : 'AND' ;
OR_KW     : 'OR' ;
NOT_KW    : 'NOT' ;

LPAREN   : '(' ;
RPAREN   : ')' ;
COMMA    : ',' ;
SEMI     : ';' ;
COLON    : ':' ;
PLUS     : '+' ;
MINUS    : '-' ;
STAR     : '*' ;
SLASH    : '/' ;
LE       : '<=' ;
GE       : '>=' ;
NE       : '<>' ;
LT       : '<' ;
GT       : '>' ;
EQ       : '=' ;

// A sigil ('$' = string, '%' = explicit integer; a bare name is also
// an integer - see ../../docs/basic-language.md) is part of the
// identifier's own spelling, same as real BASIC: A, A$ and A% are
// three distinct variables, not one name with a type annotation.
IDENT : [A-Za-z][A-Za-z0-9]* ('$' | '%')? ;

NUMBER : [0-9]+ ;

STRINGLIT : '"' ~["\r\n]* '"' ;

// REM takes the rest of the physical line verbatim - no lexer mode
// needed (unlike CLexer.g4's own asm{} block) since a comment has no
// closing sentinel of its own to watch for; it simply runs to the
// next NEWLINE.
REM_COMMENT  : 'REM' ~[\r\n]* -> skip ;
LINE_COMMENT : '\'' ~[\r\n]* -> skip ;

// Only horizontal whitespace is skipped - NEWLINE is a real token
// (see program/line in BasicParser.g4): unlike the C frontend, a
// BASIC statement's extent is exactly one physical line (or several
// joined by ':'), so line breaks are part of the grammar, not
// insignificant whitespace.
WS      : [ \t]+ -> skip ;
NEWLINE : '\r'? '\n' ;
