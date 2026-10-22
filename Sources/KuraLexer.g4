/*
 * KuraLexer.g4 — Kura v1 lexical structure.
 * Source of truth: wiki "Language Syntax" (v1).
 *
 * Whitespace and comments go to the HIDDEN channel (not skipped) so that
 * tooling (formatter, LSP) can still see them.
 */
lexer grammar KuraLexer;

// ---------------------------------------------------------------------------
// Keywords
// ---------------------------------------------------------------------------
LET   : 'let';
FN    : 'fn';
TYPE  : 'type';
IF    : 'if';
ELSE  : 'else';
MATCH : 'match';
FOR   : 'for';
IN    : 'in';
FOLD  : 'fold';
TRUE  : 'true';
FALSE : 'false';

// `None` is both the type and its only value, and appears in patterns.
NONE  : 'None';

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

// Unit literals: the suffix follows the number with no whitespace.
// (Listed before NUMBER so the longer match wins: `10px` is one token.)
PX_LIT  : NUMBER_FRAG 'px';
PCT_LIT : NUMBER_FRAG '%';
FR_LIT  : NUMBER_FRAG 'fr';

NUMBER  : NUMBER_FRAG;

STRING  : '"' ( ~["\\\r\n] | ESCAPE )* '"';

// Named colors are reserved words (spec: "Named color literals ... are also
// reserved"). The v1 set is an OPEN QUESTION in the spec; this list is a
// placeholder — edit it in one place. Must stay ahead of IDENT.
COLOR_LIT
  : 'black' | 'white' | 'red' | 'green' | 'blue' | 'yellow'
  | 'cyan' | 'magenta' | 'gray' | 'orange' | 'purple' | 'transparent'
  ;

// ---------------------------------------------------------------------------
// Identifiers
// ---------------------------------------------------------------------------

// kebab-case: lowercase letters and digits, single hyphens between parts.
// Must start with a letter so it never collides with numbers.
//   item-count, get-position, is-debug, b-c      (b-c is ONE identifier)
IDENT       : [a-z] [a-z0-9]* ( '-' [a-z0-9]+ )*;

// PascalCase: types and variants.
UPPER_IDENT : [A-Z] [a-zA-Z0-9]*;

// Pattern wildcard (not a valid identifier character otherwise).
UNDERSCORE  : '_';

// `@simd`, `@name` — a directive is a single token.
DIRECTIVE   : '@' [a-z] [a-z0-9]* ( '-' [a-z0-9]+ )*;

// ---------------------------------------------------------------------------
// Operators and punctuation
// ---------------------------------------------------------------------------

// Binary minus REQUIRES whitespace on both sides. The whitespace is folded
// into the token so that `a - b` lexes as BIN_MINUS while `-b` and `a -b`
// lex as the unary MINUS (and `a -b` then fails to parse, as the spec
// demands). Longest-match makes this beat WS.
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

// ---------------------------------------------------------------------------
// Hidden channel
// ---------------------------------------------------------------------------
LINE_COMMENT : '//' ~[\r\n]* -> channel(HIDDEN);
WS           : [ \t\r\n]+    -> channel(HIDDEN);

// ---------------------------------------------------------------------------
// Fragments
// ---------------------------------------------------------------------------
fragment NUMBER_FRAG : [0-9]+ ( '.' [0-9]+ )?;
fragment ESCAPE      : '\\' [nt"\\];
