# NOVA Language Specification

NOVA is a small, statically typed, imperative language. The formal syntax is in [GRAMMAR.md](GRAMMAR.md).

## Program structure
A program is one or more function declarations. Execution begins at `fn main()`, which must take no parameters and return no value. Functions may call each other regardless of declaration order, and may be recursive.

## Types
| Type | Representation | Notes |
|---|---|---|
| `int` | signed 64-bit | `+ - *` and unary `-` wrap (two's complement); `/` truncates toward zero; `INT_MIN / -1` wraps; `% -1` is 0 |
| `float` | IEEE-754 double | `/` by zero yields `inf`/`nan` (no trap) |
| `bool` | `true` / `false` | |

There are **no implicit conversions**. `to_float(int) -> float` and `to_int(float) -> int` (truncates toward zero; NaN → 0; out-of-range saturates) are the only conversions.

## Declarations and scope
`let x: T = e;` declares a variable and requires an initializer (so there are no uninitialized reads). The initializer is checked *before* `x` enters scope. Each `{ }` block opens a scope; function parameters and the function body's top-level declarations share one scope. An inner scope may **shadow** an outer name, but redeclaring a name in the same scope is an error. Variable lifetime ends with its block.

## Statements
`let`, assignment `x = e;`, `if (c) {…} else {…}` / `else if`, `while (c) {…}`, `return [e];`, `print(e);`, a call statement `f(args);`, and nested blocks. Braces are mandatory. `print` writes `int`, `float` (shortest `%.15g` form, `.0` appended to integral values) or `bool` (`true`/`false`) on its own line.

## Expressions
Precedence, highest first (all binary operators are left-associative):

1. unary `-` `!`
2. `*` `/` `%`
3. `+` `-`
4. `<` `<=` `>` `>=`
5. `==` `!=`
6. `&&` (short-circuit)
7. `||` (short-circuit)

Typing rules: arithmetic needs two operands of the same numeric type; `%` needs `int`; ordering comparisons need two numbers of the same type; `==`/`!=` need two operands of the same type; `&&`, `||`, `!` need `bool`; `if`/`while` conditions must be `bool`.

## Functions
`fn name(a: int, b: float) -> int { … }`. Omit `-> T` for a function returning nothing. A call to a void function is a statement only; using it as a value is an error. Every control-flow path of a non-void function must end in `return` (a `while` is assumed to be possibly skipped; an `if` returns only if both branches do).

## Runtime behaviour and limits
* Integer division/modulo by zero is a **runtime error** (`division by zero`).
* Call depth is limited (512 frames) → `stack overflow`; the VM has an instruction limit (default 2,000,000) → `execution step limit exceeded`; output is limited to 5,000 lines.
* There is no heap, no strings and no arrays.

## Diagnostics
Errors carry phase, category, code, message, explanation, suggestion and an exact source range. Warnings (do not stop compilation): unused variable `W-SEM-001`, unreachable statement `W-SEM-002`. See the `E-LEX-*`, `E-SYN-*`, `E-SEM-*` codes in the source (`lexer.cpp`, `parser.cpp`, `semantic.cpp`).
