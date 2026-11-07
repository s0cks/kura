# Kura v1 grammar

The hand-written parser in `parser.cc` implements this grammar. It replaces the ANTLR grammars
(`KuraLexer.g4` / `KuraParser.g4`), which this document is derived from; if the two ever disagree,
the parser and its tests (`tests/syntax`, `tests/grammar`) win.

Notation: `x?` optional, `x*` zero or more, `x+` one or more, `a | b` alternatives, `'t'` a token.
Whitespace and `// comments` separate tokens and mean nothing else. **There are no statement
terminators**: `let a = 1 let b = 2` is two bindings.

## Lexical rules

The lexer takes the longest match, and on a tie the keyword/literal rules come before `name`.

```
name        = [a-z_][A-Za-z0-9_]* ( '-' [A-Za-z0-9_]+ )*      kebab-case; `a-b` is ONE name
Name        = [A-Z][A-Za-z0-9_]*                               types and variants; no hyphens
raw-name    = '`' [^`\r\n]+ '`'                                for C names that clash with keywords
directive   = '@' [a-z][a-z0-9]* ( '-' [a-z0-9]+ )*
number      = [0-9]+ ( '.' [0-9]+ )?
px / % / fr = number 'px' | number '%' | number 'fr'           unit literals (the suffix always wins)
string      = '"' ( [^"\\\r\n] | '\' [nt"\\] )* '"'            one line only
color       = black white red green blue yellow cyan magenta gray orange purple transparent
keywords    = let fn type if else match for in fold use as extern true false None
'_'         = the wildcard, on its own (`_x` is a name)
'-'         = BIN_MINUS when whitespace is on BOTH sides (`a - b`), otherwise unary MINUS
operators   = |> || && == != <= >= < > ! + * / => = ... . , : ? # | ( ) { } [ ]
```

`>>` is two `>` tokens so `Seq<Seq<Px>>` closes both lists.

## Declarations

```
source       = item*
item         = directive* ( typeDecl | fnDecl | externDecl | useDecl | binding )
binding      = 'let' name ( ':' type )? '=' expression          the top level has no bare expressions
             | name '=' expression                              redefinition
typeDecl     = 'type' Name ( '<' Name ( ',' Name )* '>' )? '=' type
fnDecl       = 'fn' name '(' params? ')' ( ':' type )? '=>' expression      the parentheses are required
externDecl   = 'extern' string? 'fn' cName '(' externParams? ')' ':' type ( '=>' expression )?
useDecl      = 'use' string ( 'as' ( name | Name ) )?
params       = param ( ',' param )* ','?
param        = name ':' type
externParams = params ( ',' '...' )? ','? | '...'
cName        = name | Name | raw-name
directive    = '@name' ( '(' uiArgs? ')' )?
```

## Types

```
type         = typeMember ( '|' typeMember )*
typeMember   = Name '(' type ')'                       variant with a payload
             | Name typeArgs?                           named type
             | ( name | Name ) '.' cName typeArgs?      from a `use`d header, or `c.int` and friends
             | 'None'
             | '{' ( field ( ',' field )* ','? )? '}'   record type
             | '(' ( type ( ',' type )* ','? )? ')' '=>' type
field        = name '?'? ':' type
typeArgs     = '<' type ( ',' type )* '>'
```

## Expressions

Binary operators, loosest to tightest, all left associative:

| level | operators |
|------:|-----------|
| 0 | `x \|> call(...)`: the pipe, the loosest of all |
| 1 | `\|\|` |
| 2 | `&&` |
| 3 | `==` `!=` |
| 4 | `<` `<=` `>` `>=` |
| 5 | `+` and BIN_MINUS |
| 6 | `*` `/` |
| 7 | prefix `-` `!` |
| 8 | postfix `.member` and `(args)` |

```
expression   = operation ( '|>' pipeTarget )*
pipeTarget   = pipeCallee '(' ( expression ( ',' expression )* ','? )? ')'
pipeCallee   = ( name | Name ) ( '.' member )* | '(' expression ')'
member       = name | Name | raw-name | keyword | color

primary      = number | string | 'true' | 'false' | 'None' | unit | color
             | name | Name
             | '[' ( expression ( ',' expression )* ','? )? ']'
             | record | block | ifExpr | matchExpr | forExpr
             | '(' ( name ( ',' name )* ','? )? ')' '=>' expression      lambda
             | '(' expression ')'
             | uiElement
```

`{` starts a **record** when it is followed by `}`, `...`, or `name :`; otherwise it is a **block**.
`(` starts a **lambda** when the matching `)` is followed by `=>`.

```
record       = '{' ( recordEntry ( ',' recordEntry )* ','? )? '}'
recordEntry  = name ':' expression | '...' expression
block        = '{' ( binding | expression )* '}'               an expression, if present, is last
ifExpr       = 'if' expression block 'else' ( ifExpr | block )      `else` is required
matchExpr    = 'match' expression '{' arm ( ',' arm )* ','? '}'
arm          = pattern '=>' expression
forExpr      = 'for' name 'in' expression ( 'if' expression )? ( 'fold' name '=' expression )? '=>' expression
pattern      = '_' | name | 'None' | Name ( '(' pattern ')' )?
             | '-'? ( number | unit | color ) | string | 'true' | 'false'
```

## UI elements

```
uiElement    = directive* '#' name ( '(' uiArgs? ')' )? ( '{' uiChild* '}' )?
uiArgs       = uiArg ( ',' uiArg )* ','?
uiArg        = name ':' expression | '...' expression | expression
uiChild      = uiElement | binding
```
