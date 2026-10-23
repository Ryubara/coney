# Compiled Lua chunks (`.lua`)

Verified against: `SLUS_212.15` (NTSC-U, SHA1 `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`) and the NTSC-U disc's
`WARRIORS.WAD`. This page documents the container only, never the scripts' contents ([LEGAL.md](repo:LEGAL.md)).
The instructions inside, and how Coney's decompiler reads them back, are on [Lua 4.0 bytecode](lua-bytecode.md).

## Purpose

The game's 467 scripts ship as Lua 4.0 bytecode, the output of `luac`, in WAD entries named `<name>.lua`. What they
do and how the game runs them is on [Scripting](../scripting.md); this page is the file layout an extractor or a
loader needs. `coney-tools extract` writes the chunks as stored (`scripts/`); `coney-tools lua decompile` turns them
into readable Lua outside the repository, for research ([coney-tools](../../guides/coney-tools.md#lua)).

## Original structure

The loader is stock Lua 4.0's `lundump.c`, linked into the executable. Names are ours (after Lua's).

| Address | Name | Role | Evidence |
| --- | --- | --- | --- |
| `0x00333548` | `Lua4_LoadChunk` | reads the header, then the main function | confirmed (code) |
| `0x00333380` | `Lua4_LoadChunkHeader` | checks the signature, version `0x40`, byte order and the size bytes | confirmed (code) |
| `0x00333168` | `Lua4_LoadFunctionPrototype` | one function: source, line, parameters, vararg flag, stack size, locals, lines, constants, code | confirmed (code) |
| `0x00332db0` | `Lua4_LoadFunctionCode` | the instructions; the last must be `END` (0), else "bad code" | confirmed (code) |

## Data

All values little-endian (the byte-order flag is 1).

### Header (13 bytes, then a test number)

| Offset | Value | Meaning |
| --- | --- | --- |
| `+0x00` | `1B 4C 75 61` | signature, ESC `Lua` |
| `+0x04` | `40` | version 4.0 (the reader refuses above and below) |
| `+0x05` | `01` | little-endian |
| `+0x06`-`+0x08` | `04 04 04` | sizes of `int`, `size_t` and an instruction |
| `+0x09` | `20` | bits per instruction (32) |
| `+0x0a` | `06` | bits of the opcode |
| `+0x0b` | `09` | bits of the B field |
| `+0x0c` | `08` | size of a number (`double`) |
| `+0x0d` | f64 | test number (Lua 4.0's `3.14159265358979e8`), checked against the reader's own |

Confirmed (code) at `0x00333380` for every check. **Disc check:** all 467 Lua entries start with these 13 bytes.

### Function prototype

Repeated for the main function and, recursively, every nested one (stock Lua 4.0):

```text
string  source            "=(none)" on the disc (what `luac -s` writes)
int     lineDefined       kept on the disc
int     numParams
u8      isVararg
int     maxStackSize
int     n; n x {string name, int startPc, int endPc}    local variables (debug): n is 0 on the disc
int     n; n x int                                      line info (debug): n is 0 on the disc
int     n; n x string                                   string constants
int     n; n x f64                                      number constants
int     n; n x prototype                                nested functions
int     n; n x u32                                      instructions (the last is END)
```

A string is a `size_t` length that counts its final NUL (0 for none), then the bytes. **Disc check:** all 467 chunks
parse to exactly their entry's size with this layout ([python/src/coney_tools/lua4.py](repo:python/src/coney_tools/lua4.py)).

**Every chunk is stripped** (`luac -s`): in all 11,448 functions the local-variable and line-info counts are 0 and the
source is `=(none)`; only `lineDefined` is kept. So no chunk names its locals or gives a statement's line. Disc check
with `coney-tools lua decompile --all`; what that means for a decompile is on
[Lua 4.0 bytecode](lua-bytecode.md#what-a-chunk-keeps-and-what-stripping-removes).

Table constructors flush every 62 items. That is stock Lua 4.0.1 (`LFIELDS_PER_FLUSH`, 64 capped at a quarter of
`MAXSTACK`), not a change in the game's build ([Lua 4.0 bytecode](lua-bytecode.md#limits)).

## Behaviour

`doFile` finds a chunk by its WAD name and runs it ([Scripting](../scripting.md)).

## Coney's implementation

Coney's VM loads the same bytecode (`src/scripting/`); `coney-tools` reads it for the reference lists
(`lua4.py`), copies it for the extracted folder and decompiles it for research (`lua decompile`, the `lua4` package:
[Lua 4.0 bytecode](lua-bytecode.md)).

## Open questions

- 111 of the 467 chunks have no recovered name (the extractor writes them as `<hash>.lua`).
