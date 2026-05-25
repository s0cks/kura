grammar Kura;

kura
  : funcDef*
  ;

expression
  : literal
  | expression op=('+' | '-' | '*' | '/' | '%' | '&' | '^') expression
  | op=('+' | '-') expression
  ;

funcDef
  : IDENTIFIER funcArgsList? '=' expression
  ;

funcArgsList
  : IDENTIFIER+
  ;

listDef
  : '(' literal (',' literal)* ','? ')'
  ;

literal
  : STRING
  | NIL
  | bool
  | recordDef
  | listDef
  ;

recordDef
  : '{' propertyList '}'
  | '{' '}'
  ;

propertyList
  : property (',' property)* ','?
  ;

property
  : IDENTIFIER ':' expression
  ;

bool
  : TRUE
  | FALSE
  ;


NIL   : 'nil' ;
TRUE  : 'true' ;
FALSE : 'false' ;

STRING
  : '"' StringChar* '"'
  | '\'' StringChar* '\''
  ;

fragment StringChar
  : ~[\\\r\n]
  ;

IDENTIFIER
  : NonDigit (NonDigit | Digit)*
  ;

fragment NonDigit
  : [a-zA-Z_-]
  ;

fragment Digit
  : [0-9]
  ;

LineComment
  : '#' ~[\r\n]* -> channel(HIDDEN)
  ;

Newline
  : ('\r' '\n'? | '\n') -> channel(HIDDEN)
  ;

Whitespace
  : [ \t]+ -> channel(HIDDEN)
  ;
