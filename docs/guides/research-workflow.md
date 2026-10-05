# Research workflow

Research turns the original game into documents that someone can implement from **without opening Ghidra**. A
research page says what a part of the game does, what its data looks like and how sure we are of each claim; the
engine code is then written from that page. This guide covers who does what, how claims are graded, where findings
go and how to write them up.

The pages themselves live in `docs/research/`, one living page per subsystem or file format. How to lay a page out
is in [Writing these docs](writing-docs.md#subsystem-page).

## Clean room {#clean-room}

Coney is a clean-room reimplementation: the code has the same observable behaviour as the original but its own
structure, names and control flow. The rule is set in
[LEGAL.md](repo:LEGAL.md#clean-room); this section is how we work within it.

The work splits into two roles.

| | Analyst | Implementer |
| --- | --- | --- |
| Reads | Ghidra's decompiler output and disassembly of `SLUS_212.15`, the running game in PCSX2, the game's data files, plus everything the implementer reads | `docs/research/`, the research database (`research/*.yaml`), Coney's own code and tests |
| Writes | research pages in `docs/research/`, entries in `research/*.yaml` | engine code in `src/`, tools, tests |
| Never | implements a function whose original code they have read | reads decompiler output or disassembly, opens the Ghidra project or the emulator's debugger |

The split is per function, and it binds people and agents alike: **whoever has read a function's original code
(its decompiler output or its disassembly) never implements that function. Someone else implements it, from the
research page.** The reason is evidence, not trust. Code written by someone who has never seen the original can only
have come from the page, so its independence can be shown; code written by someone who has seen it cannot, however
carefully they worked. You may analyse one function and implement a different one, as long as you have never read
the original code of the one you implement.

For agents, Rekit enforces the split where the platform allows: the analyst agent and the implementer agent are
separate, and the implementer agent has no Ghidra or emulator tools at all. People keep it by discipline: before
implementing a function, check that you haven't read the original code of the functions involved, and hand
the work on if you have.

What crosses from analyst to implementer is **facts and behaviour**, never code: data layouts, constants, file
formats, algorithms needed to read the game's data (the CRC-32 name hash, for example), and prose descriptions of
what a function does. Short pseudocode is fine when it explains behaviour; a function's control flow copied line by
line is not (see [Writing up a finding](#writing-up-a-finding)).

### When a research page isn't enough

Sooner or later an implementer needs something the page doesn't say. Don't look at the original to fill the gap,
not even briefly: once you have read a function's original code, you can no longer implement that function (see
above), and the work has to go to someone else.

Instead:

1. Add the question to the page's **Open questions** section, saying what you need and why (what decision in the
   code depends on it).
2. Raise it with an analyst: as a GitHub issue, or, for an agent, in its report as a blocker.
3. Carry on with whatever doesn't depend on the answer, or stop and say so. A guess written into code without a note
   is worse than a gap, because nobody knows to come back to it.

An analyst answers by updating the page, which is how the answer reaches every later reader too.

## Evidence levels {#evidence-levels}

Every non-obvious claim on a research page says how we know it. A reader deciding whether to build on a claim needs
to know whether it was read in the code, seen happening, or worked out from clues. There are four levels.

| Level | Meaning | YAML value |
| --- | --- | --- |
| **confirmed (code)** | Read in the original's code, at a cited address | `confirmed-code` |
| **confirmed (runtime)** | Observed in the original game running under PCSX2; the claim states the PCSX2 version and the method (a breakpoint at an address, a memory watch, a frame) | `confirmed-runtime` |
| **inferred** | Follows from evidence that points one way, without being read or observed directly | `inferred` |
| **speculative** | A plausible guess, written down so it can be tested | `speculative` |

Examples (the current pages have no runtime observation yet, so that level is shown as a template):

- **Confirmed (code).** [WARRIORS.DIR](../research/formats/wad-dir.md): the index's constructor at `0x00149160`
  reads the entry count, seeks to `0x10`, allocates `count * 12` bytes and reads the entry table. The claim names
  the address where anyone with the executable can check it.
- **Confirmed (runtime).** [Memory](../research/memory.md#sizes-at-runtime): the `Sector Pool` is 17,217,536 bytes,
  read in PCSX2 2.9.94 over PINE from the pool object the page documents (`0x00760f80`, block size at `+0x0c`) on
  a retail boot, with the moments of the reads (start-up movies, main menu, a fight in `level102`). The claim says
  what was read, where, when and in which emulator version, so anyone can repeat it.
- **Inferred.** [Compiler](../research/compiler.md): the vtables use 8-byte `{delta, fn}` slots, the GCC 2.x
  layout, which points to GCC 2.95.x. The layout is confirmed; the compiler version is a conclusion drawn from it,
  and the page says the exact version is not confirmed.
- **Speculative.** [Recon](../research/overview.md#disc-layout): the custom `IOP.IRX` module is "probably" the
  audio driver. Nothing has been read or observed yet; the guess is recorded so that someone can test it.

A check against the game's own files is **corroboration**, not a level of its own: it is written next to the
claim it supports. [WARRIORS.DIR](../research/formats/wad-dir.md) does this: its layout is confirmed (code), and the
line beside it notes that the layout predicts `16 + 10701 × 12 = 128,428` bytes, the size of `WARRIORS.DIR` on the
disc. The check makes the claim more trustworthy, but it grades nothing by itself.

Write the level next to the claim it grades, for example `**Evidence:** confirmed (code) at 0x00149160.` A claim
that moves up a level (a guess confirmed in the code, say) is updated in place, and the old level goes away. Some
older pages say "Confidence: confirmed from code"; that means confirmed (code), and those pages are brought to the
new wording when they are next edited.

A page also says what it was checked against: the executable (`SLUS_212.15`, see
[Writing up a finding](#writing-up-a-finding)) and, for runtime claims, the PCSX2 version.

## The research database {#the-research-database}

Research pages are for reading; the research database is the same knowledge in a form tools can check. It is a set
of YAML files in `research/` at the repository root, in the schema Rekit defines:

- `research/symbols/<area>.yaml`: one entry per function or global, by address;
- `research/types/<area>.yaml`: one entry per struct, with its fields at their offsets.

```yaml
# research/symbols/<area>.yaml
- addr: 0x001490b8
  name: DVDWadIndex::Find
  tu: Device/ps2/fileio/DVDWadIndexPS2.cpp     # original translation unit, when known
  evidence: confirmed-code                     # confirmed-code | confirmed-runtime | inferred | speculative
  reimpl: src/fileio/wad_index.cpp             # filled in by `rekit research check --fix-reimpl`
  notes: CRC-32 of the lowercased name, then a linear scan.
```

```yaml
# research/types/<area>.yaml
- name: WadDirEntry
  size: 12
  evidence: confirmed-code
  fields:
    - { offset: 0x0, type: u32, name: wadOffset }
    - { offset: 0x4, type: u32, name: size }
    - { offset: 0x8, type: u32, name: nameHash, notes: "CRC-32 of the lowercased path" }
```

The database keeps three things in step:

- **the code:** `rekit research check` matches every `@orig` tag in the source against the symbols, in both
  directions (see [Comments and @orig](conventions.md#comments-and-orig));
- **the docs:** `rekit research tables` generates the symbol and type tables in `docs/research/tables/`, which are
  never edited by hand;
- **Ghidra:** `rekit ghidra apply` pushes names, types and comments into an analyst's local Ghidra project, and
  `rekit ghidra export` brings new names back, never overwriting an entry that has a higher evidence level.

The database **arrives with Rekit**. Until then, findings are recorded on the research pages only, and the YAML
is seeded from them when it arrives. One file is already there, because it is Coney's own: the script bindings.

### The script bindings {#bindings}

The files `research/bindings/<category>.yaml` list every function the game registers with its Lua state, one entry
per name: the masterlist of the game's script bindings (what FiveM calls natives), which is also the reference for
script mods. There is one file per category, named by the category's id (the ids are in `coney_tools/natives.py`), so
an entry has no `category` key; moving a binding to another category means moving its entry to that file.
`coney-tools natives render` checks the files and generates the reference pages in `docs/references/bindings/` from
them; the pages are never edited by hand, and CI runs `natives render --check` ([coney-tools](coney-tools.md#natives)).
An entry of `level.yaml`:

```yaml
- name: GetLevelId                 # the Lua global's name
  origin: game                     # game (default) | coney: a binding Coney adds for mods, with no wrapper or usage
  registered_by: RegisterBindings  # RegisterBindings (default) | ScriptSystem (the four Cfg* of the constructor)
  wrapper: 0x0036c568              # the tolua wrapper
  calls:                           # the functions of the game the wrapper calls, with our name when we have one
    - {addr: 0x0041d6f0}           # add `name: ...` once the function has one of ours
  args:                            # in the order the wrapper reads them (Lua argument 1, 2, ...)
    - {name: index, lua: number, ctype: unsigned, desc: "Index of a level record (0 is the front end)."}
  results:                         # what the wrapper pushes
    - {lua: number, desc: "The record's level number, as in levelNN.lua."}
  overloads:                       # only for the ten names registered twice: the first registration, same keys
    - {wrapper: 0x..., calls: [...], args: [...], results: [...], note: "when it is used"}
  description: "What the binding does, in one to four sentences."
  evidence: confirmed-code         # the level of the description and meanings; the signature is always confirmed (code)
  depth: thorough                  # thorough (the callee was traced) | brief (a shorter description)
  notes: "Caveats: ignored arguments, edge cases."
  usage: {chunks: 2, calls: 8, boot: true, mission1: false, result_used: true}
  coney: not implemented           # not implemented (default) | partial | implemented
```

- `lua` is how the wrapper reads an argument: `number`, `boolean`, `string`, `table` (with `elem: number` or `string`
  and, when fixed, `count`) or `userdata`; a number's `ctype` is what it becomes (`int`, `unsigned`, `float` or
  `double`). `default` is the value an argument left off takes, and `written_back: true` marks a table the binding
  writes into (every table argument of the game's bindings is). A result's `lua` is `number`, `boolean` (1 or nil),
  `string` or `usertype` (with `type`).
- `usage` is counted from the disc's compiled Lua chunks: how many chunks reference the binding's global name and
  how many references there are (`calls`), whether the boot-to-menu scripts or the first mission's scripts
  reference it, and whether a traced call keeps its result (a lower bound). It holds counts only.
- `coney` is never edited by hand: `coney-tools natives coney` sets it from Coney's binding table
  (`src/scripting/script_bindings.cpp`), writing the key only when it is not the default
  ([coney-tools](coney-tools.md#natives)).
- `depth: mechanical` (signature only, no description) is accepted so a new entry can land before it is described;
  the masterlist has none.

## Reference lists {#reference-lists}

The [Game references](../references/index.md) are lists of the things scripts, mods and Coney's code name:
characters, gangs, objects, levels, clips, anim ids, controls, colours, text tags, script constants, sounds,
script events and WAD entry names. Each is a YAML file in `research/references/<list>.yaml`, and its page in
`docs/references/` is generated from it. They hold only names, ids, numbers and our own short descriptions
(`LEGAL.md`, "Reference lists"): never game text, script source or a file from the disc.

A list file has a fixed shape; the fields of its entries are defined per list in
`python/src/coney_tools/refs_topics.py`, and each page ends with its list's fields:

```yaml
title: Characters (humans)
about: |                                  # Markdown, our own words: what the list is
  ...
complete: |                               # Markdown: what is complete and what is not
  ...
defaults: {source: "config_preload2.lua, CfgChar", evidence: inferred}
entries:
  - id: 32
    model: warr_re_cv
    label: Rembrandt (`_cv` model)        # hand-written
```

- Every entry has a **source** and an **evidence** level (the [levels](#evidence-levels) above, spelled
  `confirmed-code`, `confirmed-runtime`, `inferred`, `speculative`), its own or the list's `defaults`.
- Each entry has a stable **anchor**, `<list page>#<prefix>-<key>` (`characters.md#char-32`,
  `levels.md#level-29`), for links from research pages, issues and mods. A key never changes once published.
- Fields are either **read from the disc** or **hand-written** (each page's field table says which).
  `coney-tools refs extract` rewrites the first kind and keeps the second, and keeps entries it did not produce,
  so a hand-added entry or a name found another way survives a refresh.

Commands ([coney-tools](coney-tools.md#refs)):

```sh
uv run --project python coney-tools refs extract [DISC] [--only LIST ...] [--names FILE]   # refresh from your disc
uv run --project python coney-tools refs render [--check]                                 # write or check the pages
```

To change a list by hand, edit its YAML and run `refs render`; CI fails when a page is out of date with its YAML.
To add a field or a list, add it in `refs_topics.py` (and its reader in `refs_extract.py` when it comes from the
disc), then run `refs extract`. **Thumbnails** go in `docs/references/images/` (`characters/<model>.png`,
`objects/<name>.png`; 256 × 256, transparent, rendered by `coney --render-references` from the player's disc,
then shrunk with `coney-tools refs compress-images`); `refs extract` links each one that exists. The character
images are rendered with `--names` given every model name in `character-models.yaml`, so only the models with no
recovered name are named by hash.

## Tools

- **Ghidra with ghidra-mcp.** Static analysis of `SLUS_212.15`, with the Emotion Engine processor extension; the
  ghidra-mcp server lets analyst agents drive it. Setup: [Ghidra + ghidra-mcp](ghidra.md). The Ghidra project holds
  the game's code, so it stays on your machine and is never committed.
- **PCSX2.** For runtime evidence: memory reads and writes on the game as it runs. A bridge in `coney_tools`
  (`coney-tools emu`) is planned, so that runtime checks can be scripted and repeated; until then, see
  [Driving PCSX2](#driving-pcsx2).
- **Capture analysis.** Recording what the game sends to the graphics hardware or the sound processor and studying
  the capture. It is a later fallback, for questions that the code and the debugger answer badly (exact rendering
  state, timing). Captures contain game data: they stay in your scratch folder and never enter the repository.

### Driving PCSX2 {#driving-pcsx2}

How the first runtime pass (2026-10-04, official portable PCSX2 2.9.94) was done; it needs nothing but PCSX2, its
PINE server and the `pcsx2` MCP server (or any PINE client).

- **Set-up.** Enable PINE in `inis/PCSX2.ini` (`[EmuCore]` `EnablePINE = true`, `PINESlot = 28011`) before launching.
  Start `pcsx2-qt.exe -fastboot -- <iso>` in the background. In our run PCSX2 2.9.94 failed to open the ISO whose path
  holds commas and parentheses ("Requested filename ... does not exist"); an NTFS hard link with a plain name in your
  scratch folder (`New-Item -ItemType HardLink`) boots fine and copies nothing. Pass the path with backslashes (start
  it from PowerShell): the same link given as `C:/Users/...` from Git Bash was refused with the same error.
- **What PINE gives.** Memory reads and writes, game info and save/load state slots. No breakpoints, registers,
  pause or frame capture. PCSX2 serves one PINE client at a time, so a second client (a script of your own) blocks
  while the MCP server is connected.
- **Input and hotkeys.** Keyboard events sent to the PCSX2 window (Win32 `keybd_event` after `SetForegroundWindow`)
  reach both the pad bindings in `[Pad1]` (Return = Start, K = Cross, arrows = D-pad, WASD = left stick) and the
  hotkeys in `[Hotkeys]`: **Space pauses**, F8 saves a screenshot to `snaps/` (aspect-corrected, 1240 × 930 for the
  game's 640 × 448), F4 toggles the frame limiter. Hold a pad key for about 300 ms so that a 30 Hz game sees it.
  Leave the frame limiter on: with it off, real-time waits (the legal screen's 5 s) pass in a blink.
- **A chosen analog stick value.** Keyboard keys bound to a stick only give full deflection (raw byte 0
  or 255), but the gait thresholds need partial values. The game turns each raw stick byte into a float through a
  256-entry `float` table at `0x0050b8d0` (entry 0 is −1.0, entry 255 is +1.0); it is data, so a PINE write takes
  effect at once. To give the left stick a magnitude *m* straight up: write −*m* to entry 0 (`0x0050b8d0`) and +*m*
  to entry 255 (`0x0050b8d0 + 0x3fc`), then hold W; the pad record (`0x005dd810`; `+0x08` / `+0x0c` the stick as
  floats, `+0x00` / `+0x04` the same turned by the camera) shows the value the game uses. Write −1.0 / +1.0 back
  afterwards. The camera's right-stick code (`0x00129050`) reads the raw bytes, so the table does not change it.
  State the magnitude used with every runtime claim that depends on input ("stick magnitude 0.5, straight up").
- **Catching a moment.** Load a state, press Space to pause, make the memory writes, then send Space followed by a
  run of F8 presses (one a second, or faster) and look at the screenshots afterwards. Data drawn in one frame
  usually survives in memory until reused: a sprite batch's arrays keep the last frame's sprites after its count is
  reset, which is how the draw-order keys and colours were read.
- **Patching code to see something.** A code write lands before the recompiler first compiles the block if the
  function has not run yet; the legal screen's placement was measured by patching its clear colour to blue and
  halving its size factors, and the start-up movies were skipped by making their skip check (`0x0042a820`) return 1.
  Say on the page which patch a claim depends on.
- **Hygiene.** Save states, screenshots and logs contain game data: keep them in your scratch folder (move the
  `.p2s` files out of `pcsx2/sstates/` afterwards) and close PCSX2 when done. Screenshots are measured, never
  committed; a claim quotes the numbers.

## Writing up a finding

A finding is done when someone else can use it without asking you. For each one:

1. **Update the living page.** Find the page for the subsystem or format and change it in place; create a page only
   when none covers the subject. Don't start a dated notes page: one page per subject means the reader never has to
   reconcile two versions.
2. **Grade every claim** with an [evidence level](#evidence-levels).
3. **Add or update the YAML entries** for the functions, globals and types the finding touches (once the database exists),
   with the same evidence level as the page.
4. **Cite addresses** as `0x001490b8`: eight hex digits, in the NTSC-U executable `SLUS_212.15` (SHA1
   `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Every address in the project refers to that one file, so a reader
   with the same disc can check any claim.
5. **Describe, don't reproduce.** Explain behaviour in prose, with short pseudocode or a short snippet where it
   makes the point clearer. Never paste a whole function, decompiled or disassembled, and never paste a long
   stretch of one: it puts the original's code in the repository and hands the implementer something to copy. See
   [LEGAL.md](repo:LEGAL.md#research-limits).
6. **No game data.** Tables of offsets, constants and counts are facts and belong on the page; the bytes of a game
   file, a dump or an extracted asset never do.
7. **Build the docs** with `mkdocs build --strict` (see [Writing these docs](writing-docs.md)), which catches broken
   links and pages missing from the navigation.
