---
kind: format
---
# Lua 4.0 bytecode and its decompiler

Verified against: `SLUS_212.15` (NTSC-U), the 467 compiled scripts of the NTSC-U disc and Lua 4.0.1's source.

## Summary

The game's levels, missions, menus and configuration are Lua 4.0 scripts, shipped compiled ([Compiled Lua
chunks](lua-chunks.md) is the container, [Scripting](../scripting.md) how the game runs them). Coney runs that original
bytecode in its own virtual machine (a maintainer decision), so nothing here is needed to play the game. What this page
serves is **research**: reading a level's script is the fastest way to learn when its enemies spawn, which objectives
show and when a checkpoint is set, and the bytecode is unreadable by eye. Coney's decompiler turns it back into Lua
that compiles to the same code; this page documents how, so that anyone can check the decompiler against Lua's own
source and trust (or fix) what it prints.

It covers the instruction set as the game's chunks use it, with each opcode's effect on the stack and how often it
occurs; the fixed pattern each statement and expression compiles to; how the decompiler recognises each pattern; what
can never come back and why; and the round trip's results. How to run the command is in
[coney-tools](../../guides/coney-tools.md#lua), and how research uses its output in
[Research workflow](../../guides/research-workflow.md#scripts). The decompiled scripts themselves are game data and
never enter the repository ([LEGAL.md](repo:LEGAL.md#no-game-data)); this page shows Lua's patterns, never a line of
a game script.

How each kind of claim is graded: what Lua's compiler does is read in Lua 4.0.1's own source (MIT licence; `lopcodes.h`,
`lcode.c`, `lparser.c`, `llimits.h`, and `luac.c` and `opt.c` of its `luac`), so for the game's chunks it is inferred
from that source. The round trip, which compiles every decompiled script again and gets the game's instructions and
constants back, corroborates it; like every count from the disc it is a **disc check** ([How this was
checked](#how-this-was-checked)), not a level. An address of the game's own loader or virtual machine is confirmed
(code), cited from the page that owns it. There are no runtime claims.

## Map

### The game's side

The game links stock Lua 4.0.1's loader and virtual machine, compiled as C++. Inferred from the source path
`lua-4.0.1/src/lmem.cpp` that the function at `0x0032c758` (`Lua_Realloc`) cites, Lua's own messages in the same
block of code ([Source map](../source-map.md#middleware)), and from the interpreter loop at `0x00334478`, which
dispatches 49 cases, one per opcode of 4.0's `lopcodes.h`. The functions that matter here (names are ours, after
Lua's), all confirmed (code) on the pages that own them:

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00333548` | `Lua4_LoadChunk` | reads a chunk's header, then its main function ([Compiled Lua chunks](lua-chunks.md)) | confirmed (code) |
| `0x00333168` | `Lua4_LoadFunctionPrototype` | one function: its debug fields, constants, nested functions and code | confirmed (code) |
| `0x00334478` | `luaV_execute` | the interpreter loop; `SETLIST` multiplies its A operand by `0x3e` (62) ([Scripting](../scripting.md#notes-for-implementers)) | confirmed (code) |

### How the chunks were made

Every chunk on the disc is the output of a stock Lua 4.0 `luac` run with `-s`, which strips the debug information.
Nothing in a chunk tells 4.0 from 4.0.1 (both write version byte `0x40`), so the chunks match either; the round trip
used 4.0.1, the version of the runtime the game links ([above](#the-games-side)). Inferred from four disc checks that
each point the same way:

- the header is stock Lua 4.0's for a 32-bit little-endian build: 6 bits of opcode, 9 of the B field, 8-byte numbers
  ([Compiled Lua chunks](lua-chunks.md#header-13-bytes-then-a-test-number));
- all 11,448 functions have the source name `=(none)`, which is the name `luac -s` gives every function it strips
  (`strip()` in `luac.c`);
- none of them has a local-variable record or a line-table entry: **every chunk is fully stripped**. Only
  `lineDefined` survives, as stripping keeps it: 10,981 functions (every one but the 467 main chunks) carry the line
  their `function` keyword was on;
- the round trip: compiling Coney's decompile of each of the 467 chunks with stock `luac -s` gives the same
  instructions, constants, parameter counts, vararg flags, stack sizes and nested functions as the disc's chunk, for
  every function ([below](#round-trip)).

So the code is exactly what Lua 4.0's code generator writes for some source, and every pattern below applies as
Lua's source states it (inferred from the same checks). The game's own build of Lua changes nothing a chunk shows.

### The decompiler

The decompiler is `python/src/lua4/`, a package that imports nothing from Coney and knows nothing of this game, so it
can move to a repository of its own. Its parts, in the order a chunk goes through them:

| Module | Role |
| --- | --- |
| `chunk.py` | Loads a chunk into prototypes; honours the header's sizes, so a chunk from a 64-bit `luac` loads too |
| `opcodes.py` | The opcode numbering, operand decoding and each instruction's stack effect as the code generator counts it |
| `analysis.py` | Per function: the stack depth at every pc, the active locals (when debug information exists), jump threading, value expressions and statement boundaries |
| `conditions.py` | Rebuilds the `and`/`or` tree of a condition from its jumps |
| `expressions.py` | Symbolic execution of expression code: a stack of expression trees |
| `decompiler.py` | The statement reader: blocks, loops, `if`, `break`, locals |
| `ast.py` | The syntax tree the readers build and the renderer writes; `failures` lists the functions left unread |
| `render.py` | Writes Lua: operator priorities, literals, `function a.b()` sugar, functions placed on their `lineDefined` |
| `compare.py`, `luac.py` | The round trip: compile with `luac`, compare function by function without quoting a constant |

Coney's side is `python/src/coney_tools/lua_decompile.py` (the disc, the shared folder, the guard's marks, the round
trip), `lua_index.py` (the per-script index) and `.claude/hooks/lua_guard.py` (the guard).

## Layout

### Instruction encoding

An instruction is 32 bits, little-endian. The low 6 bits are the opcode; the remaining 26 bits are either one argument
or two. `U` is unsigned (0 to 2^26 - 1); `S` is signed, stored with an excess of `MAXARG_S` (2^25 - 1), and every jump
is an `S` that counts from the next instruction; `A` is the high 17 bits and `B` the 9 bits above the opcode. Inferred
from Lua 4.0.1's `lopcodes.h`. Disc check: every chunk's header gives these field sizes (6-bit opcode, 9-bit `B`), and
every instruction of the 467 chunks decodes to one of the 49 opcodes.

### The instruction set

Inferred from Lua 4.0.1's `lopcodes.h` (the opcode numbers and the "What it does" column) and `lcode.c` (the stack
effects); the round trip corroborates every row the disc uses (disc check).

Lua 4.0 is a stack machine: an instruction pops its operands from the top of the function's stack and pushes its
result. The stack effect is the change the code generator counts (`lcode.c`), which the decompiler replays to know
the depth at every instruction. "On the disc" is the number of times the instruction occurs in all 467 chunks (disc
check).

| # | Opcode | Operands | Stack | What it does | On the disc |
| --- | --- | --- | --- | --- | --- |
| 0 | `END` | | 0 | ends every function (the loader refuses code without it) | 11,448 |
| 1 | `RETURN` | U | to U | returns the values above slot U (the active locals) | 1,986 |
| 2 | `CALL` | A B | to A + B | calls the function at A with the values above it; keeps B results (255: all) | 99,967 |
| 3 | `TAILCALL` | A B | to B | a call whose results are returned at once | 14 |
| 4 | `PUSHNIL` | U | +U | pushes U nils | 14,669 |
| 5 | `POP` | U | -U | drops U values (locals at a block's end, `break`) | 660 |
| 6 | `PUSHINT` | S | +1 | an integer that fits the argument | 332,946 |
| 7 | `PUSHSTRING` | U | +1 | string constant U | 116,605 |
| 8 | `PUSHNUM` | U | +1 | number constant U | 100,925 |
| 9 | `PUSHNEGNUM` | U | +1 | minus number constant U (a negative literal) | 40,796 |
| 10 | `PUSHUPVALUE` | U | +1 | upvalue U of the closure (`%name`) | 0 |
| 11 | `GETLOCAL` | U | +1 | local slot U | 34,460 |
| 12 | `GETGLOBAL` | U | +1 | the global named by string constant U | 295,681 |
| 13 | `GETTABLE` | | -1 | `t[k]` from the two values on top | 12,760 |
| 14 | `GETDOTTED` | U | 0 | `t.name`, the name string constant U | 81,981 |
| 15 | `GETINDEXED` | U | 0 | `t[local U]` | 9,900 |
| 16 | `PUSHSELF` | U | +1 | `t:name`: replaces t by `t.name` and pushes t again | 44 |
| 17 | `CREATETABLE` | U | +1 | a new table sized for U items | 70,285 |
| 18 | `SETLOCAL` | U | -1 | stores the top into local slot U | 2,296 |
| 19 | `SETGLOBAL` | U | -1 | stores the top into the global named by string U | 27,519 |
| 20 | `SETTABLE` | A B | -B | `t[k] = top`, t and k A places down; pops B | 57,849 |
| 21 | `SETLIST` | A B | -B | stores the top B values as list items from A × 62 + 1 | 62,099 |
| 22 | `SETMAP` | U | -2U | stores U key/value pairs into the table below them | 2,133 |
| 23 | `ADD` | | -1 | `a + b` | 1,095 |
| 24 | `ADDI` | S | 0 | `a + S`, also `a - k` for a small integer k | 1,630 |
| 25 | `SUB` | | -1 | `a - b` | 132 |
| 26 | `MULT` | | -1 | `a * b` | 663 |
| 27 | `DIV` | | -1 | `a / b` | 219 |
| 28 | `POW` | | -1 | `a ^ b` (through the `pow` tag method) | 24 |
| 29 | `CONCAT` | U | 1 - U | joins the top U values (`a .. b .. c` is one instruction) | 7,175 |
| 30 | `MINUS` | | 0 | `-a` | 26 |
| 31 | `NOT` | | 0 | `not a` | 410 |
| 32-37 | `JMPNE`, `JMPEQ`, `JMPLT`, `JMPLE`, `JMPGT`, `JMPGE` | S | -2 | compare the two values on top and jump when the test holds | 8,609, 2,277, 241, 875, 155, 358 |
| 38 | `JMPT` | S | -1 | pop; jump if not nil | 1,987 |
| 39 | `JMPF` | S | -1 | pop; jump if nil | 6,337 |
| 40 | `JMPONT` | S | -1 | jump keeping the value if not nil, else pop it (`a or b`) | 1 |
| 41 | `JMPONF` | S | -1 | jump keeping the value if nil, else pop it (`a and b`) | 2 |
| 42 | `JMP` | S | 0 | jump | 6,990 |
| 43 | `PUSHNILJMP` | | 0 | push nil and skip one instruction (a comparison used as a value) | 2 |
| 44 | `FORPREP` | S | 0 | starts a numeric `for`; jumps past the loop when it runs no times | 826 |
| 45 | `FORLOOP` | S | -3 | steps the counter and jumps back while it is in range | 826 |
| 46 | `LFORPREP` | S | +2 | starts `for k, v in t` | 2,504 |
| 47 | `LFORLOOP` | S | -3 | next key and value, jumps back while there is one | 2,504 |
| 48 | `CLOSURE` | A B | 1 - B | a closure of nested function A over the B values on top (its upvalues) | 10,981 |

Every opcode but `PUSHUPVALUE` occurs on the disc: no game script uses an upvalue, and no function is vararg (disc
check). `CALL`, `RETURN` and `TAILCALL` set the count rather than adding to it, as the generator resets it around them.

### Limits

Inferred from Lua 4.0.1's `llimits.h`, except the game's own multiplier, which is read at its address.

- **`SETLIST` flushes every 62 items, and that is stock Lua 4.0.1.** `llimits.h` sets `LFIELDS_PER_FLUSH` to 64 and
  then caps it at a quarter of `MAXSTACK` (250), so it is 62; `RFIELDS_PER_FLUSH` (pairs per `SETMAP`) is half that,
  31. The game's VM multiplies the A operand by 62 in its `SETLIST` case (`0x003348c4`-`0x003348d8` in `0x00334478`,
  confirmed (code)). That it does so because it is built from the same headers is inferred from the stock source path
  ([above](#the-games-side)). Disc check: the round trip with stock `luac` reproduces every one of the disc's 62,099
  `SETLIST`s.
- `MAXSTACK` is 250 values per function; `MULT_RET` (255) is a call's "all results".

### What a chunk keeps, and what stripping removes

| Part | In the game's chunks | Used for |
| --- | --- | --- |
| Instructions, number and string constants, nested functions | yes | everything |
| Parameter count, vararg flag, maximum stack size | yes | function heads; the stack size settles one reading ([Behaviour](#nil-tests)) |
| `lineDefined` | yes (10,981 non-zero) | each function statement is put back on its source line |
| Local-variable records (names and pc ranges) | **no** (0 in 11,448 functions) | names and scopes of locals |
| Line table | **no** (0 entries) | the line of every other statement |
| Source name | `=(none)` | nothing |

### What `luac` itself changes

Before writing a chunk, `luac` runs `luaU_optchunk` (`opt.c`): in each function it merges equal number constants and
equal string constants and renumbers the instructions that use them. A decompile compiled again goes through the same
pass, so the constant tables come back in the same order.

## Behaviour

### The code generator's patterns {#patterns}

Inferred from Lua 4.0.1's `lparser.c` and `lcode.c`; the round trip of all 467 chunks corroborates every pattern the
disc uses (disc check).

Lua 4.0.1 compiles in one pass, straight from the parser (`lparser.c` calls `lcode.c`), and keeps a count of the stack
as it goes. Two facts make the patterns fixed and readable:

- **At the start of every statement the stack holds exactly the active locals.** A statement pushes what it needs and
  leaves nothing behind, except `local`, whose values stay where they were pushed: they *are* the new locals.
- **Jumps are threaded.** A jump that would land on an unconditional `JMP` is pointed at that jump's target instead,
  so an `if` at the end of a loop body jumps straight back to the loop's head. Different sources can therefore give
  the same code.

The patterns, each with how the decompiler (`decompiler.py`, `expressions.py`) reads it back:

| Construct | What it compiles to | How it is recognised |
| --- | --- | --- |
| `local a, b = x, y` | the values pushed (missing ones as `PUSHNIL`, extra ones popped); no instruction of its own | values still on the stack when a statement ends, or left under the next statement's operands (also when that statement is an assignment that drops surplus values, unless one of them is a call: `a, b = 1, 2, h()` is reported unread, and no disc chunk has one), or a slot a `GETLOCAL`, `SETLOCAL` or `GETINDEXED` names |
| `x = e` (global, local) | `e`, then `SETGLOBAL` or `SETLOCAL` | the store ends the statement |
| `t.k = e`, `t[k] = e` | t, k, e, then `SETTABLE 3 3` | `SETTABLE` with its table and key under the value |
| `a, t.k = x, y` | every target's table and key, then the values, then stores from the last target to the first; `SETTABLE`'s A counts the values above its table | the run of stores after the values, matched from the end |
| `f(...)` as a statement | f, the arguments, `CALL f 0` | a `CALL` that keeps no results at statement depth |
| `t:m(...)` | t, `PUSHSELF m`, the arguments, `CALL` | `PUSHSELF` under a call |
| `return ...` | the values, `RETURN` (or `TAILCALL` for one call) | the event itself |
| `if c then ... end` | the condition's jumps (true exits fall through, false exits jump past the block) | the first conditional jump at statement level that goes forward |
| `elseif`, `else` | a `JMP` to the end after each block but the last, then the next clause | a `JMP` at a block's end whose target is past the false exit |
| `while c do ... end` | the condition at the top, the body, a `JMP` back to the top | the target of a backward `JMP` is a loop head |
| `repeat ... until c` | the body, then the condition, whose false exits jump back to the top | the target of a backward conditional jump |
| `for i = a, b, c do` | a, b, c (`PUSHINT 1` when no step), `FORPREP` to after the loop, the body, `FORLOOP` back | `FORPREP` and its `FORLOOP` |
| `for k, v in t do` | t, `LFORPREP`, the body, `LFORLOOP` back | `LFORPREP` and its `LFORLOOP` |
| `break` | `POP` of the locals opened inside the loop, a `JMP` to the loop's exit and, after a `POP`, a dead `PUSHNIL` that restores the generator's count | a `JMP` to an exit of the innermost loop; also a `POP` merged into the `SETTABLE` just before it |
| `do ... end` | nothing but a `POP` of its locals at the end | a `POP` that ends locals before the enclosing block ends |
| `function a.b(...)` | `GETGLOBAL a`, `PUSHSTRING b`, `CLOSURE`, `SETTABLE` (or `SETGLOBAL` for a plain name) | a store of a `CLOSURE`; rendered with the sugar, `:` when the first parameter is `self` |

Expressions are read by symbolic execution: each push becomes a tree on a model stack and each operator combines the
trees on top. Conditions are the exception: `and`, `or`, `not` and the comparisons compile to chains of conditional
jumps, and `conditions.py` rebuilds the tree from where each jump goes (`a and b` falls through to `b`; `a or b` jumps
past it when `a` holds). Used as a value (`x = a and b`), the same operators compile to `JMPONT`/`JMPONF`, or for a
comparison to `PUSHNILJMP` and a `PUSHINT 1`, regions the reader treats as one value.

When two readings fit, the reader tries them in a fixed order and keeps the first that reads the whole block; any
reading that compiles back to the same instructions is correct, and the order picks the more natural source. Locals
get names that cannot clash: parameters `p1`, `p2`, ...; locals `l1`, `l2`, ...; loop variables `i`, `k`, `v`; never
a string the chunk uses (a global could have that name) nor a name an enclosing function still has in scope. With
debug information (the test fixtures) the real names and lines are used instead.

### A test against nil {#nil-tests}

`if x ~= nil then` and `if x then` compile to the same `JMPF`, and `if x == nil` and `if not x` to the same `JMPT`,
except that the comparison briefly pushes the `nil`, which only the function's maximum stack size records. When a
function's maximum is one more than anything else in it reaches, the reader writes the first test at that depth as a
comparison with nil. One chunk on the disc needs this (disc check).

### What cannot come back

- **Comments and layout.** `luac` drops them; the decompile is indented from its nesting.
- **Lines.** Stripped chunks keep only `lineDefined`, so each function statement starts on its original line (blank
  lines are added before it), and every other statement goes on the line after the one before it.
- **Local names.** Stripped, so they are made up as above.
- **Spellings the generator merges.** `t.k` and `t["k"]`; `x - 1` and `x + -1`; `local a; local b` and `local a, b`
  with no values; an `if` and the equivalent `if not ... else`; redundant parentheses. The decompile picks one; it
  compiles to the same code.

### Round-trip results {#round-trip}

`coney-tools lua decompile --all --roundtrip` on the NTSC-U disc (disc check, counts only):

- **467 of 467 chunks** decompile with every one of their 11,448 functions read (0 left as a "could not decompile"
  comment), and **all 467 compile back** with stock `luac -s` to the same instructions, constants, parameters, vararg
  flags, maximum stack sizes and nested functions, function by function.
- What the decompiles hold, all of which therefore round-trips: 87,508 assignments, 72,219 call statements, 3,541
  `local` statements (3,555 names), 12,937 `if`s (3,703 `elseif` and 3,212 `else` clauses), 60 `while`, 14 `repeat`,
  826 numeric and 2,504 table `for` loops, 15 `break`s, 2,000 `return`s, 10,981 nested functions, 70,285 table
  constructors, 7,175 concatenations, 44 method calls, 3,602 `and`, 524 `or` and 2,384 `not`.
- The synthetic fixtures (`python/tests/lua4/fixtures/`: expressions, control flow, closures with upvalues and varargs,
  logic, assignments; our own source) round-trip with and without debug information, and together use all 49
  opcodes, `PUSHUPVALUE` included.

## In Coney

Coney runs the game's bytecode in its own virtual machine (`src/scripting/lua_vm.cpp`, `lua_chunk.cpp`); the decompiler
plays no part in the game. It is a research tool: `coney-tools lua decompile` writes into a folder outside the
repository, and a guard refuses its output in the repository ([coney-tools](../../guides/coney-tools.md#lua)).

## Open questions

### Which scripts the unnamed chunks are {#q-unnamed-chunks}

111 of the 467 chunks have no recovered file name, so their decompiles are named by the entry's hash
([Compiled Lua chunks](lua-chunks.md#open-questions)). What depends on it: research finds a level's script by name
in the index, so an unnamed script is found only through the natives and handlers the index lists for it. How to
find out: `wad names` keeps only names it finds as strings ([coney-tools](../../guides/coney-tools.md#names)); the
rest could come from names a script builds at run time (a `doFile` of a concatenated name, read in the decompiles
that call it) hashed the same way ([Name hashing](../name-hash.md)).

### Whether the game links Lua's parser {#q-parser}

The parser is linked; whether the game ever uses it is open. Confirmed (code): the function at `0x0032e6d0` (unnamed
in Ghidra) checks a limit with the message "variables in a multiple assignment" (`0x00575dd0`), on a `,` token reads
the next target and checks it with "syntax error" (`0x00575df8`), calls itself, and otherwise expects `=` and reads
the values; for an indexed target it emits opcode 20 (`SETTABLE`). Inferred from that shape and those messages: it
is `restassign` of `lparser.c`. Its one caller besides itself, `0x0032f048`, is called by `0x0032f1e8`, which
`0x0032f538` reaches; `0x0032d828` calls `0x0032f538` after setting up a lexer and then checks `<eof> expected`
(`0x00575d08`), the shape of `luaY_parser`. The one caller of `0x0032d828`, `0x00328d98`, calls it when its input is
text and `0x00333588` otherwise, as `ldo.c`'s `f_parser` chooses between the parser and the binary loader, and
`0x00328d98` is passed as a function to `0x00328df0` (the calls confirmed (code); the `lparser.c` and `ldo.c` names
inferred from the same shapes). So, inferred from `ldo.c`, the parser runs for any buffer the game hands to Lua that
does not start with the chunk signature. What depends on it:
whether Coney's VM must also compile source text (a script or console that passes Lua source) or only load compiled
chunks, which is all the game is known to do. How to find out: in Ghidra, follow the callers of `0x00328df0` out of
the Lua block (`lua_dostring`, `lua_dobuffer`, `lua_dofile`) and see whether any game code passes them text rather
than a chunk from the WAD.

## How this was checked

The disc checks are the output of `coney-tools lua decompile --all --roundtrip` on the NTSC-U disc (counts only:
chunks, functions, unread functions and the round trip's result), with a stock Lua 4.0.1 `luac` built from the
authors' pinned source by `coney-tools workspace init --fetch lua` ([coney-tools](../../guides/coney-tools.md#lua)).
No command prints the opcode and construct counts; they come from a short script, not in the repository, over the
same chunks (the `scripts/` folder of `coney-tools extract`). To repeat them: load each chunk with `lua4.load`, walk
its functions with `Proto.walk()`, decode every instruction with `lua4.opcodes.decode` and count by opcode; for the
constructs, decompile each main function with `lua4.decompile_function` and count the nodes of the tree it returns
(`lua4.ast`) by type. Print counts only, never a name or a string.
