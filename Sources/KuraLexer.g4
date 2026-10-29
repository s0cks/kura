lexer grammar KuraLexer;

LET   : 'let';
FN    : 'fn';
TYPE  : 'type';
IF    : 'if';
ELSE  : 'else';
MATCH : 'match';
FOR   : 'for';
IN    : 'in';
FOLD  : 'fold';
USE   : 'use';
AS    : 'as';
EXTERN: 'extern';
TRUE  : 'true';
FALSE : 'false';

NONE  : 'None';

PX_LIT  : NUMBER_FRAG 'px';
PCT_LIT : NUMBER_FRAG '%';
FR_LIT  : NUMBER_FRAG 'fr';

NUMBER  : NUMBER_FRAG;

STRING  : '"' ( ~["\\\r\n] | ESCAPE )* '"';

COLOR_LIT
  : 'black' | 'white' | 'red' | 'green' | 'blue' | 'yellow'
  | 'cyan' | 'magenta' | 'gray' | 'orange' | 'purple' | 'transparent'
  ;

UNDERSCORE  : '_';

RAW_IDENT   : '`' ~[`\r\n]+ '`';

IDENT       : [a-z_] [a-zA-Z0-9_]* ( '-' [a-zA-Z0-9_]+ )*;

UPPER_IDENT : [A-Z] [a-zA-Z0-9_]*;

DIRECTIVE   : '@' [a-z] [a-z0-9]* ( '-' [a-z0-9]+ )*;

BIN_MINUS : [ \t\r\n]+ '-' [ \t\r\n]+;
MINUS     : '-';

PLUS   : '+';
STAR   : '*';
SLASH  : '/';

PIPE   : '|>';
OR     : '||';
AND    : '&&';
EQ     : '==';
NEQ    : '!=';
LE     : '<=';
GE     : '>=';
LT     : '<';
GT     : '>';
BANG   : '!';

FAT_ARROW : '=>';
ASSIGN    : '=';
SPREAD    : '...';
DOT       : '.';
COMMA     : ',';
COLON     : ':';
QMARK     : '?';
HASH      : '#';
BAR       : '|';

LPAREN : '(';
RPAREN : ')';
LBRACE : '{';
RBRACE : '}';
LBRACK : '[';
RBRACK : ']';

LINE_COMMENT : '//' ~[\r\n]* -> channel(HIDDEN);
WS           : [ \t\r\n]+    -> channel(HIDDEN);

fragment NUMBER_FRAG : [0-9]+ ( '.' [0-9]+ )?;
fragment ESCAPE      : '\\' [nt"\\];
