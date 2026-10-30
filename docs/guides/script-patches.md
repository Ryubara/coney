# Script patches

A script patch, a `.cpatch` file, holds the differences between one of the game's scripts and a modder's edit of it,
and nothing of the script's own text: what it keeps, drops, copies or rebuilds from the script is given by line and
column numbers, checked by hashes, and only the lines the modder wrote appear in it. Each player's Coney applies the
patch to the script from their own disc. This page is the format, how a patch is applied, made and merged, and the
`coney --script-patch` command line for a modder who edits in another editor. It is **Coney's own feature**, part of
Coney Studio's mission mods: the design, and why patches look like this, is [Mission mods:
Patches](studio/mission-mods.md#patches) ([#1054](issue:1054)).

What is built is the engine's half: the reader and the writer, apply, make, merge and the command line
(`src/core/script_patch*.cpp`, `src/core/sequence_diff.cpp`, `src/platform/script_patch_command.cpp`). Running a
patched script in the game in place of the original, and editing a script in Studio, are the design's later
[build issues](studio/mission-mods.md#build-issues) (5 and 6), and a patch's base text is the decompiler's output in
its frozen [form](studio/mission-mods.md#forms) (build issues 1 and 2).

## Contents {#contents}

1. [Texts and lines](#lines).
2. [The format](#format): the header, the operations, quoting, sections, the limits.
3. [Applying a patch](#apply): what is checked, and the refusals.
4. [Making a patch](#make): the diff and how it keeps the base's text out.
5. [Merging patches](#merge): two mods patching one script.
6. [The command line](#command-line).

## Texts and lines {#lines}

A text (the base, the edited copy, the result) is UTF-8 bytes cut into lines at each `\n`, which no line includes.
A text that ends with a newline has an empty last line, so `"a\nb\n"` is three lines (`a`, `b` and an empty one) and
joining the lines with `\n` gives the text back byte for byte; an empty text is one empty line. A `\r` stays part of
its line. Lines are numbered from 1, a line's bytes (its **columns**) from 0.

Two hashes check a patch, both SHA-256:

- a text's hash is the hash of its bytes (`base` and `result` in the header);
- a range's hash is the hash of its lines, each followed by `\n` (a dropped range's hash).

## The format {#format}

A `.cpatch` is UTF-8 text. Its first line is `coney-script-patch 1`, the format version; then come one or more
sections, each a header, a line `---` and the operations, in base-line order. A line may end in CRLF (a checkout that
changed the newlines changes nothing). A blank line, or one starting with `#`, is skipped anywhere, and every line but
a `+ text` line may end with a `# comment`.

```text
coney-script-patch 1
entry  = 0x1c2d3e4f
name   = "level99.lua"
chunk  = "sha256:9f2c..."
form   = "lua4 1"
base   = "sha256:41aa..."
result = "sha256:7c03..."
---
= 1-212                             # keep base lines 1 to 212
- 213-215 sha256:0b7e...            # drop base lines 213 to 215, checked by their hash
+ Coney.Log("the yard fight starts early")
< 340-352                           # copy base lines 340 to 352 here
^ 360 [0-11] "7" [13-]              # a line built from base line 360, written here
~ 401 [0-27] "3" [29-]              # base line 401 rebuilt: its columns 0-27, "3", its column 29 to the end
= 402-                              # keep the rest
```

*The made-up example of the design, with its hashes cut short.*

### The header {#header}

One `key = value` a line, each key at most once:

| Key | Value | Needed |
| --- | --- | --- |
| `entry` | `0x` and up to eight hex digits: the WAD entry the script lives in, its name's hash | no |
| `name` | quoted: the script's name | no |
| `chunk` | quoted `sha256:` and 64 hex digits: the compiled chunk the base text was made from | yes |
| `form` | quoted: the base text's [form](studio/mission-mods.md#forms), `lua4 1` for decompile form 1 | yes |
| `base` | quoted `sha256:...`: the base text's hash | yes |
| `result` | quoted `sha256:...`: the hash of the text the patch makes | yes |

### The operations {#operations}

A range is `a` (one line), `a-b` or, for a keep only, `a-` (to the end). **Keep**, **drop** and **rebuild** consume base
lines: each starts at the line after the last one consumed, so the base is read once, in order, and nothing but `+`, `<`
and `^` lines may follow a keep to the end. **Add**, **copy** and **build** only write.

| Line | Kind | What it does |
| --- | --- | --- |
| `= a-b` | Keep | Base lines a to b, unchanged |
| `- a-b sha256:...` | Drop | Base lines a to b left out; their range hash is checked |
| `+ text` | Add | A line the modder wrote: everything after the `+` and one space, as it is |
| `+"text"` | Add | The same, quoted: for a text with a control character, or a space or tab at its end |
| `+` | Add | An empty line |
| `< a-b` | Copy | Base lines a to b written here (a block moved or duplicated) |
| `~ n pieces` | Rebuild | Base line n replaced by a line made of the pieces |
| `^ n pieces` | Build | A line made of base line n's pieces written here; consumes nothing (a line moved and also changed) |

A line's **pieces** are `[a-b]` (its columns a to b), `[a-]` (column a to the end) and quoted text the modder wrote, in
order; the columns of one line come in increasing order, each at most once.

**Quoted text** is in double quotes, with `\\`, `\"`, `\t`, `\r`, `\n` and `\xHH` for a backslash, a quote, a tab, a
carriage return, a newline and any other byte. The writer quotes a `+` line only when it has to, so a patch reads like
the lines it adds; the reader takes either form.

### Sections {#sections}

A patch may hold one section per base it fits: for a script that differs between two discs (a region whose script is
not byte-identical), each section is the patch of one of them, and Coney uses the section whose `chunk` (or, on the
command line, whose `base`) matches ([Which scripts a patch fits](studio/mission-mods.md#which-scripts)). A section
starts at its first header line after the last section's operations.

### Limits {#limits}

The reader and every function hold to these limits, so a damaged or hostile patch is refused instead of eating memory
(`src/core/script_patch.h`):

| Limit | Value |
| --- | --- |
| A `.cpatch` file | 4 MiB |
| A text (base, edited copy, result) | 4 MiB and 200,000 lines |
| Sections in a file | 16 |
| Operations in a section | 200,000 |
| Pieces of one built line | 1,024 |

## Applying a patch {#apply}

`patch::applyPatch()` checks, in order, and refuses the patch at the first thing that does not hold. Nothing is ever
applied approximately: the base text is the same byte for byte on every machine, so a line number either fits or
the patch is refused.

| Refusal (`PatchFault`) | When | The message starts |
| --- | --- | --- |
| `WrongBase` | The text's hash is not the patch's `base` | `the patch is for another text: its base is sha256:...` |
| `DoesNotFit` | An operation names a line or a column the text does not have, consumes lines out of order, or the operations leave base lines unaccounted for | `the patch keeps base lines 10-40, but the text has 17 lines` and the like |
| `DamagedRange` | A dropped range's lines do not have its hash | `the patch is damaged: base lines 4-5 do not have the hash it gives` |
| `TooLarge` | A text, or the text the patch makes, passes a [limit](#limits) | `the patched text would pass the limit ...` |
| `DamagedResult` | The text made does not have the `result` hash | `the patch is damaged: its result is sha256:...` |

What a player sees for each of these when a mod starts is the design's [Refusals](studio/mission-mods.md#refusals).

## Making a patch {#make}

`patch::makePatch()` writes the smallest patch it can that holds none of the base's lines:

1. **A line diff.** Each line becomes a number (equal lines share one), and Myers' diff in linear space gives the
   fewest lines dropped and added (`src/core/sequence_diff.cpp`). A part of the diff costing more than 8,192 edits is
   taken as replaced whole: still a correct patch, only a longer one.
2. **Copies.** An added line that the base has (anywhere) is a copy, `<`, and a run of them from consecutive base
   lines is one range.
3. **Rebuilds.** An added line paired with a dropped line of the same change, in order, that keeps more than half of
   that line's bytes is a rebuild, `~`: the bytes the two agree on in runs of three or more are columns, the rest is
   text. Changing one number keeps all of the line but the number.
4. **Built lines.** An added line that is still neither, but keeps more than half of a base line elsewhere (found by
   the first or the last six bytes of its text, past its indent), is built from that line, `^`: a function moved and
   changed on the way carries none of its lines.
5. **Added lines.** Whatever is left is the modder's own text, `+`.

Dropped lines are written before the lines that replace them, and a keep that reaches the end is written `a-`.
Applying the patch made to the base gives the edited copy, byte for byte. Studio's Export adds the guard's fingerprint
check on top ([Patches](studio/mission-mods.md#format)).

## Merging patches {#merge}

`patch::mergePatches()` merges several patches of one base, in load order, as the design's [Two mods patching one
script](studio/mission-mods.md#two-mods) says. Every patch is applied first (any refusal above comes back as it is),
and patches of different bases, chunks or forms are `WrongBase`. Each patch is read as its **changes**: a run of
operations between two keeps, covering the base lines it consumes (none for a pure insertion, which sits before a line).
Two changes are the same when they cover the same lines and write the same lines, whichever operations they use.

- **Different lines merge.** Changes that share no base line are taken together, and so are ranges that only touch
  (one ends at line 6, the other starts at 7).
- **An insertion at a change's edge** goes before it when it was put before the change's first line, and after it when
  put after its last line.
- **The same change made by both** is applied once.
- **A conflict** is two changes that share a base line, two different insertions at the same place, or an insertion
  strictly inside another patch's changed range. Changes that conflict with each other, directly or through a third,
  are one conflict, reported with both patches and the base lines each side covers (`MergeConflict`). The merged patch
  keeps the side a chooser picks (the earlier patch's when there is none), so a player's recorded choice can settle it.
- **Three or more** merge one at a time, in load order.

The merged section keeps the first patch's header and has the hash of the merged text as its `result`. Whether the
merged script compiles is checked where it runs (design build issue 5).

## The command line {#command-line}

`coney --script-patch` makes and applies patches on files, for a modder who edits a decompiled script in another
editor. It needs no disc and opens no window, and it is in both builds, the player's and the one with the debug tools.

```sh
build/dev/src/platform/coney --script-patch make level99.lua level99-edited.lua --out level99.cpatch \
    --chunk sha256:9f2c... --name level99.lua
build/dev/src/platform/coney --script-patch make level99.lua level99-edited.lua --out level99.cpatch \
    --chunk-file level99.luac --entry 0x1c2d3e4f
build/dev/src/platform/coney --script-patch apply level99.cpatch level99.lua --out level99-patched.lua
```

- `make BASE EDITED --out PATCH` writes the patch from the base text to the edited copy. The chunk the base was made
  from is `--chunk sha256:HASH` or `--chunk-file FILE` (its hash is taken); `--form` (default `lua4 1`), `--name` and
  `--entry` fill the rest of the header. It prints one line: the operations, how many lines were dropped, added,
  copied, rebuilt and built from another line, and the result's hash.
- `apply PATCH BASE --out RESULT` writes the patched text, from the section whose `base` is the hash of BASE; with
  none, it says the patch was made from another script or another form of it. It prints the result's hash.

Neither prints a line of either text. The exit code is 0 when the file was written, 1 when a file could not be read or
written or the patch was refused (the message says which), and 2 for a usage error, which prints the usage.
