# Coney Studio: mission mods

The design of how a mod changes one of the game's own scripts, a mission's or any other: Studio decompiles the
script on the user's machine, the user edits it like any script, and the mod carries only the differences, which each
player's Coney applies to the script from their own disc, compiles and runs in the original's place. It covers how
well the decompiler does today, the patch format and the hash it is pinned to, two mods patching one script, the
compiler, the translator from Lua 4.0 to Lua 5.4 with an evidence-based verdict on it, the proof by trace equality,
Studio's flow and the order of the work, and the story graph through which a mod adds missions to the main story
without a patch. It ends with the build issues it proposes, in order. The parent issue is [#1054](issue:1054); this
page is [#1074](issue:1074), and its story graph [#1178](issue:1178).

**Status: design, two build issues built.** The first [build issue](#build-issues), the frozen decompile form, is
built, and so is build issue 4: the `.cpatch` format, applying, making and merging patches and the
`coney --script-patch` command line, described in [Script patches](../script-patches.md). The rest of this page is a
design in the sense of [Page kinds](../writing-docs.md#page-kinds): what is built moves into a guide. It builds on
[Coney Studio: the Lua SDK](lua-sdk.md) (the two runtimes, the [compiler](lua-sdk.md#compiler)), on [Lua 4.0
bytecode](../../research/formats/lua-bytecode.md) (the decompiler) and on the script security and mods design of
[#1062](issue:1062) ([Script security](security.md) and [Mods, packages and loading](mods.md)), whose `game-scripts`
permission this page's patches run under.

A maintainer decided the frame:

- **Decompile and edit directly.** Studio decompiles a script's Lua 4.0 bytecode on the user's machine, never hosted
  upstream, and the user edits it like any script.
- **Share only the differences**, as a ROM hack does. Each player's Coney applies them to the script from their own
  disc, compiles it and runs it in the script's place. No game script ever ships in a `.cmod`.
- **Pin to the script, not to a disc.** Coney installs from the PS2 and the Xbox discs, and a mission mod is tied to
  neither ([Which scripts a patch fits](#which-scripts)).
- **Lua 5.4 everywhere is preferred**, so that a user does not write new scripts in 5.4 and edit missions in 4.0; if a
  translator from 4.0 to 5.4 is too much trouble, 4.0 stays. **4.0 patching comes first**, and this page gives the
  verdict on 5.4 that the maintainer decides from ([The verdict](lua54-translator.md#verdict)).

## Contents {#contents}

1. [Terms](#terms).
2. [The decompiler today](#decompiler-today): the round trip on every script, and what is missing for mods.
3. [The pipeline](#pipeline): from the disc's chunk to the script that runs.
4. [Patches](#patches): the format, the hashes, which scripts a patch fits, refusals, two mods on one script.
5. [Compiling and running a patched script](#compiling).
6. [Lua 5.4: the translator](#translator): the syntax, the numbers and the other semantics, where a translated
   script runs, how it is proved, and [the verdict](lua54-translator.md#verdict).
7. [Studio's flow](#studio-flow).
8. [The story graph](#story-graph): the graph, the `[story]` section, anchors, merging, the Mods screen's story view,
   progress and saves, the test, and the questions for the user.
9. [The order of the work](#order), [What changes on other pages](#other-pages),
   [Maintainer decisions](#decisions), [Open questions](#open-questions).
10. [Proposed build issues](#build-issues), [How the numbers were found](#how-checked).

## Terms {#terms}

| Term | Meaning |
| --- | --- |
| **Game script** | One of the compiled Lua 4.0 chunks of the game's WAD: 467 on the PS2 disc, 503 on the Xbox ([Compiled Lua chunks](../../research/formats/lua-chunks.md)). Mission and level scripts, `global.lua`, the preloads, the menus and the language files are all game scripts |
| **Chunk hash** | The SHA-256 of a game script's bytes as the WAD stores them. It names the script's content, whichever disc it came from |
| **Base text** | The text a patch applies to: the decompile of a chunk in a fixed **form** (for 4.0), or that decompile translated to 5.4 in a fixed form (for 5.4). It is made on the user's machine and never leaves it |
| **Form** | A numbered version of the decompiler's output (or of the translator's). The same chunk in the same form always gives the same text, byte for byte ([Forms](#forms)) |
| **Edited copy** | The user's edited base text, kept in Studio's local cache (`.studio/`), never in the project's shared files and never in a package ([What is never in a project](project-formats.md#never)) |
| **Patch** | A `.cpatch` file (working name): the differences between a base text and an edited copy, holding only the author's own text and references to the base ([The format](#format)) |
| **Game state** | The Lua 4.0 state in which a level's game scripts run ([The two states](lua-sdk.md#two-states)) |
| **Trace** | The native calls a level's scripts make, step by step, with their arguments and results, and the engine's calls into the scripts by name ([Proof by trace equality](lua54-translator.md#proof)) |

## The decompiler today {#decompiler-today}

`coney-tools lua decompile --all --roundtrip` on the NTSC-U disc, run for this design (counts only; the command and its
output: [`lua`](../coney-tools-fidelity.md#lua)):

| Measure | Result |
| --- | --- |
| Chunks decompiled | 467 of 467 |
| Functions | 11,448, of which 0 could not be read |
| Round trip: compiled again with a stock Lua 4.0.1 `luac -s` and compared function by function (instructions, constants, parameters, vararg flag, stack size, nested functions) | 467 of 467 give the same code; 0 refused by `luac`, 0 compile to other code |
| Distinctive lines fingerprinted for the guard | 79,217 |
| Time | about 3 minutes |

So every game script on the PS2 disc decompiles to Lua that compiles back to exactly the original's code, as
[Lua 4.0 bytecode: round-trip results](../../research/formats/lua-bytecode.md#round-trip) also records. Studio can offer
every one of them for editing; it still checks each script on the user's machine before offering it
([Studio's flow](#studio-flow)), so a script from another disc that does not round-trip is never offered.

What is missing before a mod can patch a script:

- **A decompiler on the player's machine.** The decompiler is the `lua4` Python package under `python/`, a research
  tool. Coney and Studio are C++ programs that run without Python, and every player's Coney must make the same base
  text the author's Studio made. So the decompiler needs a C++ port that gives byte-identical output, and its output
  must be frozen ([Forms](#forms)).
- **Coney's compiler on edited code.** Coney compiles with its own Lua 4.0 compiler
  ([Coney's compiler](../../research/formats/lua-bytecode.md#compiler)), and on every unedited decompile it gives
  `luac`'s code: the round trip with `coney_luac` matches all 467 chunks
  ([Round-trip results](../../research/formats/lua-bytecode.md#round-trip)). Code a user writes is checked only as far
  as the fixtures and tests reach, so a construct that neither a disc script nor a fixture uses may still compile
  differently from `luac`; comparing such code against a stock `luac` is open.
- **What stripping removed.** Every chunk is stripped: local names are made up (`p1`, `l1`), there are no comments,
  and only function statements keep their lines
  ([What cannot come back](../../research/formats/lua-bytecode.md#what-cannot-come-back)). A user edits that text as it
  is; a rename the user makes is part of the edit and of the patch.
- **111 unnamed scripts.** Their WAD names are not recovered, so Studio shows them by the entry's hash
  ([Which scripts the unnamed chunks are](../../research/formats/lua-bytecode.md#q-unnamed-chunks)). A patch names its
  script by the entry's hash and its content by the chunk hash, so a missing name only makes it harder to find.

## The pipeline {#pipeline}

```mermaid
---
config:
  layout: elk
---
flowchart TB
  subgraph author ["The author's machine (Studio)"]
    a1["Chunk from the disc"] --> a2["Decompile (form n)"]
    a2 --> a3["Base text"]
    a3 --> a4["Edited copy (local)"]
    a3 --> a5["Diff: the patch"]
    a4 --> a5
  end
  subgraph player ["Each player's machine (Coney)"]
    p1["Chunk from the disc"] --> p2["Chunk hash = the patch's?"]
    p2 --> p3["Decompile (form n)"]
    p3 --> p4["Apply the patch;<br/>result hash = the patch's?"]
    p4 --> p5["Compile (Coney's<br/>Lua 4.0 compiler)"]
    p5 --> p6["Run in the game state,<br/>in the script's place"]
  end
  a5 -- ".cmod" --> p4
```

*Figure: a mission mod from the author's edit to the player's game; only the patch travels.*

## Patches {#patches}

### Why not a plain diff {#why-not-diff}

A unified diff (`diff -u`, `git diff`) carries the lines it removes and three lines of context around each change.
Those are lines of the game's script: a package holding them would ship game script text, which
[LEGAL.md](repo:LEGAL.md#no-game-data) and the maintainer's decision forbid. ROM hack formats solve the same problem:
BPS, for one, writes commands that copy from the source file the player already has and carries only the bytes the
author wrote. A Coney patch does the same for lines of text.

### The format {#format}

The format is built, with apply, make and merge: [Script patches](../script-patches.md) is its description and its
command line. A `.cpatch` is UTF-8 text, a header naming the chunk, the form and the hashes of the base text and the
result, then operations in base-line order that keep, drop, copy and rebuild base lines by number and add the author's
own lines. It keeps to four rules:

- **Only the author's text** appears in it. Everything taken from the base is a line range or a column range, so the
  patch reads as nothing without the player's own script.
- **Hashes, never text, for checking.** A dropped range carries its hash, so a patch checks every line it removes, and
  the whole result is checked against `result`.
- **Studio writes the smallest patch that keeps to that rule** ([Making a patch](../script-patches.md#make)): a line
  diff, in which an added line equal to a base line becomes a copy and a changed line that keeps most of a base line is
  rebuilt from it, so moving a function or changing one number carries none of the game's text. Before Export, Studio
  runs the guard's fingerprint check ([`lua`: the guard](../coney-tools-fidelity.md#lua)) on every file of the package,
  the patches included, and refuses to export one that holds three of five distinctive lines of a game script.
- **Line numbers are exact, never fuzzy.** The base text is the same byte for byte on every machine ([Forms](#forms)),
  so no operation needs context to find its place, and a patch that does not fit is refused, never applied
  approximately.

### Forms {#forms}

The base text must come out the same on the author's machine and on every player's, years apart. So the decompiler's
output is frozen in numbered forms:

- **Form 1 is today's output, and it is frozen** (built: [The frozen form](../coney-tools-fidelity.md#lua-form)).
  `coney-tools lua decompile --all` writes the SHA-256 of every script's decompile, and the list of 467 hashes (36 more
  from an Xbox disc, later) is committed in `research/lua-decompile-hashes.toml`: hashes of game text are allowed, the
  text is not ([LEGAL.md](repo:LEGAL.md#no-game-data)). The C++ decompiler must give the same hash for every script,
  checked by a disc test. The Python decompiler's check runs where the disc is (CI has none), so a change to its output
  is caught by the author's `lua decompile --all`, which fails naming the scripts.
- **A change to the output is a new form.** A decompiler fix that changes the text of any disc script makes form 2;
  Coney keeps producing every form a released patch names. With 0 unread functions and a full round trip today, there is
  little reason to change the output once patches ship.
- **The translator's output is frozen the same way** ([Translation is deterministic](lua54-translator.md#deterministic)).

### Which scripts a patch fits {#which-scripts}

A patch is pinned to the **chunk hash**, the content of the script, never to a disc.

- **PS2 and Xbox.** All 467 scripts of the PS2 disc are byte-identical on the Xbox disc, by SHA-1
  ([Xbox assets: Kinds of entry](../../research/xbox-assets.md#kinds-of-entry)). So a patch made from either disc
  applies to the other, unchanged: the chunk hash, the base text and the result are the same.
- **The Xbox's 36 scripts of its own.** A patch of one of them fits only an install that has it. On a PS2 install that
  script does not exist and nothing runs it, so the patch is **skipped** with a notice in the mod's details ("patch for
  an Xbox script; not used with this disc"), and the rest of the mod starts.
- **Other regions** (PAL and the rest) have not been compared. Each script is decided by its own hash: one that is
  byte-identical takes the patch, one that differs refuses it ([Refusals](#refusals)). A one-line disc check (the form
  hash list from that disc) will tell which scripts a region shares; it is an [open question](#open-questions) until
  someone runs it.
- **A patch for several bases.** An author who has two versions of a script (a region whose script differs) can put one
  section per base in the same `.cpatch`; Coney uses the section whose chunk hash matches.

### Refusals {#refusals}

Each check happens when a mod starts, before any of its scripts runs, so a mod never runs half-patched:

| Case | What the player sees |
| --- | --- |
| The script exists, with another chunk hash | The mod does not start: "*mod* changes `level99.lua` as it is on the PS2 and Xbox discs; the `level99.lua` of your disc is different, so this mod cannot run." The other mods start |
| The script does not exist on this install | The patch is skipped with a notice; the mod starts (above) |
| The patch names a form this Coney cannot produce | The mod does not start: "*mod* needs a newer Coney" |
| A dropped range's hash or the result hash differs | The mod does not start: the patch is damaged |
| The patched script does not compile | The mod does not start, with the compiler's message and line (only possible after a merge, [below](#two-mods)) |

### Two mods patching one script {#two-mods}

They are merged when their changes do not overlap, as `git merge` does, and the player is asked only when they really
overlap. All patches of one script share one base text (a 4.0 patch meeting a 5.4 one is lifted first,
[Translation is deterministic](lua54-translator.md#deterministic)), so their operations are compared on the same base lines:

- **No overlap: merged, without a question.** Changes to different base lines are applied together; two ranges that
  touch but share no line also merge, since exact line numbers leave no doubt where each goes. An identical change made
  by both is applied once.
- **A real overlap: the player chooses.** Two changes to the same base line, or two different insertions at the same
  place, are a conflict. The [Mods screen](ui.md#mods-screen) shows the two mods and the script, and the player keeps one
  mod's change for that place or disables one mod. The choice is recorded in `mods/enabled.toml` with both package
  hashes, so it is asked once, and again only when either mod changes.
- **Three or more** merge one at a time, in load order.

- **The merged script must compile.** Two changes that merge as text can still break each other (both define the same
  function); the result is compiled at once and, if it fails, the two mods cannot run together, and the message names
  both. Two changes that compile together but disagree in what they do cannot be detected, as for any two mods'
  scripts.

The merge itself is built ([Merging patches](../script-patches.md#merge)): it reports each conflict and keeps the side
a chooser picks. The Mods screen's question, the recorded choice and the compile of the merged script are build issue 5.

This replaces the mods page's earlier rule that two mods overriding one game script cannot both be enabled
([What changes on other pages](#other-pages)).

## Compiling and running a patched script {#compiling}

- **Compiled by Coney's Lua 4.0 compiler** ([Coney's compiler](../../research/formats/lua-bytecode.md#compiler), the one
  the [Lua SDK](lua-sdk.md#compiler) uses). This page adds one gate to it: **for every unedited decompile, Coney's
  compiler must produce the same functions as the disc's chunk**, compared as `--roundtrip` compares them (instructions,
  constants, parameters, vararg flag, stack size, nested functions). Lua 4.0's code generator is a fixed set of patterns
  ([The code generator's patterns](../../research/formats/lua-bytecode.md#patterns)), so a compiler written from them
  can match it. With that gate, a patched script differs from the original in exactly the lines the patch changed, and
  nowhere else. Coney's compiler keeps line information, so an error in a patched script names the line of the edited
  copy.
- **Run in the script's place.** The game state asks the script catalog for every script by name
  ([Three parts](../asset-and-script-architecture.md#script-parts)): the level entry (`global.lua`, then the level's
  script), `doFile` and `preLoadFile`. A patched script is a catalog provider that returns the compiled program instead
  of the disc's chunk, so it runs at the same moment, in the same state, with the same natives, schedule and handlers
  as the original would ([Life of the Lua state](../../research/scripting.md#life-of-the-lua-state)). Nothing else in
  the level changes.
- **Prepared once, when the mods start**: every patch applied, merged and compiled before the first level, so a broken
  patch is reported at start, never in the middle of a mission. The compiled programs are kept for the session.
- **A modded session.** A session with any patched script is modded: saves go to the modded folder and the run log
  says so, as for any mod ([Modded saves and replays](mods.md#saves-replays)). Patching game scripts is the
  `game-scripts` permission of [Script security](security.md#declared), which the player approves; Studio writes it into
  the manifest from the patches the project holds.
- **No bytecode from a mod.** A patch is applied to the player's own chunk and compiled from source by Coney; no mod
  ever supplies bytecode, which keeps [Script security](security.md#surface)'s rule that the game state runs only the
  player's own chunks and Coney's compiler's output.

## Lua 5.4: the translator {#translator}

Whether a game script's Lua 4.0 base text can be translated to Lua 5.4, so that a mission is edited and run in 5.4:
what the disc's scripts use, the syntax, numbers, booleans and iteration order, where a translated mission runs, the
proof by trace equality, the probe and the verdict, is [Lua 5.4, the translator](lua54-translator.md).

## Studio's flow {#studio-flow}

Studio's panels and editor are the [editor experience](editor-experience.md#panel-list) and [technology](technology.md)
pages', and the project folder and the `.cmod` are [Project formats](project-formats.md)'; this is what they do with
game scripts.

1. **Open.** The File system panel shows the install's game scripts under `game:/scripts/`
   ([Paths](content-and-formats.md#paths)), by name (the 111 unnamed by hash), with the mission each belongs to. The
   first time a script is opened, Studio checks it on this machine: decompile in the current form, compile with Coney's
   compiler, compare with the chunk. A script that does not round-trip is shown greyed with the reason and cannot be
   edited. The result is kept by chunk hash in Studio's local cache. This is the edit that [Missions and
   scripts](content-and-formats.md#missions) leaves to this page: there a disc script is shown as its functions and the
   natives they call; here it opens as source.
2. **Edit.** Opening a script for editing makes the edited copy in the project's local cache (`.studio/`, [The project
   folder](project-formats.md#formats): never exported, and listed in the `.gitignore` Studio writes, so a project
   shared through git carries no game text) and opens it in the script editor: in 4.0, or in 5.4 when the level's 5.4
   form is offered and the user chose it. A *Compare with original* view shows the edited copy beside the base text.
3. **Save.** Studio writes the edited copy, compiles it (errors in the Problems panel with their lines) and writes the
   patch into the project's shared files (`patches/<name>.cpatch`), adding the script to the manifest's `game-scripts`
   permission. Only the patch is the project's; a project opened on another machine rebuilds the edited copy from that
   machine's disc and the patch.
4. **Play.** Play applies the project's patches to the install's chunks exactly as a player's Coney does, so what the
   author tests is what players get. Play with other mods enabled shows any merge conflict as the mods menu would. A
   saved game script **restarts the level, not the project**: the game state is made per level, so a patch reaches it
   only when the level starts again ([the table below](#which-kind), [Project restart on
   save](lua-sdk.md#project-restart)).
5. **Convert to Lua 5.4.** For a level whose 5.4 form is offered: the patch is lifted ([Translation is
   deterministic](lua54-translator.md#deterministic)) and the edited copy reopens in 5.4.
6. **Export.** The `.cmod` ([The `.cmod` package](project-formats.md#cmod)) holds the patches and the manifest, as
   it holds the project's other files; Export refuses a package that holds a decompiled script,
   the guard's marker or three of five fingerprinted lines in any file ([The format](#format)).

### Which kind of script a file is {#which-kind}

The editor decides by the file's root, so there is no mode to choose:

| | A `project:/` script | A `game:/scripts/...` script |
| --- | --- | --- |
| What it is | The project's own Lua 5.4 file | A game script, Lua 4.0 (or its 5.4 form where offered), opened read-only |
| Saved as | Itself, in the project | A patch, `patches/<name>.cpatch`, from the local copy in `.studio/` that editing makes |
| Natives | The SDK's: available game natives and the `Coney` table ([How a native reaches the SDK](lua-sdk.md#availability)) | The game state's natives, as the original's scripts have them |
| Helpers | None: the project's state has no `global.lua` | `global.lua`'s functions and globals, called as bare globals as the original's scripts call them ([global.lua helpers](../../references/bindings/helpers.md)) |
| Runs | In the project's state ([The two states](lua-sdk.md#two-states)) | In the game state, in place of the original ([Compiling and running](#compiling)) |
| A save, with *On script save* on *Restart project* | Restarts the project | Restarts the level, the smallest restart it needs |
| A save, with *On script save* on *Restart level* | Restarts the level | Restarts the level |
| In the editor | Nothing extra | A *Compare with original* view and a banner, "Game script, saved as a patch" |

Patches are project files and go in the `.cmod` with the project's other files; what never does is the decompiled text
of a game script ([Export, step 6](#studio-flow)). Opening a `game:/` script and not editing it changes nothing.

## The story graph {#story-graph}

How a mod puts a mission into the main story without patching a game script: the story's order becomes Coney's own
data, seeded with exactly the original's, and a mod places its missions in it from its manifest, as a Minecraft
advancement names its parent and a Factorio technology its prerequisites. The user approved the frame
([#1178](issue:1178)). Without mods the graph is the original's order and the story plays as the PS2's.

**Why a patch is not enough.** The original keeps its order in two `global.lua` tables, `runNextMission`'s 20 rows and
`findLastMission`'s 19 levels, in the hub's `fRunMission` (one mission per chapter) and in fixed comparisons with the
hub's chapter number; progress is the profile's per-level completion bits, and the best records and the menus are
sized for the original's missions ([Story progress](../../research/story-progress.md#readers) lists every reader). A
mission added by patches would need a patch of `global.lua` and of the hub, a completion bit it does not have, and two
story mods would meet as overlapping text in the same three tables.

### The graph {#graph}

The story is a **chain of steps**, each either a **mission** (a level entered at checkpoint 1) or a **hub chapter**
(`level95` at a checkpoint). The original's chain, from [Story progress: the order](../../research/story-progress.md#order):

`level99`, `level80`, `level87`, `hub:1`, `level34`, `hub:2`, `level2`, `hub:3`, `level3`, `hub:4`, `level5`, `hub:5`,
`level81`, `hub:6`, `level86`, `level93`, `hub:7`, `level31`, `hub:8`, `level14`, `hub:9`, `level9`, `hub:10`,
`level51`, `level52`, `level54`, `level55`, `level84`, `hub:11`, `level64`, `hub:12`.

- **Ids.** An original mission's id is its level name (`level80`); a hub chapter's is `hub:<n>`; a mod's mission is
  `<mod>.<id>` (`yard-brawl.alley`), so two mods never share an id.
- **How a step is entered.** A mission after a mission is loaded by `runNextMission`; a mission after a hub chapter is
  the chapter's offer (its last action loads it); a hub chapter after a mission is loaded by `runNextMission` at its
  checkpoint. `level64` is a **side step**: it is reached through the Armies stages, never from `hub:11`, but it sits in
  the chain so that it counts as later than `level84`, as `findLastMission` has it.
- **What the chain gives.** The three tables the scripts read are views of it: `runNextMission`'s rows (each mission to
  the step after it, plus 0 to the first), `findLastMission`'s list (the missions, last first) and `fRunMission` (each
  chapter to the mission after it). The seed is a table of level numbers and chapter numbers in Coney's code, our own
  data ([LEGAL.md](repo:LEGAL.md#no-game-data)).
- **Who owns it.** The graph is session state of the story game mode, built once when the session's mods start and
  fixed until they stop; every machine of a co-op session builds it from the same mods in the same order, and its hash
  joins the mod list a session checks ([Coney's model](mods.md#mp-model)).

### The natives that read it {#graph-natives}

Two natives take the names of the two `global.lua` functions, and Coney sets them over the script's own after
`global.lua`'s main chunk runs at each level load. Each is `@coney-only` and names the Lua function it replaces
([Coney-only code](../conventions.md#coney-only)); the two Lua callers, `UnlockAndLoad` and
`MissionCompleteUnlocks`, stay the game's and reach them by name, as do the front end, the Rumble arenas and the four
native menus ([Who calls them](../../research/story-progress.md#callers)).

- **`findLastMission()`** walks the chain's missions from the end and returns the first that is complete: an original
  mission by its completion record, as the original asks, a mod's by its id ([Progress](#graph-progress)). It returns a
  level number, as the original does, since `LastLevel` holds it.
- **`runNextMission(arg)`** looks up the step after `LastLevel` (setting it from `findLastMission()` when it is 0 or nil)
  and does what the original's row does: `SetCheckPoint`, stop the music, `MenuLoadLevel`, and the mission-complete
  launch when `arg` is 1. With no step after it (the original's missing row) it loads nothing, as the original's call
  of a name no binding has does.
- **The hub's offer.** When the hub loads, Coney writes the chain's offer for the chapter into `fRunMission[checkpoint]`
  after `level95.lua`'s main chunk, so `story.RunMissionAction` loads it unchanged; for the original chapters the row
  written is the one already there.
- **Mod levels get numbers the scripts accept.** The scripts pass levels as numbers (`Level`, `LastLevel`,
  `MenuLoadLevel("level" .. n)`, `UM_IsLevelComplete(n)`), so each mod mission gets a level number for the session and
  a level record under the name `level<n>` that loads the mod's level. The number has to pass the scripts' range checks
  as a story mission's does ([Story progress: every reader](../../research/story-progress.md#readers)): `GetLevel`
  (`global.lua:f64`) turns 0 and 300 or more into 1, so a mission numbered there would run, complete and be found as
  level 1; `UnlockAndLoad` and `SetupLevelInventory` (`f59`, `f150`) bank the players' assets and set the second
  player's inventory only below 100; 60-69 is an Armies stage to the engine; the mission menu lists only 2-99 but 95
  and 60-65. So the numbers come from a **pool**: the numbers 2-59 that no level record uses, taken from the records
  `CfgLevels` made, given in chain order. On the NTSC-U disc that is 45 numbers (4, 6-8, 10, 12, 13, 15-19, 21-30, 32,
  33, 35-50, 53, 56-59). A story mission past the pool is refused with a message, as a missing anchor is. Coney
  overrides no game script function for this, `GetLevel` included: the number is chosen to pass the checks as they
  are. Every lookup keyed by a number (the unlock records, the inventory table `MS`, the tag offsets) has no row for a
  free number and takes the game's own default. The number is never saved: saves, replays and messages use the id.

### The manifest's `[story]` section {#story-manifest}

```toml
[[story.missions]]
id     = "alley"                   # becomes "yard-brawl.alley"
title  = "Alley Cats"              # what the menus show
level  = "levels/alley.clevel"
script = "missions/alley.mission"  # optional: a level may carry its own scripts
after  = "level80"                 # the anchor: an original mission, a hub chapter or another mod's mission
before = "level87"                 # optional guard: refused if the step after the anchor is not this one
area   = "Coney"                   # optional: the mission menu's area; the anchor's area when left out
label  = 2                         # optional: the number shown and sorted on; the anchor's when left out

[[story.missions]]
id      = "docks"
level   = "levels/docks.clevel"
after   = "hub:6"                  # chapter 6 offers it, and a new chapter follows it
chapter = "hub/docks-return.lua"   # optional: the new chapter's own actions

[[story.missions]]
id      = "payin"
level   = "levels/payin.clevel"
replace = "level86"                # takes level86's place in the chain
```

A mission names exactly one of `after` and `replace`. A project with a `[story]` section is a story mod: while it is
enabled every story game is a modded session ([Modded saves and replays](mods.md#saves-replays)), whatever its
`autostart`, and its scripts run when its missions' levels do.

**Built** ([#1301](issue:1301)): `story::parseStorySection` (`repo:src/core/story_manifest.h`) reads the section from a
manifest's whole text and skips every other table, so the rest of the manifest keeps its own reader. A `[story]`
table alone is a story mod with no missions. A manifest is refused, with its line or its mission named, for a broken
line, `[story.missions]` written as a plain table, a mission without a valid id (1-64 lower case letters, digits and
'-'), an id used twice in the mod, a mission without a `level`, one naming both or neither of `after` and `replace`,
and a `chapter` without an `after = "hub:<n>"`. Anchors and guards are written as story ids: `level80`, `hub:6`, or a
mod's mission as `<mod>.<id>`, the mod's own included.

### Anchors {#anchors}

| Anchor | Where the mission goes | What follows it |
| --- | --- | --- |
| `after = "<mission>"` | Right after that mission: `runNextMission` loads it where it loaded the next step | The step that followed the anchor |
| `after = "hub:<n>"` | Chapter *n*'s offer: the chapter's last action loads it | A **new hub chapter**, `hub:<n>.<mod>.<id>`, which offers what chapter *n* offered |
| `replace = "<mission>"` | In that mission's place, entered the same way | The same step as before; the original mission no longer plays in the story |

- **A new hub chapter** loads the hub at chapter *n*'s checkpoint, so the hub's own scripts see the story's time they
  expect: the chapter number is also the clubhouse's state, the chatter, the streets' collectables, spawners and cops
  ([The hub](../../research/story-progress.md#hub)), and a new chapter is a point in that time, not a thirteenth one.
  It runs the mod's `chapter` script when the mod has one; without one, Coney's default chapter sets chapter *n*'s
  crew and environment and puts a red circle at the next mission's giver, whose action loads it, with no cut-scene.
- **Anchoring on a replaced mission** places the mission after its replacement, since the anchor names a place in the
  story.
- **Hub chapters 11 and 12** offer nothing in the original; a mission after either is a mission after the story's end.
- **Only a mission is replaced.** A `replace` naming a hub chapter is refused ("aaa.x: 'hub:3' is a hub chapter, not
  a mission"); a replaced side step (`level64`) leaves its replacement a side step.

### Hub chapters that mod missions open {#hub-chapters}

A new chapter shares chapter *n*'s checkpoint, and the hub tells the two apart by the story's progress. Which chapter
the hub plays, what it offers, Coney's default chapter and a mod's `chapter` script, built ([#1302](issue:1302)), are
[Hub chapters that mod missions open](hub-chapters.md).

### Merging and refusals {#graph-merge}

The graph is built when the mods start, before any script runs, from the seed and each story mod's missions, mods in
load order ([Load order](mods.md#load-order)) and each mod's missions in manifest order:

- **Two missions on one anchor** go in load order: the first placed nearest the anchor, the next after it and after
  everything placed after the first (its new hub chapter, a mission anchored on it), so on a hub chapter the first
  mission's new chapter offers the second. The Mods screen can swap them, and the swap is kept in `enabled.toml` with
  both package hashes, as a script merge choice is ([Two mods patching one script](#two-mods)).
- **An anchor not placed yet** (a mission of a mod later in load order, or later in the same manifest) makes the
  mission wait: after each mission placed or refused, the earliest waiting one takes its turn again, so it is placed as
  soon as its anchor is. What still waits when no turn changes anything is refused, by the next three rules.
- **A missing anchor** (an id no step has, a hub chapter outside 1-12, a mission of a mod that is off or refused)
  refuses that mission with a message naming the mod, the mission and the anchor ("yard-brawl.alley: no story step
  'level70'"); the rest of the story plays. A mission anchored on a refused one is refused the same way
  ("yard-brawl.roof: story step 'yard-brawl.alley' was refused").
- **A `before` guard** refuses the mission when, at its turn in the merge, the step after its anchor is not the one
  named: another mod has moved in, and the author asked to be told ("bbb.y: expected 'level87' after 'level80', found
  'aaa.x'"). A guard naming a replaced mission means its replacement.
- **A cycle** (`a` after `b`, `b` after `a`) refuses every mission in it, with one message naming the cycle ("story
  cycle: aaa.x, bbb.y, aaa.x"), as a dependency cycle does ([Dependencies](mods.md#dependencies)).
- **Two replacements of one mission**: the first in load order takes the place and the other is refused ("bbb.y:
  'level86' is already replaced by 'aaa.x'"), until the player chooses on the Mods screen ([#1304](issue:1304), built:
  [the story view](#graph-mods-screen)).
- **A mission whose level or script is missing** from the package is refused at the mod's check, before the merge.

Without story mods the merge is the seed and nothing else: no question is asked and nothing is written, and the
graph's hash is the seed's.

**Built** ([#1301](issue:1301)): `story::mergeStory` (`repo:src/core/story_merge.h`) takes the seed and the enabled
story mods in load order and gives the merged graph, the missions placed (each with its manifest entry, for the
readers below), the refusals with their messages, and whether the session is modded: any story mod given makes it so,
even one whose missions were all refused. A mod's step carries its story id in `StoryStep::modId`; its level number is
0 until the natives give it one from the pool ([#1300](issue:1300)), and the new hub chapters play as
[Hub chapters that mod missions open](#hub-chapters) describes. Nothing calls the merge yet: Coney does not start
mods yet, and the story session holds the seed. `repo:tests/core/story_merge_test.cpp` and
`repo:tests/core/story_manifest_test.cpp` check each rule above on synthetic manifests.

### The Mods screen's story view {#graph-mods-screen}

The [Mods screen](ui.md#mods-screen) gains a **Story** list when any enabled mod has a `[story]` section: the merged
chain in order, original steps in the game's colours and each mod's missions under its title, with three marks:

- **Shared anchor**: two or more missions placed at one anchor, with *Swap* to change their order.
- **Replaced**: an original mission another mission stands in for, and, for two replacements, the choice between them.
- **Refused**: each refused mission with its message, so a player sees why a mission is missing without a log.

**Built** ([#1304](issue:1304)): the list's model, with the Mods screen itself still to come (build issue 3 of
[UI and menus](ui.md#build-issues): the MODS entry and the screen that draws it). `gui::ModsStoryList`
(`repo:src/gui/mods_story_list.h`) takes the seed, the enabled story mods in load order and the path of `enabled.toml`,
and is driven by the menu's own commands (`gui::MenuCommand`, the pad through `MenuInput`): a direction moves over the
rows the cursor can stop on (the mod titles and the *Refused* heading are not among them), accept does the row's action
and back closes it. Its rows are `story::buildStoryView`'s (`repo:src/core/story_view.h`): each step of the chain, a
title row before each run of one mod's missions, the new hub chapter a mission opens under its mission, and, after the
chain, the heading and one row for each refused mission with the merge's message. The screen draws the kinds in the
game's colours (an original step), the mod's (a mission) and the warning colour (a refusal).

- **Swap.** On a mission placed on a shared anchor, accept swaps it with the next mission placed there (the last swaps
  with the one before it). With three on one anchor, repeated swaps reach every order.
- **Choose.** On a refused replacement that lost to another, accept makes it the one that takes the place, and the one
  that had it is refused in turn.
- **Kept.** Both are one *choice*: the order of the group's missions, for the group of one anchor (`kind = "after"`) or
  of one replaced mission (`kind = "replace"`). It goes into `mods/enabled.toml` at once, with the package hash of each
  mission's mod:

  ```toml
  [[story.choice]]
  kind = "after"
  anchor = "level80"
  missions = ["bbb.y", "aaa.x"]      # the order the player chose, the first nearest the anchor
  packages = ["<hash of bbb>", "<hash of aaa>"]
  ```

  `story::loadStoryChoices` and `saveStoryChoices` (`repo:src/core/story_choices.h`) read and write only these tables and
  leave the rest of the file as the player and the mods menu keep it; the choice tables themselves are regenerated on
  every save (a comment or a key of the player's own inside one is not kept) and take the file's line endings.
  `mergeStory` takes the choices as its third argument: the missions of each group they apply to keep their slots in
  the turn order and take them in the chosen order, so the chain, the placed missions and the refusals all follow from
  the one rule above.
- **When a choice stops holding.** A choice applies only while its group is the same missions and every mod it names has
  the package hash it records, as a [script merge choice](#two-mods) does, and names each of the group's missions once
  (a hand-edited table that repeats one is refused). A group that changed, or a mod that was updated, falls back to the
  load order and the merge marks it `StoryGroup::stale` in `merge().groups` (the story rows do not carry it; the screen
  reads it there to say the choice no longer holds); the old table stays in the file until the player chooses for that
  group. A choice that puts the group back in load order is dropped from the file.
- **A file that cannot be written** leaves the choice in force for the session and shows the message. One that cannot
  be read, or holds a broken `[[story.choice]]` table, gives the load order and the message, and is never written over:
  a swap or a choice holds for the session only and the message says the file was left as it is, so the tables the
  player wrote (the valid ones too) are not lost until they fix the file.

### Progress, records and saves {#graph-progress}

- **Original missions keep the original's records.** Their completion is the `(level, 0, 0)` record and their rank
  records follow `MissionCompleteUnlocks`, as without mods; their best records keep their 30 slots.
- **A mod mission's progress is keyed by its id.** `MissionCompleteUnlocks` runs for it as for any level; Coney maps
  `UM_Unlock` and `UM_IsLevelComplete` on a mod level's number to the save's **story section** of the new save format
  ([#1179](issue:1179)), which keeps, per id: complete or not, the rank records, and a best record with the original's
  fields (score, total time, grade, percentage). Nothing of a mod mission goes into the 640 unlock bits or the 30 slots.
- **What the original forgets stays forgotten.** No checkpoint within a mission is saved, so a mod mission that was
  quit starts again at checkpoint 1 through the chain, as an original one does. A mod that wants to resume at a
  checkpoint saves it itself with `Coney.SaveData` and reads it when its level starts.
- **Where it is saved.** A story with mod missions is a modded session, so its saves go to `modded/` with the mod list
  ([Modded saves and replays](mods.md#saves-replays)). A save loaded without one of its story mods keeps that mod's
  section (#1179 keeps a section a session does not know), and `findLastMission` skips ids the chain lacks, so the story
  goes on from the latest mission present; the warning names the missing mod.

### The other readers {#graph-readers}

Each reader in [Story progress: every reader](../../research/story-progress.md#readers) is met:

| Reader | With the graph |
| --- | --- |
| `runNextMission`, `findLastMission` | The natives above |
| `fRunMission`, read by the chapter's last action and the "new" camera | The offer written into the row at hub load; a mod mission's giver is the flag of the chapter's own offer |
| The chapter number's fixed comparisons | A new chapter keeps chapter *n*'s number |
| The mission menu: completed levels by area, sorted by label | A completed mod mission is listed in its `area`, sorted by its `label`; it needs the ported screen ([#1071](issue:1071)) |
| The statistics list, by mission byte | Mod missions follow the original's, in chain order |
| The 30 best records, by mission byte | Mod missions' records are the story section's |
| `UM_IsLevelComplete(84)` (the menu music, the trophy zone) and the flashback requirement | Unchanged: they name original missions |

### The test that the unmodded graph is the original's {#graph-test}

Written first, before the natives:

- **The views.** The seeded graph's `runNextMission` rows equal the 20 of `global.lua:f61`, its `findLastMission` list
  the 19 of `global.lua:f62`, and its offers the 12 rows of `fRunMission`, each expected value cited from
  [Story progress](../../research/story-progress.md#global-functions).
- **The walk.** From an empty profile, the natives choose a level, the test marks it complete (a hub chapter "plays" by
  loading its offer), and the 29 steps it loads up to `hub:11` equal the chain above; with `level64` then complete,
  every later completion leads to `hub:12`.
- **Against the disc.** A disc test runs the disc's own `global.lua` in a game state with a fake profile and, for every
  prefix of the story complete (and nothing, and `level64` alone), compares the level and checkpoint the Lua
  `runNextMission` asks for with the native's; it prints the count of equal cases only.
- **Level numbers.** With synthetic level records, mod missions get the free numbers 2-59 in chain order, never one a
  record uses, 0, 1, 60-69, 95 or 100 and above; one past the pool is refused with its message. A disc test runs the
  disc's own `global.lua` main chunk with the current level set to each pool number and checks that `Level` keeps the
  number, and that `UnlockAndLoad` banks the assets and calls `runNextMission(1)`; it prints counts only.
- **Merging.** Synthetic manifests: an `after` on a mission and on a hub chapter, a replacement, two mods on one anchor
  in both load orders, a missing anchor, a failed `before`, a cycle and two replacements of one mission each give the
  chain and the messages expected; with no story mod the graph's hash equals the seed's.

### Questions decided {#graph-questions}

The four questions the design asked were settled for [#1301](issue:1301) by the coordinator under the maintainer's
grant, 2026-11-01:

- **The natives without mods.** The two natives are set in every session, mods or none: one path, proved equal to the
  original by the disc test.
- **Two replacements of one mission.** The first in load order wins and the second is refused with its message; the
  Mods screen offers the choice ([#1304](issue:1304)).
- **A new hub chapter without a script.** Coney's default chapter is the default: chapter *n*'s environment and a red
  circle at the next giver, with no cut-scene; a mod's `chapter` script is optional.
- **Mod missions on the original's screens.** Left open, outside #1301: whether completed mod missions join the
  mission menu and the statistics screen or stay at the Mods screen's `missions` point. The merge keeps what either
  needs: each placed mission's `title`, `area` and `label` (`story::PlacedMission`).

## The order of the work {#order}

1. **4.0 patching**: the frozen form and the C++ decompiler, the compiler with its round-trip gate, the patch format with
   apply and merge, patched scripts in the game state, then Studio's flow. Useful on its own, and the foundation of the
   rest.
2. **The probe**, which needs no engine work and may run as early as a maintainer wants its numbers. With its counts
   the maintainer decides between 5.4 and staying on 4.0 ([The verdict](lua54-translator.md#verdict)).
3. **If 5.4**: the translator, the 5.4 game state, the trace proof, and Studio offering each level's 5.4 form as it
   passes.

## What changes on other pages {#other-pages}

When this design is accepted:

- [The Lua SDK: the Lua 4.0 compiler](lua-sdk.md#compiler): "script overrides, a user's replacement of a disc script"
  become patches of the disc script; the compiler gains the gate that it reproduces `luac`'s code for every decompile,
  and its parser serves the translator. [The runtime](lua-sdk.md#runtime-choice): the game state may be a 5.4 state for
  a level a mod translated, if the maintainer chooses 5.4.
- [Asset and script architecture: Three parts](../asset-and-script-architecture.md#script-parts): the *mod* provider of
  the script catalog is a patched script (and, with 5.4, a translated one), not a file from `overrides/scripts/`.
- [Mods: Load order](mods.md#load-order) (done there): two mods' patches of one script merge, and the
  player chooses only on a real overlap ([Two mods](#two-mods)), instead of being refused.
- [Script security](security.md#build-issues) (done there): its build issue 12 keeps only the safety rules around this
  page's issue 5; the `.cpatch` parser joins the parsers it fuzzes.
- [UI and menus: the Mods screen](ui.md#mods-screen): the choice the player makes on a real overlap between two
  mods' patches of one script, recorded with both package hashes ([Two mods](#two-mods)).
- [Editor experience: the panels](editor-experience.md#panel-list): the File system panel lists the install's game
  scripts under `game:/scripts/`, with each script's round-trip result, for editing ([Studio's flow](#studio-flow)).
- [Project formats](project-formats.md#formats): the project folder gains `patches/` (the `.cpatch` files), which the
  [`.cmod`](project-formats.md#cmod) holds as they are.
- [Content and formats: Paths](content-and-formats.md#paths): a project names the disc it was made against, but its
  patches are pinned to each script's chunk hash, so a patch fits any install whose script matches
  ([Which scripts a patch fits](#which-scripts)). [Missions and scripts](content-and-formats.md#missions): a disc script
  can also be opened as source for a patch ([Studio's flow](#studio-flow)).
- [Lua 4.0 bytecode](../../research/formats/lua-bytecode.md#round-trip) and [`lua`](../coney-tools-fidelity.md#lua):
  the form hash list and the C++ decompiler, once built.

## Maintainer decisions {#decisions}

1. **Decided: the frame at the top of this page**: decompile and edit, share only differences, pin to the script and
   not the disc, merge two mods' patches and ask only on a real overlap, 5.4 preferred, 4.0 first.
2. **To decide after [the probe](lua54-translator.md#probe): Lua 5.4 for missions or Lua 4.0**, from
   [the verdict](lua54-translator.md#verdict) and the probe's counts.
3. **Faithful first: decided, no exception needed** ([Studio's decisions](index.md#decisions)). Studio offers a script
   for editing, and a level's 5.4 form, only once Coney has what it needs (the round trip, the compiler, the proof), and
   a patched script makes a modded session; the story without mods plays as the original.
4. **Decided: the story graph's frame** ([#1178](issue:1178)): the order as data seeded with the original's rows, read
   by two `@coney-only` natives, progress by mission id, missions placed by anchor in the manifest, load order on a
   shared anchor with a swap on the Mods screen, the original's forgetting kept, a story with mod missions a modded
   session. Its open choices are settled in [Questions decided](#graph-questions), one left open there.

## Open questions {#open-questions}

- **Other regions.** Which scripts a PAL disc (and the other regions) shares with the NTSC-U disc. Found by running
  `coney-tools lua decompile --all` on that disc and comparing the chunk hashes with the committed list.
- **One decompiler or two.** Once the C++ decompiler matches form 1, should `coney-tools lua decompile` call it and the
  Python package retire, so there is one source, or should both stay, held together by the form hash list?
- **Coney's iteration order.** If the probe shows Coney's first-set order departing from the original's hash order in
  loops that matter, Coney's 4.0 VM itself is unfaithful there; that is a fidelity question for the VM, beyond this
  page.
- **A patch of a script no level loads.** Some of the 467 may never run in the PS2 build; a patch of one changes
  nothing, and Studio could say so once the scripts each level loads are traced.

## Proposed build issues {#build-issues}

In order. Issues 1 to 6 are 4.0 patching; issue 7 is the probe; issues 8 to 11 follow only if the maintainer chooses
5.4.

1. **tools: Add the frozen decompile form and its hash list** ([#1115](issue:1115), built). `coney-tools lua decompile
   --all` writes the SHA-256 of every script's decompile beside the fingerprints and checks it against a committed list
   (`research/`, hashes only: 467 from the PS2 disc, the 36 Xbox-only ones added from an Xbox disc); a mismatch fails
   with the names of the scripts whose text changed. Tests: a fixture chunk's hash is checked; a changed renderer fails
   the check; the list holds no text.
2. **core: Add the Lua 4.0 decompiler in C++.** A port of the `lua4` package's logic into the engine (no Python at run
   time), giving byte-identical output in form 1. Tests: the committed synthetic fixtures (`python/tests/lua4/fixtures/`)
   decompile to their expected files; a disc test decompiles every chunk and compares each hash with the list, printing
   counts only.
3. **core: Add the compiler's round-trip gate.** Extends the Lua SDK's compiler issue (its issue 7): Coney's compiler
   reproduces `luac`'s code generation for the patterns the disc uses. Tests: the synthetic fixtures compile to the same
   functions as their `luac` chunks; a disc test compiles every decompile and compares it with the chunk as
   `--roundtrip` does, 467 of 467, counts only.
4. **core: Add script patches: make, apply and merge.** Built: [Script patches](../script-patches.md). The `.cpatch`
   reader and writer with its limits, apply with every hash checked, the smallest patch that carries no base text
   (copies and rebuilds), the merge of several patches of one base with conflicts reported, and the lift from 4.0 to 5.4
   left for issue 8. A `coney --script-patch make|apply` command line for modders who use another editor. Tests on
   synthetic text: apply gives the result hash; a wrong base, a damaged range and a damaged result are refused with
   their messages; a made patch holds no line of the base; two disjoint patches merge; two overlapping ones report one
   conflict; touching ranges merge.
5. **core: Run patched game scripts in the game state.** Beside [Script security](security.md#build-issues)' issue 12:
   the catalog's patched provider, patches applied, merged and compiled when the mods start; the refusals of
   [Refusals](#refusals), the Xbox-only skip, the player's recorded merge choices, the `game-scripts` permission, the
   modded session. Tests with a synthetic chunk: a patched script runs in the original's place at the level entry, by
   `doFile` and by `preLoadFile`; a mismatching chunk stops only that mod; a missing script skips the patch; a merge
   conflict without a recorded choice holds both mods.
6. **tools: Add game-script editing to Studio.** After Studio's shell and script editor: the `game:/scripts/` listing
   with the round-trip check, the edited copy in `.studio/` and the `.gitignore`, save writing the patch and the
   permission, compile errors in Problems, the compare view, Play through the player's path, and Export's guard. Tests
   with the Dear ImGui Test Engine ([Testing Studio](technology.md#testing)) on a synthetic chunk: open, edit, save
   writes a patch with none of the base's lines, Play runs the edit, Export refuses a package holding the base text.
7. **tools: Add the Lua 5.4 translation probe.** The `lua4` syntax tree written out as 5.4 with the mapping of
   [Syntax](lua54-translator.md#syntax), checked by a stock `luac` 5.4; differential runs of every script under stock
   Lua 4.0.1 and 5.4 with the same fake natives in Lua (the 4.0 interpreter built with a 32-bit `long`, as on the PS2,
   so its string hash and order are the original's); counts only: translated, parsed, functions with equal traces, loops
   whose order differs (4.0 hash, first set, 5.4), concatenations needing `lua40.str`. Its report goes to the maintainer
   for the decision. Tests: synthetic sources covering each mapping row translate and give equal traces; an
   order-dependent loop in a fixture is reported.
8. **core: Add the Lua 4.0 to 5.4 translator.** On the compiler's parser, line for line, with the `lua40` helpers, the
   boolean and number rules, the two order answers, frozen forms with their hash list, and the lift of a 4.0 patch.
   Tests: each row of the mapping on synthetic sources; the hash list on the disc; a lifted patch applies to the 5.4
   base.
9. **core: Add the Lua 5.4 game state.** A level whose scripts a mod put in 5.4 gets a 5.4 game state with the SDK's
   build settings, the game-state bridge (4.0's conventions) and the `lua40` helpers, reached by the scheduler's entries;
   every other level stays on 4.0. Tests: a synthetic level runs the same in both states; a 5.4 patch and a 4.0 patch
   of the same level run together; levels without a 5.4 patch never make a 5.4 state.
10. **tests: Add trace equality between the 4.0 and 5.4 game states.** The call tracer (a full-value trace at the native
    list), its diff, the fake host's differential runs over every script with coverage, and every playthrough test run
    in both states; the list of proved levels that Studio reads. Tests: a deliberately wrong translation (a
    concatenation without `lua40.str`) fails with the first unequal step; the differential runs report coverage.
11. **tools: Offer each proved level's Lua 5.4 form in Studio.** *Convert to Lua 5.4* for proved levels, shared scripts
    last, and 5.4 editing with the language server and debugger of projects. Tests with the Test Engine: an unproved
    level offers no conversion; a converted patch applies and plays.

### The story graph's build issues {#graph-build-issues}

Under [#1054](issue:1054), in order; the save format is [#1179](issue:1179)'s.

1. **core: Add the story graph seeded with the original's order** ([#1299](issue:1299), built:
   [In Coney](../../research/story-progress.md#coney)): the chain, its views and the walk and disc tests.
2. **core: Add native runNextMission and findLastMission over the graph** ([#1300](issue:1300)): the two natives, the
   hub's offer written into `fRunMission`, level numbers for mod missions from the pool of free numbers 2-59.
3. **core: Add the manifest's story section and the story graph merge** ([#1301](issue:1301), built:
   [Merging and refusals](#graph-merge)): `[story]`, anchors, the merge and its refusals.
4. **core: Add hub chapters that mod missions open** ([#1302](issue:1302), built:
   [Hub chapters that mod missions open](#hub-chapters)): a new chapter at chapter *n*'s time, the mod's chapter
   script or Coney's default chapter; called at the hub load with #1300.
5. **core: Add story progress by mission id to the save** ([#1303](issue:1303)): mod missions' completion, ranks and
   best records in the story section, after #1179.
6. **gameplay: Add the story view to the Mods screen** ([#1304](issue:1304), built:
   [The Mods screen's story view](#graph-mods-screen)): shared anchors with *Swap*, replacements, refusals; the model
   and the choices kept in `enabled.toml`, drawn once the Mods screen exists.

## How the numbers were found {#how-checked}

The round trip is the output of `coney-tools lua decompile --all --roundtrip` on the NTSC-U disc with the stock Lua
4.0.1 `luac` that `workspace init --fetch lua` builds. The feature counts of [What the game's scripts
use](lua54-translator.md#what-scripts-use) come from a short script, not in the repository, over the same chunks: it
loads each with `lua4.load`, walks its functions with `Proto.walk()`, decodes every instruction with
`lua4.opcodes.decode`, and counts the `GETGLOBAL` and `SETGLOBAL` instructions that name a Lua 4.0 library function or
`true` and `false`, the string constants equal to a 5.4 reserved word, and the numeric `for` loops with a `SETLOCAL` to
their control variable's slot inside the loop (that slot found by replaying the stack depth in a straight line, so the 9
is approximate; the translator finds them exactly). The values `global.lua` gives `true` and `false` were read in its
decompile. The other counts (upvalues, varargs, loops, concatenations, divisions, value comparisons) are from [Lua 4.0
bytecode](../../research/formats/lua-bytecode.md#the-instruction-set). Every output was counts only.
