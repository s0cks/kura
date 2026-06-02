parser grammar KuraParser;

options {
  tokenVocab=KuraLexer;
}

// ╭────────────╮
// │ Source     │
// ╰────────────╯
source
  : declaration* EOF
  ;

declaration
  : importDecl
  | typeDecl
  | funcDecl
  | macroDecl
  ;

// ╭────────────╮
// │ Imports    │
// ╰────────────╯
importDecl
  : IMPORT qualifiedName SEMICOLON?
  ;

qualifiedName
  : IDENTIFIER (DOT IDENTIFIER)*
  ;

// ╭────────────╮
// │ Types      │
// ╰────────────╯
typeDecl
  : TYPE IDENTIFIER ASSIGN typeVariantList SEMICOLON?
  ;

typeVariantList
  : typeVariant (SINGLE_PIPE typeVariant)*
  ;

typeVariant
  : IDENTIFIER
  ;

// ╭────────────╮
// │ Functions  │
// ╰────────────╯

funcDecl
  : FN
    IDENTIFIER
    parameterList
    DOUBLE_ARROW
    expression
    SEMICOLON?
  ;

parameterList
  : LPAREN (parameter | parameter (COMMA parameter)+)? RPAREN
  ;

parameter
  : IDENTIFIER
  ;

// ╭────────────╮
// │ Macros     │
// ╰────────────╯

macroDecl
  : MACRO
    IDENTIFIER
    parameterList?
    DOUBLE_ARROW
    expression
    SEMICOLON?
  ;

// ╭────────────╮
// │ Expressions│
// ╰────────────╯

expression
  : binaryOpExpr
  ;

binaryOpExpr
    : binaryOpExpr (STAR | SLASH | PERCENT) binaryOpExpr
    | binaryOpExpr (PLUS | MINUS) binaryOpExpr
    | binaryOpExpr (LT | LTE | GT | GTE) binaryOpExpr
    | binaryOpExpr (EQ | NEQ) binaryOpExpr
    | binaryOpExpr AND binaryOpExpr
    | binaryOpExpr OR binaryOpExpr
    | binaryOpExpr PIPE binaryOpExpr
    | <assoc=right> binaryOpExpr ASSIGN binaryOpExpr
    | unaryExpr
    ;

unaryExpr
  : (BANG | PLUS | MINUS) unaryExpr
  | postfixExpr
  ;

postfixExpr
  : primaryExpr postfixPart*
  ;

postfixPart
  : functionCall
  | propertyAccess
  | optionalPropertyAccess
  | indexAccess
  ;

functionCall
  : LPAREN argumentList? RPAREN
  ;

propertyAccess
  : DOT IDENTIFIER
  ;

optionalPropertyAccess
  : OPTIONALDOT IDENTIFIER
  ;

indexAccess
  : LBRACKET expression RBRACKET
  ;

// ╭────────────╮
// │ Primary    │
// ╰────────────╯
primaryExpr
  : literal
  | qualifiedName
  | listExpr
  | listComprehensionExpr
  | recordExpr
  | blockExpr
  | ifExpr
  | matchExpr
  | uiExpr
  | LPAREN expression RPAREN
  ;

// ╭────────────╮
// │ Collections│
// ╰────────────╯
listExpr
  : LBRACKET argumentList? RBRACKET
  ;

listComprehensionExpr
  : LBRACKET
    expression
    'for' IDENTIFIER 'in' expression listComprehensionClause
    RBRACKET
  ;

listComprehensionClause
  : 'if' expression
  ;

// TODO(@s0cks): add typed records
// type Person = { name: String, age: Int }
recordExpr
  : LBRACE recordFieldList? RBRACE
  ;

recordFieldList
  : recordField (COMMA recordField)* COMMA?
  ;

recordField
  : IDENTIFIER COLON expression
  | spreadExpr
  ;

argumentList
  : expression (COMMA expression)* COMMA?
  ;

spreadExpr
  : SPREAD expression
  ;

// ╭────────────╮
// │ Blocks     │
// ╰────────────╯
blockExpr
  : LBRACE statement* RBRACE
  ;

statement
  : expression SEMICOLON?
  ;

// ╭────────────╮
// │ UI DSL     │
// ╰────────────╯
uiExpr
  : HASH IDENTIFIER uiProps? uiChildren?
  ;

uiProps
  : LPAREN uiPropList? RPAREN
  ;

uiPropList
  : uiProp (COMMA uiProp)* COMMA?
  ;

uiProp
  : IDENTIFIER COLON expression
  | spreadExpr
  ;

uiChildren
  : blockExpr
  ;
//TODO(@s0cks): refactor

// ╭────────────╮
// │ If         │
// ╰────────────╯
ifExpr
  : IF expression blockExpr (ELSE blockExpr)?
  ;

// ╭────────────╮
// │ Match      │
// ╰────────────╯

matchExpr
  : MATCH expression
    LBRACE
    matchArm*
    RBRACE
  ;

matchArm
  : pattern DOUBLE_ARROW expression SEMICOLON?
  ;

// ╭────────────╮
// │ Patterns   │
// ╰────────────╯

pattern
  : wildcardPattern
  | literalPattern
  | identifierPattern
  | variantPattern
  | recordPattern
  | listPattern
  ;

wildcardPattern
  : UNDERSCORE
  ;

literalPattern
  : literal
  ;

identifierPattern
  : IDENTIFIER
  ;

variantPattern
  : IDENTIFIER LPAREN patternList? RPAREN
  ;

recordPattern
  : LBRACE patternFieldList? RBRACE
  ;

patternFieldList
  : patternField (COMMA patternField)* COMMA?
  ;

patternField
  : IDENTIFIER
  ;

listPattern
  : LBRACKET patternList? RBRACKET
  ;

patternList
  : pattern (COMMA pattern)*
  ;

// ╭────────────╮
// │ Literals   │
// ╰────────────╯

literal
  : NUMBER      # LiteralNumber
  | STRING      # LiteralString
  | TRUE        # LiteralTrue
  | FALSE       # LiteralFalse
  | NULL        # LiteralNull
  | NONE        # LiteralNone
  | MEASUREMENT # LiteralMeasurement
  ;
