/*
 * KuraParser.g4 — Kura v1 syntax.
 * Source of truth: wiki "Language Syntax" (v1).
 *
 * This grammar only decides *shape*. Everything the spec labels a compile
 * error that needs names/types (exhaustive match, unit mismatches, redefinition
 * keeping its type, recursion, top-level params annotated, entry-point
 * signatures, ...) belongs in later passes.
 */
parser grammar KuraParser;

options { tokenVocab = KuraLexer; }

// ---------------------------------------------------------------------------
// Program
// ---------------------------------------------------------------------------

source
  : item* EOF
  ;

item
  : directive* ( typeDecl | fnDecl | letBinding | redefinition )
  ;

// `@vec`, `@meta(tags: [ Button, User ])` — hints only (see wiki Language-Directives).
// Directives attach to bindings, functions and UI elements.
directive
  : DIRECTIVE ( LPAREN uiArgList? RPAREN )?
  ;

// ---------------------------------------------------------------------------
// Declarations
// ---------------------------------------------------------------------------

typeDecl
  : TYPE UPPER_IDENT typeParams? ASSIGN typeExpr
  ;

typeParams
  : LT UPPER_IDENT ( COMMA UPPER_IDENT )* GT
  ;

// Top-level function. Parentheses always required. Return type optional here;
// the "required for init/update/view" rule is a semantic check.
fnDecl
  : FN IDENT LPAREN paramList? RPAREN ( COLON typeExpr )? FAT_ARROW expression
  ;

paramList
  : param ( COMMA param )* COMMA?
  ;

param
  : IDENT COLON typeExpr
  ;

letBinding
  : LET IDENT ( COLON typeExpr )? ASSIGN expression
  ;

// `x = expression` — redefinition (NOT mutation). `x.name = ...` is deliberately
// not accepted: the left side is an identifier only.
redefinition
  : IDENT ASSIGN expression
  ;

statement
  : directive* ( letBinding | redefinition )
  ;

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

// A union is a `|`-separated list of members; a single member is just a type.
typeExpr
  : typeMember ( BAR typeMember )*
  ;

typeMember
  : UPPER_IDENT LPAREN typeExpr RPAREN        # VariantType   // Click(Position)
  | UPPER_IDENT typeArgs?                     # NamedType     // Tick, Number, Seq<Px>
  | NONE                                      # NoneType
  | recordType                                # RecordTypeMember
  | functionType                              # FunctionTypeMember
  ;

typeArgs
  : LT typeExpr ( COMMA typeExpr )* GT
  ;

// There is no bare `Record` type: properties are always listed.
recordType
  : LBRACE ( recordField ( COMMA recordField )* COMMA? )? RBRACE
  ;

// `z?: Number` is shorthand for `z: Optional<Number>`.
recordField
  : IDENT QMARK? COLON typeExpr
  ;

functionType
  : LPAREN ( typeExpr ( COMMA typeExpr )* COMMA? )? RPAREN FAT_ARROW typeExpr
  ;

// ---------------------------------------------------------------------------
// Expressions
// ---------------------------------------------------------------------------

// `|>` is the lowest-precedence operator. The right-hand side is restricted to
// a call so `x |> f(y)` is always `f(x, y)`; anything else is a syntax error.
expression
  : operation ( PIPE pipeTarget )*
  ;

pipeTarget
  : pipeCallee LPAREN argumentList? RPAREN
  ;

pipeCallee
  : IDENT ( DOT IDENT )*
  | UPPER_IDENT
  | LPAREN expression RPAREN
  ;

// Highest to lowest precedence, per the Operators table.
// Binary minus is its own token (BIN_MINUS) because it needs whitespace on
// both sides; unary minus is MINUS.
operation
  : operation DOT IDENT                                   # PropertyAccess
  | operation LPAREN argumentList? RPAREN                 # CallExpr
  | op = ( MINUS | BANG ) operation                       # UnaryExpr
  | operation op = ( STAR | SLASH ) operation             # MultiplicativeExpr
  | operation op = ( PLUS | BIN_MINUS ) operation         # AdditiveExpr
  | operation op = ( LT | LE | GT | GE ) operation        # ComparisonExpr
  | operation op = ( EQ | NEQ ) operation                 # EqualityExpr
  | operation AND operation                               # LogicalAndExpr
  | operation OR operation                                # LogicalOrExpr
  | primary                                               # PrimaryExpr
  ;

