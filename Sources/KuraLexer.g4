lexer grammar KuraLexer;

// ╭────────────╮
// │ Keywords   │
// ╰────────────╯
IMPORT       : 'import' ;
TYPE         : 'type' ;
FN           : 'fn' ;
MACRO        : 'macro' ;
MATCH        : 'match' ;
IF           : 'if' ;
ELSE         : 'else' ;
FOR          : 'for' ;
IN           : 'in' ;
WHERE        : 'where' ;
TRUE         : 'true' ;
FALSE        : 'false' ;
NULL         : 'null' ;
NONE         : 'none' ;

// ╭────────────╮
// │ Operators  │
// ╰────────────╯
PIPE         : '|>' ;
SINGLE_PIPE  : '|' ;
SPREAD       : '...' ;
OPTIONALDOT  : '?.' ;
OR           : '||' ;
AND          : '&&' ;
EQ           : '==' ;
NEQ          : '!=' ;
LT           : '<' ;
LTE          : '<=' ;
GT           : '>' ;
GTE          : '>=' ;
ASSIGN       : '=' ;
PLUS         : '+' ;
MINUS        : '-' ;
STAR         : '*' ;
SLASH        : '/' ;
PERCENT      : '%' ;
BANG         : '!' ;
DOT          : '.' ;
COMMA        : ',' ;
COLON        : ':' ;
SEMICOLON    : ';' ;
UNDERSCORE   : '_' ;
DOUBLE_ARROW : '=>' ;

LPAREN       : '(' ;
RPAREN       : ')' ;

LBRACE       : '{' ;
RBRACE       : '}' ;

LBRACKET     : '[' ;
RBRACKET     : ']' ;
HASH         : '#' ;

// ╭────────────╮
// │ Literals   │
// ╰────────────╯
MEASUREMENT
  : WHOLE_NUMBER 'px'
  | DECIMAL_NUMBER
    (
      'fr'
      | 'em'
      | 'ms'
      | 'deg'
      | 'rad'
      | 'vw'
      | 'vh'
      | '%'
    )
  ;

fragment WHOLE_NUMBER
  : '0' | [1-9] [0-9]*
  ;

fragment DECIMAL_NUMBER
  : WHOLE_NUMBER '.' WHOLE_NUMBER
  ;

NUMBER
  : [0-9]+ ('.' [0-9]+)?
  ;

STRING
  : '"' (~["\\] | ESCAPE_SEQUENCE)* '"'
  ;

fragment ESCAPE_SEQUENCE
  : '\\' .
  ;

// ╭────────────╮
// │ Identifiers│
// ╰────────────╯
IDENTIFIER
  : [a-zA-Z_][a-zA-Z0-9_]*
  ;

// ╭────────────╮
// │ Comments   │
// ╰────────────╯
LINE_COMMENT
  : '//' ~[\r\n]* -> skip
  ;

BLOCK_COMMENT
  : '/*' .*? '*/' -> skip
  ;

WHITESPACE
  : [ \t\r\n]+ -> skip
  ;
