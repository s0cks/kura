# Kura v1 grammar

The hand-written parser in `parser.cc` implements this grammar. It replaces the ANTLR grammars
(`KuraLexer.g4` / `KuraParser.g4`), which this document was first derived from; if the two ever
disagree, the parser and its tests (`tests/syntax`, `tests/grammar`) win.

Notation: `x?` optional, `x*` zero or more, `x+` one or more, `a | b` alternatives, `'t'` a token,
`( ... )` grouping, and text after two or more spaces is a comment. Whitespace and `// comments`
separate tokens and mean nothing else. **There are no statement terminators**: `let a = 1 let b = 2`
is two bindings.

Rules the grammar can't say (they depend on what came before, or on the line a token is on) are
written under the rule they affect and collected again in [Context rules](#context-rules).

## Lexical rules

The lexer takes the longest match, and on a tie the keyword/literal rules come before `name`.

```text
name        = [a-z_][A-Za-z0-9_]* ( '-' segment )*             kebab-case; `a-b` is ONE name
segment     = [A-Za-z0-9_]+ with at least one non-digit        `x-1` is `x` minus `1`; `pos-2d` is one name
Name        = [A-Z][A-Za-z0-9_]*                               types and variants; no hyphens
raw-name    = '`' [^`\r\n]+ '`'                                for C names that clash with keywords
directive   = '@' [a-z][a-z0-9]* ( '-' [a-z0-9]+ )*
number      = [0-9]+ ( '.' [0-9]+ )?                           `0..10` is `0` `..` `10`
px / % / fr = number 'px' | number '%' | number 'fr'           unit literals (the suffix always wins)
string      = '"' ( text | escape | interpolation )* '"'       one line only
text        = [^"\\{\r\n]
escape      = '\' [nt"\\{}]                                    `\{` and `\}` are literal braces
interpolation = '{' ( [^{}"\r\n] | string | interpolation )* '}'
color       = black white red green blue yellow cyan magenta gray orange purple transparent
keywords    = let fn type if else match for in fold use as extern when is static-assert true false None
`set`       is a keyword only where a set rule can start (the name `set` followed by another name);
            anywhere else it is an ordinary name, so `fn set(...)` and `set(1)` still work
'_'         = the wildcard, on its own (`_x` is a name)
'-'         = BIN_MINUS when whitespace is on BOTH sides (`a - b`), otherwise unary MINUS
operators   = |> <| || && ?? == != <= >= < > ! + - * / % => = ... .. ..= . ?. , : ? # | ( ) { } [ ]
              += -= *= /= %=
```

`>>` and `<<` are two tokens each (`>` `>` and `<` `<`, with nothing between them), so that
`Seq<Seq<Px>>` closes both lists. Where an operator is expected the parser puts them back together
as the composition operators.

A lone `}` in a string is plain text; only `{` starts an interpolation. The text between the braces of
an interpolation is parsed as one expression, on its own (see [Strings](#strings)).

## Declarations

```text
source       = item*
item         = directive* ( typeDecl | fnDecl | externDecl | useDecl | binding )
             | setRule | staticAssert                           no directives on these two
binding      = 'let' ( name | recordPattern ) ( ':' type )? '=' expression
             | name '=' expression                              redefinition
typeDecl     = 'type' Name ( '<' Name ( ',' Name )* '>' )? '=' type
fnDecl       = 'fn' name '(' params? ')' ( ':' type )? '=>' setRule* expression
externDecl   = 'extern' string? 'fn' cName '(' externParams? ')' ':' type ( '=>' expression )?
useDecl      = 'use' string ( 'as' ( name | Name ) )?
setRule      = 'set' name '(' uiArgs? ')'                       `set button(fill: black)`
staticAssert = 'static-assert' '(' expression ( ',' string )? ','? ')'
params       = param ( ',' param )* ','?
param        = name ':' type
externParams = params ( ',' '...' )? ','? | '...'
cName        = name | Name | raw-name
directive    = '@name' ( '(' uiArgs? ')' )?
```

The top level has no bare expressions. A function's parentheses are required. `set` rules before the body
need no braces; a `let` in a body needs one (`=> { let a = 1 ... }`). The message of a `static-assert` is a
string, written plainly or with interpolation. A string with an interpolation can't be the header of a `use`
or the symbol of an `extern`.

A destructuring `let` has no `name`: `let { message, status: code, ...rest } = r`.

## Types

```text
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

Operators, loosest to tightest:

| level | operators                                                              | associativity                                                |
| ----: | ---------------------------------------------------------------------- | ------------------------------------------------------------ |
|     0 | `x \|> stage`, `f <\| x`: the pipes                                    | `\|>` left, `<\|` right; the two can't be mixed in one chain |
|     1 | `>>` `<<`: composition                                                 | left                                                         |
|     2 | `a..b` `a..=b` `a..` `..b` `..=b`: ranges                              | none                                                         |
|     3 | `??`                                                                   | right                                                        |
|     4 | `\|\|`                                                                 | left                                                         |
|     5 | `&&`                                                                   | left                                                         |
|     6 | `x is pattern`                                                         | left                                                         |
|     7 | `==` `!=`                                                              | left                                                         |
|     8 | `<` `<=` `>` `>=`                                                      | left                                                         |
|     9 | `+` and BIN_MINUS                                                      | left                                                         |
|    10 | `*` `/` `%`                                                            | left                                                         |
|    11 | prefix `-` `!`                                                         |                                                              |
|    12 | postfix `.member` `?.member` `(args)` `?.(args)` `[index]` `?.[index]` | left                                                         |

```text
expression   = operation ( pipe )*                              pipes of one direction only
pipe         = '|>' stage | '<|' operation
operation    = compose
compose      = range ( ( '>>' | '<<' ) range )*
range        = coalesce ( ( '..' | '..=' ) coalesce? )?
             | ( '..' | '..=' ) coalesce?
coalesce     = or ( '??' coalesce )?
or           = and ( '||' and )*
and          = is ( '&&' is )*
is           = equality ( 'is' pattern )*
equality     = comparison ( ( '==' | '!=' ) comparison )*
comparison   = additive ( ( '<' | '<=' | '>' | '>=' ) additive )*
additive     = multiplicative ( ( '+' | BIN_MINUS ) multiplicative )*
multiplicative = unary ( ( '*' | '/' | '%' ) unary )*
unary        = ( '-' | '!' )* postfix
postfix      = primary ( '.' member | '?.' member | args | '?.' args | index | '?.' index )*
args         = '(' ( arg ( ',' arg )* ','? )? ')'
arg          = '_' | expression                                 a whole-argument `_` is a placeholder
index        = '[' expression ']'                               the index may be a range: a slice
member       = name | Name | raw-name | keyword | color
```

A `..` that begins a line does not continue the line before it, and neither does a `[`; they start something
new (a match arm, a Seq). A range's end is left out when what follows can't start an expression.

### Pipes and placeholders

```text
stage        = operation                                        what is piped into
```

After `|>`, the stage is read as an `operation` (level 1 and tighter) and then has one of two meanings:

* It contains a `_`: `10 |> add(_, 10)`, `x |> _ + 1`. Every `_` in the stage is the piped value,
  which is computed once. (A `_` that is a whole argument of an *inner* call counts too: `x |> f(g(_))`.)
* It has no `_` and is a call whose callee is a name, a `Name`, a `name.member` chain or a parenthesized
  expression: `x |> f(y)` is `f(x, y)`, and `payload |> Click()` is `Click(payload)`. Anything else, such
  as `x |> f` or `x |> 1 + 2`, is an error.

Outside a pipe stage a `_` is only allowed as a whole argument of a call, where it makes a partial
application: `add(_, 10)` is a function waiting for its first argument. `f <| x` applies the function on
its left to `x`; `f <| g <| x` is `f(g(x))`.

### Primary expressions

```text
primary      = number | string | 'true' | 'false' | 'None' | unit | color
             | name | Name | '_'                                '_' only where the rules above allow it
             | seq | comprehension
             | record | recordUpdate | block
             | ifExpr | whenExpr | matchExpr | forExpr
             | '(' ( name ( ',' name )* ','? )? ')' '=>' expression      lambda
             | '(' expression ')'
             | uiElement
seq          = '[' ( expression ( ',' expression )* ','? )? ']'
comprehension = '[' expression 'for' forHead ']'                no `fold`
```

`(` starts a **lambda** when the matching `)` is followed by `=>` (except in a guard, see below).

`{` starts

* a **record** when it is followed by `}`, `...`, `name :` or `name ,`;
* a **record update** when it is followed by `name ( '.' member )* |`;
* otherwise a **block**.

So `{ x }` is a block that returns `x`, and `{ x, }` is a record with one punned entry.

```text
record       = '{' ( recordEntry ( ',' recordEntry )* ','? )? '}'
recordEntry  = name ':' expression
             | name                                             a pun: `{ width, height: 4 }`; the entry is `width: width`
             | '...' expression
recordUpdate = '{' path '|' updateEntry ( ',' updateEntry )* ','? '}'
path         = name ( '.' member )*
updateEntry  = path ( ':' | '+=' | '-=' | '*=' | '/=' | '%=' ) expression
block        = '{' ( binding | setRule | staticAssert | expression )* '}'     an expression, if present, is last
ifExpr       = 'if' condition block 'else' ( ifExpr | block )   `else` is required
whenExpr     = 'when' condition block                           the value is None when the condition is false
condition    = expression                                       see the `is` rule below
matchExpr    = 'match' condition '{' arm ( ','? arm )* ','? '}'                 at least one arm
arm          = pattern ( 'if' expression )? '=>' expression     the guard comes after the pattern
forExpr      = 'for' forHead ( 'fold' name '=' expression )? '=>' expression
forHead      = ( name | '_' ) ( ',' pattern )? 'in' expression ( 'if' expression )?
```

Entries of a record update that name a nested path, `player.hp -= 1`, change that field in place.
`x += v` stands for `x: <the record>.x + v`, and likewise for the other operators.

A comprehension is a `forExpr` without `fold` that builds a Seq: `[ x + 1 for _, x in s if x > 1 ]`.

### Loop bindings

The bindings of `for` follow Go:

* `for i in xs`: the single name is the **index**,
* `for i, x in xs`: the index and the element,
* `for _, x in xs`: only the element, `for i, _ in xs`: only the index.

The element may be a pattern: `for _, { x, y } in points`. Three names are an error.

### Patterns

```text
pattern      = '_' | name | 'None' | variantPattern
             | recordPattern | rangePattern | literal
variantPattern = Name ( '(' pattern ')' )?
recordPattern  = Name? '{' ( fieldPattern ( ',' fieldPattern )* ','? )? '}'
fieldPattern   = name ( ':' pattern )?                           without the `:`, the field binds its own name
               | '...' name?                                    the rest of the fields; last
literal        = '-'? ( number | unit | color ) | string | 'true' | 'false'
rangePattern   = rangeEnd ( '..' | '..=' ) rangeEnd?
               | ( '..' | '..=' ) rangeEnd
rangeEnd       = '-'? ( number | unit | color )
```

`match` arms, `is` and the element of a `for` take any pattern; a destructuring `let` takes a record pattern,
whose fields nest any pattern. Strings with an interpolation aren't patterns. `Name {` starts a record pattern, except in the condition of an `if`, a
`when` or a `match`, where that `{` is the block, so `if x is Point { 1 } else { 2 }` tests `x is Point`;
parenthesize to use a record pattern there: `if (x is Point { x, y }) { ... }`.

## Strings

A string with no `{` is a plain string. Otherwise it is a template: texts and expressions in alternation,
`"x is {pos.x}"` is `"x is "`, `pos.x`, `""`. Each interpolation holds exactly one expression, which can
contain strings (and so more interpolations). `{}` and `{ a b }` are errors, and so is `{a:.2}`: format
specifiers are reserved. Tokens inside an interpolation keep their offsets in the file, so diagnostics
point into the braces.

## UI elements

```text
uiElement    = directive* '#' name ( '(' uiArgs? ')' )? ( '{' uiChild* '}' )?
uiArgs       = uiArg ( ',' uiArg )* ','?
uiArg        = name ':' expression
             | name                                             when a comma follows: a pun, `#button(width,)`
             | '...' expression                                 the entries of a record; a later entry wins
             | expression                                       positional
uiChild      = uiElement | binding | setRule
             | string                                           a text node, `#text("...")`
             | unit                                             a spacer: `1fr`, `10px`, `50%`
             | whenExpr | ifExpr | forExpr | matchExpr
             | expression starting with a name                  a call such as `helper(1, 2)`
```

Positional arguments come last: `#text(fill: red, "hi")`. A name followed by a comma is a pun (`#button(width,)`
is `#button(width: width)`); a name that is the last argument, with no comma after it, is positional, as in
`#text(label)`. Adjacent strings stay separate text nodes.

## Context rules

These are decided by the parser from the tokens around them.

| rule                                                                                                                                              | where                                                         |
| ------------------------------------------------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------- |
| `x-1` is a subtraction, so it needs spaces: the diagnostic says "did you mean `x - 1`?"                                                           | the lexer ends a name before a hyphen followed only by digits |
| a `[` or a `..` at the start of a line begins something new                                                                                       | postfix `[`, infix `..`                                       |
| `(...)` followed by `=>` is a lambda, except in a guard (`pattern if guard =>`, `for ... if guard =>`), where the `=>` belongs to the arm or loop | lambdas                                                       |
| in the condition of `if`, `when` and `match`, `Name {` ends the pattern                                                                           | `is`                                                          |
| `_` outside a pipe stage must be a whole argument of a call                                                                                       | placeholders                                                  |
| `\|>` and `<\|` can't be mixed without parentheses                                                                                                | pipes                                                         |
| positional arguments of an element come last                                                                                                      | `#element(...)`                                               |
| a record update lists at least one field                                                                                                          | `{ p \| ... }`                                                |
| only `let` bindings, `set` rules, `static-assert`s and one final expression may be in a block                                                     | blocks                                                        |

## What the compiler builds today

The parser accepts all of the above. `ExprBuilder` turns this much into Expr IR:

* puns (in records and element arguments; directive arguments are never puns), `when`, strings as text
  children, `|>` with `_`, `<|`;
* loops over a Seq with the index, the element or both (`for i in xs`, `for _, x in xs`, `for i, x in xs`),
  also as `[ ... for ... ]`, with a guard and with `fold`;
* `s[i]`, `a?.b`, `a?.[i]` and `a?.(args)`; an index out of range panics when the program runs, and a
  constant index that can't be in range is a compile error;
* `a ?? b`, which is `match a { Some(x) => x, _ => b }`;
* `add(_, 10)` as a function of its `_`s, and `f >> g` and `f << g` as functions of one argument, where each
  operand is a stage as after a `|>`: `add(10) >> mul(2)` is `(x) => mul(add(x, 10), 2)`;
* patterns: variants, `None`, literals, ranges (`0..10`, `0..=9`, `..5`, `7..`), records (`{ x, y: 0 }`, also
  with a type name, which is only checked to exist, and nested), and guards (`pattern if guard =>`); they work
  in `match` and after `is`;
* `let { a, b: c, p: { x } } = value`, with `_` to skip a field; the names must be fields of the value;
* record updates with every operator and nested paths; a field can be updated only once per update, because
  `x += 1` means `x: <the record>.x + 1`;
* `static-assert` with a condition made of literals and of earlier `let`s that are themselves computable;
* everything that was part of v1 before. `%` now builds a modulus; it used to be built as `||`.

It reports "valid v1 syntax but the Expr IR has no node for it yet" for the rest: ranges outside
patterns, slices, `...rest` and type names in a destructuring `let`, `...rest` in a record pattern, and spacer
children.

Strings with `{...}` are built as the pieces joined with `+`, each interpolated value turned into text as it is
written (a string stays as it is). `set` rules are built into the properties of the elements below them, as the
defaults they give: see the guide on UI elements.
