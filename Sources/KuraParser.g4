parser grammar KuraParser;

options { tokenVocab = KuraLexer; }

source
  : item* EOF
  ;

item
  : directive* ( typeDecl | fnDecl | externDecl | useDecl | letBinding | redefinition )
  ;

directive
  : DIRECTIVE ( LPAREN uiArgList? RPAREN )?
  ;

typeDecl
  : TYPE UPPER_IDENT typeParams? ASSIGN typeExpr
  ;

typeParams
  : LT UPPER_IDENT ( COMMA UPPER_IDENT )* GT
  ;

fnDecl
  : FN IDENT LPAREN paramList? RPAREN ( COLON typeExpr )? FAT_ARROW expression
  ;

useDecl
  : USE STRING ( AS qualifier )?
  ;

externDecl
  : EXTERN STRING? FN cName LPAREN externParams? RPAREN COLON typeExpr ( FAT_ARROW expression )?
  ;

externParams
  : paramList ( COMMA SPREAD )? COMMA?
  | SPREAD
  ;

cName
  : IDENT
  | UPPER_IDENT
  | RAW_IDENT
  ;

memberName
  : cName
  | LET | FN | TYPE | IF | ELSE | MATCH | FOR | IN | FOLD
  | TRUE | FALSE | NONE | USE | AS | EXTERN | COLOR_LIT
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

redefinition
  : IDENT ASSIGN expression
  ;

statement
  : directive* ( letBinding | redefinition )
  ;

typeExpr
  : typeMember ( BAR typeMember )*
  ;

typeMember
  : UPPER_IDENT LPAREN typeExpr RPAREN        # VariantType   // Click(Position)
  | UPPER_IDENT typeArgs?                     # NamedType     // Tick, Number, Seq<Px>
  | qualifier DOT cName typeArgs?             # QualifiedType // rl.Vector2, math.uint8_t
  | NONE                                      # NoneType
  | recordType                                # RecordTypeMember
  | functionType                              # FunctionTypeMember
  ;

typeArgs
  : LT typeExpr ( COMMA typeExpr )* GT
  ;

qualifier
  : IDENT
  | UPPER_IDENT
  ;

recordType
  : LBRACE ( recordField ( COMMA recordField )* COMMA? )? RBRACE
  ;

recordField
  : IDENT QMARK? COLON typeExpr
  ;

functionType
  : LPAREN ( typeExpr ( COMMA typeExpr )* COMMA? )? RPAREN FAT_ARROW typeExpr
  ;

expression
  : operation ( PIPE pipeTarget )*
  ;

pipeTarget
  : pipeCallee LPAREN argumentList? RPAREN
  ;

pipeCallee
  : IDENT ( DOT memberName )*
  | UPPER_IDENT ( DOT memberName )*
  | LPAREN expression RPAREN
  ;

operation
  : operation DOT memberName                              # PropertyAccess
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

recordLiteral
  : LBRACE RBRACE
  | LBRACE recordEntry ( COMMA recordEntry )* COMMA? RBRACE
  ;

recordEntry
  : IDENT COLON expression              # PropertyEntry
  | SPREAD expression                   # SpreadEntry
  ;

blockExpr
  : LBRACE statement+ expression? RBRACE
  | LBRACE expression RBRACE
  ;

block
  : LBRACE statement* expression? RBRACE
  ;

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

forExpr
  : FOR IDENT IN expression
    ( IF expression )?
    ( FOLD IDENT ASSIGN expression )?
    FAT_ARROW expression
  ;

lambda
  : LPAREN ( IDENT ( COMMA IDENT )* COMMA? )? RPAREN FAT_ARROW expression
  ;

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
