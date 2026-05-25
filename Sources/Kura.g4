grammar Kura;

screen
  : funcDef*
  ;

expression
  : literal
  | functionCall
  | expression op=('+' | '-' | '*' | '/' | '%' | '&' | '^') expression
  | op=('+' | '-') expression
  | markup
  | expression '.' IDENTIFIER
  | expression '?.' IDENTIFIER
  | IDENTIFIER
  | matchExpr
  ;

expressionList
  : expression expression*
  ;

matchExpr
  : 'match' expression caseExpr* defaultExpr?
  ;

caseExpr
  : (literal | IDENTIFIER) '->' expressionList
  ;

defaultExpr
  : '_' '->' expressionList
  ;

markup
  : '#' IDENTIFIER '(' arguments? ')' ('[' markupList? ']')?
  ;

markupList
  : markup+
  ;

functionCall
  : IDENTIFIER '(' arguments? ')'
  ;

arguments
  : expression (',' expression)*
  ;

funcDef
  : IDENTIFIER funcArgsList? '=' expression
  ;

funcArgsList
  : IDENTIFIER+
  ;

listDef
  : '(' expression (',' expression)* ','? ')'
  ;

literal
  : STRING
  | NIL
  | NONE
  | NUMBER
  | bool
  | recordDef
  | listDef
  | MEASUREMENT
  ;

recordDef
  : '{' propertyList? '}'
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

MEASUREMENT
  : WholeNumber 'px'
  | (WholeNumber | Decimal) 'fr'
  ;

fragment WholeNumber
  : '0'
  | [1-9] [0-9]*
  ;

fragment Decimal
  : [0-9]+ '.' [0-9]+
  ;

NUMBER
  : WholeNumber
  | Decimal
  ;

NONE
  : 'none'
  ;

NIL
  : 'nil'
  ;

TRUE
  : 'true'
  ;

FALSE
  : 'false'
  ;

STRING
  : '"' StringChar* '"'
  | '\'' StringChar* '\''
  ;

IDENTIFIER
  : NonDigit (NonDigit | Digit)*
  ;

fragment StringChar
  : ~[\\\r\n]
  ;

fragment NonDigit
  : [a-zA-Z_-]
  ;

fragment Digit
  : [0-9]
  ;

LineComment
  : '//' ~[\r\n]* -> channel(HIDDEN)
  ;

Newline
  : ('\r' '\n'? | '\n') -> channel(HIDDEN)
  ;

Whitespace
  : [ \t]+ -> channel(HIDDEN)
  ;
