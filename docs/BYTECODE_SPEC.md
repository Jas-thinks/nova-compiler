# NOVA VM Bytecode Specification

This is **NOVA VM bytecode** for a stack machine implemented in C++ (`compiler/src/vm/vm.cpp`). It is *not* native machine code and LLVM is not used.

## Values
Tagged values: `int` (int64), `float` (double), `bool`. The static type checker guarantees operand types, so the VM performs no coercions.

## Machine state
* **Code:** one flat array of instructions. Address 0 is `CALL main, 0`, address 1 is `HALT`; function bodies follow.
* **Call stack:** frames `{function, return address, call line, locals[], operand stack[]}`. Each frame has its own operand stack.
* **Locals:** numbered slots per function; slots `0..n-1` are the parameters, then variables and compiler temporaries. Slots exist only for names that remain after optimization.
* There is no heap and no global memory.

## Instruction set
| Opcode | Operands | Effect |
|---|---|---|
| `PUSH k` | constant | push `k` |
| `POP` | | discard top |
| `LOAD s` | slot | push `locals[s]` |
| `STORE s` | slot | `locals[s] = pop()` |
| `ADD SUB MUL DIV MOD` | | pop b, pop a, push `a op b` (`DIV`/`MOD` on ints trap on zero) |
| `LT LE GT GE EQ NE` | | pop b, pop a, push bool |
| `NEG NOT` | | unary minus / logical not |
| `I2F F2I` | | `to_float` / `to_int` |
| `JMP t` | address | `ip = t` |
| `JZ t` / `JNZ t` | address | pop bool; jump if false / true |
| `CALL f, n` | function, argc | pop `n` args into a new frame's slots `0..n-1`, push frame, jump to entry |
| `RET` | | pop value, destroy frame, push value on caller's stack, resume after the call |
| `RETV` | | destroy frame, resume (void function) |
| `PRINT` | | pop and append to output |
| `HALT` | | stop |

(The `novac isa` command and `GET /api/isa` list these straight from the compiler.)

## Code generation
Per IR instruction: operands are pushed (`PUSH` for constants, `LOAD` for names), the operator executes, and a result is `STORE`d to its destination slot. Jumps are patched after all labels of the function are placed. A call whose result is unused is followed by `POP` if the callee returns a value. Each bytecode instruction records the source line/column of its IR instruction (the source map).

Example `print(10 + 20 * 2);` unoptimized:
```
PUSH 20 ; PUSH 2 ; MUL ; STORE t1 ; PUSH 10 ; LOAD t1 ; ADD ; STORE t2 ; LOAD t2 ; PRINT
```
optimized (constants folded): `PUSH 50 ; PRINT`.

## Runtime errors
`division by zero`, `modulo by zero`, `stack overflow: call depth exceeded 512`, `execution step limit exceeded (N instructions)`, `output limit exceeded`, plus internal-consistency errors (`operand stack underflow`, `instruction pointer out of range`) that indicate a compiler bug. Errors report the failing instruction's source line.

## Execution results and snapshots
`runVM` returns output lines, step count, per-opcode counts, max operand-stack and call depth, and (when tracing) a `Snapshot` **before every instruction**: `ip`, every frame's locals and operand stack, output length, and what the previous instruction did. The debugger consumes these.
