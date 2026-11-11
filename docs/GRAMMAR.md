# NOVA Grammar

The grammar below is the *exact text* loaded by the LR(1) generator (`compiler/src/parser/grammar.cpp`). `eps` denotes the empty string; uppercase `ID`, `INT_LIT`, `FLOAT_LIT` are token classes produced by the lexer; every other terminal is a literal lexeme.

```
Program     -> FuncList
FuncList    -> FuncList Func | Func
Func        -> fn ID ( ParamsOpt ) RetOpt Block
ParamsOpt   -> Params | eps
Params      -> Params , Param | Param
Param       -> ID : Type
RetOpt      -> -> Type | eps
Type        -> int | float | bool
Block       -> { StmtList }
StmtList    -> StmtList Stmt | eps
Stmt        -> VarDecl | Assign | IfStmt | WhileStmt | ReturnStmt | PrintStmt | CallStmt | Block
VarDecl     -> let ID : Type = Expr ;
Assign      -> ID = Expr ;
IfStmt      -> if ( Expr ) Block ElsePart
ElsePart    -> else Block | else IfStmt | eps
WhileStmt   -> while ( Expr ) Block
ReturnStmt  -> return ExprOpt ;
ExprOpt     -> Expr | eps
PrintStmt   -> print ( Expr ) ;
CallStmt    -> Call ;
Expr        -> OrExpr
OrExpr      -> OrExpr || AndExpr | AndExpr
AndExpr     -> AndExpr && EqExpr | EqExpr
EqExpr      -> EqExpr == RelExpr | EqExpr != RelExpr | RelExpr
RelExpr     -> RelExpr < AddExpr | RelExpr <= AddExpr | RelExpr > AddExpr | RelExpr >= AddExpr | AddExpr
AddExpr     -> AddExpr + MulExpr | AddExpr - MulExpr | MulExpr
MulExpr     -> MulExpr * UnaryExpr | MulExpr / UnaryExpr | MulExpr % UnaryExpr | UnaryExpr
UnaryExpr   -> - UnaryExpr | ! UnaryExpr | Primary
Primary     -> INT_LIT | FLOAT_LIT | true | false | ID | Call | ( Expr )
Call        -> ID ( ArgsOpt )
ArgsOpt     -> Args | eps
Args        -> Args , Expr | Expr
```

## Properties

* **Size:** 72 productions (+1 augmented start), 39 terminals (incl. `$`), 33 nonterminals (incl. `S'`).
* **Unambiguous.** Precedence is encoded by stratifying expressions into levels (`Or < And < Eq < Rel < Add < Mul < Unary`); every binary level is left-recursive, giving left associativity. There is **no dangling else**: `if`/`while` bodies must be blocks.
* **LR(1), no conflicts.** The canonical LR(1) construction yields 241 states, 662 shift, 1209 reduce and 410 goto entries and reports zero conflicts (`GET /api/health` and the Syntax Analysis page show the live numbers; the unit test `parser.grammarIsLR1WithoutConflicts` enforces it).
* `to_float` and `to_int` are not keywords: they are ordinary identifiers resolved by the semantic analyzer as built-in functions.

## Lexical grammar

| Class | Pattern |
|---|---|
| identifier | `[A-Za-z_][A-Za-z0-9_]*` (unless a keyword) |
| keywords | `fn let if else while return print int float bool true false` |
| `INT_LIT` | `[0-9]+` (must fit in signed 64 bits) |
| `FLOAT_LIT` | `[0-9]+ . [0-9]+` |
| operators | `+ - * / % < <= > >= == != && \|\| ! = ->` |
| punctuation | `( ) { } , ; :` |
| comments | `// …` to end of line; `/* … */` (not nested) |

Lexical errors reported: invalid character, lone `&` / `|`, unterminated block comment, `3.` (digits required after the point), `12abc`, integer literal too large.
