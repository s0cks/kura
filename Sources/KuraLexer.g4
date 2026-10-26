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
USE   : 'use';
AS    : 'as';
EXTERN: 'extern';
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

// Pattern wildcard. Listed before IDENT so that a lone `_` is the wildcard and
// anything longer (`_exit`, `__x`) is an identifier.
UNDERSCORE  : '_';

// A name that would otherwise be a keyword: `type`, `in`, `red`. Only names
// that come from C need this.
RAW_IDENT   : '`' ~[`\r\n]+ '`';

// Kura code is written in kebab-case (item-count, get-position, b-c), but C
// names are accepted as they are: gl_clear, glClear, _exit.
// Must start with a letter or `_` so it never collides with numbers.
IDENT       : [a-z_] [a-zA-Z0-9_]* ( '-' [a-zA-Z0-9_]+ )*;

// PascalCase: types and variants. C names that start with a capital (SDL_Init,
// GL_TRUE) are also lexed as this.
UPPER_IDENT : [A-Z] [a-zA-Z0-9_]*;

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
