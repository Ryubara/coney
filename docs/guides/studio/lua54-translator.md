# Coney Studio: Lua 5.4, the translator

A maintainer prefers Lua 5.4 everywhere: projects are written in 5.4 ([The runtime](lua-sdk.md#runtime-choice)), and
editing missions in 4.0 beside them is inconsistent. A translator would turn a game script's 4.0 base text into 5.4,
and the mission would be edited and run in 5.4. This page says what that takes, from the disc's own numbers, and gives
the verdict a maintainer decides from. It is part of [Coney Studio: mission mods](mission-mods.md), whose
[patches](mission-mods.md#patches) a translated script carries; the parent issue is [#1054](issue:1054).

**Status: design.** Nothing on this page is built. It is a design in the sense of
[Page kinds](../writing-docs.md#page-kinds).

## Contents {#contents}

1. [What the game's scripts use](#what-scripts-use).
2. [Syntax](#syntax), [Numbers](#numbers), [Booleans](#booleans) and [Iteration order](#order-risk).
3. [Where a translated mission runs](#where-it-runs) and [Translation is deterministic](#deterministic).
4. [Proof by trace equality](#proof), [the probe](#probe) and [the verdict](#verdict).

## What the game's scripts use {#what-scripts-use}

The disc makes the translator smaller than a general 4.0-to-5.4 translator would be (disc checks, counts only:
[How the numbers were found](mission-mods.md#how-checked)):

| Feature | On the disc | Meaning for the translator |
| --- | --- | --- |
| Upvalues (`%name`) | 0 uses | Nothing to translate; 4.0's copied upvalues never meet 5.4's shared ones ([Lua 4.0 bytecode](../../research/formats/lua-bytecode.md#the-instruction-set)) |
| Vararg functions (`...`, `arg`) | 0 | Nothing to translate |
| Lua library functions | 12 names: `strsub` 21 reads, `type` 13, `tinsert` 12, `strlen` 12, `max` 9, `min` 6, `getn` 5, `strfind` 4, `abs` 2, `tremove` 1, `mod` 1, `tonumber` 1 | A short table of mappings; no `format`, `tostring`, trigonometry, tag methods or `dostring` at all (`random`, read 343 times, is a game native) |
| `true` and `false` | read 8,669 and 6,006 times; set once each, in `global.lua`, to 1 and nil | 5.4 makes both reserved words with other values ([Booleans](#booleans)) |
| Table loops `for k, v in t do` | 2,504 | 5.4 spells them `for k, v in next, t do`; their order is the open risk ([Iteration order](#order-risk)) |
| Numeric `for` loops | 826, about 9 of which assign their own control variable | In 4.0 that changes the loop; in 5.4 it does not ([Syntax](#syntax)) |
| Concatenations | 7,175 | Numbers turned into text must read as 4.0 writes them ([Numbers](#numbers)) |
| Divisions | 219 | Same results in both: `/` is float division in each |
| 5.4 reserved words used as names | `goto` appears once, as a string constant | Written `t["goto"]` if it is a field name |

## Syntax {#syntax}

The translator reads the 4.0 base text with Coney's Lua 4.0 parser (the compiler's front end) and writes 5.4,
**line for line**: each 4.0 line becomes one 5.4 line, so a 4.0 patch's line numbers hold on the 5.4 base.

| Lua 4.0 | Lua 5.4 |
| --- | --- |
| `for k, v in t do` | `for k, v in next, t do` (raw traversal, as 4.0's; `pairs` would honour a `__pairs`) |
| `for i = a, b, c do` whose body assigns `i` | a `while` loop on the same line (4.0's loop steps the variable the body changed; 5.4's control variable is a copy) |
| `%name` upvalue, `...` and `arg` | not on the disc; translated to a local captured by the closure and to `...` with `local arg = table.pack(...)` for scripts users write |
| `strsub`, `strlen`, `strfind`, `type`, `max`, `min`, `abs`, `tonumber` | `string.sub`, `string.len`, `string.find`, `type`, `math.max`, `math.min`, `math.abs`, `tonumber` (the same results for the arguments the disc passes) |
| `getn`, `tinsert`, `tremove`, `mod` | `lua40.getn`, `lua40.tinsert`, `lua40.tremove`, `lua40.mod`: a small helper table of the game state that keeps 4.0's rules (as Lua 4.0.1's `lbaselib.c` defines the first three: `getn` reads a table's `n` field first; `tinsert` and `tremove` keep `n` up to date; and its `lmathlib.c` the last: `mod` is C's `fmod`, NaN for a zero divisor where 5.4's `math.fmod` raises an error on integers) |
| `true`, `false` (globals) | `1`, `nil` ([Booleans](#booleans)) |
| a field named by a 5.4 reserved word | `t["goto"]` |

## Numbers {#numbers}

Lua 4.0 has one number type, a double. Lua 5.4 has integers and floats. Where that shows:

| Operation | Lua 4.0 | Lua 5.4 | Risk and answer |
| --- | --- | --- | --- |
| Division `a / b` | double | always a float | none |
| `+ - *` on whole numbers | double, exact to 2^53 | integer, wraps at 2^63 | none for the values scripts hold (counts, handles, ids, milliseconds) |
| A number made text (`..`, `tostring`) | `%.16g`: `3`, `0.3333333333333333` | an integral float shows `3.0`; `%.14g`: `0.33333333333333` | **real**: a name built as `"vMark0" .. n` or a message with a number differs as soon as `n` is a float. Answer: the translator wraps each concatenation operand that is not a string literal in `lua40.str`, which writes a number as 4.0 does; the trace catches any miss, since strings passed to natives are compared byte for byte |
| Comparison of numbers | doubles | integers and floats compared by value | none |
| Table keys | `t[2]` and `t[2.0]` are one key | 5.4 turns a float key with an integral value into the integer key | none |
| `string.format("%d", 2.5)` | truncates | raises an error | not on the disc (no `format` call) |
| Numbers from natives | doubles | the game-state bridge hands a whole number as an integer, anything else as a float | none for arithmetic; text is covered by `lua40.str` |

## Booleans {#booleans}

Lua 4.0 has no booleans: `global.lua` sets the globals `true` to 1 and `false` to nil, scripts pass them to natives and
test them, and a comparison used as a value gives 1 or nil. In 5.4, `true` and `false` are reserved words with boolean
values, and `1 == true` is false. So the translator writes `1` and `nil` where the base reads `true` and `false`, drops
`global.lua`'s two assignments, and writes a comparison or `not` used as a value (rare on the disc: 2 comparison values,
3 `and`/`or` values) as `(... and 1 or nil)`. Natives in the 5.4 game state take and return 1 and nil as in 4.0, never
booleans. A translated mission therefore reads `HuSetSomething(h, 1)` where a project would write `true`: honest about
the game's own values, and the one visible difference from a project's 5.4.

## Iteration order {#order-risk}

The one difference that is not mechanical. A loop over a table visits its keys in an order the language does not
specify, and the order can decide which native is called first (which human spawns first, and so which handle it
gets):

- the **original** game uses Lua 4.0's hash order;
- **Coney's 4.0 VM** visits keys in the order they were first set, its own choice
  ([Front end: Coney's implementation](../../research/frontend.md#coneys-implementation));
- **Lua 5.4** visits a table's array part in index order and its hash part in its own hash order.

For a list built in order (`{a, b, c}`, or filled 1, 2, 3, ...), all three agree. They can differ for a table with
string keys, and for a list that `tinsert` gave an `n` field. How many of the 2,504 loops iterate such tables with an
effect that depends on the order is not known; [the probe](#probe) measures it. For each loop that differs, the
translator has two answers: when the order is a function of the keys (a list, ascending), an ordered traversal
(`lua40.inorder`); otherwise the order of the constructor that built the table, which the translator can read when the
table is made from a literal and never grown. A loop that needs neither cannot be translated faithfully, and its level
is not offered in 5.4.

## Where a translated mission runs {#where-it-runs}

Lua 5.4 cannot run in the 4.0 game state, and the level's scripts share one state: the level script calls
`global.lua`'s helpers, reads the preloads' tables and the language file's strings. So a mission in 5.4 means its
**whole game state** is in 5.4:

- **A 5.4 game state, per level.** When a level has a 5.4 patch from an enabled mod, Coney makes its game state as a Lua
  5.4 state (the [build settings](lua-sdk.md#lua-build) of the SDK: fixed hash seed, fixed sort pivot, Coney's
  allocator) instead of the 4.0 one, and runs in it the translation of every script the level loads: `global.lua`, the
  level's script, what `doFile` and `preLoadFile` ask for, each from its own disc chunk through the frozen decompile and
  translation forms, or from a mod's patch. Every other level runs in 4.0 as always, since a level unload destroys the
  game state anyway ([Life of the Lua state](../../research/scripting.md#life-of-the-lua-state)).
- **Patched 4.0 scripts in a 5.4 level** are translated after patching, so a mod with a 4.0 patch of `global.lua` and
  another with a 5.4 patch of a mission run together.
- **The natives through a game-state bridge.** The same native list as the 4.0 state, with 4.0's conventions: numbers,
  strings, nil, tables written back after the call, callbacks by name, 1 and nil for truth. It is the
  [value bridge](lua-sdk.md#bridge) of projects with the boolean row changed, so one bridge serves both.
- **The same scheduler.** Level entry, the update function, scheduled calls, message handlers, animation callbacks and
  triggers reach the 5.4 game state at the same points as the 4.0 one, through the one `ScriptScheduler`
  ([Three parts](../asset-and-script-architecture.md#script-parts)).
- **Still apart from projects.** A translated mission runs in the game state, not in a project state: the
  [two states](lua-sdk.md#two-states) rule holds, and a project's scripts still reach a mission only through natives.

## Translation is deterministic {#deterministic}

The translator is a pure function of the 4.0 text and its own form number; the decompile is a pure function of the
chunk and its form. So the 5.4 base text of a script is fixed by its chunk hash and the two form numbers, the same on
every machine, and a patch against it applies everywhere, as a 4.0 patch does. Its forms are frozen as the decompiler's
are ([Forms](mission-mods.md#forms)): the hash of every script's translation is committed, and a change is a new form.

Because the translation is line for line, a 4.0 patch can be **lifted** to 5.4: translate the base and the patched text
and take their difference. Studio does that when a user converts a mission to 5.4, and Coney does it when a 4.0 patch
must meet a 5.4 one ([Two mods](mission-mods.md#two-mods)).

## Proof by trace equality {#proof}

A script reaches the game only through natives and the engine reaches a script only by calling it by name
([Calling Lua from C++](../../research/scripting.md#calling-lua-from-c)). So two runs whose traces are equal (the same
native calls, in the same order, with the same arguments and results, step by step, and the same calls into the scripts)
did the same thing to the game. That is the parity test the architecture page sets for Coney's own level rewrites
([Three parts](../asset-and-script-architecture.md#script-parts)), and the translator uses the same one:

- **The trace.** Recorded by a call tracer that wraps every native in the native list (the list is the
  [Lua SDK's first build issue](lua-sdk.md#build-issues); the tracer is step 4 of the architecture page's
  [deferred order](../asset-and-script-architecture.md#later), built here as issue 10): the step, what started the
  script (level entry, update, scheduled call, message, callback, trigger), each native's name, arguments and results
  as `script::Value`s, tables in full (today's `--script-trace` shows a table as `{table}`,
  [Event logs](../running-play-level.md#event-log)), and each call into the scripts. Values are compared as the natives
  see them, so 5.4's integer and float subtypes do not show, and strings are compared byte for byte.
- **Playthrough runs.** Every [playthrough test](../testing.md#playthrough-tests) of a level runs twice, with the disc's
  scripts in the 4.0 game state and with their translations in the 5.4 one, the same input script and seed, and the two
  traces must be equal at every step. The front end's scripted run is compared the same way.
- **Differential runs on every script.** A fake native host answers every native from a seeded generator, by the result
  type the masterlist gives; each function of each script is called with seeded arguments in both runtimes, many seeds
  each, and the traces compared. It needs no playable mission, so it covers all 467 scripts (503 with an Xbox disc) and
  branches no playthrough reaches, and it reports which functions and jumps it reached.
- **The gate.** Studio offers a level's 5.4 form only when its differential runs agree on every function and, where
  Coney plays the mission, every playthrough test's traces are equal. Shared scripts (`global.lua`, the preloads, the
  language files) run in every level, so their 5.4 forms are offered last, once every level that runs them passes.
  Studio thus reflects Coney: a level gains its 5.4 form when Coney proves it, and the playthrough runs reach only as
  far as Coney plays each mission ([Missions](../../missions/index.md) has their status).

What the traces **prove**: on the runs made, the translated scripts behave exactly as the 4.0 scripts do in Coney.
What they **do not prove**: paths no run reached (the differential runs' coverage says how much is left); anything about
the original game, since the reference is Coney's 4.0 VM running the disc chunk, whose own fidelity is checked by the
[runtime comparison](../testing.md#runtime-comparison); and a mod's own 5.4 code, which needs no proof, being the
author's 5.4 by intent.

## The probe {#probe}

The verdict below rests on numbers the bytecode alone cannot give: how many loops differ in order, how many
concatenations need `lua40.str`, whether any construct escapes the mapping. A probe finds them cheaply, before any
engine work, with tools that exist: the `lua4` package's syntax tree written out as 5.4 by a second renderer, every
translation checked by a stock `luac` 5.4, and differential runs of all 467 scripts under a stock Lua 4.0.1
interpreter (built from the same pinned source as the `luac` of `workspace init --fetch lua`) and a stock Lua 5.4,
each with the same fake natives written in Lua. It prints counts only: scripts that translate and parse, functions
whose traces agree, loops whose order differs between 4.0's hash order, first-set order and 5.4's order. For string
and number keys the stock 4.0 interpreter's order is the original's (the game links the same table code, and those
keys hash by value), so the probe also shows how often Coney's first-set order departs from the original's. That holds
only for an interpreter built with a 32-bit `long`, as the PS2's is: Lua 4.0 hashes strings in an `unsigned long`,
which a 64-bit Linux or macOS build widens, sending long string keys to other slots.

## The verdict {#verdict}

**Feasible, and worth doing after 4.0 patching, if the probe confirms the iteration order is contained.**

| Part | Effort | Risk | Evidence |
| --- | --- | --- | --- |
| The syntax mapping | S | Low | No upvalues, no varargs, 12 library names, one reserved word ([What the game's scripts use](#what-scripts-use)); built on the compiler's parser and a 5.4 renderer |
| Numbers and booleans | S to M | Low to medium | Mechanical rules and the `lua40` helpers; every miss shows as an unequal string or value in a trace |
| Iteration order | unknown: S if few loops differ, L if many | **The real risk** | Not measurable from the bytecode alone; [the probe](#probe) measures it |
| The 5.4 game state and its bridge | M to L | Medium | Shares the project runtime's build and bridge ([Lua SDK build issue 2](lua-sdk.md#build-issues)); the scheduler and callbacks are the existing entry points, reached through one `ScriptScheduler` |
| The proof (traces, the fake host, both runs) | M | Low | The same parity test Coney's own level rewrites need; nothing is built only for the translator |
| Keeping it | ongoing | Low | Two forms to freeze and two game-state runtimes to keep; the faithful game stays on 4.0, so a bug in the 5.4 state touches only levels a mod put there |

Taken together, about the size of the Lua 4.0 compiler (M to L, [Risks](../asset-and-script-architecture.md#risks)),
and most of it is shared with work already planned. The cost the maintainer would accept with 5.4: a second game-state
runtime kept for the life of the project, and translated missions that read slightly unlike a project's 5.4 (1 and nil
for truth, `lua40.str` in concatenations). What 5.4 buys: one language for every script a user touches, and Studio's
completions, errors and debugger on missions as on projects, since lua-language-server cannot read 4.0
([Language tooling](technology.md#language-tooling)).

**The decision rule**, from the probe's counts: go to 5.4 when the differential runs agree on every function once the
mechanical rules are in, and the order-dependent loops are few enough to translate by the two answers above. Keep 4.0
when the order-dependent loops are many or need history the translator cannot see. Either way 4.0 patching is built
first and stays: it is what a 5.4 patch falls back to for a level not yet proved, and its pieces (the decompiler in C++,
the compiler, the patch format, the catalog provider) are the translator's foundation.

A third way was weighed and set aside: **5.4 syntax over the 4.0 runtime**, a 5.4-looking text compiled back to 4.0 and
run in the 4.0 game state. It avoids the order and number risks and gives the language server something to read, but
it is a dialect: `true` would be 1, `//` and integers would not exist, and a user would meet a language that looks like
5.4 and behaves like 4.0, which is the inconsistency the maintainer wants to remove.
