# Research workflow

Research turns what engineers learn about the original game into documents. A research page says what a part of the
game does, what its data looks like and how sure we are of each claim, so the next engineer does not have to work it
out again. This guide is the overview: how we study the original, how findings are written up and how a function
counts as understood. The grading of claims, the research data, the tools and the traces each have a page of their own.

The pages themselves live in `docs/research/`, one living page per subject. Their kinds, front matter and sections
are in [Writing these docs](writing-docs.md#research-pages).

## The pages {#pages}

Each page holds whole sections, and every section keeps its anchor, so a link names the page that holds it, such as
`research-tools.md#driving-pcsx2`.

### [Evidence levels and run records](research-evidence.md)

- [Evidence levels](research-evidence.md#evidence-levels): the four levels and how a claim is graded.
- [How a level is written](research-evidence.md#level-forms): the spellings `docs check` accepts.
- [Run records](research-evidence.md#run-records): what a runtime claim cites, and the commands that write one.

### [The research data](research-data.md)

- [The research database](research-data.md#the-research-database): the database, its function and source-file data, the
  script bindings, the [reference lists](research-data.md#reference-lists) and the
  [mission list](research-data.md#missions).
- [The function database](research-function-database.md), [the source files](research-source-files.md) and
  [finding where a behaviour lives](research-where.md) have a page each.

### [Research tools](research-tools.md)

- [The tools](research-tools.md#tools): Ghidra, PCSX2, the Xbox executable and captures.
- [Running Coney](research-tools.md#running-coney): starting it without taking the keyboard focus.
- [Driving PCSX2](research-tools.md#driving-pcsx2): recipes, states, patches, input and the claims on the copies.
  {#driving-pcsx2}

### [Traces and playthroughs](research-traces.md)

- [Recording a trace](research-traces.md#recording-a-trace): scenarios, patches and fields.
  {#recording-a-trace}
- [Hooks](research-traces.md#hooks): logging the original's calls.
  {#hooks}
- [Comparing with Coney](research-traces.md#comparing-with-coney): the trace diff.
  {#comparing-with-coney}
- [Differential playthroughs](research-traces.md#differential-playthroughs): a mission played on both games.
  {#differential-playthroughs}

<!-- The five ids above keep the links that name research-workflow.md#<section> working in the legacy research pages
that cite them. Touching a research page holds it to the whole research standard, so they go, with these ids, once
those pages are touched and cite the page that holds the section. -->

## Before you research {#upstream}

Check what already exists before starting a question, in the merged pages and in work that is not merged yet:

1. Search `docs/research/` (`git grep -i <term> -- docs/research research`) for a page that already covers it.
2. List the open pull requests (`gh pr list`) and search their branches: `git fetch`, then
   `git log --oneline origin/<branch> -- docs/research` and `git grep -i <term> origin/<branch> -- docs/research`.
   Another engineer may have the finding written up, or be part-way through it.
3. If something covers the question, extend that page or branch (or wait for it to merge) instead of writing a second
   account of the same thing; if it disagrees with what you find, say so on the page with both evidence levels.

Only a question that nothing covers starts new research.

## Studying the original {#studying-the-original}

Coney is a reimplementation: its code has the same observable behaviour as the original. There is one role, the
engineer, for people and agents alike. An engineer studies the original (`SLUS_212.15` in Ghidra through
ghidra-mcp, the game running in PCSX2 through the pcsx2 MCP server, the game's data files), writes up what is worth
keeping on a research page and in `research/*.yaml`, and implements it in `src/`, tools and tests.

A page is worth writing where the next engineer would otherwise have to work the same thing out again: data
layouts, constants, file formats, what a subsystem does and the questions still open. Nothing from the game goes into
the repository on the way ([LEGAL.md](repo:LEGAL.md#no-game-data)): a page holds facts and behaviour, with short
pseudocode or a short snippet where it explains a point (see [Writing up a finding](#writing-up-a-finding)).

Start with the lookup: `coney-tools research where` says which page already describes an address, a function name, a
source file or a word a player uses, and `orig study` prints the same lines under its own summary
([Finding where a behaviour lives](research-where.md)). Read the page it names before the decompile. The tools for the
rest are on [Research tools](research-tools.md#tools).

### Moving a ported function {#moving}

Moving an `@orig` function (into a renamed class, another class or file, or under another name) needs no new
`orig study` or `orig compare` when its code and its `@orig` tags are as they were: `orig review-stamp --branch`,
`task review` and `pr verify` recognise the move by the address, the tags and the body with its own class and name
taken out, and say so. Change the code or a tag in the same branch and the function is a changed one again, owing its
compare. The rule, and what counts as the same code, is [A moved function](orig-body-hash.md#moved).

### When a research page isn't enough

Sooner or later you need something the page doesn't say. Look at the original to answer it, and put the answer on
the page, which is how it reaches every later reader too. If it can't be answered now:

1. Add the question to the page's **Open questions** section, saying what you need and why (what decision in the
   code depends on it).
2. Raise it as a GitHub issue or, for an agent, in its report as a blocker.
3. Carry on with whatever doesn't depend on the answer, or stop and say so. A guess written into code without a note
   is worse than a gap, because nobody knows to come back to it.

## The game's scripts {#scripts}

A level's missions, objectives, spawns, hints and checkpoints are decided by its Lua scripts, so **level and mission
research starts from the decompiled scripts**, then goes to Ghidra for the natives they call and to PCSX2 to watch it
happen:

1. Decompile every script once into the shared folder, `uv run --project python coney-tools lua decompile --all`
   (`<workspace>/scratch/lua`, the same copy for every worktree; [`lua`](coney-tools-fidelity.md#lua)). Rerun it only
   when the decompiler changes.
2. Open `index.md` there: per script, its functions with the natives each calls and the handlers it registers
   (messages, callbacks, updates, scheduled calls, trigger spheres). Find the level's script and the function that does
   what you are after, then read that function in `<name>.lua`. Local names are made up (`l1`, `p1`): the chunks keep
   none ([Lua 4.0 bytecode](../research/formats/lua-bytecode.md#what-cannot-come-back)).
3. Look up each native the function calls in the [script bindings](../references/bindings/index.md) and, where they say
   too little, at its address in Ghidra.

**Describe, never paste.** A decompile is game script text ([LEGAL.md](repo:LEGAL.md#no-game-data)): it stays in the
shared folder. A research page says in our own words what a script does and cites it by script and function
(`level99.lua`, `Setup`, the native and its arguments where they matter), as for code read in Ghidra
([Writing up a finding](#writing-up-a-finding)). A guard refuses a decompiled file, or three of any five lines of one
(quoted, in a list or table, or commented out alike), in the repository, in pre-commit and before every file write an
agent makes ([The guard](coney-tools-fidelity.md#lua)). A single quoted line passes, and a whole listing is still
wrong when the guard cannot see it: a decompile you did not fingerprint, a paraphrase line by line. The same holds for
an issue or a pull request description, which no guard checks.

## Lessons learned {#lessons-learned}

What past sessions taught, written so that it holds for any game. Multi-agent coordination, the shared machine and
merging are in [How we work](how-we-work.md#shared-machine); the emulator's mechanics are in
[Driving PCSX2](research-tools.md#driving-pcsx2).

### Method

- **Runtime beats reading.** When the code is ambiguous, run it: read or write the memory of the original while a
  scripted scenario plays, and grade the result `confirmed (runtime)`. Say on the page which patch or input the claim
  depends on.
- **Drive every movement like a player would.** Give input as a gamepad with analog sticks, at realistic partial
  deflections, and state the magnitude with every claim that depends on it ("stick 0.5, straight up"). A behaviour
  found only at full deflection is usually not what the player meets.
- **Input goes through the game's memory, never the window.** Writing pad state over the remote-memory interface (the
  patches in [Driving PCSX2](research-tools.md#driving-pcsx2)) is exact, repeatable and leaves the user's keyboard and
  focus alone. Use posted window messages only for the rare hotkey, and never move focus.
- **One sample per update.** Compare games by the update, not by wall-clock time: count the game's own ticks, record
  one row per update, and apply scripted input as each update is seen. Rows labelled by the poll rather than the
  update are off by one, which looks like a behaviour difference and is not.
- **Test your instruments.** A recorder, hook or diff tool is code too. Pin its labelling with a test that drives a
  fake game (the hook recorder once labelled every entry one update early until a test with two-message updates
  exposed it), and say in the guide how recordings made before a fix are wrong. When a result is off by a fixed
  amount, suspect the tool before the code.
- **Compare missions by events, not frames.** Past a few seconds, two games drift apart in time for harmless reasons
  (AI timing, loads, the camera). Compare the order of the events a player notices instead, drive both with one
  adaptive policy that plays by what is on screen, and let the first milestone one game never reaches say where to
  look ([Differential playthroughs](research-traces.md#differential-playthroughs)).
- **Compare, then localise.** Run the same scenario on both games and diff the traces; the first differing column and
  step says where to read. A difference goes back as a question or a fix, never as a page edit that hides it.
- **Trace the cause, not the symptom.** A stuck character, a missing effect or a wrong value usually comes from a
  state that is never reset or a handler that never fires; follow who writes the value and who clears it. Write down
  what you ruled out, in one line, so nobody repeats it.
- **Test through the path the player uses.** A reimplementation grows shortcuts for testing (start a level directly,
  skip the menus), and each shortcut that sets the game up its own way hides bugs the player meets: a pause that only
  the real path has, a hand-over between checkpoints that a direct start never plays, a debug service wired to one
  path only. Keep one set-up that every start goes through, differing only in where it starts, and run playthrough
  tests through it. When a play-tester sees a bug that a test does not, first ask which path each one took.
- **Find a second source for the same function.** Another build of the game (named library calls, inline strings) can
  read faster; cite evidence only at the primary executable's addresses.
- **Ask the data.** Tables, lists and counts can be read from the disc by tool and printed as counts and hashes;
  that is evidence anyone can rerun, and it puts no game data in the repository.

### Conduct

- **Claim before use, release after.** Anything shared and scarce (an emulator copy, a lock) has one user at a time:
  claim it with the tool, release it when done, and close what you started.
- **Keep other people's slots read-only.** Quick-save slots, settings and files someone else made are never overwritten;
  save your own states to files in scratch, and put patched words back before reusing a saved state.
- **Add names, never undo them.** The shared Ghidra project is written by many engineers: rename and annotate freely,
  but do not revert another engineer's names; a disagreement goes on the page as an open question.
- **Do not guess in code.** A gap in a page becomes an open question on the page and a marked stand-in in code, never
  a silent guess ([When a research page isn't enough](#when-a-research-page-isnt-enough)).
- **Research is done when the page maps the behaviour to the code:** a name and plate comment in Ghidra, and a page
  that names the function, cites its address with an evidence level and says what the code alone does not show
  ([The Understood measure](#understood)). The engineer who ports it reads the function itself.
- **Weak evidence stays labelled.** A screenshot is a source of measurements, not a claim; a claim quotes numbers, the
  scenario that produced them and the build or state they came from.

## Writing up a finding

A finding is done when someone else can use it without asking you. For each one:

1. **Update the living page.** Find the page for the subsystem or format and change it in place; create a page only
   when none covers the subject. Don't start a dated notes page: one page per subject means the reader never has to
   reconcile two versions.
2. **Grade every claim** with an [evidence level](research-evidence.md#evidence-levels); a runtime claim cites the
   [run record](research-evidence.md#run-records) of the run, and `coney-tools research claims` lists what the branch
   graded.
3. **Add or update the entries** of the research database for what the finding touches: the function entries of
   `research/functions/<system>.toml` ([the function database](research-data.md#function-database)), with the same
   evidence level as the page. Globals and types stay on the page until the database has them.
4. **Cite addresses** as `0x001490b8`: eight hex digits, in the NTSC-U executable `SLUS_212.15` (SHA1
   `e9cb2cc49aa046b9e494313dce2f5038ed17b2f4`). Every address in the project refers to that one file, so a reader
   with the same disc can check any claim. The check holds an address to the Ghidra listing and the executable's
   sections ([Addresses](writing-docs.md#addresses)), and a function is cited with the name Ghidra gives it.
5. **Describe, don't reproduce.** Explain behaviour in prose, with short pseudocode or a short snippet where it
   makes the point clearer. Never paste a whole function listing from Ghidra or a disassembly, and never paste a long
   stretch of one: it puts the original's code in the repository. See
   [LEGAL.md](repo:LEGAL.md#research-limits).
6. **No game data.** Tables of offsets, constants and counts are facts and belong on the page; the bytes of a game
   file, a dump or an extracted asset never do.
7. **Check and build the docs:** `coney-tools docs check` holds every page you touched to the standard
   ([Checking a page](writing-docs.md#checking)), and `mkdocs build --strict` catches broken links and pages missing
   from the navigation. A page in the legacy file keeps its excuses until a change touches it; then the mechanical
   rules apply page-wide and the section you edit is graded ([Legacy pages](writing-docs.md#legacy-pages)).
8. **Ship the docs with the code.** CI refuses a pull request that changes `src/` or `python/src/` without changing
   anything under `docs/` or `research/`. A change that needs no documentation (a refactor, a rename) says so with a
   line `Docs: none` in the pull request description; editing the description re-runs the check. Check it yourself with
   `git diff --name-only main... | uv run --project python coney-tools pr check-docs`. (The description itself is
   checked against the pull request template by `coney-tools pr check-body`.) Counts quoted in a page by
   hand are held to the data by tests and by `{count}` in the [reference lists](research-data.md#reference-lists).

## The Understood measure {#understood}

A function is researched in full ("understood" on the [progress page](../progress/index.md)) when it has a meaningful
name and a plate comment in the shared Ghidra project, and a research page cites its address in a table row or paragraph
that states an evidence level and says what it does, or has an entry with a `role` and a `level` in the
[function database](research-data.md#function-database) (an entry whose page states no level for the function has
none and does not count). A function nobody needs (dead code, a
debug stub) is cited with `not needed: <reason>` instead, or has an entry with level `not-needed` and the reason as its
role. The bar checks the name and the citation; the plate comment is listed in the backlog. Citations on the
[source map](../research/source-map.md) do not count; a script binding's wrapper counts through its `evidence` in
`research/bindings`.

The names, sizes and plate flags come from `docs/progress/ghidra-functions.tsv`, exported from Ghidra; the
citations are read from the pages and the function database on every docs build. After renaming functions in
Ghidra, refresh both (ghidra-mcp on `:8090` must be running; the export only reads):

```sh
uv run --project python coney-tools ghidra export-names    # re-export the listing (--check: exit 1 when stale)
uv run --project python coney-tools progress check     # the inputs agree; the docs build writes the bars
uv run --project python coney-tools progress backlog "<scratch>/research-backlog"   # per-subsystem to-do lists
```

The backlog writes one Markdown file per subsystem, outside the repository: every function that is not understood,
largest first, with what it lacks (`name`, `cite` or `evidence`), whether it has a plate comment, and the name a page
already gives it. A row that lacks only `name` is documented already; renaming it in Ghidra is the cheapest step.