argumentList
  : expression ( COMMA expression )* COMMA?
  ;

primary
  : NUMBER                                                # NumberLit
  | STRING                                                # StringLit
  | TRUE                                                  # TrueLit
  | FALSE                                                 # FalseLit
  | NONE                                                  # NoneLit
  | unitLiteral                                           # UnitLit
  | IDENT                                                 # VarRef
  | UPPER_IDENT                                           # VariantRef   // Tick, or callee of Click(...)
  | seqLiteral                                            # SeqLit
  | recordLiteral                                         # RecordLit
  | blockExpr                                             # BlockLit
  | ifExpr                                                # IfLit
  | matchExpr                                             # MatchLit
  | forExpr                                               # ForLit
  | lambda                                                # LambdaLit
  | uiExpr                                                # UiLit
  | LPAREN expression RPAREN                                    # ParenExpr
  ;

unitLiteral
  : PX_LIT | PCT_LIT | FR_LIT | COLOR_LIT
  ;

seqLiteral
  : LBRACK ( expression ( COMMA expression )* COMMA? )? RBRACK
  ;

// ---------------------------------------------------------------------------
// Records vs blocks
// ---------------------------------------------------------------------------
// A `{` starting with `...` or `name :` is a record. Any other non-empty `{ }`
// is a block. Empty `{}` is a record in value position, and a block only where
// the grammar demands a block (if/else bodies, see `block`).

recordLiteral
  : LBRACE RBRACE
  | LBRACE recordEntry ( COMMA recordEntry )* COMMA? RBRACE
  ;

recordEntry
  : IDENT COLON expression              # PropertyEntry
  | SPREAD expression                   # SpreadEntry
  ;

// Non-empty block in value position. Value = last expression; with no final
// expression the value is None.
blockExpr
  : LBRACE statement+ expression? RBRACE
  | LBRACE expression RBRACE
  ;

// Block where the grammar expects a block — may be empty (`if ready { } else { }`).
block
  : LBRACE statement* expression? RBRACE
  ;

// ---------------------------------------------------------------------------
// Control flow
// ---------------------------------------------------------------------------

ifExpr
  : IF expression block ELSE ( ifExpr | block )
  ;

matchExpr
  : MATCH expression LBRACE matchArm ( COMMA matchArm )* COMMA? RBRACE
  ;

matchArm
  : pattern FAT_ARROW expression
  ;

pattern
  : UNDERSCORE                                  # WildcardPattern
  | IDENT                                       # BindingPattern
  | NONE                                        # NonePattern
  | UPPER_IDENT ( LPAREN pattern RPAREN )?      # VariantPattern
  | literalPattern                              # LiteralPatternAlt
  ;

literalPattern
  : MINUS? ( NUMBER | unitLiteral )
  | STRING
  | TRUE
  | FALSE
  ;

// for <name> in <seq> [if <guard>] [fold <acc> = <initial>] => <expression>
forExpr
  : FOR IDENT IN expression
    ( IF expression )?
    ( FOLD IDENT ASSIGN expression )?
    FAT_ARROW expression
  ;

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

// Anonymous function: (x, y) => x + y
lambda
  : LPAREN ( IDENT ( COMMA IDENT )* COMMA? )? RPAREN FAT_ARROW expression
  ;

// ---------------------------------------------------------------------------
// UI elements
// ---------------------------------------------------------------------------
// `#text("Hello")`, `#button(on-click: Close){ #text("Close") }`.
// The Views page is still empty, so this mirrors the shape the old grammar and
// wiki examples used: `#tag`, optional `( args )`, optional `{ children }`.
// Directives may precede an element: `@meta(tags: [ Player ]) #input`.

uiExpr
  : directive* HASH IDENT uiProps? uiChildren?
  ;

uiProps
  : LPAREN uiArgList? RPAREN
  ;

uiArgList
  : uiArg ( COMMA uiArg )* COMMA?
  ;

uiArg
  : IDENT COLON expression        # NamedUiArg
  | SPREAD expression             # SpreadUiArg
  | expression                    # PositionalUiArg
  ;

uiChildren
  : LBRACE uiChild* RBRACE
  ;

uiChild
  : uiExpr                        # ElementChild
  | statement                     # BindingChild
  ;
